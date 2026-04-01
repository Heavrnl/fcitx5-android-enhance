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
        private const val INITIAL_RECONNECT_DELAY_MS = 3000L  // 初始重连间隔 3 秒
        private const val MAX_RECONNECT_DELAY_MS = 60000L     // 最大重连间隔 60 秒
        private const val PING_INTERVAL_MS = 15000L
        private const val HANDSHAKE_TIMEOUT_MS = 15000L       // 握手超时 15 秒
    }

    private val json = Json { 
        ignoreUnknownKeys = true 
        isLenient = true
        encodeDefaults = true
    }
    
    private val client = OkHttpClient.Builder()
        .pingInterval(30, TimeUnit.SECONDS)
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.MINUTES) // No timeout for WebSocket
        .build()

    private var webSocket: WebSocket? = null
    private var isConnected = false
    private var isConnecting = false       // 标记是否正在建立连接（防止并发连接）
    private var shouldReconnect = true
    private var reconnectAttempt = 0       // 当前重连次数（用于指数退避）
    private var pingJob: Job? = null
    private var reconnectJob: Job? = null
    private var handshakeTimeoutJob: Job? = null  // 握手超时检测任务

    var onProfileChanged: ((ProfileDto) -> Unit)? = null
    var onConnected: (() -> Unit)? = null
    var onDisconnected: ((Throwable?) -> Unit)? = null

    private val authHeader: String by lazy {
        val credentials = "$username:$password"
        val encoded = Base64.getEncoder().encodeToString(credentials.toByteArray())
        "Basic $encoded"
    }

    /**
     * 连接到 SignalR Hub
     */
    fun connect() {
        if (isConnected || isConnecting) {
            Timber.tag(TAG).d("Already connected or connecting, skipping")
            return
        }
        isConnecting = true
        shouldReconnect = true
        
        // 清理旧的 WebSocket，防止多个实例并存
        webSocket?.close(1000, "Reconnecting")
        webSocket = null
        
        val wsUrl = buildWebSocketUrl()
        Timber.tag(TAG).d("Connecting to: $wsUrl (attempt=$reconnectAttempt)")

        val request = Request.Builder()
            .url(wsUrl)
            .addHeader("Authorization", authHeader)
            .build()

        webSocket = client.newWebSocket(request, WebSocketListenerImpl())
        
        // 启动握手超时检测：如果超时未完成握手，强制断开并重连
        startHandshakeTimeout()
    }

    /**
     * 断开连接
     */
    fun disconnect() {
        shouldReconnect = false
        isConnecting = false
        reconnectAttempt = 0
        pingJob?.cancel()
        reconnectJob?.cancel()
        handshakeTimeoutJob?.cancel()
        webSocket?.close(1000, "Client disconnect")
        webSocket = null
        isConnected = false
    }

    /**
     * 构建 WebSocket URL
     */
    private fun buildWebSocketUrl(): String {
        val baseUrl = serverUrl.trimEnd('/')
        val wsScheme = if (baseUrl.startsWith("https://")) "wss://" else "ws://"
        val host = baseUrl.removePrefix("https://").removePrefix("http://")
        return "$wsScheme$host$HUB_PATH"
    }

    /**
     * 发送 SignalR 握手
     */
    /**
     * 发送 SignalR 握手
     */
    private fun sendHandshake() {
        // 使用硬编码的 JSON 字符串以确保格式正确
        // 注意：SignalR 协议要求握手消息必须以 RECORD_SEPARATOR (0x1E) 结尾
        val handshake = """{"protocol":"json","version":1}""" + RECORD_SEPARATOR
        if (webSocket?.send(handshake) == true) {
            Timber.tag(TAG).d("Sent handshake: $handshake")
        } else {
            Timber.tag(TAG).e("Failed to send handshake")
        }
    }

    /**
     * 发送 Ping 消息
     */
    private fun sendPing() {
        val ping = SignalRPingMessage()
        val message = json.encodeToString(ping) + RECORD_SEPARATOR
        webSocket?.send(message)
    }

    /**
     * 启动 Ping 定时任务
     */
    private fun startPingJob() {
        pingJob?.cancel()
        pingJob = launch {
            while (isConnected) {
                delay(PING_INTERVAL_MS)
                if (isConnected) {
                    sendPing()
                }
            }
        }
    }

    /**
     * 握手超时检测
     * 如果握手在指定时间内未完成，强制断开连接并触发重连
     */
    private fun startHandshakeTimeout() {
        handshakeTimeoutJob?.cancel()
        handshakeTimeoutJob = launch {
            delay(HANDSHAKE_TIMEOUT_MS)
            if (!isConnected && isConnecting) {
                Timber.tag(TAG).w("Handshake timeout after ${HANDSHAKE_TIMEOUT_MS}ms, forcing reconnect")
                isConnecting = false
                webSocket?.cancel() // 使用 cancel() 强制关闭，不等待服务器响应
                webSocket = null
                scheduleReconnect()
            }
        }
    }

    /**
     * 计算指数退避延迟
     * 从 3s 开始，每次翻倍，最大 60s
     */
    private fun getReconnectDelay(): Long {
        val delay = INITIAL_RECONNECT_DELAY_MS * (1L shl minOf(reconnectAttempt, 4))
        return minOf(delay, MAX_RECONNECT_DELAY_MS)
    }

    /**
     * 计划重连
     */
    private fun scheduleReconnect() {
        if (!shouldReconnect) return
        
        reconnectJob?.cancel()
        val delayMs = getReconnectDelay()
        reconnectAttempt++
        reconnectJob = launch {
            Timber.tag(TAG).d("Reconnecting in ${delayMs}ms (attempt=$reconnectAttempt)...")
            delay(delayMs)
            if (shouldReconnect) {
                connect()
            }
        }
    }

    /**
     * 处理收到的消息
     */
    private fun handleMessage(text: String) {
        // SignalR 消息以 RECORD_SEPARATOR 分隔
        val messages = text.split(RECORD_SEPARATOR).filter { it.isNotBlank() }
        
        for (msgText in messages) {
            try {
                // 首先尝试解析为握手响应
                if (!isConnected) {
                    val handshakeResponse = json.decodeFromString<SignalRHandshakeResponse>(msgText)
                    if (handshakeResponse.error == null) {
                        isConnected = true
                        isConnecting = false
                        reconnectAttempt = 0  // 连接成功，重置重连计数
                        handshakeTimeoutJob?.cancel()
                        Timber.tag(TAG).i("Connected to server")
                        startPingJob()
                        onConnected?.invoke()
                    } else {
                        Timber.tag(TAG).e("Handshake error: ${handshakeResponse.error}")
                        isConnecting = false
                        disconnect()
                    }
                    continue
                }

                // 解析为调用消息
                val message = json.decodeFromString<SignalRInvocationMessage>(msgText)
                
                when (message.type) {
                    SignalRMessageType.INVOCATION -> {
                        handleInvocation(message)
                    }
                    SignalRMessageType.PING -> {
                        // Pong - 服务器发来的 ping，不需要响应
                    }
                    SignalRMessageType.CLOSE -> {
                        Timber.tag(TAG).d("Server closed connection")
                        isConnected = false
                        onDisconnected?.invoke(null)
                        scheduleReconnect()
                    }
                }
            } catch (e: Exception) {
                Timber.tag(TAG).w(e, "Failed to parse message: $msgText")
            }
        }
    }

    /**
     * 处理服务器调用
     */
    private fun handleInvocation(message: SignalRInvocationMessage) {
        when (message.target) {
            "RemoteProfileChanged" -> {
                val args = message.arguments
                if (args != null && args.isNotEmpty()) {
                    try {
                        val profile = json.decodeFromJsonElement<ProfileDto>(args[0])
                        Timber.tag(TAG).d("Profile changed: type=${profile.type}, text=${profile.text?.take(50)}")
                        onProfileChanged?.invoke(profile)
                    } catch (e: Exception) {
                        Timber.tag(TAG).e(e, "Failed to parse ProfileDto")
                    }
                }
            }
            "RemoteHistoryChanged" -> {
                // 历史记录变化，暂不处理
                Timber.tag(TAG).d("History changed (ignored)")
            }
            else -> {
                Timber.tag(TAG).d("Unknown invocation: ${message.target}")
            }
        }
    }

    /**
     * WebSocket 监听器
     */
    private inner class WebSocketListenerImpl : WebSocketListener() {
        override fun onOpen(webSocket: WebSocket, response: Response) {
            Timber.tag(TAG).i("WebSocket opened: ${response.message}")
            sendHandshake()
        }

        override fun onMessage(webSocket: WebSocket, text: String) {
            Timber.tag(TAG).v("Received message: $text")
            handleMessage(text)
        }

        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
            Timber.tag(TAG).i("WebSocket closing: $code $reason")
        }

        override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
            Timber.tag(TAG).w("WebSocket closed: $code $reason")
            isConnected = false
            isConnecting = false
            pingJob?.cancel()
            handshakeTimeoutJob?.cancel()
            onDisconnected?.invoke(null)
            scheduleReconnect()
        }

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
            Timber.tag(TAG).e(t, "WebSocket failure: ${response?.code} ${response?.message}")
            isConnected = false
            isConnecting = false
            pingJob?.cancel()
            handshakeTimeoutJob?.cancel()
            onDisconnected?.invoke(t)
            scheduleReconnect()
        }
    }
}
