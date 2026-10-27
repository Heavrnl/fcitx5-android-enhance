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
        private const val RECONNECT_DELAY_MS = 1000L           // 固定重连间隔 1 秒
        private const val PING_INTERVAL_MS = 15000L
        private const val HANDSHAKE_TIMEOUT_MS = 15000L       // 握手超时 15 秒
        private const val ALIVE_TIMEOUT_MS = 45000L           // 活性超时 45 秒（3 个 ping 周期未收到消息则判定假死）
        private const val WATCHDOG_INTERVAL_MS = 30000L       // 看门狗巡检间隔
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

    @Volatile
    private var webSocket: WebSocket? = null
    @Volatile
    private var isConnected = false
    @Volatile
    private var isConnecting = false       // 标记是否正在建立连接（防止并发连接）
    @Volatile
    private var shouldReconnect = true
    private var reconnectAttempt = 0       // 当前重连次数（仅用于日志）
    private var pingJob: Job? = null
    private var reconnectJob: Job? = null
    private var watchdogJob: Job? = null
    private var handshakeTimeoutJob: Job? = null  // 握手超时检测任务
    @Volatile
    private var lastMessageTime = 0L               // 最后收到服务器消息的时间戳

    /**
     * 状态机锁。
     *
     * WebSocket 回调跑在 OkHttp 的读线程，ping/超时/重连跑在协程里，
     * 之前出现过「活性超时把 socket 置空的同时，迟到的握手响应又把 isConnected 置回 true」，
     * 结果 isConnected=true 但 webSocket=null —— ping 循环直接退出、connect() 又因为
     * "already connected" 直接返回，从此永久不再重连（日志里 2026-10-26 20:47 就是这样死的）。
     * 所有状态迁移都必须在这把锁里做，且迁移时重新校验 socket 身份。
     */
    private val stateLock = Any()

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
        var stale: WebSocket? = null
        synchronized(stateLock) {
            // 半死状态（isConnected=true 但 socket 已经没了）不能当成"已连接"直接跳过，
            // 否则就再也不会重连了 —— 这正是线上"同步突然没反应、只能重启"的成因。
            if (isConnecting || (isConnected && webSocket != null)) {
                Timber.tag(TAG).d("Already connected or connecting, skipping")
                SyncLog.d("WS", "connect skipped: connected=$isConnected connecting=$isConnecting")
                return
            }
            isConnected = false
            isConnecting = true
            shouldReconnect = true
            stale = webSocket
            webSocket = null
        }
        // 清理旧的 WebSocket，防止多个实例并存
        stale?.close(1000, "Reconnecting")

        val wsUrl = buildWebSocketUrl()
        Timber.tag(TAG).d("Connecting to: $wsUrl (attempt=$reconnectAttempt)")
        SyncLog.i("WS", "connect attempt=${reconnectAttempt + 1} url=$wsUrl")

        val request = Request.Builder()
            .url(wsUrl)
            .addHeader("Authorization", authHeader)
            .build()

        val socket = client.newWebSocket(request, WebSocketListenerImpl())
        synchronized(stateLock) { if (isConnecting) webSocket = socket }

        // 启动握手超时检测：如果超时未完成握手，强制断开并重连
        startHandshakeTimeout(socket)
        startWatchdog()
    }

    /**
     * 把连接状态强制拉回"未连接"并重连。
     * 冷启动/假死/状态不一致等所有"需要重来一遍"的路径都走这里，避免各处手写状态迁移写漏。
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
     * 看门狗：兜住"连接状态和实际 socket 脱节"这一类错误。
     * 每 30 秒校验一次不变量：isConnected 必须对应一个真实的当前 socket，
     * 未连接时必须有重连在进行。违反就强制重连。
     */
    private fun startWatchdog() {
        if (watchdogJob?.isActive == true) return
        watchdogJob = launch {
            while (shouldReconnect) {
                delay(WATCHDOG_INTERVAL_MS)
                if (!shouldReconnect) break
                val connectedWithoutSocket = synchronized(stateLock) {
                    isConnected && webSocket == null
                }
                if (connectedWithoutSocket) {
                    SyncLog.w("WS", "watchdog: isConnected=true but socket is gone")
                    forceReconnect("watchdog: connected without socket")
                    continue
                }
                val idle = !isConnected && !isConnecting && reconnectJob?.isActive != true
                if (idle) {
                    SyncLog.w("WS", "watchdog: not connected and no reconnect pending")
                    scheduleReconnect("watchdog: idle without socket")
                }
            }
            SyncLog.d("WS", "watchdog stopped")
        }
    }

    /**
     * 断开连接
     */
    fun disconnect() {
        SyncLog.i("WS", "disconnect() by client (shouldReconnect=false)")
        var old: WebSocket? = null
        synchronized(stateLock) {
            shouldReconnect = false
            isConnecting = false
            reconnectAttempt = 0
            old = webSocket
            webSocket = null
            isConnected = false
        }
        pingJob?.cancel()
        reconnectJob?.cancel()
        handshakeTimeoutJob?.cancel()
        watchdogJob?.cancel()
        old?.close(1000, "Client disconnect")
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
    private fun sendHandshake(webSocket: WebSocket) {
        // 使用硬编码的 JSON 字符串以确保格式正确
        // 注意：SignalR 协议要求握手消息必须以 RECORD_SEPARATOR (0x1E) 结尾
        val handshake = """{"protocol":"json","version":1}""" + RECORD_SEPARATOR
        if (webSocket.send(handshake)) {
            Timber.tag(TAG).d("Sent handshake: $handshake")
        } else {
            Timber.tag(TAG).e("Failed to send handshake")
        }
    }

    /**
     * 发送 Ping 消息
     */
    private fun sendPing(webSocket: WebSocket) {
        val ping = SignalRPingMessage()
        val message = json.encodeToString(ping) + RECORD_SEPARATOR
        webSocket.send(message)
    }

    /**
     * 启动 Ping 定时任务
     */
    private fun startPingJob(webSocket: WebSocket) {
        pingJob?.cancel()
        pingJob = launch {
            var ticks = 0
            while (isCurrentSocket(webSocket) && isConnected) {
                delay(PING_INTERVAL_MS)
                if (isCurrentSocket(webSocket) && isConnected) {
                    ticks++
                    // 检查活性超时：如果距离上次收到消息超过阈值，认为连接假死
                    val elapsed = System.currentTimeMillis() - lastMessageTime
                    if (elapsed > ALIVE_TIMEOUT_MS) {
                        Timber.tag(TAG).w("Connection seems dead (no message for ${elapsed}ms), forcing reconnect")
                        SyncLog.w("WS", "alive timeout: no server message for ${elapsed}ms, force reconnect")
                        forceReconnect("alive timeout ${elapsed}ms")
                        return@launch
                    }
                    // 每 4 次 ping（约 1 分钟）记一条心跳，用于离线后回看"最后一次活着是什么时候"
                    if (ticks % 4 == 0) {
                        SyncLog.d("WS", "heartbeat: ping #$ticks, last server message ${elapsed}ms ago")
                    }
                    sendPing(webSocket)
                }
            }
            SyncLog.d("WS", "ping job ended (connected=$isConnected current=${isCurrentSocket(webSocket)})")
            // 循环退出但状态仍是"已连接" = socket 被换掉/清理掉了但标志没跟上，
            // 这种状态不会再有心跳和活性检测，必须主动重启连接。
            if (isConnected) forceReconnect("ping loop exited while isConnected=true")
        }
    }

    /**
     * 握手超时检测
     * 如果握手在指定时间内未完成，强制断开连接并触发重连
     */
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

    /**
     * 获取重连延迟（固定 1 秒）
     */
    private fun getReconnectDelay(): Long = RECONNECT_DELAY_MS

    /**
     * 计划重连
     */
    private fun scheduleReconnect(reason: String) {
        if (!shouldReconnect) {
            SyncLog.d("WS", "reconnect suppressed (shouldReconnect=false) after: $reason")
            return
        }
        
        reconnectJob?.cancel()
        val delayMs = getReconnectDelay()
        reconnectAttempt++
        SyncLog.i("WS", "reconnect scheduled in ${delayMs}ms (attempt=$reconnectAttempt) reason=$reason")
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
    private fun handleMessage(webSocket: WebSocket, text: String) {
        if (!isCurrentSocket(webSocket)) {
            Timber.tag(TAG).d("Ignoring message from stale WebSocket")
            return
        }

        // SignalR 消息以 RECORD_SEPARATOR 分隔
        val messages = text.split(RECORD_SEPARATOR).filter { it.isNotBlank() }
        
        // 收到任何消息都更新活性时间戳
        lastMessageTime = System.currentTimeMillis()
        
        for (msgText in messages) {
            try {
                // 首先尝试解析为握手响应
                if (!isConnected) {
                    val handshakeResponse = json.decodeFromString<SignalRHandshakeResponse>(msgText)
                    if (handshakeResponse.error == null) {
                        // 关键：在锁里**重新校验 socket 身份**再迁移状态。
                        // 这里曾经出现过：活性超时刚把 webSocket 置空，这条迟到的握手响应
                        // 又把 isConnected 置回 true，于是 ping 循环立刻退出、
                        // connect() 因 "already connected" 永久跳过 —— 同步彻底死掉只能重启。
                        val accepted = synchronized(stateLock) {
                            if (isCurrentSocket(webSocket) && !isConnected) {
                                isConnected = true
                                isConnecting = false
                                reconnectAttempt = 0  // 连接成功，重置重连计数
                                lastMessageTime = System.currentTimeMillis()  // 初始化活性时间戳
                                true
                            } else false
                        }
                        if (!accepted) {
                            SyncLog.w("WS", "drop late handshake response (socket already replaced)")
                            continue
                        }
                        handshakeTimeoutJob?.cancel()
                        SyncLog.i("WS", "handshake ok, connected (pid=${android.os.Process.myPid()})")
                        Timber.tag(TAG).i("Connected to server")
                        startPingJob(webSocket)
                        onConnected?.invoke()
                    } else {
                        Timber.tag(TAG).e("Handshake error: ${handshakeResponse.error}")
                        // 之前这里走 disconnect()，会把 shouldReconnect 置 false，
                        // 之后永远不再重连（只能重启 App / 关开开关）。
                        // 握手被拒也要继续重试，由看门狗保证不会卡死。
                        SyncLog.e(
                            "WS",
                            "handshake rejected by server: ${handshakeResponse.error} -> will retry"
                        )
                        forceReconnect("handshake rejected: ${handshakeResponse.error}")
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
                        // 不打印 text 内容，避免剪贴板明文进入落盘日志
                        Timber.tag(TAG).d("Profile changed: type=${profile.type}, dataName=${profile.dataName}")
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
