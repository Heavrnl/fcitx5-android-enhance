package org.fcitx.fcitx5.android.input.voice

import android.view.View
import androidx.lifecycle.lifecycleScope
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.input.FcitxInputMethodService
import org.fcitx.fcitx5.android.input.broadcast.PreeditEmptyStateComponent
import org.fcitx.fcitx5.android.input.dependency.inputMethodService
import org.fcitx.fcitx5.android.input.dependency.theme
import org.fcitx.fcitx5.android.input.wm.InputWindow
import org.mechdancer.dependency.manager.must
import android.annotation.SuppressLint
import android.content.Intent
import android.view.MotionEvent
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.ui.main.MainActivity
import org.fcitx.fcitx5.android.ui.main.settings.SettingsRoute
import timber.log.Timber

class VoiceWindow(private val startImmediately: Boolean = false) : InputWindow.ExtendedInputWindow<VoiceWindow>() {

    private val service: FcitxInputMethodService by manager.inputMethodService()
    private val theme by manager.theme()

    /** 用于判断用户是否已经开始新的组字（拼音 preedit），避免兜底定稿时误提交拼音 */
    private val preeditState: PreeditEmptyStateComponent by manager.must()

    private companion object {
        /**
         * 松手后等待"最终结果"的超时。超过这个时间还没收到 sentence_end，
         * 就把当前的 composing 直接定稿，否则识别文字会一直带下划线，
         * 甚至切到别的输入框时被丢掉。
         */
        const val FINAL_RESULT_TIMEOUT_MS = 2500L
    }

    // 录音状态
    private var isRecording = false

    /** 已请求停止录音（松手/点停止），正在等服务器的最终结果 */
    private var stopRequested = false

    /** 本次会话是否已收尾（最终结果已提交或已超时定稿）。收尾后要忽略迟到的结果 */
    private var sessionDone = false

    /** 最近一次提交的最终文本，用于挡掉松手后服务器对同一句的重复上报 */
    private var lastCommittedText: String? = null

    /** 当前输入框里是否真的有我们写入的 composing（下划线）文本 */
    private var composingActive = false

    private val mainHandler = android.os.Handler(android.os.Looper.getMainLooper())
    private var finalTimeout: Runnable? = null

    private val recognizer by lazy {
        VoiceRecognizer(
            context,
            service.lifecycleScope,
            onTextResult = { text, isFinal ->
                when {
                    // 已收尾：迟到的分片/重复上报一律丢掉，否则会把上屏的内容再插一遍
                    sessionDone -> Timber.d("Voice: drop late result (final=$isFinal)")

                    // 松手后已经提交过最终结果，此时再来分片只能是迟到的
                    !isFinal && stopRequested && lastCommittedText != null ->
                        Timber.d("Voice: drop late partial after final commit")

                    !isFinal -> {
                        // 增量文本，因为是全句覆盖（带修改），用 composing text 实时显示
                        ui.statusText.text = text
                        service.currentInputConnection?.let {
                            it.setComposingText(text, 1)
                            composingActive = true
                        }
                    }

                    else -> {
                        // 最终结果：commitText 会先删掉 composing 区域再插入。
                        // 所以绝对不能在 stopRecording() 里提前 finishComposingText()，
                        // 否则「下划线部分先定稿 + 最终结果再插一遍」= 整句重复两遍。
                        if (stopRequested && text == lastCommittedText) {
                            // 松手后服务器把同一句已定稿的结果又报了一遍，重复插入会整句重复
                            Timber.w("Voice: duplicate final after stop, skipped")
                            finishComposingIfNeeded()
                        } else {
                            ui.statusText.text = text
                            service.commitText(text)
                            composingActive = false
                            lastCommittedText = text
                        }
                        // 松手后可能还有不止一句的尾巴：每收到一条最终结果就把兜底定稿往后推
                        if (stopRequested) scheduleFinalTimeout()
                    }
                }
            },
            onError = { err ->
                ui.statusText.text = "错误: $err"
                isRecording = false
                ui.setRecordingState(false)
                stopRequested = true
                finishComposingIfNeeded()
                sessionDone = true
                cancelFinalTimeout()
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
        cancelFinalTimeout()
        stopRequested = false
        sessionDone = false
        composingActive = false
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
        // 防重入：避免重复启动导致多个识别会话
        if (isRecording) return
        // 新一轮会话：清掉上一轮的收尾状态和还没触发的兜底超时
        cancelFinalTimeout()
        stopRequested = false
        sessionDone = false
        lastCommittedText = null
        composingActive = false
        isRecording = true
        ui.statusText.text = "正在聆听..."
        ui.setRecordingState(true)
        recognizer.start()
    }

    private fun stopRecording() {
        if (!isRecording) return
        isRecording = false
        stopRequested = true
        sessionDone = false
        ui.statusText.text = getIdleHintText()
        ui.setRecordingState(false)
        recognizer.stop()
        scheduleFinalTimeout()
        // 这里**不能**调用 finishComposingText()：
        // 识别结果是异步回来的，提前把 composing 定稿会让随后到达的最终 commitText
        // 在它后面再插一遍，导致整句话重复两遍。
        // 正常路径由 onTextResult 的 commitText 覆盖 composing；
        // 兜底路径由 scheduleFinalTimeout() 超时后定稿。
    }

    /**
     * 松手后等最终结果：每收到一条最终结果就重置，安静了 [FINAL_RESULT_TIMEOUT_MS] 才真正收尾。
     * 这样既能等完"尾巴上还有一句"的情况，也不会让没等到结果的文字一直带下划线。
     */
    private fun scheduleFinalTimeout() {
        cancelFinalTimeout()
        val runnable = Runnable {
            finalTimeout = null
            if (sessionDone || !stopRequested) return@Runnable
            sessionDone = true
            finishComposingIfNeeded()
            ui.statusText.text = getIdleHintText()
            Timber.w("Voice: no more results in ${FINAL_RESULT_TIMEOUT_MS}ms, voice input finished")
        }
        finalTimeout = runnable
        mainHandler.postDelayed(runnable, FINAL_RESULT_TIMEOUT_MS)
    }

    /** 只在"确实有我们写入的 composing"且"用户没开始新的组字（拼音）"时才定稿 */
    private fun finishComposingIfNeeded() {
        if (composingActive && preeditState.isEmpty) {
            service.currentInputConnection?.finishComposingText()
        }
        composingActive = false
    }

    private fun cancelFinalTimeout() {
        finalTimeout?.let { mainHandler.removeCallbacks(it) }
        finalTimeout = null
    }

    // 根据当前模式返回闲置时的提示文字
    private fun getIdleHintText(): String {
        return when (AppPrefs.getInstance().voiceInput.voiceInputMode.getValue()) {
            AppPrefs.VoiceInputMode.Click -> "点击开始说话"
            AppPrefs.VoiceInputMode.LongPress -> "长按开始说话"
        }
    }
}
