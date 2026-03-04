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

    fun init() {
        Timber.i("SyncClipboardManager: init called")
        prefs.enabled.registerOnChangeListener(enabledListener)
        
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

    private val clipboardListener = ClipboardManager.OnClipboardUpdateListener { entry ->
        // 仅处理文本更新 (图片目前似乎是手动或 separate logic handles?)
        // 实际上 SyncClipboardManager 原有逻辑也没有自动上传图片
        // 只有手动上传 (uploadLatestImage)
        if (prefs.enabled.getValue() && !entry.isImage) {
             val text = entry.text
             if (text.isNotBlank()) {
                 // 避免循环：如果我们刚刚收到这个文本，就不回传了
                 // 但 handleProfileChanged 已经处理了 setLocalClipboard
                 // ClipboardManager 会再次回调 listener
                 // 我们需要区分来源
                 
                 // 如果文本在 pendingEchoes 中，说明是我们发的（且收到了回显），或者是刚刚收到的
                 // 但这里是 *本地* 更新触发的
                 // 如果 handleProfileChanged 调用了 setLocalClipboard -> trigger listener -> here
                 // 此时 pendingEchoes 应该包含该文本 (如果是 echo)
                 // 或者是我们刚收到的
                 
                 // 简化逻辑：如果内容与我们最后一次收到的内容相同，忽略
                 if (text == lastReceivedText) {
                     return@OnClipboardUpdateListener
                 }
                 
                 launch {
                     uploadText(text)
                 }
             }
        }
    }


    private var signalRClient: SyncClipboardSignalR? = null

    private fun startSync() {
        Timber.i("SyncClipboardManager: attempting to start sync")
        signalRClient?.disconnect()
        signalRClient = createSignalRClient()
        
        if (signalRClient == null) {
            Timber.w("SyncClipboardManager: failed to create client (missing config?)")
            return
        }
        
        signalRClient?.onProfileChanged = { profile ->
            launch {
                handleProfileChanged(profile)
            }
        }
        
        signalRClient?.onConnected = {
            Timber.i("SyncClipboard: WebSocket connected")
        }
        
        signalRClient?.onDisconnected = { error ->
            if (error != null) {
                Timber.w(error, "SyncClipboard: WebSocket disconnected")
            } else {
                Timber.i("SyncClipboard: WebSocket disconnected (clean)")
            }
        }
        
        signalRClient?.connect()
        Timber.i("SyncClipboard: Started with WebSocket")
    }

    private fun stopSync() {
        Timber.i("SyncClipboardManager: stopSync called")
        signalRClient?.disconnect()
        signalRClient = null
        client = null
        Timber.i("SyncClipboard: Stopped")
    }

    /**
     * 处理服务器推送的 ProfileDto
     */
    private suspend fun handleProfileChanged(profile: ProfileDto) {
        Timber.d("SyncClipboardManager: handleProfileChanged: type=${profile.type}, text=${profile.text?.take(20)}..., dataName=${profile.dataName}")
        try {
            when (profile.type) {
                ProfileDto.TYPE_TEXT -> {
                    val text = profile.text
                    // 检查是否是我们自己发送的回显
                    if (pendingEchoes.remove(text)) {
                        Timber.d("SyncClipboard: Ignoring echo")
                        return
                    }

                    if (!text.isNullOrBlank() && text != getLocalClipboardText()) {
                        lastReceivedText = text
                        setLocalClipboard(text)
                        Timber.d("SyncClipboard: Received text from server")
                    }
                }
                ProfileDto.TYPE_IMAGE, ProfileDto.TYPE_FILE -> {
                    val dataName = profile.dataName
                    if (!dataName.isNullOrBlank() && profile.hasData) {
                        val httpClient = client ?: createClient() ?: return
                        val imageResult = httpClient.downloadFile(dataName)
                        imageResult.onSuccess { bytes ->
                            saveImageToClipboardHistory(bytes, dataName)
                            if (prefs.saveToGallery.getValue()) {
                                saveToGallery(bytes, dataName)
                            }
                            Timber.d("SyncClipboard: Downloaded file from server: $dataName")
                        }
                    }
                }
            }
        } catch (e: Exception) {
            Timber.e(e, "SyncClipboard: Error handling profile change")
        }
    }

    private var lastReceivedText: String? = null

    private suspend fun uploadText(text: String) {
        val client = client ?: createClient() ?: return
        pendingEchoes.add(text)
        val result = client.putClipboard(SyncClipboardData.text(text))
        if (result.isSuccess) {
            Timber.d("SyncClipboard: Uploaded text: ${text.take(20)}...")
        } else {
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
                Timber.d("SyncClipboard: Image saved to clipboard history")
            }
        } catch (e: Exception) {
            Timber.e(e, "Failed to save image to clipboard history")
        }
    }

    /**
     * 上传图片到服务器
     */
    suspend fun uploadImage(bitmap: Bitmap, filename: String = "image.png"): Boolean {
        val client = client ?: createClient() ?: return false
        
        val bytes = ByteArrayOutputStream().use { stream ->
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
            stream.toByteArray()
        }
        
        val hash = calculateServerHash(filename, bytes)
        val uploadResult = client.uploadFile(filename, bytes)
        
        return uploadResult.map {
            val data = SyncClipboardData.image(hash, filename)
            client.putClipboard(data).isSuccess
        }.getOrDefault(false)
    }

    /**
     * 上传剪贴板中最新的图片到服务器
     */
    suspend fun uploadLatestImage(): Boolean {
        val lastEntry = ClipboardManager.lastEntry ?: return false
        if (!lastEntry.isImage) return false
        
        val file = File(lastEntry.imagePath)
        if (!file.exists()) return false
        
        val bytes = file.readBytes()
        val client = client ?: createClient() ?: return false
        
        val filename = "sync_${System.currentTimeMillis()}.png"
        val hash = calculateServerHash(filename, bytes)
        
        val uploadResult = client.uploadFile(filename, bytes)
        return uploadResult.map {
            val data = SyncClipboardData.image(hash, filename)
            client.putClipboard(data).isSuccess
        }.getOrDefault(false)
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
            return lastEntry.text
        }
        
        // 如果内部缓存为空，尝试直接读取系统剪贴板（仅在 IME 获得焦点时有效）
        return try {
            val clip = clipboardManager.primaryClip ?: return null
            if (clip.itemCount == 0) return null
            clip.getItemAt(0)?.text?.toString()
        } catch (e: Exception) {
            // Android 10+ 在后台时会抛出 SecurityException
            Timber.d("Cannot access system clipboard (expected on Android 10+ in background)")
            null
        }
    }

    private fun setLocalClipboard(text: String) {
        try {
            val clip = ClipData.newPlainText("SyncClipboard", text)
            clipboardManager.setPrimaryClip(clip)
        } catch (e: Exception) {
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
