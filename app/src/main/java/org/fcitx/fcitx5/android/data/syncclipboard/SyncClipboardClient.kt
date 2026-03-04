/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.syncclipboard

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import timber.log.Timber
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.util.Base64

/**
 * SyncClipboard API 客户端
 * 使用 HttpURLConnection 实现，支持 Basic Auth
 */
class SyncClipboardClient(
    private val serverUrl: String,
    private val username: String,
    private val password: String
) {
    private val json = Json { 
        ignoreUnknownKeys = true 
        encodeDefaults = true
    }
    
    private val authHeader: String by lazy {
        val credentials = "$username:$password"
        val encoded = Base64.getEncoder().encodeToString(credentials.toByteArray())
        "Basic $encoded"
    }

    /**
     * 获取服务器剪切板内容
     */
    suspend fun getClipboard(): Result<SyncClipboardData> = withContext(Dispatchers.IO) {
        try {
            val url = URL("$serverUrl/SyncClipboard.json")
            val connection = (url.openConnection() as HttpURLConnection).apply {
                requestMethod = "GET"
                setRequestProperty("Authorization", authHeader)
                connectTimeout = 5000
                readTimeout = 5000
            }
            
            val responseCode = connection.responseCode
            if (responseCode == HttpURLConnection.HTTP_OK) {
                val responseBody = connection.inputStream.bufferedReader().readText()
                val data = json.decodeFromString<SyncClipboardData>(responseBody)
                Result.success(data)
            } else {
                Timber.e("Failed to get clipboard: HTTP $responseCode")
                Result.failure(IOException("HTTP error: $responseCode"))
            }
        } catch (e: Exception) {
            Timber.e(e, "Failed to get clipboard from server")
            Result.failure(e)
        }
    }

    /**
     * 上传剪切板内容到服务器
     */
    suspend fun putClipboard(data: SyncClipboardData): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val url = URL("$serverUrl/SyncClipboard.json")
            val connection = (url.openConnection() as HttpURLConnection).apply {
                requestMethod = "PUT"
                setRequestProperty("Authorization", authHeader)
                setRequestProperty("Content-Type", "application/json")
                doOutput = true
                connectTimeout = 5000
                readTimeout = 5000
            }
            
            val body = json.encodeToString(data)
            connection.outputStream.bufferedWriter().use { it.write(body) }
            
            val responseCode = connection.responseCode
            if (responseCode in 200..299) {
                Result.success(Unit)
            } else {
                Timber.e("Failed to put clipboard: HTTP $responseCode")
                Result.failure(IOException("HTTP error: $responseCode"))
            }
        } catch (e: Exception) {
            Timber.e(e, "Failed to put clipboard to server")
            Result.failure(e)
        }
    }

    /**
     * 下载文件
     */
    suspend fun downloadFile(filename: String): Result<ByteArray> = withContext(Dispatchers.IO) {
        try {
            val url = URL("$serverUrl/file/$filename")
            val connection = (url.openConnection() as HttpURLConnection).apply {
                requestMethod = "GET"
                setRequestProperty("Authorization", authHeader)
                connectTimeout = 10000
                readTimeout = 30000
            }
            
            val responseCode = connection.responseCode
            if (responseCode == HttpURLConnection.HTTP_OK) {
                val bytes = connection.inputStream.readBytes()
                Result.success(bytes)
            } else {
                Timber.e("Failed to download file: HTTP $responseCode")
                Result.failure(IOException("HTTP error: $responseCode"))
            }
        } catch (e: Exception) {
            Timber.e(e, "Failed to download file: $filename")
            Result.failure(e)
        }
    }

    /**
     * 上传文件
     */
    suspend fun uploadFile(filename: String, data: ByteArray): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val url = URL("$serverUrl/file/$filename")
            val connection = (url.openConnection() as HttpURLConnection).apply {
                requestMethod = "PUT"
                setRequestProperty("Authorization", authHeader)
                setRequestProperty("Content-Type", "application/octet-stream")
                doOutput = true
                connectTimeout = 10000
                readTimeout = 30000
            }
            
            connection.outputStream.use { it.write(data) }
            
            val responseCode = connection.responseCode
            if (responseCode in 200..299) {
                Result.success(Unit)
            } else {
                Timber.e("Failed to upload file $filename: HTTP $responseCode")
                Result.failure(IOException("HTTP error: $responseCode"))
            }
        } catch (e: Exception) {
            Timber.e(e, "Failed to upload file: $filename")
            Result.failure(e)
        }
    }
}
