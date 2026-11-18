/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.syncclipboard

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.decodeFromJsonElement
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import timber.log.Timber
import java.util.Base64
import java.util.concurrent.TimeUnit

/**
 * SignalR 客户端
 * 使用 OkHttp WebSocket 实现 SignalR JSON 协议
 *
 * 功耗优化说明：
 * 1. 移除 OkHttp 内置的 pingInterval，统一由 SignalR 应用层心跳保活，避免双层定时唤醒射频。
 * 2. 重连采用指数退避（1s -> 60s），且在无网络时挂起所有重试与看门狗，等待系统网络恢复回调。
 * 3. disconnect() 时彻底取消所有协程并清空连接池，息屏休眠期间做到零唤醒。
 */
class SyncClipboardSignalR(
    private val serverUrl: String,
    private val username: String,
    private val password: String
) : CoroutineScope by CoroutineScope(SupervisorJob() + Dispatchers.IO) {

    companion object {
        private const val TAG = "SignalR"
        private const val HUB_PATH = "/SyncClipboardHub"
        private const val RECORD_SEPARATOR = '\u001E' // ASCII 30 - SignalR message delimiter
        private const val PING_INTERVAL_MS = 20_000L          // SignalR 心跳周期 20 秒
        private const val HANDSHAKE_TIMEOUT_MS = 15_000L      // 握手超时 15 秒
        private const val ALIVE_TIMEOUT_MS = 65_000L          // 活性超时 65 秒（超过 3 个心跳周期未收到消息则判定假死）
        private const val WATCHDOG_INTERVAL_MS = 30_000L      // 看门狗巡检间隔
        private val RECONNECT_BACKOFF_MS = longArrayOf(1_000L, 2_000L, 5_000L, 15_000L, 30_000L, 60_000L)
    }

    private val json = Json {
        ignoreUnknownKeys = true
        isLenient = true
        encodeDefaults = true
    }

    // 仅由 SignalR 协议层 PING 控制心跳，避免与 OkHttp pingInterval 重复唤醒网络
    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.MINUTES)
        .build()

    @Volatile
    private var webSocket: WebSocket? = null

    @Volatile
    private var isConnected = false

    @Volatile
    private var isConnecting = false

    @Volatile
    private var shouldReconnect = false

    @Volatile
    private var networkAvailable = true

    @Volatile
    private var waitingForNetwork = false

    private var reconnectAttempt = 0
    private var pingJob: Job? = null
    private var reconnectJob: Job? = null
    private var watchdogJob: Job? = null
    private var handshakeTimeoutJob: Job? = null

    @Volatile
    private var lastMessageTime = 0L

    /**
     * 状态机锁。
     * WebSocket 回调跑在 OkHttp 线程，ping/超时/重连跑在协程里，
     * 所有状态迁移必须在锁内完成并校验 socket 身份，防止"假在线"死锁。
     */
    private val stateLock = Any()

    var onProfileChanged: ((ProfileDto) -> Unit)? = null
    var onConnecting: (() -> Unit)? = null
    var onConnected: (() -> Unit)? = null
    var onDisconnected: ((Throwable?) -> Unit)? = null

    val isConnectedNow: Boolean
        get() = synchronized(stateLock) { isConnected && webSocket != null }

    val isConnectingNow: Boolean
        get() = synchronized(stateLock) { isConnecting }

    private val authHeader: String by lazy {
        val credentials = "$username:$password"
        val encoded = Base64.getEncoder().encodeToString(credentials.toByteArray())
        "Basic $encoded"
    }

    /**
     * 连接到 SignalR Hub
     */
    fun connect() {
        var stale: WebSocket? = null
        var deferredNoNetwork = false
        synchronized(stateLock) {
            if (isConnecting || (isConnected && webSocket != null)) {
                Timber.tag(TAG).d("Already connected or connecting, skipping")
                SyncLog.d("WS", "connect skipped: connected=$isConnected connecting=$isConnecting")
                return
            }
            shouldReconnect = true
            if (!networkAvailable) {
                isConnected = false
                isConnecting = false
                waitingForNetwork = true
                stale = webSocket
                webSocket = null
                deferredNoNetwork = true
            } else {
                isConnected = false
                isConnecting = true
                waitingForNetwork = false
                stale = webSocket
                webSocket = null
            }
        }
        stale?.close(1000, "Reconnecting")

        if (deferredNoNetwork) {
            reconnectJob?.cancel()
            watchdogJob?.cancel()
            SyncLog.i("WS", "connect deferred: no network")
            onDisconnected?.invoke(null)
            return
        }

        val wsUrl = buildWebSocketUrl()
        Timber.tag(TAG).d("Connecting to: $wsUrl (attempt=$reconnectAttempt)")
        SyncLog.i("WS", "connect attempt=${reconnectAttempt + 1} url=$wsUrl")
        onConnecting?.invoke()

        val request = Request.Builder()
            .url(wsUrl)
            .addHeader("Authorization", authHeader)
            .build()

        val socket = client.newWebSocket(request, WebSocketListenerImpl())
        val stillConnecting = synchronized(stateLock) {
            if (isConnecting && shouldReconnect) {
                webSocket = socket
                true
            } else {
                false
            }
        }
        if (!stillConnecting) {
            socket.cancel()
            return
        }

        startHandshakeTimeout(socket)
        startWatchdog()
    }

    /**
     * 立即复位退避计数并重连（用于网络切换/亮屏唤醒/手动触发）
     */
    fun reconnectNow(reason: String) {
        var old: WebSocket? = null
        val canConnect: Boolean
        synchronized(stateLock) {
            old = webSocket
            webSocket = null
            isConnected = false
            isConnecting = false
            shouldReconnect = true
            reconnectAttempt = 0
            waitingForNetwork = !networkAvailable
            canConnect = networkAvailable
        }
        pingJob?.cancel()
        reconnectJob?.cancel()
        handshakeTimeoutJob?.cancel()
        runCatching { old?.cancel() }
        SyncLog.i("WS", "reconnect now: $reason (net=$canConnect)")
        if (canConnect) {
            connect()
        } else {
            watchdogJob?.cancel()
            onDisconnected?.invoke(null)
        }
    }

    /**
     * 同步系统网络可用性；无网时停止盲目重连与看门狗，恢复网络后立刻复位重连。
     */
    fun setNetworkAvailable(available: Boolean) {
        val shouldStartNow: Boolean
        val shouldPauseNoNetwork: Boolean
        synchronized(stateLock) {
            if (networkAvailable == available) return
            networkAvailable = available
            if (available) {
                waitingForNetwork = false
                reconnectAttempt = 0
                shouldStartNow = shouldReconnect && !isConnected && !isConnecting
                shouldPauseNoNetwork = false
            } else {
                shouldStartNow = false
                shouldPauseNoNetwork = shouldReconnect
            }
        }

        if (available) {
            SyncLog.i("WS", "network restored (shouldStart=$shouldStartNow)")
            if (shouldStartNow) {
                reconnectJob?.cancel()
                connect()
            }
        } else if (shouldPauseNoNetwork) {
            SyncLog.i("WS", "network lost -> stop timers and wait for network")
            forceReconnect("network lost")
        }
    }

    /**
     * 把连接状态强制拉回"未连接"并按策略重连。
     */
    private fun forceReconnect(reason: String) {
        var old: WebSocket? = null
        synchronized(stateLock) {
            old = webSocket
            webSocket = null
            isConnected = false
            isConnecting = false
        }
        pingJob?.cancel()
        handshakeTimeoutJob?.cancel()
        runCatching { old?.cancel() }
        onDisconnected?.invoke(null)
        SyncLog.w("WS", "force reconnect: $reason")
        scheduleReconnect(reason)
    }

    /**
     * 看门狗：兜住"连接状态和实际 socket 脱节"的状态不一致。
     * 无网络时主动退出，等待网络恢复时由 connect() 重新拉起。
     */
    private fun startWatchdog() {
        if (watchdogJob?.isActive == true) return
        watchdogJob = launch {
            while (shouldReconnect && networkAvailable) {
                delay(WATCHDOG_INTERVAL_MS)
                if (!shouldReconnect || !networkAvailable) break
                val connectedWithoutSocket = synchronized(stateLock) {
                    isConnected && webSocket == null
                }
                if (connectedWithoutSocket) {
                    SyncLog.w("WS", "watchdog: isConnected=true but socket is gone")
                    forceReconnect("watchdog: connected without socket")
                    continue
                }
                val idle = synchronized(stateLock) {
                    shouldReconnect && networkAvailable && !isConnected && !isConnecting
                } && reconnectJob?.isActive != true
                if (idle) {
                    SyncLog.w("WS", "watchdog: not connected and no reconnect pending")
                    scheduleReconnect("watchdog: idle without socket")
                }
            }
            SyncLog.d("WS", "watchdog stopped")
        }
    }

    /**
     * 主动断开连接并停止所有后台任务、清空空闲连接池（用于用户关闭或息屏休眠）。
     */
    fun disconnect() {
        SyncLog.i("WS", "disconnect() by client (shouldReconnect=false)")
        var old: WebSocket? = null
        synchronized(stateLock) {
            shouldReconnect = false
            isConnecting = false
            waitingForNetwork = false
            reconnectAttempt = 0
            old = webSocket
            webSocket = null
            isConnected = false
        }
        pingJob?.cancel()
        reconnectJob?.cancel()
        handshakeTimeoutJob?.cancel()
        watchdogJob?.cancel()
        runCatching { old?.close(1000, "Client disconnect") }
        runCatching { client.connectionPool.evictAll() }
    }

    private fun buildWebSocketUrl(): String {
        val baseUrl = serverUrl.trimEnd('/')
        val wsScheme = if (baseUrl.startsWith("https://")) "wss://" else "ws://"
        val host = baseUrl.removePrefix("https://").removePrefix("http://")
        return "$wsScheme$host$HUB_PATH"
    }

    private fun sendHandshake(webSocket: WebSocket) {
        val handshake = """{"protocol":"json","version":1}""" + RECORD_SEPARATOR
        if (webSocket.send(handshake)) {
            Timber.tag(TAG).d("Sent handshake: $handshake")
        } else {
            Timber.tag(TAG).e("Failed to send handshake")
        }
    }

    private fun sendPing(webSocket: WebSocket) {
        val ping = SignalRPingMessage()
        val message = json.encodeToString(ping) + RECORD_SEPARATOR
        webSocket.send(message)
    }

    private fun startPingJob(webSocket: WebSocket) {
        pingJob?.cancel()
        pingJob = launch {
            var ticks = 0
            while (isCurrentSocket(webSocket) && isConnected) {
                delay(PING_INTERVAL_MS)
                if (isCurrentSocket(webSocket) && isConnected) {
                    ticks++
                    val elapsed = System.currentTimeMillis() - lastMessageTime
                    if (elapsed > ALIVE_TIMEOUT_MS) {
                        Timber.tag(TAG).w("Connection seems dead (no message for ${elapsed}ms), forcing reconnect")
                        SyncLog.w("WS", "alive timeout: no server message for ${elapsed}ms, force reconnect")
                        forceReconnect("alive timeout ${elapsed}ms")
                        return@launch
                    }
                    // 每 3 次 ping（约 1 分钟）记一条心跳，用于离线排查
                    if (ticks % 3 == 0) {
                        SyncLog.d("WS", "heartbeat: ping #$ticks, last server message ${elapsed}ms ago")
                    }
                    sendPing(webSocket)
                }
            }
            SyncLog.d("WS", "ping job ended (connected=$isConnected current=${isCurrentSocket(webSocket)})")
            if (isConnected && shouldReconnect) {
                forceReconnect("ping loop exited while isConnected=true")
            }
        }
    }

    private fun startHandshakeTimeout(webSocket: WebSocket) {
        handshakeTimeoutJob?.cancel()
        handshakeTimeoutJob = launch {
            delay(HANDSHAKE_TIMEOUT_MS)
            val stillWaiting = synchronized(stateLock) {
                isCurrentSocket(webSocket) && !isConnected && isConnecting
            }
            if (stillWaiting) {
                Timber.tag(TAG).w("Handshake timeout after ${HANDSHAKE_TIMEOUT_MS}ms, forcing reconnect")
                SyncLog.w("WS", "handshake timeout after ${HANDSHAKE_TIMEOUT_MS}ms (no handshake response)")
                forceReconnect("handshake timeout")
            }
        }
    }

    private fun isCurrentSocket(webSocket: WebSocket): Boolean =
        this.webSocket === webSocket

    private fun getReconnectDelay(attemptIndex: Int): Long {
        val idx = attemptIndex.coerceIn(0, RECONNECT_BACKOFF_MS.lastIndex)
        return RECONNECT_BACKOFF_MS[idx]
    }

    private fun scheduleReconnect(reason: String) {
        val delayMs: Long
        val attemptNum: Int
        synchronized(stateLock) {
            if (!shouldReconnect) {
                SyncLog.d("WS", "reconnect suppressed (shouldReconnect=false) after: $reason")
                return
            }
            if (!networkAvailable) {
                waitingForNetwork = true
                reconnectJob?.cancel()
                watchdogJob?.cancel()
                SyncLog.i("WS", "reconnect deferred until network available (reason=$reason)")
                return
            }
            delayMs = getReconnectDelay(reconnectAttempt)
            reconnectAttempt++
            attemptNum = reconnectAttempt
        }

        reconnectJob?.cancel()
        SyncLog.i("WS", "reconnect scheduled in ${delayMs}ms (attempt=$attemptNum) reason=$reason")
        reconnectJob = launch {
            Timber.tag(TAG).d("Reconnecting in ${delayMs}ms (attempt=$attemptNum)...")
            delay(delayMs)
            val canConnect = synchronized(stateLock) { shouldReconnect && networkAvailable }
            if (canConnect) {
                connect()
            }
        }
    }

    private fun handleMessage(webSocket: WebSocket, text: String) {
        if (!isCurrentSocket(webSocket)) {
            Timber.tag(TAG).d("Ignoring message from stale WebSocket")
            return
        }

        val messages = text.split(RECORD_SEPARATOR).filter { it.isNotBlank() }
        lastMessageTime = System.currentTimeMillis()

        for (msgText in messages) {
            try {
                if (!isConnected) {
                    val handshakeResponse = json.decodeFromString<SignalRHandshakeResponse>(msgText)
                    if (handshakeResponse.error == null) {
                        val accepted = synchronized(stateLock) {
                            if (isCurrentSocket(webSocket) && !isConnected) {
                                isConnected = true
                                isConnecting = false
                                waitingForNetwork = false
                                reconnectAttempt = 0
                                lastMessageTime = System.currentTimeMillis()
                                true
                            } else false
                        }
                        if (!accepted) {
                            SyncLog.w("WS", "drop late handshake response (socket already replaced)")
                            continue
                        }
                        handshakeTimeoutJob?.cancel()
                        val pid = runCatching { android.os.Process.myPid() }.getOrDefault(-1)
                        SyncLog.i("WS", "handshake ok, connected (pid=$pid)")
                        Timber.tag(TAG).i("Connected to server")
                        startPingJob(webSocket)
                        onConnected?.invoke()
                    } else {
                        Timber.tag(TAG).e("Handshake error: ${handshakeResponse.error}")
                        SyncLog.e(
                            "WS",
                            "handshake rejected by server: ${handshakeResponse.error} -> will retry"
                        )
                        forceReconnect("handshake rejected: ${handshakeResponse.error}")
                    }
                    continue
                }

                val message = json.decodeFromString<SignalRInvocationMessage>(msgText)

                when (message.type) {
                    SignalRMessageType.INVOCATION -> handleInvocation(message)
                    SignalRMessageType.PING -> {
                        // 服务器发来的 ping，不需要响应
                    }
                    SignalRMessageType.CLOSE -> {
                        Timber.tag(TAG).d("Server closed connection")
                        SyncLog.i("WS", "server sent CLOSE message")
                        forceReconnect("server CLOSE message")
                    }
                }
            } catch (e: Exception) {
                SyncLog.w("WS", "parse failed: ${e.javaClass.simpleName}: ${e.message} raw=${text.take(120)}", e)
                Timber.tag(TAG).w(e, "Failed to parse message: $msgText")
            }
        }
    }

    private fun handleInvocation(message: SignalRInvocationMessage) {
        when (message.target) {
            "RemoteProfileChanged" -> {
                val args = message.arguments
                if (args != null && args.isNotEmpty()) {
                    try {
                        val profile = json.decodeFromJsonElement<ProfileDto>(args[0])
                        Timber.tag(TAG).d("Profile changed: type=${profile.type}, dataName=${profile.dataName}")
                        onProfileChanged?.invoke(profile)
                    } catch (e: Exception) {
                        Timber.tag(TAG).e(e, "Failed to parse ProfileDto")
                    }
                }
            }
            "RemoteHistoryChanged" -> {
                Timber.tag(TAG).d("History changed (ignored)")
            }
            else -> {
                Timber.tag(TAG).d("Unknown invocation: ${message.target}")
            }
        }
    }

    private inner class WebSocketListenerImpl : WebSocketListener() {
        override fun onOpen(webSocket: WebSocket, response: Response) {
            if (!isCurrentSocket(webSocket)) {
                webSocket.close(1000, "Stale connection")
                return
            }
            Timber.tag(TAG).i("WebSocket opened: ${response.message}")
            SyncLog.i("WS", "websocket opened: code=${response.code} msg=${response.message}")
            sendHandshake(webSocket)
        }

        override fun onMessage(webSocket: WebSocket, text: String) {
            Timber.tag(TAG).v("Received message: $text")
            handleMessage(webSocket, text)
        }

        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
            Timber.tag(TAG).i("WebSocket closing: $code $reason")
        }

        override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
            Timber.tag(TAG).w("WebSocket closed: $code $reason")
            val wasCurrent = synchronized(stateLock) {
                if (!isCurrentSocket(webSocket)) {
                    false
                } else {
                    this@SyncClipboardSignalR.webSocket = null
                    isConnected = false
                    isConnecting = false
                    true
                }
            }
            if (!wasCurrent) {
                Timber.tag(TAG).d("Ignoring close callback from stale WebSocket")
                return
            }
            pingJob?.cancel()
            handshakeTimeoutJob?.cancel()
            SyncLog.w("WS", "closed by peer/broken: code=$code reason=${reason.ifBlank { "-" }}")
            onDisconnected?.invoke(null)
            scheduleReconnect("onClosed code=$code")
        }

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
            Timber.tag(TAG).e(t, "WebSocket failure: ${response?.code} ${response?.message}")
            val wasCurrent = synchronized(stateLock) {
                if (!isCurrentSocket(webSocket)) {
                    false
                } else {
                    this@SyncClipboardSignalR.webSocket = null
                    isConnected = false
                    isConnecting = false
                    true
                }
            }
            if (!wasCurrent) {
                Timber.tag(TAG).d("Ignoring failure callback from stale WebSocket")
                return
            }
            pingJob?.cancel()
            handshakeTimeoutJob?.cancel()
            SyncLog.e(
                "WS",
                "failure: ${t.javaClass.simpleName}: ${t.message} http=${response?.code ?: "-"} ${response?.message ?: ""}"
            )
            onDisconnected?.invoke(t)
            scheduleReconnect("onFailure ${t.javaClass.simpleName}")
        }
    }
}
