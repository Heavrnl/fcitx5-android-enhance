package org.fcitx.fcitx5.android.input.voice

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString.Companion.toByteString
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.json.JSONObject
import timber.log.Timber
import java.util.UUID

class VoiceRecognizer(
    private val context: Context,
    private val scope: CoroutineScope,
    private val onTextResult: (String, Boolean) -> Unit,
    private val onError: (String) -> Unit
) {
    @Volatile
    private var isRecording = false
    private var audioRecord: AudioRecord? = null
    private var recordingJob: Job? = null
    @Volatile
    private var webSocket: WebSocket? = null
    private var taskId: String? = null
    @Volatile
    private var finishPending = false
    private val client = OkHttpClient()

    // 录音缓冲：WebSocket 未就绪时暂存音频数据
    @Volatile
    private var wsReady = false
    private val audioBuffer = mutableListOf<Pair<ShortArray, Int>>()

    private val sampleRate = 16000
    private val channelConfig = AudioFormat.CHANNEL_IN_MONO
    private val audioFormat = AudioFormat.ENCODING_PCM_16BIT
    private val bufferSize = AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormat)

    fun start() {
        // 防重入：如果已经在录音，先完整清理旧会话，避免多个 WebSocket 同时活跃导致重复转录
        if (isRecording || webSocket != null) {
            Timber.w("VoiceRecognizer.start() 被重复调用，先清理旧会话")
            stopInternal()
            try {
                webSocket?.close(1000, "Restarting")
            } catch (e: Exception) {
                Timber.e(e)
            }
            webSocket = null
            taskId = null
        }

        val apiKey = AppPrefs.getInstance().voiceInput.dashscopeApiKey.getValue().trim()
        if (apiKey.isBlank()) {
            onError("请先在设置中配置 DashScope API Key")
            return
        }
        val workspaceId = AppPrefs.getInstance().voiceInput.dashscopeWorkspaceId.getValue().trim()
        if (workspaceId.isBlank()) {
            onError("请先在设置中配置 DashScope 工作空间 ID")
            return
        }
        if (!workspaceId.matches(Regex("[A-Za-z0-9-]+"))) {
            onError("DashScope 工作空间 ID 格式不正确")
            return
        }
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            onError("暂无录音权限")
            return
        }

        try {
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.MIC,
                sampleRate,
                channelConfig,
                audioFormat,
                bufferSize * 2
            )
            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                onError("录音设备初始化失败")
                return
            }
        } catch (e: Exception) {
            Timber.e(e, "Failed to start recording")
            onError("启动录音失败: ${e.message}")
            return
        }

        // 立即启动录音，在 WebSocket 连接期间缓冲音频数据
        try {
            audioRecord?.startRecording()
            isRecording = true
            recordingJob = scope.launch(Dispatchers.IO) {
                val buffer = ShortArray(bufferSize)
                while (isActive && isRecording) {
                    val readSize = audioRecord?.read(buffer, 0, buffer.size) ?: 0
                    if (readSize > 0) {
                        if (wsReady) {
                            // WebSocket 已就绪，直接发送
                            webSocket?.let { sendAudioBuffer(it, buffer, readSize) }
                        } else {
                            // WebSocket 未就绪，缓冲数据
                            synchronized(audioBuffer) {
                                audioBuffer.add(Pair(buffer.copyOf(), readSize))
                            }
                        }
                    }
                }
            }
        } catch (e: Exception) {
            Timber.e(e, "Error starting audio read loop")
            onError("录音读取失败")
            return
        }

        connectWebSocket(apiKey, workspaceId)
    }

    private fun connectWebSocket(apiKey: String, workspaceId: String) {
        Timber.i("connectWebSocket: apiKey length = ${apiKey.length}, startsWith = ${apiKey.take(4)}")
        val region = AppPrefs.getInstance().voiceInput.dashscopeRegion.getValue()
        val domain = if (region == AppPrefs.DashScopeRegion.International) {
            "$workspaceId.ap-southeast-1.maas.aliyuncs.com"
        } else {
            "$workspaceId.cn-beijing.maas.aliyuncs.com"
        }
        val request = Request.Builder()
            .url("wss://$domain/api-ws/v1/inference")
            .addHeader("Authorization", "Bearer $apiKey")
            .build()

        val currentTaskId = UUID.randomUUID().toString().replace("-", "")
        taskId = currentTaskId
        finishPending = false
        webSocket = client.newWebSocket(request, object: WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                if (this@VoiceRecognizer.webSocket !== webSocket) {
                    webSocket.close(1000, "Stale session")
                    return
                }
                Timber.i("WebSocket onOpen")
                val runTask = JSONObject().apply {
                    put("header", taskHeader("run-task", currentTaskId))
                    put("payload", JSONObject().apply {
                        put("task_group", "audio")
                        put("task", "asr")
                        put("function", "recognition")
                        put("model", "qwen-audio-3.0-asr-flash-streaming")
                        put("parameters", JSONObject().apply {
                            put("format", "pcm")
                            put("sample_rate", sampleRate)
                        })
                        put("input", JSONObject())
                    })
                }
                webSocket.send(runTask.toString())
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                if (this@VoiceRecognizer.webSocket !== webSocket) return
                Timber.d("WebSocket onMessage: $text")
                try {
                    val json = JSONObject(text)
                    val header = json.optJSONObject("header") ?: return
                    when (header.optString("event")) {
                        "task-started" -> {
                            synchronized(audioBuffer) {
                                for ((buffer, size) in audioBuffer) {
                                    sendAudioBuffer(webSocket, buffer, size)
                                }
                                audioBuffer.clear()
                                if (!finishPending) {
                                    wsReady = true
                                }
                            }
                            if (finishPending) {
                                sendFinishTask(webSocket, currentTaskId)
                            }
                        }
                        "result-generated" -> {
                            val sentence = json.optJSONObject("payload")
                                ?.optJSONObject("output")
                                ?.optJSONObject("sentence")
                            val textResult = sentence?.optString("text").orEmpty()
                            if (textResult.isNotEmpty()) {
                                val isFinal = sentence?.optBoolean("sentence_end", false) == true
                                scope.launch(Dispatchers.Main) {
                                    onTextResult(textResult, isFinal)
                                }
                            }
                        }
                        "task-finished" -> {
                            wsReady = false
                            webSocket.close(1000, "Recognition finished")
                        }
                        "task-failed" -> {
                            val message = header.optString("error_message", "语音识别失败")
                            scope.launch(Dispatchers.Main) {
                                onError(message)
                            }
                            webSocket.close(1011, "Recognition failed")
                        }
                    }
                } catch(e: Exception) {
                    Timber.e(e, "Parse message error")
                }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                if (this@VoiceRecognizer.webSocket !== webSocket) return
                Timber.e(t, "WebSocket onFailure")
                scope.launch(Dispatchers.Main) {
                    onError("连接失败: ${t.message}")
                }
                this@VoiceRecognizer.webSocket = null
                taskId = null
                stopInternal()
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                if (this@VoiceRecognizer.webSocket !== webSocket) return
                Timber.i("WebSocket onClosed: $code $reason")
                this@VoiceRecognizer.webSocket = null
                taskId = null
                stopInternal()
            }
        })
    }

    private fun sendAudioBuffer(webSocket: WebSocket, buffer: ShortArray, size: Int) {
        val bytes = ByteArray(size * 2)
        for (i in 0 until size) {
            bytes[i * 2] = (buffer[i].toInt() and 0xFF).toByte()
            bytes[i * 2 + 1] = ((buffer[i].toInt() shr 8) and 0xFF).toByte()
        }
        webSocket.send(bytes.toByteString())
    }

    fun stop() {
        if (!isRecording) return
        stopInternal(clearAudioBuffer = false)

        try {
            val socket = webSocket
            val currentTaskId = taskId
            if (socket != null && currentTaskId != null && wsReady) {
                sendFinishTask(socket, currentTaskId)
            } else {
                finishPending = true
            }
        } catch (e: Exception) {
            Timber.e(e)
        }
    }

    private fun taskHeader(action: String, taskId: String) = JSONObject().apply {
        put("action", action)
        put("task_id", taskId)
        put("streaming", "duplex")
    }

    private fun sendFinishTask(webSocket: WebSocket, taskId: String) {
        finishPending = false
        val finishTask = JSONObject().apply {
            put("header", taskHeader("finish-task", taskId))
            put("payload", JSONObject().put("input", JSONObject()))
        }
        webSocket.send(finishTask.toString())
    }
    
    private fun stopInternal(clearAudioBuffer: Boolean = true) {
        isRecording = false
        wsReady = false
        recordingJob?.cancel()
        recordingJob = null
        if (clearAudioBuffer) {
            synchronized(audioBuffer) {
                audioBuffer.clear()
            }
        }
        try {
            audioRecord?.stop()
            audioRecord?.release()
        } catch (e: Exception) {
            Timber.e(e)
        } finally {
            audioRecord = null
        }
    }
}
