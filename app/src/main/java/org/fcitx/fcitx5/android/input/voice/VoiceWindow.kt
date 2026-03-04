package org.fcitx.fcitx5.android.input.voice

import android.view.View
import androidx.lifecycle.lifecycleScope
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.input.FcitxInputMethodService
import org.fcitx.fcitx5.android.input.dependency.inputMethodService
import org.fcitx.fcitx5.android.input.dependency.theme
import org.fcitx.fcitx5.android.input.wm.InputWindow
import timber.log.Timber
import android.annotation.SuppressLint
import android.content.Intent
import android.view.MotionEvent
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.ui.main.MainActivity
import org.fcitx.fcitx5.android.ui.main.settings.SettingsRoute

class VoiceWindow(private val startImmediately: Boolean = false) : InputWindow.ExtendedInputWindow<VoiceWindow>() {

    private val service: FcitxInputMethodService by manager.inputMethodService()
    private val theme by manager.theme()
    
    // 录音状态
    private var isRecording = false

    private val recognizer by lazy {
        VoiceRecognizer(
            context,
            service.lifecycleScope,
            onTextResult = { text, isFinal ->
                if (!isFinal) {
                    // 增量文本，因为是全句覆盖（带修改），用 composing text 实时显示
                    ui.statusText.text = text
                    service.currentInputConnection?.setComposingText(text, 1)
                } else {
                    // 结束并确认为最终结果
                    ui.statusText.text = text
                    service.commitText(text)
                }
            },
            onError = { err ->
                ui.statusText.text = "错误: $err"
                isRecording = false
            }
        )
    }

    private val ui by lazy {
        VoiceUi(context, theme).apply {
            settingsButton.setOnClickListener {
                val intent = Intent(context, MainActivity::class.java).apply {
                    action = Intent.ACTION_RUN
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    putExtra(MainActivity.EXTRA_SETTINGS_ROUTE, SettingsRoute.VoiceInput)
                }
                context.startActivity(intent)
            }
            
            modeSwitchButton.setOnClickListener {
                val prefs = AppPrefs.getInstance().voiceInput
                val newMode = if (prefs.voiceInputMode.getValue() == AppPrefs.VoiceInputMode.Click) {
                    AppPrefs.VoiceInputMode.LongPress
                } else {
                    AppPrefs.VoiceInputMode.Click
                }
                prefs.voiceInputMode.setValue(newMode)
                updateModeUi()
            }

            // 删除按钮：单击和长按连续删除
            val deleteHandler = android.os.Handler(android.os.Looper.getMainLooper())
            var deleteRunnable: Runnable? = null
            deleteButton.setOnClickListener {
                service.sendDownUpKeyEvents(android.view.KeyEvent.KEYCODE_DEL)
            }
            deleteButton.setOnLongClickListener {
                // 开始连续删除
                deleteRunnable = object : Runnable {
                    override fun run() {
                        service.sendDownUpKeyEvents(android.view.KeyEvent.KEYCODE_DEL)
                        deleteHandler.postDelayed(this, 50)
                    }
                }
                deleteHandler.post(deleteRunnable!!)
                true
            }
            deleteButton.setOnTouchListener { _, event ->
                if (event.action == android.view.MotionEvent.ACTION_UP ||
                    event.action == android.view.MotionEvent.ACTION_CANCEL) {
                    deleteRunnable?.let { deleteHandler.removeCallbacks(it) }
                    deleteRunnable = null
                }
                false
            }

            updateModeUi()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun VoiceUi.updateModeUi() {
        val currentMode = AppPrefs.getInstance().voiceInput.voiceInputMode.getValue()
        modeSwitchButton.text = context.getString(currentMode.stringRes)

        // 非录音状态下即时更新提示文字
        if (!isRecording) {
            statusText.text = getIdleHintText()
        }
        
        recordButton.setOnClickListener(null)
        recordButton.setOnTouchListener(null)
        
        when (currentMode) {
            AppPrefs.VoiceInputMode.Click -> {
                recordButton.setOnClickListener {
                    if (isRecording) {
                        stopRecording()
                    } else {
                        startRecording()
                    }
                }
            }
            AppPrefs.VoiceInputMode.LongPress -> {
                recordButton.setOnTouchListener { _, event ->
                    when (event.action) {
                        MotionEvent.ACTION_DOWN -> {
                            if (!isRecording) startRecording()
                            true
                        }
                        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                            if (isRecording) stopRecording()
                            true
                        }
                        else -> false
                    }
                }
            }
        }
    }

    override fun onCreateView(): View = ui.root

    override fun onAttached() {
        if (startImmediately) {
            startRecording()
        } else {
            isRecording = false
            ui.statusText.text = getIdleHintText()
            ui.setRecordingState(false, animate = false)
        }
    }

    override fun onDetached() {
        if (isRecording) {
            stopRecording()
        }
    }

    override val title: String
        get() = context.getString(R.string.switch_to_voice_input)

    private fun startRecording() {
        isRecording = true
        ui.statusText.text = "正在聆听..."
        ui.setRecordingState(true)
        // 获取光标前400个字符作为语料，增强识别效果
        val corpusText = service.currentInputConnection
            ?.getTextBeforeCursor(400, 0)
            ?.toString()
            ?.trim()
            ?: ""
        Timber.i("startRecording: corpusText length=${corpusText.length}, content='${corpusText.take(50)}'")
        recognizer.start(corpusText)
    }

    private fun stopRecording() {
        isRecording = false
        ui.statusText.text = getIdleHintText()
        ui.setRecordingState(false)
        recognizer.stop()
        service.currentInputConnection?.finishComposingText()
    }

    // 根据当前模式返回闲置时的提示文字
    private fun getIdleHintText(): String {
        return when (AppPrefs.getInstance().voiceInput.voiceInputMode.getValue()) {
            AppPrefs.VoiceInputMode.Click -> "点击开始说话"
            AppPrefs.VoiceInputMode.LongPress -> "长按开始说话"
        }
    }
}
