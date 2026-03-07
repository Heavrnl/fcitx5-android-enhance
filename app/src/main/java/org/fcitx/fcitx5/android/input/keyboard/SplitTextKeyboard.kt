/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2025 Fcitx5 for Android Contributors
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
import splitties.views.imageResource

/**
 * 分离键盘 — 在 QWERTY 基础上从 T/Y 处切分，中间插入间隔，
 * G 和 V 在左右两侧各出现一个，空格也拆分成左右两份。
 */
@SuppressLint("ViewConstructor")
class SplitTextKeyboard(
    context: Context,
    theme: Theme
) : BaseKeyboard(context, theme, Layout) {

    enum class CapsState { None, Once, Lock }

    companion object {
        const val Name = "SplitText"

        // 👇 修改这个变量，将其变小（比如 0.07f），中间的空隙就会变大；改成（0.08f）按键变大、空隙变小
        private const val W = 0.07f 
        
        private const val PAD_2 = W * 0.5f
        private const val GAP_1 = 1.0f - 10 * W
        private const val GAP_2 = 1.0f - 11 * W
        
        private const val CAPS_W = 1.5f * W
        private const val FUNC_W = 0.08f 
        private const val SPACE_W = 5.5f * W - FUNC_W * 2 - W

        val Layout: List<List<KeyDef>> = listOf(
            // 第1行：Q W E R T | Y U I O P
            listOf(
                AlphabetKey("q", "1", percentWidth = W),
                AlphabetKey("w", "2", percentWidth = W),
                AlphabetKey("e", "3", percentWidth = W),
                AlphabetKey("r", "4", percentWidth = W),
                AlphabetKey("t", "5", percentWidth = W),
                SpacerDef(percentWidth = GAP_1),
                AlphabetKey("y", "6", percentWidth = W),
                AlphabetKey("u", "7", percentWidth = W),
                AlphabetKey("i", "8", percentWidth = W),
                AlphabetKey("o", "9", percentWidth = W),
                AlphabetKey("p", "0", percentWidth = W)
            ),
            // 第2行：A S D F G | G H J K L
            listOf(
                SpacerDef(percentWidth = PAD_2),
                AlphabetKey("a", "@", percentWidth = W),
                AlphabetKey("s", "*", percentWidth = W),
                AlphabetKey("d", "+", percentWidth = W),
                AlphabetKey("f", "-", percentWidth = W),
                AlphabetKey("g", "=", percentWidth = W),
                SpacerDef(percentWidth = GAP_2),
                AlphabetKey("g", "=", percentWidth = W),
                AlphabetKey("h", "/", percentWidth = W),
                AlphabetKey("j", "#", percentWidth = W),
                AlphabetKey("k", "(", percentWidth = W),
                AlphabetKey("l", ")", percentWidth = W),
                SpacerDef(percentWidth = PAD_2)
            ),
            // 第3行：Caps Z X C V | V B N M Backspace
            listOf(
                CapsKey(percentWidth = CAPS_W),
                AlphabetKey("z", "'", percentWidth = W),
                AlphabetKey("x", ":", percentWidth = W),
                AlphabetKey("c", "\"", percentWidth = W),
                AlphabetKey("v", "?", percentWidth = W),
                SpacerDef(percentWidth = GAP_2),
                AlphabetKey("v", "?", percentWidth = W),
                AlphabetKey("b", "!", percentWidth = W),
                AlphabetKey("n", "~", percentWidth = W),
                AlphabetKey("m", "\\", percentWidth = W),
                BackspaceKey(percentWidth = CAPS_W)
            ),
            // 第4行：符 123 , Space | Space . Lang Return
            listOf(
                LayoutSwitchKey("符", PickerWindow.Key.Symbol.name, FUNC_W, KeyDef.Appearance.Variant.Alternative),
                LayoutSwitchKey("123", NumberKeyboard.Name, FUNC_W, KeyDef.Appearance.Variant.Alternative),
                CommaKey(percentWidth = W, variant = KeyDef.Appearance.Variant.Alternative),
                SpaceKey(percentWidth = SPACE_W),
                SpacerDef(percentWidth = GAP_2),
                SpaceKey(percentWidth = SPACE_W, viewId = R.id.button_space_right),
                SymbolKey(".", percentWidth = W, variant = KeyDef.Appearance.Variant.Alternative),
                LanguageKey(percentWidth = FUNC_W),
                ReturnKey(percentWidth = FUNC_W)
            )
        )
    }

    val caps: ImageKeyView by lazy { findViewById(R.id.button_caps) }
    val backspace: ImageKeyView by lazy { findViewById(R.id.button_backspace) }
    val quickphrase: ImageKeyView by lazy { findViewById(R.id.button_quickphrase) }
    val lang: ImageKeyView by lazy { findViewById(R.id.button_lang) }
    val `return`: ImageKeyView by lazy { findViewById(R.id.button_return) }

    private val showLangSwitchKey = AppPrefs.getInstance().keyboard.showLangSwitchKey

    @Keep
    private val showLangSwitchKeyListener = ManagedPreference.OnChangeListener<Boolean> { _, v ->
        updateLangSwitchKey(v)
    }

    private val keepLettersUppercase by AppPrefs.getInstance().keyboard.keepLettersUppercase

    init {
        updateLangSwitchKey(showLangSwitchKey.getValue())
        showLangSwitchKey.registerOnChangeListener(showLangSwitchKeyListener)
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

    override fun onAttach() {
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
        // 分离键盘有两个 space 键，通过 ID 匹配更新两个键的文本
        textKeys.filter { it.id == R.id.button_space || it.id == R.id.button_space_right }
            .forEach { spaceView ->
                spaceView.mainText.text = buildString {
                    append(ime.displayName)
                    ime.subMode.run { label.ifEmpty { name.ifEmpty { null } } }?.let { append(" ($it)") }
                }
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
            if (it.def !is KeyDef.Appearance.AltText) return@forEach
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
                    if (str.isEmpty() || str[0].run { isLetter() || isWhitespace() }) return@forEach
                    transformPunctuation(str)
                }
            }
        }
    }

    override fun onDetach() {
        super.onDetach()
    }
}
