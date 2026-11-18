/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2026 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android

import org.fcitx.fcitx5.android.data.syncclipboard.ProfileDto
import org.fcitx.fcitx5.android.data.syncclipboard.SyncClipboardSignalR
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.net.ServerSocket
import java.net.Socket
import java.security.MessageDigest
import java.util.Base64
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

class SyncClipboardSignalRTest {

    /**
     * 极简本地 WebSocket + SignalR Hub 测试服务端，用于验证真实网络状态流转。
     */
    private class MockSignalRServer : AutoCloseable {
        private val serverSocket = ServerSocket(0)
        val port: Int get() = serverSocket.localPort
        val connectionAttempts = AtomicInteger(0)
        val receivedProfiles = CopyOnWriteArrayList<ProfileDto>()

        @Volatile
        private var running = true
        private val sockets = CopyOnWriteArrayList<Socket>()

        @Volatile
        private var onClientHandshakeDone: ((OutputStream) -> Unit)? = null

        fun start(onHandshake: ((OutputStream) -> Unit)? = null) {
            onClientHandshakeDone = onHandshake
            thread(isDaemon = true) {
                while (running && !serverSocket.isClosed) {
                    val client = runCatching { serverSocket.accept() }.getOrNull() ?: break
                    sockets.add(client)
                    connectionAttempts.incrementAndGet()
                    thread(isDaemon = true) { handleClient(client) }
                }
            }
        }

        private fun handleClient(socket: Socket) {
            runCatching {
                val input = BufferedInputStream(socket.getInputStream())
                val output = socket.getOutputStream()
                val headers = readHttpHeaders(input)
                val wsKey = headers.lineSequence()
                    .firstOrNull { it.startsWith("Sec-WebSocket-Key:", ignoreCase = true) }
                    ?.substringAfter(":")?.trim()
                    ?: return

                val acceptKey = Base64.getEncoder().encodeToString(
                    MessageDigest.getInstance("SHA-1")
                        .digest("${wsKey}258EAFA5-E914-47DA-95CA-C5AB0DC85B11".toByteArray())
                )
                val response = buildString {
                    append("HTTP/1.1 101 Switching Protocols\r\n")
                    append("Upgrade: websocket\r\n")
                    append("Connection: Upgrade\r\n")
                    append("Sec-WebSocket-Accept: $acceptKey\r\n\r\n")
                }
                output.write(response.toByteArray())
                output.flush()

                // 读取客户端发来的 SignalR 握手帧
                val firstFrame = readWsTextFrame(input)
                if (firstFrame != null && firstFrame.contains("\"protocol\":\"json\"")) {
                    sendWsTextFrame(output, "{}\u001E")
                    onClientHandshakeDone?.invoke(output)
                }

                // 保持连接直到客户端关闭（模拟息屏 disconnect）
                while (running && !socket.isClosed) {
                    val frame = readWsTextFrame(input) ?: break
                    if (frame.isEmpty()) break
                }
            }
            runCatching { socket.close() }
        }

        private fun readHttpHeaders(input: InputStream): String {
            val buf = ByteArrayOutputStream()
            var prev4 = 0
            while (true) {
                val b = input.read()
                if (b < 0) break
                buf.write(b)
                prev4 = (prev4 shl 8) or (b and 0xFF)
                if (prev4 == 0x0D0A0D0A) break
            }
            return buf.toString(Charsets.UTF_8.name())
        }

        private fun readWsTextFrame(input: InputStream): String? {
            val b0 = input.read()
            if (b0 < 0) return null
            val opcode = b0 and 0x0F
            if (opcode == 0x8) return null // close frame
            val b1 = input.read()
            if (b1 < 0) return null
            val masked = (b1 and 0x80) != 0
            var len = b1 and 0x7F
            if (len == 126) {
                len = ((input.read() shl 8) or input.read())
            } else if (len == 127) {
                return null
            }
            val mask = if (masked) ByteArray(4) { input.read().toByte() } else null
            val payload = ByteArray(len)
            var offset = 0
            while (offset < len) {
                val r = input.read(payload, offset, len - offset)
                if (r < 0) return null
                offset += r
            }
            if (mask != null) {
                for (i in payload.indices) {
                    payload[i] = (payload[i].toInt() xor mask[i % 4].toInt()).toByte()
                }
            }
            return String(payload, Charsets.UTF_8)
        }

        fun sendWsTextFrame(output: OutputStream, text: String) {
            val data = text.toByteArray(Charsets.UTF_8)
            val header = ByteArrayOutputStream()
            header.write(0x81)
            if (data.size < 126) {
                header.write(data.size)
            } else {
                header.write(126)
                header.write((data.size ushr 8) and 0xFF)
                header.write(data.size and 0xFF)
            }
            synchronized(output) {
                output.write(header.toByteArray())
                output.write(data)
                output.flush()
            }
        }

        override fun close() {
            running = false
            sockets.forEach { runCatching { it.close() } }
            runCatching { serverSocket.close() }
        }
    }

    @Test
    fun `no network defers connect without hitting server and resumes on restore`() {
        MockSignalRServer().use { server ->
            val connectedLatch = CountDownLatch(1)
            server.start()

            val client = SyncClipboardSignalR("http://127.0.0.1:${server.port}", "u", "p").apply {
                onConnected = { connectedLatch.countDown() }
            }

            // 断网状态调用 connect()：不得尝试建立 TCP 连接
            client.setNetworkAvailable(false)
            client.connect()
            Thread.sleep(250)
            assertEquals("Offline connect must not attempt TCP handshake", 0, server.connectionAttempts.get())
            assertFalse(client.isConnectedNow)
            assertFalse(client.isConnectingNow)

            // 网络恢复后应立即连接成功
            client.setNetworkAvailable(true)
            assertTrue("Should connect immediately when network restores", connectedLatch.await(5, TimeUnit.SECONDS))
            assertTrue(client.isConnectedNow)
            assertEquals(1, server.connectionAttempts.get())

            client.disconnect()
            assertFalse(client.isConnectedNow)
        }
    }

    @Test
    fun `disconnect pauses completely and reconnects cleanly on wake`() {
        MockSignalRServer().use { server ->
            val firstConnect = CountDownLatch(1)
            val secondConnect = CountDownLatch(1)
            val pushReceived = CountDownLatch(1)
            val connectCount = AtomicInteger(0)
            val handshakeCount = AtomicInteger(0)
            val receivedTexts = CopyOnWriteArrayList<String>()

            server.start { out ->
                if (handshakeCount.incrementAndGet() == 2) {
                    val push = """{"type":1,"target":"RemoteProfileChanged","arguments":[{"type":"Text","text":"hello-after-wake","hasData":false}]}""" + "\u001E"
                    server.sendWsTextFrame(out, push)
                }
            }

            val client = SyncClipboardSignalR("http://127.0.0.1:${server.port}", "u", "p").apply {
                onConnected = {
                    if (connectCount.incrementAndGet() == 1) {
                        firstConnect.countDown()
                    } else {
                        secondConnect.countDown()
                    }
                }
                onProfileChanged = { profile ->
                    profile.text?.let {
                        receivedTexts.add(it)
                        pushReceived.countDown()
                    }
                }
            }

            // 1. 首次连接成功
            client.connect()
            assertTrue(firstConnect.await(5, TimeUnit.SECONDS))
            assertTrue(client.isConnectedNow)

            // 2. 模拟息屏休眠：disconnect() 后不再自动重连
            client.disconnect()
            assertFalse(client.isConnectedNow)
            assertFalse(client.isConnectingNow)
            Thread.sleep(300)
            assertEquals("Disconnected client must stay asleep", 1, server.connectionAttempts.get())

            // 3. 模拟亮屏恢复：重新 connect() 应干净建连并可接收远端推送
            client.connect()
            assertTrue(secondConnect.await(5, TimeUnit.SECONDS))
            assertTrue(pushReceived.await(5, TimeUnit.SECONDS))
            assertTrue(receivedTexts.contains("hello-after-wake"))

            client.disconnect()
        }
    }

    @Test
    fun `reconnectNow resets backoff and replaces existing connection`() {
        MockSignalRServer().use { server ->
            val firstConnect = CountDownLatch(1)
            val secondConnect = CountDownLatch(1)
            val count = AtomicInteger(0)
            server.start()

            val client = SyncClipboardSignalR("http://127.0.0.1:${server.port}", "u", "p").apply {
                onConnected = {
                    if (count.incrementAndGet() == 1) firstConnect.countDown() else secondConnect.countDown()
                }
            }

            client.connect()
            assertTrue(firstConnect.await(5, TimeUnit.SECONDS))

            client.reconnectNow("network_switched")
            assertTrue(secondConnect.await(5, TimeUnit.SECONDS))
            assertEquals(2, server.connectionAttempts.get())

            client.disconnect()
        }
    }
}
