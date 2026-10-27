/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.syncclipboard

import android.content.ClipData
import android.content.ContentValues
import android.graphics.Bitmap
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import androidx.annotation.Keep
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
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

/**
 * SyncClipboard 同步管理器
 * 使用 SignalR WebSocket 实时同步剪切板
 */
object SyncClipboardManager : CoroutineScope by CoroutineScope(SupervisorJob() + Dispatchers.Default) {

    /** 远端内容刚写入本地剪贴板后的回显去重窗口（毫秒） */
    private const val ECHO_GUARD_WINDOW_MS = 3_000L

    private val prefs = AppPrefs.getInstance().syncClipboard
    private val clipboardManager = appContext.clipboardManager
    
    private var client: SyncClipboardClient? = null

    @Keep
    private val enabledListener = ManagedPreference.OnChangeListener<Boolean> { _, enabled ->
        if (enabled) {
            startSync()
        } else {
            stopSync()
        }
    }

    private var cleanupJob: kotlinx.coroutines.Job? = null

    private val cleanupListener = ManagedPreference.OnChangeListener<Any> { _, _ ->
        scheduleCleanup()
    }

    // 服务器配置变更时自动重连
    @Keep
    private val configChangeListener = ManagedPreference.OnChangeListener<String> { _, _ ->
        if (prefs.enabled.getValue()) {
            Timber.i("SyncClipboard: Config changed, reconnecting...")
            stopSync()
            startSync()
        }
    }

    fun init() {
        SyncLog.i("Sync", "init: pid=${android.os.Process.myPid()}")
        SyncLog.logConfigSnapshot(
            enabled = prefs.enabled.getValue(),
            serverUrl = prefs.serverUrl.getValue(),
            username = prefs.username.getValue(),
            password = prefs.password.getValue()
        )
        SyncLog.i(
            "Sync",
            "flags: screenshotDetection=${prefs.screenshotDetection.getValue()} " +
                "autoUploadShot=${prefs.screenshotAutoUpload.getValue()} " +
                "saveToGallery=${prefs.saveToGallery.getValue()} " +
                "autoClearGallery=${prefs.autoClearGallery.getValue()} " +
                "clearInterval=${prefs.galleryClearInterval.getValue()}d"
        )
        Timber.i("SyncClipboardManager: init called")
        prefs.enabled.registerOnChangeListener(enabledListener)
        
        // 注册配置变更监听器，URL/用户名/密码变更时自动重连
        prefs.serverUrl.registerOnChangeListener(configChangeListener)
        prefs.username.registerOnChangeListener(configChangeListener)
        prefs.password.registerOnChangeListener(configChangeListener)
        
        // Register listeners for cleanup
        prefs.saveToGallery.registerOnChangeListener(cleanupListener)
        prefs.autoClearGallery.registerOnChangeListener(cleanupListener)
        prefs.galleryClearInterval.registerOnChangeListener(cleanupListener)
        
        ClipboardManager.addOnUpdateListener(clipboardListener)
        
        if (prefs.enabled.getValue()) {
            Timber.i("SyncClipboardManager: feature enabled, starting sync")
            startSync()
        } else {
            Timber.i("SyncClipboardManager: feature disabled")
        }
        
        // Initialize screenshot detector
        ScreenshotDetector.init()

        // Initial cleanup schedule
        scheduleCleanup()
    }

    private fun scheduleCleanup() {
        cleanupJob?.cancel()
        if (prefs.enabled.getValue() && prefs.saveToGallery.getValue() && prefs.autoClearGallery.getValue()) {
            cleanupJob = launch {
                while (true) {
                    cleanOldGalleryImages()
                    cleanClipboardImagesDir()
                    // Check every hour effectively
                    kotlinx.coroutines.delay(3600000L)
                }
            }
        }
    }

    // ... (existing code)

    private suspend fun cleanOldGalleryImages() {
        val intervalDays = prefs.galleryClearInterval.getValue()
        Timber.d("SyncClipboard: cleanOldGalleryImages called. Interval=${intervalDays} days")
        
        if (intervalDays <= 0) {
            Timber.d("SyncClipboard: Cleanup disabled (interval <= 0)")
            return
        }

        // DATE_ADDED is in seconds
        val now = System.currentTimeMillis() / 1000
        val cutoff = now - (intervalDays * 24 * 3600)
        Timber.d("SyncClipboard: Cleanup cutoff time: $cutoff (Now: $now)")
        
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
            // First count how many would be deleted (optional, for debugging)
            val projection = arrayOf(MediaStore.Images.Media._ID, MediaStore.Images.Media.DATE_ADDED, MediaStore.Images.Media.DISPLAY_NAME)
            resolver.query(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, projection, selection, selectionArgs, null)?.use { cursor ->
                Timber.d("SyncClipboard: Found ${cursor.count} images to delete")
                while (cursor.moveToNext()) {
                    val name = cursor.getString(2)
                    val dateAdded = cursor.getLong(1)
                    Timber.d("SyncClipboard: Will delete: $name (Added: $dateAdded, Cutoff: $cutoff)")
                }
            }

            val count = resolver.delete(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, selection, selectionArgs)
            if (count > 0) {
                Timber.i("SyncClipboard: Cleaned $count old images from gallery")
            } else {
                Timber.d("SyncClipboard: No images needed cleanup")
            }
        } catch (e: Exception) {
            Timber.e(e, "SyncClipboard: Failed to clean old gallery images")
        }
    }

    private fun cleanClipboardImagesDir() {
        val dir = ClipboardManager.imageDir
        val files = dir.listFiles()
        if (files.isNullOrEmpty()) {
            Timber.d("SyncClipboard: No files in clipboard_images to clean")
            return
        }

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
        } else {
            Timber.d("SyncClipboard: No clipboard_images files were deleted")
        }
    }

    private fun createSignalRClient(): SyncClipboardSignalR? {
        val url = prefs.serverUrl.getValue()
        val username = prefs.username.getValue()
        val password = prefs.password.getValue()
        
        if (url.isBlank() || username.isBlank() || password.isBlank()) {
            Timber.w("SyncClipboard: Missing configuration")
            return null
        }
        
        return SyncClipboardSignalR(url.trimEnd('/'), username, password)
    }

    private val pendingEchoes = java.util.Collections.synchronizedSet(HashSet<String>())
    // 图片上传的回显过滤：记录我们自己上传的图片文件名，防止服务器推送回来时重复保存
    private val pendingImageEchoes = java.util.Collections.synchronizedSet(HashSet<String>())

    private val clipboardListener = ClipboardManager.OnClipboardUpdateListener { entry ->
        // 本机剪贴板变化 -> 上传。这里每一个"放弃上传"的分支都必须留痕，
        // 否则出问题时无法区分"没触发"和"触发了但被过滤"。
        val detail = if (entry.isImage) "path=${entry.imagePath}" else SyncLog.textMeta(entry.text)
        SyncLog.d("UP", "local clip update: image=${entry.isImage} sensitive=${entry.sensitive} $detail")
        // 仅处理文本更新 (图片目前是手动上传)
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
        // 避免回环：只把"刚刚应用远端内容"这一小段时间内的相同文本当成回显。
        // 之前是无条件用 lastReceivedText 去重，导致用户过一会儿再手动复制同一段文本时
        // 永远传不上去（日志里表现为 skip: identical to lastReceivedText），也是"偶发不生效"的成因之一。
        if (text == lastReceivedText &&
            android.os.SystemClock.elapsedRealtime() - lastReceivedTextAt < ECHO_GUARD_WINDOW_MS
        ) {
            SyncLog.d("UP", "skip: echo of just-applied remote text ${SyncLog.textMeta(text)}")
            return@OnClipboardUpdateListener
        }
        launch {
            uploadText(text)
        }
    }


    private var signalRClient: SyncClipboardSignalR? = null

    private fun startSync() {
        Timber.i("SyncClipboardManager: attempting to start sync")
        SyncLog.i("Sync", "startSync requested")
        signalRClient?.disconnect()
        signalRClient = createSignalRClient()
        client = createClient()  // 初始化 HTTP 客户端
        
        if (signalRClient == null) {
            SyncLog.w("Sync", "startSync aborted: missing server url / username / password")
            Timber.w("SyncClipboardManager: failed to create client (missing config?)")
            return
        }
        
        signalRClient?.onProfileChanged = { profile ->
            launch {
                handleProfileChanged(profile)
            }
        }
        
        signalRClient?.onConnected = {
            SyncLog.i("WS", "connected")
            Timber.i("SyncClipboard: WebSocket connected")
        }
        
        signalRClient?.onDisconnected = { error ->
            if (error != null) {
                SyncLog.w("WS", "disconnected: ${error.javaClass.simpleName}: ${error.message}")
                Timber.w(error, "SyncClipboard: WebSocket disconnected")
            } else {
                SyncLog.i("WS", "disconnected (clean)")
                Timber.i("SyncClipboard: WebSocket disconnected (clean)")
            }
        }
        
        signalRClient?.connect()
        SyncLog.i("Sync", "started with WebSocket")
        Timber.i("SyncClipboard: Started with WebSocket")
    }

    private fun stopSync() {
        Timber.i("SyncClipboardManager: stopSync called")
        SyncLog.i("Sync", "stopSync called by user/config change")
        signalRClient?.disconnect()
        signalRClient = null
        client = null
        Timber.i("SyncClipboard: Stopped")
    }

    /**
     * 处理服务器推送的 ProfileDto
     */
    private suspend fun handleProfileChanged(profile: ProfileDto) {
        // 注意：不要再把 text 前 N 个字符写进日志，落盘日志要避免泄露剪贴板内容
        Timber.d("SyncClipboardManager: handleProfileChanged: type=${profile.type}, dataName=${profile.dataName}")
        SyncLog.d("DOWN", "profile: type=${profile.type} hasData=${profile.hasData} dataName=${profile.dataName} ${SyncLog.textMeta(profile.text)}")
        try {
            when (profile.type) {
                ProfileDto.TYPE_TEXT -> {
                    val text = profile.text
                    // 检查是否是我们自己发送的回显
                    if (pendingEchoes.remove(text)) {
                        SyncLog.d("DOWN", "skip: own echo (pendingEchoes) ${SyncLog.textMeta(text)}")
                        Timber.d("SyncClipboard: Ignoring echo")
                        return
                    }

                    if (text.isNullOrBlank()) {
                        SyncLog.w("DOWN", "skip: empty text in profile")
                        return
                    }
                    val local = getLocalClipboardText()
                    if (text == local) {
                        SyncLog.d("DOWN", "skip: already equal to local clipboard ${SyncLog.textMeta(text)}")
                        return
                    }

                    lastReceivedText = text
                    lastReceivedTextAt = android.os.SystemClock.elapsedRealtime()
                    SyncLog.i("DOWN", "apply remote text -> local clipboard ${SyncLog.textMeta(text)} pendingEchoes=${pendingEchoes.size}")
                    setLocalClipboard(text)
                }
                ProfileDto.TYPE_IMAGE, ProfileDto.TYPE_FILE -> {
                    val dataName = profile.dataName
                    if (dataName.isNullOrBlank() || !profile.hasData) {
                        SyncLog.w("DOWN", "skip: file profile without dataName/hasData (name=$dataName hasData=${profile.hasData})")
                        return
                    }
                    // 检查是否是我们自己上传的图片回显
                    if (pendingImageEchoes.remove(dataName)) {
                        SyncLog.d("DOWN", "skip: own image echo $dataName")
                        Timber.d("SyncClipboard: Ignoring image echo: $dataName")
                        return
                    }
                    val httpClient = client ?: createClient()
                    if (httpClient == null) {
                        SyncLog.w("DOWN", "skip: no http client (missing config)")
                        return
                    }
                    SyncLog.i("DOWN", "downloading file $dataName ...")
                    val startedAt = android.os.SystemClock.elapsedRealtime()
                    val imageResult = httpClient.downloadFile(dataName)
                    val cost = android.os.SystemClock.elapsedRealtime() - startedAt
                    imageResult.onSuccess { bytes ->
                        SyncLog.i("DOWN", "downloaded $dataName (${bytes.size} bytes) in ${cost}ms")
                        saveImageToClipboardHistory(bytes, dataName)
                        if (prefs.saveToGallery.getValue()) {
                            // 重命名文件，避免以 "screenshot" 开头
                            // 从而不会被 ScreenshotDetector 误识别为新截图导致无限循环
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
            }
        } catch (e: Exception) {
            SyncLog.e("DOWN", "error handling profile: ${e.javaClass.simpleName}: ${e.message}", e)
            Timber.e(e, "SyncClipboard: Error handling profile change")
        }
    }

    private var lastReceivedText: String? = null
    /** lastReceivedText 写入时刻（elapsedRealtime），回显去重只在短窗口内有效 */
    private var lastReceivedTextAt = 0L

    private suspend fun uploadText(text: String) {
        val client = client ?: createClient()
        if (client == null) {
            SyncLog.w("UP", "aborted: no http client (server url / username / password missing)")
            return
        }
        SyncLog.i("UP", "PUT /SyncClipboard.json ${SyncLog.textMeta(text)}")
        pendingEchoes.add(text)
        val startedAt = android.os.SystemClock.elapsedRealtime()
        val result = client.putClipboard(SyncClipboardData.text(text))
        val cost = android.os.SystemClock.elapsedRealtime() - startedAt
        if (result.isSuccess) {
            SyncLog.i("UP", "ok in ${cost}ms ${SyncLog.textMeta(text)}")
            Timber.d("SyncClipboard: Uploaded text in ${cost}ms")
        } else {
            SyncLog.w("UP", "failed in ${cost}ms: ${result.exceptionOrNull()?.javaClass?.simpleName}: ${result.exceptionOrNull()?.message}")
            Timber.w("SyncClipboard: Failed to upload text")
            pendingEchoes.remove(text)
        }
    }


    /**
     * 创建 HTTP 客户端（用于文件下载等）
     */
    private fun createClient(): SyncClipboardClient? {
        val url = prefs.serverUrl.getValue()
        val username = prefs.username.getValue()
        val password = prefs.password.getValue()
        
        if (url.isBlank() || username.isBlank() || password.isBlank()) {
            return null
        }
        
        return SyncClipboardClient(url.trimEnd('/'), username, password)
    }

    /**
     * 保存图片到剪贴板历史（会在键盘剪贴板面板显示）
     */
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

    /**
     * 上传图片到服务器
     */
    suspend fun uploadImage(bitmap: Bitmap, filename: String = "image.png"): Boolean {
        val client = client ?: createClient()
        if (client == null) {
            SyncLog.w("UP", "image aborted: no http client (missing config)")
            return false
        }
        
        val bytes = ByteArrayOutputStream().use { stream ->
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
            stream.toByteArray()
        }
        
        SyncLog.i("UP", "image upload start name=$filename size=${bytes.size} ${bitmap.width}x${bitmap.height}")
        val hash = calculateServerHash(filename, bytes)
        val uploadResult = client.uploadFile(filename, bytes)
        
        // 记录上传的文件名，用于过滤服务器推回来的回显
        pendingImageEchoes.add(filename)
        
        return uploadResult.map {
            val data = SyncClipboardData.image(hash, filename)
            val success = client.putClipboard(data).isSuccess
            if (success) {
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
        val client = client ?: createClient()
        if (client == null) {
            SyncLog.w("UP", "latest image aborted: no http client (missing config)")
            return false
        }
        
        val filename = "sync_${System.currentTimeMillis()}.png"
        SyncLog.i("UP", "latest image upload start name=$filename size=${bytes.size}")
        val hash = calculateServerHash(filename, bytes)
        
        // 记录上传的文件名，用于过滤服务器推回来的回显
        pendingImageEchoes.add(filename)
        
        val uploadResult = client.uploadFile(filename, bytes)
        return uploadResult.map {
            val data = SyncClipboardData.image(hash, filename)
            val success = client.putClipboard(data).isSuccess
            if (success) {
                SyncLog.i("UP", "latest image upload ok name=$filename")
            } else {
                SyncLog.w("UP", "latest image upload failed at putClipboard name=$filename")
                pendingImageEchoes.remove(filename)
            }
            success
        }.getOrElse {
            SyncLog.w("UP", "latest image upload failed at uploadFile name=$filename: ${it.javaClass.simpleName}: ${it.message}")
            pendingImageEchoes.remove(filename)
            false
        }
    }

    /**
     * 获取本地剪贴板文本
     * 
     * 由于 Android 10+ 的安全限制，后台应用无法直接读取系统剪贴板
     * (错误: "Denying clipboard access to app, application is not in focus")
     * 
     * 解决方案：使用内部 ClipboardManager.lastEntry，它在键盘显示时（IME 获得焦点）
     * 通过 OnPrimaryClipChangedListener 捕获剪贴板内容
     */
    private fun getLocalClipboardText(): String? {
        // 优先使用内部 ClipboardManager 的缓存数据
        // 这些数据是在 IME 获得焦点时通过 OnPrimaryClipChangedListener 捕获的
        val lastEntry = ClipboardManager.lastEntry
        if (lastEntry != null && !lastEntry.isImage && lastEntry.text.isNotBlank()) {
            SyncLog.d("CB", "local text source=cache ${SyncLog.textMeta(lastEntry.text)}")
            return lastEntry.text
        }
        
        // 如果内部缓存为空，尝试直接读取系统剪贴板（仅在 IME 获得焦点时有效）
        return try {
            val clip = clipboardManager.primaryClip
            if (clip == null) {
                SyncLog.d("CB", "local text source=none (cache empty, primaryClip=null)")
                return null
            }
            if (clip.itemCount == 0) {
                SyncLog.d("CB", "local text source=none (cache empty, clip has 0 items)")
                return null
            }
            clip.getItemAt(0)?.text?.toString()?.also {
                SyncLog.d("CB", "local text source=system ${SyncLog.textMeta(it)}")
            }
        } catch (e: Exception) {
            // Android 10+ 在后台时会抛出 SecurityException
            SyncLog.w("CB", "cannot access system clipboard: ${e.javaClass.simpleName}: ${e.message}")
            Timber.d("Cannot access system clipboard (expected on Android 10+ in background)")
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

    private fun calculateServerHash(filename: String, bytes: ByteArray): String {
        val digest = MessageDigest.getInstance("SHA-256")
        
        // 1. Calculate SHA256 of the content
        val contentHashBytes = digest.digest(bytes)
        // Microsoft's Convert.ToHexString returns Uppercase Hex
        val contentHashHex = contentHashBytes.joinToString("") { "%02X".format(it) }
        
        // 2. Combine filename and uppercase content hash
        // Server side: var combinedString = $"{fileName}|{contentHash.ToUpperInvariant()}";
        val combined = "$filename|$contentHashHex"
        
        // 3. Calculate SHA256 of the combined string
        val finalHashBytes = digest.digest(combined.toByteArray(Charsets.UTF_8))
        
        // 4. Return as Uppercase Hex
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
            } else {
                Timber.w("SyncClipboard: Failed to create gallery entry")
            }
        } catch (e: Exception) {
            Timber.e(e, "SyncClipboard: Failed to save to gallery")
        }
    }


}
