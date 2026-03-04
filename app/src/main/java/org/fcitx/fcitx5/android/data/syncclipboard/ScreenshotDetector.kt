/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.syncclipboard

import android.content.ContentResolver
import android.database.ContentObserver
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
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
import timber.log.Timber
import java.io.File

/**
 * Detects screenshots using ContentObserver on MediaStore
 * Similar to how Gboard shows screenshots in clipboard
 */
object ScreenshotDetector : CoroutineScope by CoroutineScope(SupervisorJob() + Dispatchers.Default) {

    private val prefs = AppPrefs.getInstance().syncClipboard
    private val contentResolver: ContentResolver = appContext.contentResolver
    
    private var observer: ScreenshotContentObserver? = null
    
    // Debounce interval in milliseconds
    private const val DEBOUNCE_INTERVAL = 1000L

    @Keep
    private val detectionEnabledListener = ManagedPreference.OnChangeListener<Boolean> { _, enabled ->
        if (enabled) {
            startDetection()
        } else {
            stopDetection()
        }
    }

    fun init() {
        prefs.screenshotDetection.registerOnChangeListener(detectionEnabledListener)
        
        if (prefs.screenshotDetection.getValue()) {
            startDetection()
        }
    }

    private fun startDetection() {
        if (observer != null) return
        
        observer = ScreenshotContentObserver(Handler(Looper.getMainLooper())) { uri ->
            launch { processNewImage(uri) }
        }
        
        // Register for external images
        contentResolver.registerContentObserver(
            MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
            true,
            observer!!
        )
        
        Timber.i("ScreenshotDetector: Started detection")
    }

    private fun stopDetection() {
        observer?.let {
            contentResolver.unregisterContentObserver(it)
        }
        observer = null
        Timber.i("ScreenshotDetector: Stopped detection")
    }

    private class ScreenshotContentObserver(
        handler: Handler,
        private val onScreenshotDetected: (Uri) -> Unit
    ) : ContentObserver(handler) {
        
        private var lastProcessedUri: Uri? = null
        private var lastProcessedTime: Long = 0
        
        override fun onChange(selfChange: Boolean, uri: Uri?) {
            super.onChange(selfChange, uri)
            
            if (uri == null) return
            
            // Debounce: avoid processing the same screenshot multiple times
            val now = System.currentTimeMillis()
            if (uri == lastProcessedUri && now - lastProcessedTime < DEBOUNCE_INTERVAL) {
                return
            }
            
            lastProcessedUri = uri
            lastProcessedTime = now
            onScreenshotDetected(uri)
        }
    }

    private suspend fun processNewImage(uri: Uri) {
        try {
            // Query for the image details
            val projection = arrayOf(
                MediaStore.Images.Media._ID,
                MediaStore.Images.Media.DATA,
                MediaStore.Images.Media.DISPLAY_NAME,
                MediaStore.Images.Media.DATE_ADDED
            )
            
            val cursor = contentResolver.query(
                uri,
                projection,
                null,
                null,
                "${MediaStore.Images.Media.DATE_ADDED} DESC"
            )
            
            cursor?.use {
                if (!it.moveToFirst()) return
                
                val dataColumn = it.getColumnIndexOrThrow(MediaStore.Images.Media.DATA)
                val path = it.getString(dataColumn) ?: return
                
                // Check if it's a screenshot (common patterns)
                if (!isScreenshot(path)) {
                    Timber.d("ScreenshotDetector: Skipping non-screenshot: $path")
                    return
                }
                
                Timber.d("ScreenshotDetector: Detected screenshot: $path")
                
                // Add to clipboard
                addScreenshotToClipboard(path, uri)
            }
        } catch (e: Exception) {
            Timber.e(e, "ScreenshotDetector: Failed to process image")
        }
    }

    /**
     * Check if the image path indicates a screenshot
     * Common patterns:
     * - /Screenshots/ in path
     * - /DCIM/Screenshots/ 
     * - Filename starts with "Screenshot"
     * - Filename contains "screenshot"
     */
    private fun isScreenshot(path: String): Boolean {
        val lowerPath = path.lowercase()
        val fileName = File(path).name.lowercase()
        
        return lowerPath.contains("/screenshots/") ||
               lowerPath.contains("/screenshot") ||
               fileName.startsWith("screenshot") ||
               fileName.contains("screen_shot") ||
               fileName.contains("screen-shot") ||
               // Some devices use different patterns
               (lowerPath.contains("/dcim/") && fileName.contains("screenshot"))
    }

    private suspend fun addScreenshotToClipboard(path: String, uri: Uri) {
        try {
            val file = File(path)
            if (!file.exists()) {
                Timber.w("ScreenshotDetector: Screenshot file not found: $path")
                return
            }
            
            val bytes = file.readBytes()
            val imageDir = ClipboardManager.imageDir
            
            // Create clipboard entry from image bytes
            val entry = ClipboardEntry.fromImageBytes(bytes, imageDir, "image/png")
            if (entry != null) {
                ClipboardManager.insertImageEntry(entry)
                Timber.i("ScreenshotDetector: Screenshot added to clipboard")
                
                // Auto-upload if enabled
                if (prefs.screenshotAutoUpload.getValue() && prefs.enabled.getValue()) {
                    uploadScreenshot(bytes)
                }
            }
        } catch (e: Exception) {
            Timber.e(e, "ScreenshotDetector: Failed to add screenshot to clipboard")
        }
    }

    private suspend fun uploadScreenshot(bytes: ByteArray) {
        try {
            val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
            if (bitmap != null) {
                val filename = "screenshot_${System.currentTimeMillis()}.png"
                val success = SyncClipboardManager.uploadImage(bitmap, filename)
                if (success) {
                    Timber.i("ScreenshotDetector: Screenshot uploaded successfully")
                } else {
                    Timber.w("ScreenshotDetector: Failed to upload screenshot")
                }
                bitmap.recycle()
            }
        } catch (e: Exception) {
            Timber.e(e, "ScreenshotDetector: Failed to upload screenshot")
        }
    }
}
