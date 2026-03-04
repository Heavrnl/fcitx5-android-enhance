/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2023 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.input.keyboard

import android.annotation.SuppressLint
import android.content.Context
import android.view.View
import androidx.annotation.Keep
import androidx.core.view.allViews
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.core.InputMethodEntry
import org.fcitx.fcitx5.android.core.KeyState
import org.fcitx.fcitx5.android.core.KeyStates
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreference
import org.fcitx.fcitx5.android.data.theme.Theme
import org.fcitx.fcitx5.android.input.picker.PickerWindow
import org.fcitx.fcitx5.android.input.popup.PopupAction
import org.fcitx.fcitx5.android.input.handwriting.HandwritingEngine
import org.fcitx.fcitx5.android.input.keyboard.HandwritingOverlayView
import android.content.ContextWrapper
import org.fcitx.fcitx5.android.input.FcitxInputMethodService
import android.view.MotionEvent
import android.view.ViewConfiguration
import android.os.SystemClock
import org.fcitx.fcitx5.android.input.keyboard.KeyAction
import splitties.views.imageResource
import kotlin.math.abs

@SuppressLint("ViewConstructor")
class TextKeyboard(
    context: Context,
    theme: Theme
) : BaseKeyboard(context, theme, Layout) {

    enum class CapsState { None, Once, Lock }

    companion object {
        const val Name = "Text"

        val Layout: List<List<KeyDef>> = listOf(
            listOf(
                AlphabetKey("Q", "1"),
                AlphabetKey("W", "2"),
                AlphabetKey("E", "3"),
                AlphabetKey("R", "4"),
                AlphabetKey("T", "5"),
                AlphabetKey("Y", "6"),
                AlphabetKey("U", "7"),
                AlphabetKey("I", "8"),
                AlphabetKey("O", "9"),
                AlphabetKey("P", "0")
            ),
            listOf(
                AlphabetKey("A", "@"),
                AlphabetKey("S", "*"),
                AlphabetKey("D", "+"),
                AlphabetKey("F", "-"),
                AlphabetKey("G", "="),
                AlphabetKey("H", "/"),
                AlphabetKey("J", "#"),
                AlphabetKey("K", "("),
                AlphabetKey("L", ")")
            ),
            listOf(
                CapsKey(),
                AlphabetKey("Z", "'"),
                AlphabetKey("X", ":"),
                AlphabetKey("C", "\""),
                AlphabetKey("V", "?"),
                AlphabetKey("B", "!"),
                AlphabetKey("N", "~"),
                AlphabetKey("M", "\\"),
                BackspaceKey()
            ),
            listOf(
                LayoutSwitchKey("符", PickerWindow.Key.Symbol.name, 0.12f, KeyDef.Appearance.Variant.Alternative),
                LayoutSwitchKey("123", NumberKeyboard.Name, 0.12f, KeyDef.Appearance.Variant.Alternative),
                CommaKey(0.1f, KeyDef.Appearance.Variant.Alternative),
                SpaceKey(),
                SymbolKey(".", 0.1f, KeyDef.Appearance.Variant.Alternative),
                LanguageKey(),
                ReturnKey()
            )
        )
    }

    val caps: ImageKeyView by lazy { findViewById(R.id.button_caps) }
    val backspace: ImageKeyView by lazy { findViewById(R.id.button_backspace) }
    val quickphrase: ImageKeyView by lazy { findViewById(R.id.button_quickphrase) }
    val lang: ImageKeyView by lazy { findViewById(R.id.button_lang) }
    val space: TextKeyView by lazy { findViewById(R.id.button_space) }
    val `return`: ImageKeyView by lazy { findViewById(R.id.button_return) }

    private val showLangSwitchKey = AppPrefs.getInstance().keyboard.showLangSwitchKey

    @Keep
    private val showLangSwitchKeyListener = ManagedPreference.OnChangeListener<Boolean> { _, v ->
        updateLangSwitchKey(v)
    }

    private val enableHandwritingPref = AppPrefs.getInstance().keyboard.enableHandwriting
    private var enableHandwriting = enableHandwritingPref.getValue()
    @Keep
    private val enableHandwritingListener = ManagedPreference.OnChangeListener<Boolean> { _, v ->
        enableHandwriting = v
        if (!v && isWriting) {
            handwritingOverlay.cancelWriting()
            isWriting = false
        }
        handwritingOverlay.visibility = if (v) View.VISIBLE else View.GONE
    }

    private val keepLettersUppercase by AppPrefs.getInstance().keyboard.keepLettersUppercase
    
    // 全局引擎，建议放到单例或跟随 InputView 生命周期
    // 这里为简便先跟随 TextKeyboard 实例
    private val handwritingEngine = HandwritingEngine()

    private val touchSlop by lazy { ViewConfiguration.get(context).scaledTouchSlop }
    private val longPressTimeout by lazy { ViewConfiguration.getLongPressTimeout().toLong() }
    
    // 手写触发的短按停顿阈值（150ms，比长按短，足以区分瞬间按压滑动）
    private val writingTriggerDelay = 100L

    // 手写拦截状态机
    private var isWriting = false
    private var startX = 0f
    private var startY = 0f
    private var touchStartTime = 0L
    private var isMultiTouch = false
    private var isSwipeIgnored = false // 这个触摸序列是否已被定性为普通的快速 Swipe（输入符号）

    // 悬而未决的候选词
    private var pendingHandwritingCandidates: List<String>? = null

    private val fcitxService: FcitxInputMethodService? by lazy {
        var ctx = context
        while (ctx is ContextWrapper) {
            if (ctx is FcitxInputMethodService) break
            ctx = ctx.baseContext
        }
        ctx as? FcitxInputMethodService
    }

    private fun clearHandwritingCandidates() {
        pendingHandwritingCandidates = null
        fcitxService?.activeInputView?.updateHandwritingCandidates(emptyList(), null)
    }

    private fun commitPendingHandwriting() {
        pendingHandwritingCandidates?.let {
            if (it.isNotEmpty()) {
                onAction(KeyAction.CommitAction(it[0]))
            }
        }
        clearHandwritingCandidates()
    }

    private val handwritingOverlay: HandwritingOverlayView by lazy {
        HandwritingOverlayView(context, handwritingEngine) { results ->
            if (results.isNotEmpty()) {
                val bestMatch = results[0]
                // 首选项不再直接上屏，而是设置给输入框作为带有下划线的 Composing
                fcitxService?.currentInputConnection?.setComposingText(bestMatch, 1)
                
                // 记录状态，并在候选条上展示全量候选
                pendingHandwritingCandidates = results
                
                if (results.size > 1) {
                    fcitxService?.activeInputView?.updateHandwritingCandidates(results) { text, index ->
                        if (pendingHandwritingCandidates != null) {
                            // 由于处于 composing 状态，CommitAction 会直接覆盖 ComposingText
                            onAction(KeyAction.CommitAction(text))
                            clearHandwritingCandidates()
                        }
                    }
                } else {
                    fcitxService?.activeInputView?.updateHandwritingCandidates(emptyList(), null)
                }
            } else {
                clearHandwritingCandidates()
            }
        }
    }

    init {
        updateLangSwitchKey(showLangSwitchKey.getValue())
        showLangSwitchKey.registerOnChangeListener(showLangSwitchKeyListener)
        enableHandwritingPref.registerOnChangeListener(enableHandwritingListener)
        
        handwritingOverlay.visibility = if (enableHandwriting) View.VISIBLE else View.GONE
        
        // 置于最上层，铺满整个键盘区域
        addView(handwritingOverlay, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
    }

    private val textKeys: List<TextKeyView> by lazy {
        allViews.filterIsInstance(TextKeyView::class.java).toList()
    }

    private var capsState: CapsState = CapsState.None

    private fun transformAlphabet(c: String): String {
        return when (capsState) {
            CapsState.None -> c.lowercase()
            else -> c.uppercase()
        }
    }

    private var punctuationMapping: Map<String, String> = mapOf()
    private fun transformPunctuation(p: String) = punctuationMapping.getOrDefault(p, p)

    override fun onAction(action: KeyAction, source: KeyActionListener.Source) {
        // 如果将要执行其他按键动作（非点选手写候选引起的），且当前有悬而未决的手写候选，则提交首选项并清理状态。
        if (action !is KeyAction.CommitAction && pendingHandwritingCandidates != null) {
            commitPendingHandwriting()
        }

        var transformed = action
        when (action) {
            is KeyAction.FcitxKeyAction -> when (source) {
                KeyActionListener.Source.Keyboard -> {
                    when (capsState) {
                        CapsState.None -> {
                            transformed = action.copy(act = action.act.lowercase())
                        }
                        CapsState.Once -> {
                            transformed = action.copy(
                                act = action.act.uppercase(),
                                states = KeyStates(KeyState.Virtual, KeyState.Shift)
                            )
                            switchCapsState()
                        }
                        CapsState.Lock -> {
                            transformed = action.copy(
                                act = action.act.uppercase(),
                                states = KeyStates(KeyState.Virtual, KeyState.CapsLock)
                            )
                        }
                    }
                }
                KeyActionListener.Source.Popup -> {
                    if (capsState == CapsState.Once) {
                        switchCapsState()
                    }
                }
            }
            is KeyAction.CapsAction -> switchCapsState(action.lock)
            else -> {}
        }
        super.onAction(transformed, source)
    }

    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        if (!enableHandwriting) {
            return super.dispatchTouchEvent(ev)
        }

        val x = ev.x
        val y = ev.y
        val t = SystemClock.uptimeMillis()

        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                startX = x
                startY = y
                touchStartTime = t
                isMultiTouch = false
                isSwipeIgnored = false
                handwritingOverlay.resetRecognizeJob()

                if (handwritingOverlay.hasInk()) {
                    // 如果手写板上还有墨水，说明用户处于连续写字会话中。
                    // 此时这一下“哪怕只是轻轻一点”也得算作手笔（如“二”字的第一笔点），
                    // 绝不放行给底层键盘诱发按键高亮或穿透。立即全权接管。
                    isWriting = true
                    handwritingOverlay.startWriting(startX, startY, touchStartTime)
                    return true
                } else {
                    // 如果存在上一次未确认的手写候选，说明用户想继续新的一轮手写输入
                    if (pendingHandwritingCandidates != null) {
                        commitPendingHandwriting()
                    }
                    isWriting = false
                    // 干净状况：立即放行第一指接触，赋予底盘按键首发感应（譬如长按弹出小窗或点亮背景）
                    super.dispatchTouchEvent(ev)
                    return true
                }
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                isMultiTouch = true
                super.dispatchTouchEvent(ev)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (isMultiTouch || isSwipeIgnored) {
                    return super.dispatchTouchEvent(ev)
                }

                if (!isWriting) {
                    val dx = abs(x - startX)
                    val dy = abs(y - startY)
                    
                    // 如果移动超过了判定阈值
                    if (dx > touchSlop || dy > touchSlop) {
                        val touchDuration = t - touchStartTime
                        
                        // 首笔判断：是否在有效的时间窗口内
                        if (touchDuration < writingTriggerDelay) {
                            // 发生滑动的速度极快，认定为原生 Swipe (如上下划输入符号)
                            isSwipeIgnored = true
                            return super.dispatchTouchEvent(ev)
                        } else if (touchDuration > longPressTimeout) {
                            // 长按停留超过了阈值，原生按键极可能已弹出长按菜单或触发功能
                            // 此时滑动不应强行截走变手写，而当归还系统
                            isSwipeIgnored = true
                            return super.dispatchTouchEvent(ev)
                        } else {
                            // 停顿适当时间后画首笔，切断原生事件，正式全盘接管手写
                            isWriting = true

                            // 强行用虚假的 CANCEL 吃掉底层的 DOWN
                            val cancelEvent = MotionEvent.obtain(ev).apply { action = MotionEvent.ACTION_CANCEL }
                            super.dispatchTouchEvent(cancelEvent)
                            cancelEvent.recycle()
                            
                            // 启动画板
                            handwritingOverlay.startWriting(startX, startY, touchStartTime)
                            handwritingOverlay.continueWriting(x, y, t)
                            return true
                        }
                    } else {
                        // 未超过滑动阈值前，继续放养原生系统让它保持运作
                        // 若原地按住太久，直接判定本次非手写，锁断后续的画写
                        if (t - touchStartTime > longPressTimeout) {
                            isSwipeIgnored = true
                        }
                        super.dispatchTouchEvent(ev)
                        return true
                    }
                } else {
                    // 已处于手写模式，喂给画板，彻底隔绝底层键盘
                    handwritingOverlay.continueWriting(x, y, t)
                    return true
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (isMultiTouch || isSwipeIgnored) {
                    return super.dispatchTouchEvent(ev)
                }

                // 对于正在手写（刚画完长线，或者是已经被锁定的第二笔 DOWN）
                if (isWriting) {
                    handwritingOverlay.endWriting(x, y, t)
                    // 但凡带有书写墨迹的时刻，我们松手就不去结算它，让定时器自动判；
                    // 否则如果不带墨迹却在此刻松手（比如只是刚启动写就放了），那也只属于空放。
                    isWriting = false
                    return true
                } else {
                    // 没有触发手写，说明这只是个普通的短促点击
                    super.dispatchTouchEvent(ev)
                    return true
                }
            }
        }
        return super.dispatchTouchEvent(ev)
    }

    override fun onAttach() {
        handwritingEngine.initModel()
        capsState = CapsState.None
        updateCapsButtonIcon()
        updateAlphabetKeys()
    }

    override fun onReturnDrawableUpdate(returnDrawable: Int) {
        `return`.img.imageResource = returnDrawable
    }

    override fun onPunctuationUpdate(mapping: Map<String, String>) {
        punctuationMapping = mapping
        updatePunctuationKeys()
    }

    override fun onInputMethodUpdate(ime: InputMethodEntry) {
        space.mainText.text = buildString {
            append(ime.displayName)
            ime.subMode.run { label.ifEmpty { name.ifEmpty { null } } }?.let { append(" ($it)") }
        }
        if (capsState != CapsState.None) {
            switchCapsState()
        }
    }

    private fun transformPopupPreview(c: String): String {
        if (c.length != 1) return c
        if (c[0].isLetter()) return transformAlphabet(c)
        return transformPunctuation(c)
    }

    override fun onPopupAction(action: PopupAction) {
        val newAction = when (action) {
            is PopupAction.PreviewAction -> action.copy(content = transformPopupPreview(action.content))
            is PopupAction.PreviewUpdateAction -> action.copy(content = transformPopupPreview(action.content))
            is PopupAction.ShowKeyboardAction -> {
                val label = action.keyboard.label
                if (label.length == 1 && label[0].isLetter())
                    action.copy(keyboard = KeyDef.Popup.Keyboard(transformAlphabet(label)))
                else action
            }
            else -> action
        }
        super.onPopupAction(newAction)
    }

    private fun switchCapsState(lock: Boolean = false) {
        capsState =
            if (lock) {
                when (capsState) {
                    CapsState.Lock -> CapsState.None
                    else -> CapsState.Lock
                }
            } else {
                when (capsState) {
                    CapsState.None -> CapsState.Once
                    else -> CapsState.None
                }
            }
        updateCapsButtonIcon()
        updateAlphabetKeys()
    }

    private fun updateCapsButtonIcon() {
        caps.img.apply {
            imageResource = when (capsState) {
                CapsState.None -> R.drawable.ic_capslock_none
                CapsState.Once -> R.drawable.ic_capslock_once
                CapsState.Lock -> R.drawable.ic_capslock_lock
            }
        }
    }

    private fun updateLangSwitchKey(visible: Boolean) {
        lang.visibility = if (visible) View.VISIBLE else View.GONE
    }

    private fun updateAlphabetKeys() {
        textKeys.forEach {
            if (it.def !is KeyDef.Appearance.AltText) return
            it.mainText.text = it.def.displayText.let { str ->
                if (str.length != 1 || !str[0].isLetter()) return@forEach
                if (keepLettersUppercase) str.uppercase() else transformAlphabet(str)
            }
        }
    }

    private fun updatePunctuationKeys() {
        textKeys.forEach {
            if (it is AltTextKeyView) {
                it.def as KeyDef.Appearance.AltText
                it.altText.text = transformPunctuation(it.def.altText)
            } else {
                it.def as KeyDef.Appearance.Text
                it.mainText.text = it.def.displayText.let { str ->
                    if (str[0].run { isLetter() || isWhitespace() }) return@forEach
                    transformPunctuation(str)
                }
            }
        }
    }

    override fun onDetach() {
        handwritingEngine.close()
        super.onDetach()
    }

}