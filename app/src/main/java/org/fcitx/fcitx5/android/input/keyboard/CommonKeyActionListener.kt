/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2024 Fcitx5 for Android Contributors
 */

package org.fcitx.fcitx5.android.input.keyboard

import androidx.core.content.ContextCompat
import org.fcitx.fcitx5.android.input.bar.KawaiiBarComponent
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.fcitx.fcitx5.android.core.FcitxAPI
import org.fcitx.fcitx5.android.daemon.launchOnReady
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.input.broadcast.PreeditEmptyStateComponent
import org.fcitx.fcitx5.android.input.candidates.horizontal.HorizontalCandidateComponent
import org.fcitx.fcitx5.android.input.dependency.context
import org.fcitx.fcitx5.android.input.dependency.fcitx
import org.fcitx.fcitx5.android.input.dependency.inputMethodService
import org.fcitx.fcitx5.android.input.dialog.AddMoreInputMethodsPrompt
import org.fcitx.fcitx5.android.input.dialog.InputMethodPickerDialog
import org.fcitx.fcitx5.android.input.keyboard.CommonKeyActionListener.BackspaceSwipeState.Reset
import org.fcitx.fcitx5.android.input.keyboard.CommonKeyActionListener.BackspaceSwipeState.Selection
import org.fcitx.fcitx5.android.input.keyboard.CommonKeyActionListener.BackspaceSwipeState.Stopped
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.CommitAction
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.DeleteSelectionAction
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.FcitxKeyAction
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.LangSwitchAction
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.MoveSelectionAction
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.PickerSwitchAction
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.QuickPhraseAction
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.ShowInputMethodPickerAction
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.SpaceLongPressAction
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.SymAction
import org.fcitx.fcitx5.android.input.keyboard.KeyAction.UnicodeAction
import org.fcitx.fcitx5.android.input.picker.PickerWindow
import org.fcitx.fcitx5.android.input.wm.InputWindowManager
import org.fcitx.fcitx5.android.utils.switchToNextIME
import org.mechdancer.dependency.Dependent
import org.mechdancer.dependency.UniqueComponent
import org.mechdancer.dependency.manager.ManagedHandler
import org.mechdancer.dependency.manager.managedHandler
import org.mechdancer.dependency.manager.must

class CommonKeyActionListener :
    UniqueComponent<CommonKeyActionListener>(), Dependent, ManagedHandler by managedHandler() {

    enum class BackspaceSwipeState {
        Stopped, Selection, Reset
    }

    private val context by manager.context()
    private val fcitx by manager.fcitx()
    private val service by manager.inputMethodService()
    private val preeditState: PreeditEmptyStateComponent by manager.must()
    private val horizontalCandidate: HorizontalCandidateComponent by manager.must()
    private val windowManager: InputWindowManager by manager.must()
    private val kawaiiBar: KawaiiBarComponent by manager.must()

    private var lastPickerType by AppPrefs.getInstance().internal.lastPickerType

    private val kbdPrefs = AppPrefs.getInstance().keyboard

    private val spaceKeyLongPressBehavior by kbdPrefs.spaceKeyLongPressBehavior
    private val langSwitchKeyBehavior by kbdPrefs.langSwitchKeyBehavior

    private var backspaceSwipeState = Stopped

    private var voiceRecognizer: org.fcitx.fcitx5.android.input.voice.VoiceRecognizer? = null
    // 防重入标志：防止长按空格键重复触发语音识别
    private var isVoiceListening = false

    /** 松手后等待最终结果的超时，超时就把 composing 定稿，避免文字一直带下划线 */
    private val voiceFinalTimeoutMs = 2500L

    /** 已松手，正在等服务器的最终结果 */
    private var voiceStopRequested = false

    /** 本次会话已收尾（最终结果已提交或已超时定稿），之后要忽略迟到的结果 */
    private var voiceSessionDone = false

    /** 最近一次提交的最终文本，用于挡掉松手后服务器对同一句的重复上报 */
    private var lastVoiceCommittedText: String? = null

    /** 当前输入框里是否真的有我们写入的 composing（下划线）文本 */
    private var voiceComposingActive = false

    private var voiceFinalJob: Job? = null

    private fun startVoiceRecognition() {
        // 防重入：如果已经在语音识别中，直接跳过
        if (isVoiceListening) return
        isVoiceListening = true
        // 新一轮会话：清掉上一轮的收尾状态和还没触发的兜底超时
        voiceFinalJob?.cancel()
        voiceFinalJob = null
        voiceStopRequested = false
        voiceSessionDone = false
        lastVoiceCommittedText = null
        voiceComposingActive = false
        if (voiceRecognizer == null) {
            voiceRecognizer = org.fcitx.fcitx5.android.input.voice.VoiceRecognizer(
                context,
                service.lifecycleScope,
                onTextResult = { text, isFinal ->
                    when {
                        // 已收尾：迟到的分片/重复上报一律丢掉，否则会把上屏的内容再插一遍
                        voiceSessionDone ->
                            timber.log.Timber.d("Voice: drop late result (final=$isFinal)")

                        // 松手后已经提交过最终结果，此时再来分片只能是迟到的
                        !isFinal && voiceStopRequested && lastVoiceCommittedText != null ->
                            timber.log.Timber.d("Voice: drop late partial after final commit")

                        !isFinal -> service.currentInputConnection?.let {
                            it.setComposingText(text, 1)
                            voiceComposingActive = true
                        }

                        else -> {
                            // 最终结果：commitText 会先删掉 composing 区域再插入，
                            // 所以 stopVoiceRecognition() 里绝对不能提前 finishComposingText()，
                            // 否则就是「下划线部分先定稿 + 最终结果再插一遍」= 整句重复两遍。
                            if (voiceStopRequested && text == lastVoiceCommittedText) {
                                // 松手后服务器把同一句已定稿的结果又报了一遍，重复插入会整句重复
                                timber.log.Timber.w("Voice: duplicate final after stop, skipped")
                                if (voiceComposingActive) {
                                    service.currentInputConnection?.finishComposingText()
                                    voiceComposingActive = false
                                }
                            } else {
                                service.commitText(text)
                                voiceComposingActive = false
                                lastVoiceCommittedText = text
                            }
                            // 松手后可能还有不止一句的尾巴：每收到一条最终结果就把兜底定稿往后推
                            if (voiceStopRequested) startVoiceFinalTimeout()
                        }
                    }
                },
                onError = { err ->
                    timber.log.Timber.e("Voice recognition error: $err")
                    isVoiceListening = false
                    voiceStopRequested = true
                    finishVoiceComposingIfNeeded()
                    voiceSessionDone = true
                    voiceFinalJob?.cancel()
                    voiceFinalJob = null
                    kawaiiBar.hideVoiceListening()
                }
            )
        }
        kawaiiBar.showVoiceListening()
        voiceRecognizer?.start()
    }

    private fun stopVoiceRecognition() {
        voiceRecognizer?.stop()
        isVoiceListening = false
        voiceStopRequested = true
        // 这里**不能**调用 finishComposingText()：识别结果是异步回来的，
        // 提前把 composing 定稿会让随后到达的最终 commitText 在它后面再插一遍，
        // 结果就是「一句话上屏两遍」。正常路径由 commitText 覆盖 composing，
        // 兜底路径由 startVoiceFinalTimeout() 在安静一段时间后定稿。
        startVoiceFinalTimeout()
        kawaiiBar.hideVoiceListening()
    }

    /**
     * 松手后等最终结果：每收到一条最终结果就重置，安静了 [voiceFinalTimeoutMs] 才真正收尾。
     * 这样既能等完"尾巴上还有一句"的情况，也不会让没等到结果的文字一直带下划线。
     */
    private fun startVoiceFinalTimeout() {
        voiceFinalJob?.cancel()
        voiceFinalJob = service.lifecycleScope.launch {
            delay(voiceFinalTimeoutMs)
            if (!voiceSessionDone) {
                voiceSessionDone = true
                finishVoiceComposingIfNeeded()
                timber.log.Timber.w(
                    "Voice: no more results in ${voiceFinalTimeoutMs}ms, voice input finished"
                )
            }
        }
    }

    /** 只在"确实有我们写入的 composing"且"用户没开始新的组字（拼音）"时才定稿 */
    private fun finishVoiceComposingIfNeeded() {
        if (voiceComposingActive && preeditState.isEmpty) {
            service.currentInputConnection?.finishComposingText()
        }
        voiceComposingActive = false
    }

    // there should be a new fcitx API for this
    private suspend fun FcitxAPI.commitAndReset() {
        if (inputMethodEntryCached.languageCode.startsWith("zh")) {
            // Chinese: select 1st candidate, except prediction candidates
            if (clientPreeditCached.isNotEmpty() || inputPanelCached.preedit.isNotEmpty()) {
                // preedit not empty, maybe there are candidates to select ...
                select(0)
            }
        } else {
            // Other languages: commit preedit as-is
            service.finishComposing()
        }
        reset()
    }

    private fun showInputMethodPicker() {
        fcitx.launchOnReady {
            service.lifecycleScope.launch {
                service.showDialog(InputMethodPickerDialog.build(it, service, context))
            }
        }
    }

    val listener by lazy {
        KeyActionListener { action, _ ->
            when (action) {
                is FcitxKeyAction -> service.postFcitxJob {
                    sendKey(action.act, action.states.states, action.code)
                }
                is SymAction -> service.postFcitxJob {
                    sendKey(action.sym, action.states)
                }
                is CommitAction -> service.postFcitxJob {
                    commitAndReset()
                    service.lifecycleScope.launch { service.commitText(action.text) }
                }
                is QuickPhraseAction -> service.postFcitxJob {
                    commitAndReset()
                    triggerQuickPhrase()
                }
                is UnicodeAction -> service.postFcitxJob {
                    commitAndReset()
                    triggerUnicode()
                }
                is LangSwitchAction -> {
                    when (langSwitchKeyBehavior) {
                        LangSwitchBehavior.Enumerate -> {
                            service.postFcitxJob {
                                if (enabledIme().size < 2) {
                                    service.lifecycleScope.launch {
                                        service.showDialog(AddMoreInputMethodsPrompt.build(context))
                                    }
                                } else {
                                    enumerateIme()
                                }
                            }
                        }
                        LangSwitchBehavior.ToggleActivate -> {
                            service.postFcitxJob {
                                toggleIme()
                            }
                        }
                        LangSwitchBehavior.NextInputMethodApp -> {
                            service.switchToNextIME()
                        }
                    }
                }
                is ShowInputMethodPickerAction -> showInputMethodPicker()
                is MoveSelectionAction -> {
                    when (backspaceSwipeState) {
                        Stopped -> {
                            backspaceSwipeState = if (
                                preeditState.isEmpty &&
                                horizontalCandidate.adapter.total <= 0 // total is -1 on initialization
                            ) {
                                service.applySelectionOffset(action.start, action.end)
                                Selection
                            } else {
                                Reset
                            }
                        }
                        Selection -> {
                            service.applySelectionOffset(action.start, action.end)
                        }
                        Reset -> {}
                    }
                }
                is DeleteSelectionAction -> {
                    when (backspaceSwipeState) {
                        Stopped -> {}
                        Selection -> service.deleteSelection()
                        Reset -> if (action.totalCnt < 0) { // swipe left
                            service.postFcitxJob { reset() }
                        }
                    }
                    backspaceSwipeState = Stopped
                }
                is PickerSwitchAction -> {
                    // update lastSymbolType only when specified explicitly
                    val key = action.key?.also { k -> lastPickerType = k.name }
                        ?: runCatching { PickerWindow.Key.valueOf(lastPickerType) }.getOrNull()
                        ?: PickerWindow.Key.Emoji
                    ContextCompat.getMainExecutor(service).execute {
                        windowManager.attachWindow(key)
                    }
                }
                is SpaceLongPressAction -> {
                    when (spaceKeyLongPressBehavior) {
                        SpaceLongPressBehavior.None -> {}
                        SpaceLongPressBehavior.Enumerate -> service.postFcitxJob {
                            enumerateIme()
                        }
                        SpaceLongPressBehavior.ToggleActivate -> service.postFcitxJob {
                            toggleIme()
                        }
                        SpaceLongPressBehavior.ShowPicker -> showInputMethodPicker()
                        SpaceLongPressBehavior.VoiceInput -> {
                            startVoiceRecognition()
                        }
                    }
                }
                is KeyAction.SpaceLongPressReleaseAction -> {
                    if (spaceKeyLongPressBehavior == SpaceLongPressBehavior.VoiceInput) {
                        stopVoiceRecognition()
                    }
                }
                else -> {}
            }
        }
    }
}
