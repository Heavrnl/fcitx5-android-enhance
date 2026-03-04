package org.fcitx.fcitx5.android.input.voice

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Base64
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
    private var isRecording = false
    private var audioRecord: AudioRecord? = null
    private var recordingJob: Job? = null
    private var webSocket: WebSocket? = null
    private val client = OkHttpClient()

    // 录音缓冲：WebSocket 未就绪时暂存音频数据
    @Volatile
    private var wsReady = false
    private val audioBuffer = mutableListOf<Pair<ShortArray, Int>>()

    private val sampleRate = 16000
    private val channelConfig = AudioFormat.CHANNEL_IN_MONO
    private val audioFormat = AudioFormat.ENCODING_PCM_16BIT
    private val bufferSize = AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormat)

    fun start(corpusText: String = "") {
        val apiKey = AppPrefs.getInstance().voiceInput.dashscopeApiKey.getValue().trim()
        if (apiKey.isBlank()) {
            onError("请先在设置中配置 DashScope API Key")
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
                            sendAudioBuffer(buffer, readSize)
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

        connectWebSocket(apiKey, corpusText)
    }

    private fun connectWebSocket(apiKey: String, corpusText: String = "") {
        Timber.i("connectWebSocket: apiKey length = ${apiKey.length}, startsWith = ${apiKey.take(4)}")
        val region = AppPrefs.getInstance().voiceInput.dashscopeRegion.getValue()
        val domain = if (region == AppPrefs.DashScopeRegion.International) {
            "dashscope-intl.aliyuncs.com"
        } else {
            "dashscope.aliyuncs.com"
        }
        val request = Request.Builder()
            .url("wss://$domain/api-ws/v1/realtime?model=qwen3-asr-flash-realtime")
            .addHeader("Authorization", "Bearer $apiKey")
            .addHeader("OpenAI-Beta", "realtime=v1")
            .build()
        
        webSocket = client.newWebSocket(request, object: WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                Timber.i("WebSocket onOpen")
                val sessionUpdate = JSONObject().apply {
                    put("event_id", "event_${UUID.randomUUID()}")
                    put("type", "session.update")
                    put("session", JSONObject().apply {
                        put("modalities", org.json.JSONArray().put("text"))
                        put("input_audio_format", "pcm")
                        put("sample_rate", 16000)
                        put("input_audio_transcription", JSONObject().apply {
                            put("language", "")
                            put("model", "qwen3-asr-flash-realtime")
                            // 语料，用于增强识别效果
                            if (corpusText.isNotBlank()) {
                                put("corpus", JSONObject().apply {
                                    put("text", corpusText)
                                })
                            }
                        })
                        put("turn_detection", JSONObject().apply {
                            put("type", "server_vad")
                        })
                    })
                }
                val sessionJson = sessionUpdate.toString()
                Timber.i("session.update JSON: $sessionJson")
                webSocket.send(sessionJson)

                // 发送 WebSocket 连接期间缓冲的音频数据
                synchronized(audioBuffer) {
                    for ((buf, size) in audioBuffer) {
                        sendAudioBuffer(buf, size)
                    }
                    audioBuffer.clear()
                }
                wsReady = true
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                Timber.d("WebSocket onMessage: $text")
                try {
                    val json = JSONObject(text)
                    val type = json.optString("type")
                    
                    if (type == "conversation.item.input_audio_transcription.completed") {
                        val transcript = json.optString("transcript")
                        if (transcript.isNotEmpty()) {
                            scope.launch(Dispatchers.Main) {
                                onTextResult(transcript, true)
                            }
                        }
                    } else if (type == "conversation.item.input_audio_transcription.text") {
                        // VAD 模式下，中间临时结果可能放在 stash 里，全量部分在 text 里
                        val t = json.optString("text", "")
                        val stash = json.optString("stash", "")
                        val combined = t + stash
                        if (combined.isNotEmpty()) {
                            scope.launch(Dispatchers.Main) {
                                onTextResult(combined, false)
                            }
                        }
                    } else if (type == "response.audio_transcript.delta" || type == "conversation.item.input_audio_transcription.delta") {
                        val delta = json.optString("delta", "")
                        if (delta.isNotEmpty()) {
                            scope.launch(Dispatchers.Main) {
                                onTextResult(delta, false)
                            }
                        }
                    } else if (json.has("text") || json.has("delta") || json.has("transcript")) {
                        // 兜底 generic extraction
                        val t = json.optString("transcript", json.optString("text", json.optString("delta", "")))
                        if (t.isNotEmpty()) {
                            scope.launch(Dispatchers.Main) {
                                onTextResult(t, false)
                            }
                        }
                    }
                } catch(e: Exception) {
                    Timber.e(e, "Parse message error")
                }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                Timber.e(t, "WebSocket onFailure")
                scope.launch(Dispatchers.Main) {
                    onError("连接失败: ${t.message}")
                }
                stopInternal()
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                Timber.i("WebSocket onClosed: $code $reason")
                stopInternal()
            }
        })
    }

    private fun sendAudioBuffer(buffer: ShortArray, size: Int) {
        val bytes = ByteArray(size * 2)
        for (i in 0 until size) {
            bytes[i * 2] = (buffer[i].toInt() and 0xFF).toByte()
            bytes[i * 2 + 1] = ((buffer[i].toInt() shr 8) and 0xFF).toByte()
        }
        val encoded = Base64.encodeToString(bytes, Base64.NO_WRAP)
        val event = JSONObject().apply {
            put("event_id", "event_${UUID.randomUUID()}")
            put("type", "input_audio_buffer.append")
            put("audio", encoded)
        }
        webSocket?.send(event.toString())
    }

    fun stop() {
        if (!isRecording) return
        stopInternal()
        
        try {
            val commit = JSONObject().apply {
                put("event_id", "event_${UUID.randomUUID()}")
                put("type", "input_audio_buffer.commit")
            }
            webSocket?.send(commit.toString())
            webSocket?.close(1000, "User stopped")
        } catch (e: Exception) {
            Timber.e(e)
        } finally {
            webSocket = null
        }
    }
    
    private fun stopInternal() {
        isRecording = false
        wsReady = false
        recordingJob?.cancel()
        recordingJob = null
        synchronized(audioBuffer) {
            audioBuffer.clear()
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
