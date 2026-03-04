/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.clipboard.db

import android.content.ClipData
import android.content.ClipDescription
import android.content.ContentResolver
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Build
import androidx.room.ColumnInfo
import androidx.room.Entity
import androidx.room.PrimaryKey
import org.fcitx.fcitx5.android.utils.timestamp
import timber.log.Timber
import java.io.File
import java.io.FileOutputStream
import java.security.MessageDigest
import java.util.UUID

@Entity(tableName = ClipboardEntry.TABLE_NAME)
data class ClipboardEntry(
    @PrimaryKey(autoGenerate = true)
    val id: Int = 0,
    val text: String,
    val pinned: Boolean = false,
    @ColumnInfo(defaultValue = "-1")
    val timestamp: Long = System.currentTimeMillis(),
    @ColumnInfo(defaultValue = ClipDescription.MIMETYPE_TEXT_PLAIN)
    val type: String = ClipDescription.MIMETYPE_TEXT_PLAIN,
    @ColumnInfo(defaultValue = "0")
    val deleted: Boolean = false,
    @ColumnInfo(defaultValue = "0")
    val sensitive: Boolean = false,
    // 新增：图片文件路径
    @ColumnInfo(defaultValue = "")
    val imagePath: String = ""
) {
    /**
     * 是否是图片类型
     */
    val isImage: Boolean
        get() = imagePath.isNotEmpty() && type.startsWith("image/")

    companion object {
        const val BULLET = "•"
        const val TABLE_NAME = "clipboard"

        private val IS_SENSITIVE = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ClipDescription.EXTRA_IS_SENSITIVE
        } else {
            "android.content.extra.IS_SENSITIVE"
        }

        /**
         * 从 ClipData 创建 ClipboardEntry
         * 支持文本和图片
         */
        fun fromClipData(
            clipData: ClipData,
            context: Context,
            imageDir: File,
            transformer: ((String) -> String)? = null
        ): ClipboardEntry? {
            val desc = clipData.description
            val item = clipData.getItemAt(0) ?: return null
            val mimeType = desc.getMimeType(0)
            val sensitive = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                desc.extras?.getBoolean(IS_SENSITIVE) ?: false
            } else {
                false
            }
            val timestamp = clipData.timestamp()

            // 检查是否是图片
            if (mimeType.startsWith("image/")) {
                val uri = item.uri
                if (uri != null) {
                    val imagePath = saveImageFromUri(context, uri, imageDir)
                    if (imagePath != null) {
                        return ClipboardEntry(
                            text = "[图片]",
                            timestamp = timestamp,
                            type = mimeType,
                            sensitive = sensitive,
                            imagePath = imagePath
                        )
                    }
                }
            }

            // 处理文本
            val str = item.text?.toString() ?: return null
            return ClipboardEntry(
                text = if (transformer != null) transformer(str) else str,
                timestamp = timestamp,
                type = mimeType,
                sensitive = sensitive
            )
        }

        /**
         * 兼容旧版调用（仅文本）
         */
        fun fromClipData(
            clipData: ClipData,
            transformer: ((String) -> String)? = null
        ): ClipboardEntry? {
            val desc = clipData.description
            val item = clipData.getItemAt(0) ?: return null
            val str = item.text?.toString() ?: return null
            val sensitive = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                desc.extras?.getBoolean(IS_SENSITIVE) ?: false
            } else {
                false
            }
            return ClipboardEntry(
                text = if (transformer != null) transformer(str) else str,
                timestamp = clipData.timestamp(),
                type = desc.getMimeType(0),
                sensitive = sensitive
            )
        }

        /**
         * 从 URI 保存图片到本地文件
         */
        private fun saveImageFromUri(context: Context, uri: Uri, imageDir: File): String? {
            return try {
                val contentResolver = context.contentResolver
                val inputStream = contentResolver.openInputStream(uri) ?: return null
                
                // 生成唯一文件名
                val filename = "clip_${System.currentTimeMillis()}_${UUID.randomUUID().toString().take(8)}.png"
                val imageFile = File(imageDir, filename)
                
                // 确保目录存在
                imageDir.mkdirs()
                
                // 读取并压缩图片
                val bitmap = BitmapFactory.decodeStream(inputStream)
                inputStream.close()
                
                if (bitmap == null) return null
                
                // 缩放大图
                val scaledBitmap = scaleBitmapIfNeeded(bitmap, 1024)
                
                // 保存为 PNG
                FileOutputStream(imageFile).use { fos ->
                    scaledBitmap.compress(Bitmap.CompressFormat.PNG, 90, fos)
                }
                
                if (scaledBitmap != bitmap) {
                    scaledBitmap.recycle()
                }
                bitmap.recycle()
                
                imageFile.absolutePath
            } catch (e: Exception) {
                Timber.e(e, "Failed to save image from URI: $uri")
                null
            }
        }

        /**
         * 缩放过大的图片
         */
        private fun scaleBitmapIfNeeded(bitmap: Bitmap, maxSize: Int): Bitmap {
            val width = bitmap.width
            val height = bitmap.height
            
            if (width <= maxSize && height <= maxSize) {
                return bitmap
            }
            
            val scale = maxSize.toFloat() / maxOf(width, height)
            val newWidth = (width * scale).toInt()
            val newHeight = (height * scale).toInt()
            
            return Bitmap.createScaledBitmap(bitmap, newWidth, newHeight, true)
        }

        /**
         * 从字节数组创建图片 ClipboardEntry（用于 SyncClipboard）
         */
        fun fromImageBytes(bytes: ByteArray, imageDir: File, mimeType: String = "image/png"): ClipboardEntry? {
            return try {
                val filename = "sync_${System.currentTimeMillis()}_${UUID.randomUUID().toString().take(8)}.png"
                val imageFile = File(imageDir, filename)
                imageDir.mkdirs()
                
                imageFile.writeBytes(bytes)
                
                ClipboardEntry(
                    text = "[同步图片]",
                    timestamp = System.currentTimeMillis(),
                    type = mimeType,
                    imagePath = imageFile.absolutePath
                )
            } catch (e: Exception) {
                Timber.e(e, "Failed to save image from bytes")
                null
            }
        }
    }
}