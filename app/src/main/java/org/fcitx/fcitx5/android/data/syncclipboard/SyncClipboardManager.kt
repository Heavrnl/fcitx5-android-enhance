/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.syncclipboard

import android.content.BroadcastReceiver
import android.content.ClipData
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Build
import android.os.Environment
import android.os.PowerManager
import android.os.SystemClock
import android.provider.MediaStore
import androidx.annotation.Keep
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import androidx.core.content.getSystemService
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.fcitx.fcitx5.android.FcitxApplication
import org.fcitx.fcitx5.android.data.clipboard.ClipboardManager
import org.fcitx.fcitx5.android.data.clipboard.db.ClipboardEntry
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreference
import org.fcitx.fcitx5.android.utils.appContext
import org.fcitx.fcitx5.android.utils.clipboardManager
import timber.log.Timber
import java.io.ByteArrayOutputStream
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/**
 * SyncClipboard 同步管理器
 * 使用 SignalR WebSocket 实时同步剪切板，并采用「亮屏活跃 + 息屏休眠 + 关键时机主动补同步」策略：
 * 1. 息屏超过宽限期后彻底断开 WebSocket 并停止所有心跳、看门狗和定时任务，零唤醒不阻断深度睡眠。
 * 2. 亮屏/解锁/重连/键盘弹出时立即恢复连接并执行远端与本地双向补偿同步。
 * 3. 感知系统网络状态变化，无网时不空转重试，联网或切网瞬间平滑恢复。
 */
object SyncClipboardManager : CoroutineScope by CoroutineScope(SupervisorJob() + Dispatchers.Default) {

    enum class SyncState {
        Disabled,
        PausedScreenOff,
        WaitingNetwork,
        Connecting,
        Connected
    }

    /** 远端内容刚写入本地剪贴板后的回显去重窗口（毫秒） */
    private const val ECHO_GUARD_WINDOW_MS = 5_000L

    /** 息屏后延迟进入休眠的宽限时间，避免误按电源键又立刻亮屏导致反复重连，同时在系统深睡前及时断网 */
    private const val SCREEN_OFF_GRACE_MS = 5_000L

    /** 主动拉取远端剪贴板的最小节流间隔 */
    private const val PULL_THROTTLE_MS = 1_500L

    /** 相册清理周期间隔（1 小时） */
    private const val CLEANUP_INTERVAL_MS = 3_600_000L

    private const val STATE_PREFS_NAME = "sync_clipboard_state"
    private const val KEY_LAST_REMOTE_SIG = "last_remote_sig"
    private const val KEY_LAST_CLEANUP_AT = "last_cleanup_at"

    private val prefs = AppPrefs.getInstance().syncClipboard
    private val clipboardManager = appContext.clipboardManager

    private val initialized = AtomicBoolean(false)
    private var statePrefs: SharedPreferences? = null
    private var powerManager: PowerManager? = null
    private var connectivityManager: ConnectivityManager? = null

    private var client: SyncClipboardClient? = null
    private var signalRClient: SyncClipboardSignalR? = null

    private val pullMutex = Mutex()
    private val pendingLocalUploadCount = AtomicInteger(0)

    @Volatile
    private var isScreenOffPaused = false

    @Volatile
    private var isNetworkAvailable = true

    @Volatile
    private var currentNetworkHandle: Long? = null

    @Volatile
    private var lastPullSuccessAt = 0L

    @Volatile
    private var screenOffAtElapsed = 0L

    private var screenOffPauseJob: Job? = null
    private var cleanupJob: Job? = null

    private val _syncState = MutableStateFlow(SyncState.Disabled)
    val syncState: StateFlow<SyncState> = _syncState.asStateFlow()

    private val pendingEchoes = java.util.Collections.synchronizedSet(HashSet<String>())
    private val pendingImageEchoes = java.util.Collections.synchronizedSet(HashSet<String>())

    @Volatile
    private var lastReceivedText: String? = null

    @Volatile
    private var lastReceivedTextAt = 0L

    @Keep
    private val enabledListener = ManagedPreference.OnChangeListener<Boolean> { _, enabled ->
        if (enabled) {
            onFeatureEnabled()
        } else {
            onFeatureDisabled()
        }
    }

    @Keep
    private val foregroundServiceListener = ManagedPreference.OnChangeListener<Boolean> { _, enabled ->
        if (enabled && prefs.enabled.getValue()) {
            SyncClipboardService.start(appContext)
        } else {
            SyncClipboardService.stop(appContext)
        }
    }

    @Keep
    private val cleanupListener = ManagedPreference.OnChangeListener<Any> { _, _ ->
        scheduleCleanup()
    }

    @Keep
    private val configChangeListener = ManagedPreference.OnChangeListener<String> { key, _ ->
        // 切换服务器或账号时清空上次已同步远端指纹，确保能立即拉取新服务器上的最新内容
        if (key == prefs.serverUrl.key || key == prefs.username.key) {
            clearLastSyncedRemoteSignature()
        }
        if (prefs.enabled.getValue()) {
            Timber.i("SyncClipboard: Config changed, reconnecting...")
            stopSyncConnection()
            if (!isScreenOffPaused) {
                startOrEnsureSync("config_changed")
            } else {
                refreshSyncState()
            }
        }
    }

    @Keep
    private val localCopyListener = ClipboardManager.OnClipboardUpdateListener { entry ->
        val detail = if (entry.isImage) "path=${entry.imagePath}" else SyncLog.textMeta(entry.text)
        SyncLog.d("UP", "local clip update: image=${entry.isImage} sensitive=${entry.sensitive} $detail")
        if (!prefs.enabled.getValue()) {
            SyncLog.d("UP", "skip: feature disabled")
            return@OnClipboardUpdateListener
        }
        if (entry.isImage) {
            SyncLog.d("UP", "skip: image entry (images are uploaded manually / by screenshot detector)")
            return@OnClipboardUpdateListener
        }
        val text = entry.text
        if (text.isBlank()) {
            SyncLog.d("UP", "skip: blank text")
            return@OnClipboardUpdateListener
        }
        if (text == lastReceivedText &&
            SystemClock.elapsedRealtime() - lastReceivedTextAt < ECHO_GUARD_WINDOW_MS
        ) {
            SyncLog.d("UP", "skip: echo of just-applied remote text ${SyncLog.textMeta(text)}")
            return@OnClipboardUpdateListener
        }
        launch {
            uploadText(text)
        }
    }

    @Keep
    private val screenStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                Intent.ACTION_SCREEN_OFF -> onScreenOff()
                Intent.ACTION_SCREEN_ON -> onScreenOn()
                Intent.ACTION_USER_PRESENT -> onUserUnlocked()
            }
        }
    }

    @Keep
    private val networkCallback = object : ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network: Network) {
            val newHandle = network.networkHandle
            val prevHandle = currentNetworkHandle
            val wasOffline = !isNetworkAvailable
            currentNetworkHandle = newHandle
            isNetworkAvailable = true
            signalRClient?.setNetworkAvailable(true)

            if (!prefs.enabled.getValue() || !hasValidConfig()) {
                refreshSyncState()
                return
            }
            if (isScreenOffPaused) {
                SyncLog.d("NET", "network available while screen-off paused, keep sleeping")
                refreshSyncState()
                return
            }

            val networkChanged = prevHandle != null && prevHandle != newHandle
            if (wasOffline || networkChanged) {
                SyncLog.i("NET", "network active (wasOffline=$wasOffline changed=$networkChanged)")
                val sr = signalRClient
                if (sr == null) {
                    startOrEnsureSync("network_available")
                } else {
                    sr.reconnectNow(if (networkChanged) "network_switched" else "network_restored")
                }
            }
            refreshSyncState()
        }

        override fun onLost(network: Network) {
            val online = checkCurrentOnline()
            if (online) {
                // 如从 Wi-Fi 切换至移动数据时旧网络断开但新网络已就绪，不误报断网
                currentNetworkHandle = activeNetworkHandleCompat()
                return
            }
            isNetworkAvailable = false
            currentNetworkHandle = null
            SyncLog.i("NET", "network lost")
            signalRClient?.setNetworkAvailable(false)
            refreshSyncState()
        }
    }

    fun init() {
        if (FcitxApplication.getInstance().isDirectBootMode) {
            SyncLog.i("Sync", "init skipped in direct boot mode")
            return
        }
        if (!initialized.compareAndSet(false, true)) return

        statePrefs = runCatching {
            appContext.getSharedPreferences(STATE_PREFS_NAME, Context.MODE_PRIVATE)
        }.getOrNull()
        powerManager = appContext.getSystemService<PowerManager>()
        connectivityManager = appContext.getSystemService<ConnectivityManager>()
        isNetworkAvailable = checkCurrentOnline()
        currentNetworkHandle = activeNetworkHandleCompat()

        SyncLog.i("Sync", "init: pid=${android.os.Process.myPid()} interactive=${isScreenInteractive()} online=$isNetworkAvailable")
        SyncLog.logConfigSnapshot(
            enabled = prefs.enabled.getValue(),
            serverUrl = prefs.serverUrl.getValue(),
            username = prefs.username.getValue(),
            password = prefs.password.getValue()
        )
        SyncLog.i(
            "Sync",
            "flags: foregroundService=${prefs.foregroundService.getValue()} " +
                "screenshotDetection=${prefs.screenshotDetection.getValue()} " +
                "autoUploadShot=${prefs.screenshotAutoUpload.getValue()} " +
                "saveToGallery=${prefs.saveToGallery.getValue()} " +
                "autoClearGallery=${prefs.autoClearGallery.getValue()} " +
                "clearInterval=${prefs.galleryClearInterval.getValue()}d"
        )

        prefs.enabled.registerOnChangeListener(enabledListener)
        prefs.foregroundService.registerOnChangeListener(foregroundServiceListener)
        prefs.serverUrl.registerOnChangeListener(configChangeListener)
        prefs.username.registerOnChangeListener(configChangeListener)
        prefs.password.registerOnChangeListener(configChangeListener)
        prefs.saveToGallery.registerOnChangeListener(cleanupListener)
        prefs.autoClearGallery.registerOnChangeListener(cleanupListener)
        prefs.galleryClearInterval.registerOnChangeListener(cleanupListener)

        ClipboardManager.addOnLocalCopyListener(localCopyListener)
        registerScreenReceiver()
        registerNetworkCallback()

        ScreenshotDetector.init()

        if (prefs.enabled.getValue()) {
            if (prefs.foregroundService.getValue()) {
                SyncClipboardService.start(appContext)
            }
            if (isScreenInteractive()) {
                isScreenOffPaused = false
                startOrEnsureSync("init")
                launch { pullRemoteClipboardIfNeeded("init") }
                scheduleCleanup()
            } else {
                enterScreenOffPause(immediate = true, reason = "init_screen_off")
            }
        } else {
            SyncClipboardService.stop(appContext)
            refreshSyncState()
        }
    }

    /**
     * 软键盘弹出或获得焦点时由 FcitxInputMethodService 调用。
     * 补拉起可能被后台拦截的保活服务，并在连接未就绪时立刻恢复连接与校验远端。
     */
    fun onImeActivated() {
        if (!prefs.enabled.getValue()) return
        screenOffPauseJob?.cancel()
        screenOffAtElapsed = 0L
        val wasPaused = isScreenOffPaused
        isScreenOffPaused = false

        if (prefs.foregroundService.getValue() && !SyncClipboardService.isRunning) {
            SyncClipboardService.start(appContext)
        }
        if (!hasValidConfig()) {
            refreshSyncState()
            return
        }

        val sr = signalRClient
        if (wasPaused || sr == null || !sr.isConnectedNow) {
            SyncLog.i("Sync", "ime activated (wasPaused=$wasPaused connected=${sr?.isConnectedNow == true}) -> ensure sync")
            startOrEnsureSync("ime_activated")
            launch { pullRemoteClipboardIfNeeded("ime_activated") }
            scheduleCleanup()
        }
    }

    /**
     * 开机解锁完成或应用更新后由 SyncClipboardBootReceiver 调用。
     */
    fun onBootCompleted() {
        if (!prefs.enabled.getValue()) return
        if (prefs.foregroundService.getValue() && !SyncClipboardService.isRunning) {
            SyncClipboardService.start(appContext)
        }
        if (isScreenInteractive()) {
            screenOffPauseJob?.cancel()
            isScreenOffPaused = false
            startOrEnsureSync("boot_completed")
            launch { pullRemoteClipboardIfNeeded("boot_completed") }
            scheduleCleanup()
        } else {
            enterScreenOffPause(immediate = true, reason = "boot_screen_off")
        }
    }

    /**
     * 息屏/后台事件触发的短时直通上传（如验证码通知提取），
     * 不唤醒 WebSocket 长连接与心跳，仅持有一把不超过 15 秒的临时 WakeLock 发完 HTTP 请求即释放。
     */
    fun uploadTextForWakeup(text: String, reason: String) {
        if (text.isBlank()) return
        if (!prefs.enabled.getValue() || !hasValidConfig()) {
            SyncLog.d("UP", "wakeup upload($reason) skipped: disabled or config incomplete")
            return
        }
        // 预先登记回显屏蔽窗口，避免随后的系统剪贴板回调重复上传同段文本
        lastReceivedText = text
        lastReceivedTextAt = SystemClock.elapsedRealtime()

        launch {
            val wakeLock = runCatching {
                powerManager?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "fcitx5:SyncWakeupUpload")?.apply {
                    setReferenceCounted(false)
                    acquire(15_000L)
                }
            }.getOrNull()
            try {
                SyncLog.i(
                    "UP",
                    "wakeup upload($reason) start interactive=${isScreenInteractive()} ${SyncLog.textMeta(text)}"
                )
                var ok = uploadText(text)
                if (!ok) {
                    // 息屏刚收到短信唤醒时网络接口可能延迟就绪，短等 1.5s 后补充一次
                    delay(1_500L)
                    if (prefs.enabled.getValue()) {
                        ok = uploadText(text)
                    }
                }
                SyncLog.i("UP", "wakeup upload($reason) finished ok=$ok")
            } finally {
                runCatching {
                    if (wakeLock?.isHeld == true) {
                        wakeLock.release()
                    }
                }
            }
        }
    }

    /**
     * 用户在设置页手动点击同步状态触发立即重连与拉取校验。
     */
    fun triggerManualSync() {
        if (!prefs.enabled.getValue() || !hasValidConfig()) {
            refreshSyncState()
            return
        }
        screenOffPauseJob?.cancel()
        isScreenOffPaused = false
        isNetworkAvailable = checkCurrentOnline()
        val sr = signalRClient
        if (sr == null) {
            startOrEnsureSync("manual")
        } else {
            sr.setNetworkAvailable(isNetworkAvailable)
            sr.reconnectNow("manual")
        }
        launch { pullRemoteClipboardIfNeeded("manual", force = true) }
    }

    private fun onFeatureEnabled() {
        SyncLog.i("Sync", "feature enabled by user")
        if (prefs.foregroundService.getValue()) {
            SyncClipboardService.start(appContext)
        }
        if (isScreenInteractive()) {
            isScreenOffPaused = false
            startOrEnsureSync("enabled")
            launch { pullRemoteClipboardIfNeeded("enabled", force = true) }
            scheduleCleanup()
        } else {
            enterScreenOffPause(immediate = true, reason = "enabled_while_screen_off")
        }
    }

    private fun onFeatureDisabled() {
        SyncLog.i("Sync", "feature disabled by user")
        screenOffPauseJob?.cancel()
        cleanupJob?.cancel()
        isScreenOffPaused = false
        stopSyncConnection()
        SyncClipboardService.stop(appContext)
        refreshSyncState()
    }

    private fun onScreenOff() {
        if (!prefs.enabled.getValue()) return
        if (isScreenOffPaused) return
        screenOffAtElapsed = SystemClock.elapsedRealtime()
        screenOffPauseJob?.cancel()
        SyncLog.d("PWR", "ACTION_SCREEN_OFF -> schedule pause in ${SCREEN_OFF_GRACE_MS}ms")
        screenOffPauseJob = launch {
            delay(SCREEN_OFF_GRACE_MS)
            if (!isScreenInteractive()) {
                enterScreenOffPause(immediate = false, reason = "screen_off_grace_elapsed")
            }
        }
    }

    private fun onScreenOn() {
        screenOffPauseJob?.cancel()
        screenOffAtElapsed = 0L
        if (!prefs.enabled.getValue()) return
        val wasPaused = isScreenOffPaused
        isScreenOffPaused = false
        isNetworkAvailable = checkCurrentOnline()
        currentNetworkHandle = activeNetworkHandleCompat()
        SyncLog.d("PWR", "ACTION_SCREEN_ON (wasPaused=$wasPaused online=$isNetworkAvailable)")

        if (!hasValidConfig()) {
            refreshSyncState()
            return
        }
        if (wasPaused || signalRClient?.isConnectedNow != true) {
            startOrEnsureSync("screen_on")
            launch { pullRemoteClipboardIfNeeded("screen_on") }
        } else {
            // 未触发息屏休眠的短时灭屏再亮起，也检查一次远端以防期间切屏漏推
            launch { pullRemoteClipboardIfNeeded("screen_on") }
        }
        scheduleCleanup()
    }

    private fun onUserUnlocked() {
        screenOffPauseJob?.cancel()
        screenOffAtElapsed = 0L
        if (!prefs.enabled.getValue()) return
        val wasPaused = isScreenOffPaused
        isScreenOffPaused = false
        isNetworkAvailable = checkCurrentOnline()
        currentNetworkHandle = activeNetworkHandleCompat()
        SyncLog.d("PWR", "ACTION_USER_PRESENT (wasPaused=$wasPaused online=$isNetworkAvailable)")

        if (hasValidConfig()) {
            if (wasPaused || signalRClient?.isConnectedNow != true) {
                startOrEnsureSync("user_present")
            }
            launch { pullRemoteClipboardIfNeeded("user_present") }
        }
        // 尝试补读可能在息屏/锁屏切换期间产生的本地剪贴板变更
        ClipboardManager.checkAndSyncSystemClipboard("user_present")
        scheduleCleanup()
    }

    private fun enterScreenOffPause(immediate: Boolean, reason: String) {
        if (isScreenOffPaused && immediate) {
            refreshSyncState()
            return
        }
        isScreenOffPaused = true
        cleanupJob?.cancel()
        signalRClient?.disconnect()
        SyncLog.i("PWR", "entered deep-sleep pause ($reason): ws disconnected, all timers stopped")
        refreshSyncState()
    }

    private fun registerScreenReceiver() {
        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_SCREEN_OFF)
            addAction(Intent.ACTION_SCREEN_ON)
            addAction(Intent.ACTION_USER_PRESENT)
        }
        ContextCompat.registerReceiver(
            appContext,
            screenStateReceiver,
            filter,
            ContextCompat.RECEIVER_EXPORTED
        )
    }

    private fun registerNetworkCallback() {
        val cm = connectivityManager ?: return
        runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                cm.registerDefaultNetworkCallback(networkCallback)
            } else {
                val request = NetworkRequest.Builder()
                    .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                    .build()
                cm.registerNetworkCallback(request, networkCallback)
            }
        }.onFailure { e ->
            SyncLog.w("NET", "registerNetworkCallback failed: ${e.message}", e)
        }
    }

    private fun isScreenInteractive(): Boolean {
        return powerManager?.isInteractive ?: true
    }

    private fun checkCurrentOnline(): Boolean {
        val cm = connectivityManager ?: return true
        return runCatching {
            val active = cm.activeNetwork ?: return@runCatching false
            val caps = cm.getNetworkCapabilities(active) ?: return@runCatching false
            caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
        }.getOrDefault(true)
    }

    private fun activeNetworkHandleCompat(): Long? {
        val cm = connectivityManager ?: return null
        return runCatching { cm.activeNetwork?.networkHandle }.getOrNull()
    }

    private fun hasValidConfig(): Boolean {
        return prefs.serverUrl.getValue().isNotBlank() &&
            prefs.username.getValue().isNotBlank() &&
            prefs.password.getValue().isNotBlank()
    }

    private fun createSignalRClient(): SyncClipboardSignalR? {
        if (!hasValidConfig()) {
            Timber.w("SyncClipboard: Missing configuration")
            return null
        }
        return SyncClipboardSignalR(
            prefs.serverUrl.getValue().trimEnd('/'),
            prefs.username.getValue(),
            prefs.password.getValue()
        )
    }

    private fun createClient(): SyncClipboardClient? {
        if (!hasValidConfig()) return null
        return SyncClipboardClient(
            prefs.serverUrl.getValue().trimEnd('/'),
            prefs.username.getValue(),
            prefs.password.getValue()
        )
    }

    private fun startOrEnsureSync(reason: String) {
        if (!prefs.enabled.getValue()) {
            refreshSyncState()
            return
        }
        if (isScreenOffPaused) {
            refreshSyncState()
            return
        }
        if (!hasValidConfig()) {
            SyncLog.w("Sync", "startSync($reason) aborted: missing server url / username / password")
            refreshSyncState()
            return
        }
        if (client == null) {
            client = createClient()
        }
        var sr = signalRClient
        if (sr == null) {
            sr = createSignalRClient()
            if (sr == null) {
                refreshSyncState()
                return
            }
            sr.onProfileChanged = { profile ->
                launch { handleProfileChanged(profile) }
            }
            sr.onConnecting = {
                refreshSyncState()
            }
            sr.onConnected = {
                if (checkPauseIfScreenAlreadyOff()) {
                    // 息屏超时宽限期已过但延迟恰好因 CPU 休眠滞后，立即切回休眠态
                } else {
                    SyncLog.i("WS", "connected")
                    Timber.i("SyncClipboard: WebSocket connected")
                    refreshSyncState()
                    // 每次重连/握手成功后主动校验一次远端剪贴板，补齐断连期间漏掉的变更
                    launch { pullRemoteClipboardIfNeeded("ws_connected") }
                }
            }
            sr.onDisconnected = { error ->
                if (error != null) {
                    SyncLog.w("WS", "disconnected: ${error.javaClass.simpleName}: ${error.message}")
                    Timber.w(error, "SyncClipboard: WebSocket disconnected")
                } else {
                    SyncLog.i("WS", "disconnected (clean)")
                    Timber.i("SyncClipboard: WebSocket disconnected (clean)")
                }
                refreshSyncState()
            }
            signalRClient = sr
        }

        sr.setNetworkAvailable(isNetworkAvailable)
        if (!sr.isConnectedNow && !sr.isConnectingNow) {
            SyncLog.i("Sync", "connect requested ($reason)")
            sr.connect()
        }
        refreshSyncState()
    }

    private fun stopSyncConnection() {
        Timber.i("SyncClipboardManager: stopSyncConnection called")
        SyncLog.i("Sync", "stopSyncConnection called")
        signalRClient?.disconnect()
        signalRClient = null
        client = null
        refreshSyncState()
    }

    private fun refreshSyncState() {
        val next = when {
            !prefs.enabled.getValue() || !hasValidConfig() -> SyncState.Disabled
            isScreenOffPaused -> SyncState.PausedScreenOff
            !isNetworkAvailable -> SyncState.WaitingNetwork
            signalRClient?.isConnectedNow == true -> SyncState.Connected
            else -> SyncState.Connecting
        }
        if (_syncState.value != next) {
            _syncState.value = next
        }
        if (SyncClipboardService.isRunning) {
            SyncClipboardService.updateNotificationIfRunning(appContext, next)
        }
    }

    /**
     * 主动从服务端拉取最新剪贴板，并与持久化远端指纹比对：
     * 只有远端确实发生变化时才写入本地，避免用老数据覆盖本地新复制的内容。
     */
    private suspend fun pullRemoteClipboardIfNeeded(reason: String, force: Boolean = false) {
        if (!prefs.enabled.getValue() || !hasValidConfig()) return
        if (isScreenOffPaused || !isNetworkAvailable) return
        if (pendingLocalUploadCount.get() > 0) {
            SyncLog.d("DOWN", "pull($reason) skipped: local upload in progress")
            return
        }

        pullMutex.withLock {
            if (isScreenOffPaused || !isNetworkAvailable || !prefs.enabled.getValue()) return@withLock
            if (pendingLocalUploadCount.get() > 0) return@withLock

            val now = SystemClock.elapsedRealtime()
            if (!force && now - lastPullSuccessAt < PULL_THROTTLE_MS) {
                SyncLog.d("DOWN", "pull($reason) throttled")
                return@withLock
            }

            val httpClient = client ?: createClient() ?: return@withLock
            val result = httpClient.getClipboard()
            result.onFailure { e ->
                SyncLog.w("DOWN", "pull($reason) failed: ${e.javaClass.simpleName}: ${e.message}")
                return@withLock
            }

            val data = result.getOrNull() ?: return@withLock
            lastPullSuccessAt = SystemClock.elapsedRealtime()

            val signature = computeSignature(data)
            if (signature == null) {
                SyncLog.d("DOWN", "pull($reason): remote empty or unsupported type=${data.type}")
                return@withLock
            }
            val lastSig = getLastSyncedRemoteSignature()
            if (signature == lastSig) {
                SyncLog.d("DOWN", "pull($reason): remote unchanged")
                return@withLock
            }
            if (pendingLocalUploadCount.get() > 0) {
                SyncLog.d("DOWN", "pull($reason) abort apply: local upload started meanwhile")
                return@withLock
            }

            SyncLog.i("DOWN", "pull($reason): detected remote change type=${data.type}")
            when (data.type) {
                SyncClipboardData.TYPE_TEXT -> {
                    handleIncomingText(data.text, source = "pull:$reason")
                }
                SyncClipboardData.TYPE_IMAGE, SyncClipboardData.TYPE_FILE -> {
                    handleIncomingFile(
                        dataName = data.dataName,
                        hash = data.hash,
                        hasData = data.hasData,
                        source = "pull:$reason"
                    )
                }
            }
        }
    }

    private fun checkPauseIfScreenAlreadyOff(): Boolean {
        if (!isScreenInteractive() &&
            !isScreenOffPaused &&
            screenOffAtElapsed > 0L &&
            SystemClock.elapsedRealtime() - screenOffAtElapsed >= SCREEN_OFF_GRACE_MS
        ) {
            enterScreenOffPause(immediate = false, reason = "screen_off_elapsed_guard")
            return true
        }
        return isScreenOffPaused
    }

    /**
     * 处理服务器 WebSocket 推送的 ProfileDto
     */
    private suspend fun handleProfileChanged(profile: ProfileDto) {
        if (checkPauseIfScreenAlreadyOff()) return
        Timber.d("SyncClipboardManager: handleProfileChanged: type=${profile.type}, dataName=${profile.dataName}")
        SyncLog.d(
            "DOWN",
            "profile: type=${profile.type} hasData=${profile.hasData} dataName=${profile.dataName} ${SyncLog.textMeta(profile.text)}"
        )
        try {
            when (profile.type) {
                ProfileDto.TYPE_TEXT -> {
                    val text = profile.text
                    if (text.isNullOrBlank()) {
                        SyncLog.w("DOWN", "skip: empty text in profile")
                        return
                    }
                    handleIncomingText(text, source = "ws")
                }
                ProfileDto.TYPE_IMAGE, ProfileDto.TYPE_FILE -> {
                    handleIncomingFile(
                        dataName = profile.dataName,
                        hash = profile.hash,
                        hasData = profile.hasData,
                        source = "ws"
                    )
                }
            }
        } catch (e: Exception) {
            SyncLog.e("DOWN", "error handling profile: ${e.javaClass.simpleName}: ${e.message}", e)
            Timber.e(e, "SyncClipboard: Error handling profile change")
        }
    }

    private suspend fun handleIncomingText(text: String, source: String) {
        if (text.isBlank()) return
        val signature = textSignature(text)

        if (pendingEchoes.remove(text)) {
            saveLastSyncedRemoteSignature(signature)
            SyncLog.d("DOWN", "skip($source): own echo (pendingEchoes) ${SyncLog.textMeta(text)}")
            return
        }

        // 优先直接读取系统真实剪贴板；如果系统拒绝后台读取，不能拿旧缓存误判跳过
        val realLocal = readRealSystemClipboardTextOrNull()
        if (text == realLocal) {
            saveLastSyncedRemoteSignature(signature)
            ClipboardManager.recordRemoteText(text)
            SyncLog.d("DOWN", "skip($source): already equal to system clipboard ${SyncLog.textMeta(text)}")
            return
        }

        lastReceivedText = text
        lastReceivedTextAt = SystemClock.elapsedRealtime()
        ClipboardManager.markRemoteTextApplied(text)
        saveLastSyncedRemoteSignature(signature)
        SyncLog.i(
            "DOWN",
            "apply remote text($source) -> local clipboard ${SyncLog.textMeta(text)} pendingEchoes=${pendingEchoes.size}"
        )
        setLocalClipboard(text)
        ClipboardManager.recordRemoteText(text)
    }

    private suspend fun handleIncomingFile(
        dataName: String?,
        hash: String?,
        hasData: Boolean,
        source: String
    ) {
        if (dataName.isNullOrBlank() || !hasData) {
            SyncLog.w("DOWN", "skip($source): file profile without dataName/hasData (name=$dataName hasData=$hasData)")
            return
        }
        val signature = fileSignature(dataName, hash)
        if (pendingImageEchoes.remove(dataName)) {
            saveLastSyncedRemoteSignature(signature)
            SyncLog.d("DOWN", "skip($source): own image echo $dataName")
            return
        }

        val httpClient = client ?: createClient()
        if (httpClient == null) {
            SyncLog.w("DOWN", "skip($source): no http client (missing config)")
            return
        }

        SyncLog.i("DOWN", "downloading file($source) $dataName ...")
        val startedAt = SystemClock.elapsedRealtime()
        val imageResult = httpClient.downloadFile(dataName)
        val cost = SystemClock.elapsedRealtime() - startedAt
        imageResult.onSuccess { bytes ->
            SyncLog.i("DOWN", "downloaded $dataName (${bytes.size} bytes) in ${cost}ms")
            saveLastSyncedRemoteSignature(signature)
            saveImageToClipboardHistory(bytes, dataName)
            if (prefs.saveToGallery.getValue()) {
                val galleryName = if (dataName.lowercase().startsWith("screenshot")) {
                    "sync_$dataName"
                } else {
                    dataName
                }
                saveToGallery(bytes, galleryName)
            }
            Timber.d("SyncClipboard: Downloaded file from server: $dataName")
        }.onFailure { e ->
            SyncLog.w("DOWN", "download failed $dataName in ${cost}ms: ${e.javaClass.simpleName}: ${e.message}", e)
        }
    }

    private suspend fun uploadText(text: String): Boolean {
        val httpClient = client ?: createClient()
        if (httpClient == null) {
            SyncLog.w("UP", "aborted: no http client (server url / username / password missing)")
            return false
        }
        pendingLocalUploadCount.incrementAndGet()
        try {
            SyncLog.i("UP", "PUT /SyncClipboard.json ${SyncLog.textMeta(text)}")
            pendingEchoes.add(text)
            val startedAt = SystemClock.elapsedRealtime()
            val result = httpClient.putClipboard(SyncClipboardData.text(text))
            val cost = SystemClock.elapsedRealtime() - startedAt
            if (result.isSuccess) {
                saveLastSyncedRemoteSignature(textSignature(text))
                SyncLog.i("UP", "ok in ${cost}ms ${SyncLog.textMeta(text)}")
                Timber.d("SyncClipboard: Uploaded text in ${cost}ms")
                return true
            } else {
                SyncLog.w(
                    "UP",
                    "failed in ${cost}ms: ${result.exceptionOrNull()?.javaClass?.simpleName}: ${result.exceptionOrNull()?.message}"
                )
                Timber.w("SyncClipboard: Failed to upload text")
                pendingEchoes.remove(text)
                return false
            }
        } finally {
            pendingLocalUploadCount.decrementAndGet()
        }
    }

    /**
     * 上传图片到服务器
     */
    suspend fun uploadImage(bitmap: Bitmap, filename: String = "image.png"): Boolean {
        val httpClient = client ?: createClient()
        if (httpClient == null) {
            SyncLog.w("UP", "image aborted: no http client (missing config)")
            return false
        }

        val bytes = ByteArrayOutputStream().use { stream ->
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
            stream.toByteArray()
        }

        pendingLocalUploadCount.incrementAndGet()
        try {
            SyncLog.i("UP", "image upload start name=$filename size=${bytes.size} ${bitmap.width}x${bitmap.height}")
            val hash = calculateServerHash(filename, bytes)
            val uploadResult = httpClient.uploadFile(filename, bytes)
            pendingImageEchoes.add(filename)

            return uploadResult.map {
                val data = SyncClipboardData.image(hash, filename)
                val success = httpClient.putClipboard(data).isSuccess
                if (success) {
                    saveLastSyncedRemoteSignature(fileSignature(filename, hash))
                    SyncLog.i("UP", "image upload ok name=$filename")
                } else {
                    SyncLog.w("UP", "image upload failed at putClipboard name=$filename")
                    pendingImageEchoes.remove(filename)
                }
                success
            }.getOrElse {
                SyncLog.w("UP", "image upload failed at uploadFile name=$filename: ${it.javaClass.simpleName}: ${it.message}")
                pendingImageEchoes.remove(filename)
                false
            }
        } finally {
            pendingLocalUploadCount.decrementAndGet()
        }
    }

    /**
     * 上传剪贴板中最新的图片到服务器
     */
    suspend fun uploadLatestImage(): Boolean {
        val lastEntry = ClipboardManager.lastEntry
        if (lastEntry == null) {
            SyncLog.w("UP", "latest image aborted: no clipboard entry")
            return false
        }
        if (!lastEntry.isImage) {
            SyncLog.w("UP", "latest image aborted: last entry is text, not image")
            return false
        }

        val file = File(lastEntry.imagePath)
        if (!file.exists()) {
            SyncLog.w("UP", "latest image aborted: file missing ${lastEntry.imagePath}")
            return false
        }

        val bytes = file.readBytes()
        val httpClient = client ?: createClient()
        if (httpClient == null) {
            SyncLog.w("UP", "latest image aborted: no http client (missing config)")
            return false
        }

        val filename = "sync_${System.currentTimeMillis()}.png"
        pendingLocalUploadCount.incrementAndGet()
        try {
            SyncLog.i("UP", "latest image upload start name=$filename size=${bytes.size}")
            val hash = calculateServerHash(filename, bytes)
            pendingImageEchoes.add(filename)

            val uploadResult = httpClient.uploadFile(filename, bytes)
            return uploadResult.map {
                val data = SyncClipboardData.image(hash, filename)
                val success = httpClient.putClipboard(data).isSuccess
                if (success) {
                    saveLastSyncedRemoteSignature(fileSignature(filename, hash))
                    SyncLog.i("UP", "latest image upload ok name=$filename")
                } else {
                    SyncLog.w("UP", "latest image upload failed at putClipboard name=$filename")
                    pendingImageEchoes.remove(filename)
                }
                success
            }.getOrElse {
                SyncLog.w(
                    "UP",
                    "latest image upload failed at uploadFile name=$filename: ${it.javaClass.simpleName}: ${it.message}"
                )
                pendingImageEchoes.remove(filename)
                false
            }
        } finally {
            pendingLocalUploadCount.decrementAndGet()
        }
    }

    /**
     * 直接向系统剪贴板读取当前文本；无焦点被拦截或为空时返回 null。
     */
    private fun readRealSystemClipboardTextOrNull(): String? {
        return try {
            val clip = clipboardManager.primaryClip
            if (clip == null || clip.itemCount == 0) {
                null
            } else {
                clip.getItemAt(0)?.text?.toString()
            }
        } catch (e: Exception) {
            SyncLog.d("CB", "system clipboard read blocked in background: ${e.javaClass.simpleName}")
            null
        }
    }

    private fun setLocalClipboard(text: String) {
        try {
            val clip = ClipData.newPlainText("SyncClipboard", text)
            clipboardManager.setPrimaryClip(clip)
            SyncLog.i("DOWN", "setPrimaryClip ok ${SyncLog.textMeta(text)}")
        } catch (e: Exception) {
            SyncLog.e("DOWN", "setPrimaryClip failed: ${e.javaClass.simpleName}: ${e.message}", e)
            Timber.e(e, "Failed to set local clipboard")
        }
    }

    private fun computeSignature(data: SyncClipboardData): String? {
        return when (data.type) {
            SyncClipboardData.TYPE_TEXT -> {
                if (data.text.isBlank()) null else textSignature(data.text)
            }
            SyncClipboardData.TYPE_IMAGE, SyncClipboardData.TYPE_FILE -> {
                if (data.dataName.isBlank() || !data.hasData) null else fileSignature(data.dataName, data.hash)
            }
            else -> null
        }
    }

    private fun textSignature(text: String): String {
        val digest = MessageDigest.getInstance("SHA-256").digest(text.toByteArray(Charsets.UTF_8))
        val hex = digest.joinToString("") { "%02x".format(it) }
        return "T:$hex"
    }

    private fun fileSignature(dataName: String, hash: String?): String {
        return "F:$dataName:${hash.orEmpty().uppercase()}"
    }

    private fun getLastSyncedRemoteSignature(): String {
        return statePrefs?.getString(KEY_LAST_REMOTE_SIG, "") ?: ""
    }

    private fun saveLastSyncedRemoteSignature(signature: String) {
        statePrefs?.edit { putString(KEY_LAST_REMOTE_SIG, signature) }
    }

    private fun clearLastSyncedRemoteSignature() {
        statePrefs?.edit { remove(KEY_LAST_REMOTE_SIG) }
    }

    private fun scheduleCleanup() {
        cleanupJob?.cancel()
        if (!prefs.enabled.getValue() ||
            !prefs.saveToGallery.getValue() ||
            !prefs.autoClearGallery.getValue() ||
            isScreenOffPaused
        ) {
            return
        }
        cleanupJob = launch {
            while (!isScreenOffPaused) {
                val lastAt = statePrefs?.getLong(KEY_LAST_CLEANUP_AT, 0L) ?: 0L
                val elapsed = System.currentTimeMillis() - lastAt
                if (elapsed < CLEANUP_INTERVAL_MS) {
                    delay(CLEANUP_INTERVAL_MS - elapsed)
                }
                if (isScreenOffPaused) break
                cleanOldGalleryImages()
                cleanClipboardImagesDir()
                statePrefs?.edit { putLong(KEY_LAST_CLEANUP_AT, System.currentTimeMillis()) }
                delay(CLEANUP_INTERVAL_MS)
            }
        }
    }

    private suspend fun cleanOldGalleryImages() {
        val intervalDays = prefs.galleryClearInterval.getValue()
        Timber.d("SyncClipboard: cleanOldGalleryImages called. Interval=${intervalDays} days")
        if (intervalDays <= 0) return

        val now = System.currentTimeMillis() / 1000
        val cutoff = now - (intervalDays * 24 * 3600)
        val resolver = appContext.contentResolver

        val selection = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            "${MediaStore.Images.Media.DATE_ADDED} < ? AND ${MediaStore.Images.Media.RELATIVE_PATH} LIKE ?"
        } else {
            "${MediaStore.Images.Media.DATE_ADDED} < ? AND ${MediaStore.Images.Media.DATA} LIKE ?"
        }
        val selectionArgs = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            arrayOf(cutoff.toString(), "%Pictures/Fcitx5%")
        } else {
            arrayOf(cutoff.toString(), "%/Pictures/Fcitx5/%")
        }

        try {
            val count = resolver.delete(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, selection, selectionArgs)
            if (count > 0) {
                Timber.i("SyncClipboard: Cleaned $count old images from gallery")
            }
        } catch (e: Exception) {
            Timber.e(e, "SyncClipboard: Failed to clean old gallery images")
        }
    }

    private fun cleanClipboardImagesDir() {
        val dir = ClipboardManager.imageDir
        val files = dir.listFiles()
        if (files.isNullOrEmpty()) return

        var deleted = 0
        files.forEach { file ->
            runCatching {
                if (file.isFile && file.delete()) {
                    deleted++
                }
            }.onFailure { e ->
                Timber.w(e, "SyncClipboard: Failed to delete clipboard image: ${file.absolutePath}")
            }
        }
        if (deleted > 0) {
            Timber.i("SyncClipboard: Cleaned $deleted files from clipboard_images")
        }
    }

    private suspend fun saveImageToClipboardHistory(bytes: ByteArray, filename: String) {
        try {
            val imageDir = ClipboardManager.imageDir
            val entry = ClipboardEntry.fromImageBytes(bytes, imageDir, "image/png")
            if (entry != null) {
                ClipboardManager.insertImageEntry(entry)
                SyncLog.i("DOWN", "image saved to clipboard history name=$filename path=${entry.imagePath}")
                Timber.d("SyncClipboard: Image saved to clipboard history")
            } else {
                SyncLog.w("DOWN", "image decode failed, not saved: name=$filename size=${bytes.size}")
            }
        } catch (e: Exception) {
            SyncLog.e("DOWN", "failed to save image to clipboard history: ${e.message}", e)
            Timber.e(e, "Failed to save image to clipboard history")
        }
    }

    private fun calculateServerHash(filename: String, bytes: ByteArray): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val contentHashBytes = digest.digest(bytes)
        val contentHashHex = contentHashBytes.joinToString("") { "%02X".format(it) }
        val combined = "$filename|$contentHashHex"
        val finalHashBytes = digest.digest(combined.toByteArray(Charsets.UTF_8))
        return finalHashBytes.joinToString("") { "%02X".format(it) }
    }

    private suspend fun saveToGallery(bytes: ByteArray, filename: String) {
        val context = appContext
        val resolver = context.contentResolver
        val contentValues = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, filename)
            put(MediaStore.MediaColumns.MIME_TYPE, "image/png")
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/Fcitx5")
                put(MediaStore.MediaColumns.IS_PENDING, 1)
            }
        }

        try {
            val uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, contentValues)
            if (uri != null) {
                resolver.openOutputStream(uri)?.use { stream ->
                    stream.write(bytes)
                }
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    contentValues.clear()
                    contentValues.put(MediaStore.MediaColumns.IS_PENDING, 0)
                    resolver.update(uri, contentValues, null, null)
                }
                Timber.d("SyncClipboard: Saved image to gallery: $uri")
            }
        } catch (e: Exception) {
            Timber.e(e, "SyncClipboard: Failed to save to gallery")
        }
    }
}
