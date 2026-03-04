/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2025 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.input.bar

import android.graphics.Color
import android.animation.ArgbEvaluator
import android.animation.ValueAnimator
import android.graphics.drawable.ColorDrawable
import android.os.Build
import android.util.Size
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.animation.Animation
import android.view.animation.DecelerateInterpolator
import android.view.animation.OvershootInterpolator
import android.view.animation.TranslateAnimation
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InlineSuggestion
import android.view.inputmethod.InlineSuggestionsResponse
import android.view.inputmethod.InputMethodSubtype
import android.widget.FrameLayout
import android.widget.ViewAnimator
import android.widget.inline.InlineContentView
import androidx.annotation.Keep
import androidx.annotation.RequiresApi
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.Dispatchers
import android.graphics.BitmapFactory
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.core.CapabilityFlag
import org.fcitx.fcitx5.android.core.CapabilityFlags
import org.fcitx.fcitx5.android.core.FcitxEvent.CandidateListEvent
import org.fcitx.fcitx5.android.data.clipboard.ClipboardManager
import org.fcitx.fcitx5.android.data.clipboard.db.ClipboardEntry
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreference
import org.fcitx.fcitx5.android.data.theme.ThemeManager
import org.fcitx.fcitx5.android.input.bar.ExpandButtonStateMachine.State.ClickToAttachWindow
import org.fcitx.fcitx5.android.input.bar.ExpandButtonStateMachine.State.ClickToDetachWindow
import org.fcitx.fcitx5.android.input.bar.ExpandButtonStateMachine.State.Hidden
import org.fcitx.fcitx5.android.input.bar.KawaiiBarStateMachine.BooleanKey.CandidateEmpty
import org.fcitx.fcitx5.android.input.bar.KawaiiBarStateMachine.BooleanKey.PreeditEmpty
import org.fcitx.fcitx5.android.input.bar.KawaiiBarStateMachine.TransitionEvent.CandidatesUpdated
import org.fcitx.fcitx5.android.input.bar.KawaiiBarStateMachine.TransitionEvent.ExtendedWindowAttached
import org.fcitx.fcitx5.android.input.bar.KawaiiBarStateMachine.TransitionEvent.PreeditUpdated
import org.fcitx.fcitx5.android.input.bar.KawaiiBarStateMachine.TransitionEvent.WindowDetached
import org.fcitx.fcitx5.android.input.bar.ui.CandidateUi
import org.fcitx.fcitx5.android.input.bar.ui.IdleUi
import org.fcitx.fcitx5.android.input.bar.ui.TitleUi
import org.fcitx.fcitx5.android.input.bar.ui.VoiceListeningUi
import org.fcitx.fcitx5.android.input.broadcast.InputBroadcastReceiver
import org.fcitx.fcitx5.android.input.candidates.expanded.ExpandedCandidateStyle
import org.fcitx.fcitx5.android.input.candidates.expanded.window.FlexboxExpandedCandidateWindow
import org.fcitx.fcitx5.android.input.candidates.expanded.window.GridExpandedCandidateWindow
import org.fcitx.fcitx5.android.input.candidates.horizontal.HorizontalCandidateComponent
import org.fcitx.fcitx5.android.input.clipboard.ClipboardWindow
import org.fcitx.fcitx5.android.input.dependency.UniqueViewComponent
import org.fcitx.fcitx5.android.input.dependency.context
import org.fcitx.fcitx5.android.input.dependency.inputMethodService
import org.fcitx.fcitx5.android.input.dependency.theme
import org.fcitx.fcitx5.android.input.editing.TextEditingWindow
import org.fcitx.fcitx5.android.input.keyboard.CommonKeyActionListener
import org.fcitx.fcitx5.android.input.keyboard.CustomGestureView
import org.fcitx.fcitx5.android.input.keyboard.KeyboardWindow
import org.fcitx.fcitx5.android.input.popup.PopupComponent
import org.fcitx.fcitx5.android.input.status.StatusAreaWindow
import org.fcitx.fcitx5.android.input.wm.InputWindow
import org.fcitx.fcitx5.android.input.wm.InputWindowManager
import org.fcitx.fcitx5.android.input.wm.windows.ToolbarEditWindow
import org.fcitx.fcitx5.android.utils.AppUtil
import org.fcitx.fcitx5.android.utils.InputMethodUtil
import org.mechdancer.dependency.DynamicScope
import org.mechdancer.dependency.manager.must
import splitties.bitflags.hasFlag
import splitties.dimensions.dp
import splitties.views.backgroundColor
import splitties.views.dsl.core.add
import splitties.views.dsl.core.lParams
import splitties.views.dsl.core.matchParent
import java.util.concurrent.Executor
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine
import kotlin.math.min

class KawaiiBarComponent : UniqueViewComponent<KawaiiBarComponent, FrameLayout>(),
    InputBroadcastReceiver {

    private val context by manager.context()
    private val theme by manager.theme()
    private val service by manager.inputMethodService()
    private val windowManager: InputWindowManager by manager.must()
    private val horizontalCandidate: HorizontalCandidateComponent by manager.must()
    private val commonKeyActionListener: CommonKeyActionListener by manager.must()
    private val popup: PopupComponent by manager.must()

    private val prefs = AppPrefs.getInstance()

    private val clipboardSuggestion = prefs.clipboard.clipboardSuggestion
    private val clipboardItemTimeout = prefs.clipboard.clipboardItemTimeout
    private val clipboardMaskSensitive by prefs.clipboard.clipboardMaskSensitive
    private val expandedCandidateStyle by prefs.keyboard.expandedCandidateStyle
    private val expandToolbarByDefault by prefs.keyboard.expandToolbarByDefault
    private val toolbarNumRowOnPassword by prefs.keyboard.toolbarNumRowOnPassword
    private val showVoiceInputButton by prefs.keyboard.showVoiceInputButton

    private var clipboardTimeoutJob: Job? = null

    private var isClipboardFresh: Boolean = false
    private var isInlineSuggestionPresent: Boolean = false
    private var isCapabilityFlagsPassword: Boolean = false
    private var isKeyboardLayoutNumber: Boolean = false
    private var isToolbarManuallyToggled: Boolean = false
    private var isEditingToolbar: Boolean = false
    private var currentEditWindow: ToolbarEditWindow? = null

    @Keep
    private val onClipboardUpdateListener =
        ClipboardManager.OnClipboardUpdateListener {
            if (!clipboardSuggestion.getValue()) return@OnClipboardUpdateListener
            service.lifecycleScope.launch {
                if (it.isImage) {
                    // 图片类型：显示缩略图和简短文本
                    idleUi.clipboardUi.icon.visibility = View.GONE
                    idleUi.clipboardUi.openLinkButton.visibility = View.GONE
                    idleUi.clipboardUi.image.visibility = View.VISIBLE
                    idleUi.clipboardUi.text.text = context.getString(R.string.clipboard_image_simple)

                    // 加载缩略图
                    val imagePath = it.imagePath
                    val bitmap = withContext(Dispatchers.IO) {
                        try {
                            val options = BitmapFactory.Options().apply {
                                inJustDecodeBounds = true
                            }
                            BitmapFactory.decodeFile(imagePath, options)
                            var sampleSize = 1
                            val targetSize = 64 // 缩略图不需要太大
                            if (options.outHeight > targetSize || options.outWidth > targetSize) {
                                val halfHeight = options.outHeight / 2
                                val halfWidth = options.outWidth / 2
                                while ((halfHeight / sampleSize) >= targetSize && (halfWidth / sampleSize) >= targetSize) {
                                    sampleSize *= 2
                                }
                            }
                            val decodeOptions = BitmapFactory.Options().apply {
                                inSampleSize = sampleSize
                            }
                            BitmapFactory.decodeFile(imagePath, decodeOptions)
                        } catch (e: Exception) {
                            null
                        }
                    }
                    idleUi.clipboardUi.image.setImageBitmap(bitmap)

                    isClipboardFresh = true
                    launchClipboardTimeoutJob()
                } else if (it.text.isEmpty()) {
                    isClipboardFresh = false
                } else {
                    // 文本类型：恢复图标显示
                    idleUi.clipboardUi.icon.visibility = View.VISIBLE
                    idleUi.clipboardUi.image.visibility = View.GONE
                    
                    // URL Check
                    val match = android.util.Patterns.WEB_URL.matcher(it.text)
                    if (match.find()) {
                        val url = match.group()
                        idleUi.clipboardUi.openLinkButton.visibility = View.VISIBLE
                        idleUi.clipboardUi.openLinkButton.setOnClickListener {
                             try {
                                val finalUrl = if (!url.startsWith("http://") && !url.startsWith("https://")) {
                                    "http://$url"
                                } else url
                                val intent = android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse(finalUrl)).apply {
                                    addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
                                }
                                service.startActivity(intent)
                             } catch (e: Exception) {
                                 timber.log.Timber.e(e, "Failed to open URL: $url")
                             }
                        }
                    } else {
                        idleUi.clipboardUi.openLinkButton.visibility = View.GONE
                    }

                    idleUi.clipboardUi.text.text = if (it.sensitive && clipboardMaskSensitive) {
                        ClipboardEntry.BULLET.repeat(min(42, it.text.length))
                    } else {
                        it.text.take(42)
                    }
                    isClipboardFresh = true
                    launchClipboardTimeoutJob()
                }
                evalIdleUiState()
            }
        }

    @Keep
    private val onClipboardSuggestionUpdateListener =
        ManagedPreference.OnChangeListener<Boolean> { _, it ->
            if (!it) {
                isClipboardFresh = false
                evalIdleUiState()
                clipboardTimeoutJob?.cancel()
                clipboardTimeoutJob = null
            }
        }

    @Keep
    private val onClipboardTimeoutUpdateListener =
        ManagedPreference.OnChangeListener<Int> { _, _ ->
            when (idleUi.currentState) {
                IdleUi.State.Clipboard -> {
                    // renew timeout when clipboard suggestion is present
                    launchClipboardTimeoutJob()
                }
                else -> {}
            }
        }

    private fun launchClipboardTimeoutJob() {
        clipboardTimeoutJob?.cancel()
        val timeout = clipboardItemTimeout.getValue() * 1000L
        // never transition to ClipboardTimedOut state when timeout < 0
        if (timeout < 0L) return
        clipboardTimeoutJob = service.lifecycleScope.launch {
            delay(timeout)
            isClipboardFresh = false
            clipboardTimeoutJob = null
        }
    }

    private fun evalIdleUiState(fromUser: Boolean = false) {
        if (isEditingToolbar) return // 编辑期间不响应外部状态（如剪贴板）对 IdleUi 状态栏的干扰
        val newState = when {
            isClipboardFresh -> IdleUi.State.Clipboard
            isInlineSuggestionPresent -> IdleUi.State.InlineSuggestion
            isCapabilityFlagsPassword && !isKeyboardLayoutNumber -> IdleUi.State.NumberRow
            /**
             * state matrix:
             *                               expandToolbarByDefault
             *                          |   \   |    true |   false
             * isToolbarManuallyToggled |  true |   Empty | Toolbar
             *                          | false | Toolbar |   Empty
             */
            expandToolbarByDefault == isToolbarManuallyToggled -> IdleUi.State.Empty
            else -> IdleUi.State.Toolbar
        }
        if (newState == idleUi.currentState) return
        idleUi.updateState(newState, fromUser)
    }

    private val hideKeyboardCallback = View.OnClickListener {
        if (isEditingToolbar) {
            exitToolbarEditMode()
            return@OnClickListener
        }
        service.requestHideSelf(0)
    }

    private val swipeDownHideKeyboardCallback = CustomGestureView.OnGestureListener { _, e ->
        if (e.type == CustomGestureView.GestureType.Up && e.totalY > 0) {
            service.requestHideSelf(0)
            true
        } else false
    }

    private var voiceInputSubtype: Pair<String, InputMethodSubtype>? = null

    private val switchToVoiceInputCallback = View.OnClickListener {
        if (isEditingToolbar) {
            exitToolbarEditMode()
            return@OnClickListener
        }
        val (id, subtype) = voiceInputSubtype ?: return@OnClickListener
        InputMethodUtil.switchInputMethod(service, id, subtype)
    }

    private val idleUi: IdleUi by lazy {
        IdleUi(context, theme, popup, commonKeyActionListener).apply {
            menuButton.setOnClickListener {
                if (isEditingToolbar) {
                    exitToolbarEditMode()
                    return@setOnClickListener
                }
                when (idleUi.currentState) {
                    IdleUi.State.Empty -> {
                        isToolbarManuallyToggled = !expandToolbarByDefault
                        evalIdleUiState(fromUser = true)
                    }
                    IdleUi.State.Toolbar -> {
                        isToolbarManuallyToggled = expandToolbarByDefault
                        evalIdleUiState(fromUser = true)
                    }
                    else -> {
                        isToolbarManuallyToggled = !expandToolbarByDefault
                        idleUi.updateState(IdleUi.State.Toolbar, fromUser = true)
                    }
                }
                // reset timeout timer (if present) when user switch layout
                if (clipboardTimeoutJob != null) {
                    launchClipboardTimeoutJob()
                }
            }
            hideKeyboardButton.apply {
                setOnClickListener(hideKeyboardCallback)
                swipeEnabled = true
                swipeThresholdY = dp(HEIGHT.toFloat())
                onGestureListener = swipeDownHideKeyboardCallback
            }
            buttonsUi.apply {
                undoButton.setOnClickListener {
                    service.sendCombinationKeyEvents(KeyEvent.KEYCODE_Z, ctrl = true)
                }
                redoButton.setOnClickListener {
                    service.sendCombinationKeyEvents(KeyEvent.KEYCODE_Z, ctrl = true, shift = true)
                }
                cursorMoveButton.setOnClickListener {
                    windowManager.attachWindow(TextEditingWindow())
                }
                voiceButton.setOnClickListener {
                    windowManager.attachWindow(org.fcitx.fcitx5.android.input.voice.VoiceWindow())
                }
                clipboardButton.setOnClickListener {
                    windowManager.attachWindow(ClipboardWindow())
                }
                quickPhraseButton.setOnClickListener {
                    windowManager.attachWindow(org.fcitx.fcitx5.android.input.quickphrase.QuickPhraseWindow())
                }
                moreButton.setOnClickListener {
                    windowManager.attachWindow(StatusAreaWindow())
                }

                // 绑定拖拽开启的编辑模式
                onEditActionRequested = {
                    startToolbarEditMode()
                }

                // 绑定直接点减号送到仓库
                onButtonRemoved = { key ->
                    currentEditWindow?.catchDroppedButtonFromTop(key)
                }
            }
            clipboardUi.suggestionView.apply {
                setOnClickListener {
                    ClipboardManager.lastEntry?.let { entry ->
                        if (entry.isImage) {
                            // 图片类型：使用 Rich Content API 发送图片
                            try {
                                val imageFile = java.io.File(entry.imagePath)
                                if (imageFile.exists()) {
                                    val imageUri = androidx.core.content.FileProvider.getUriForFile(
                                        context,
                                        "${context.packageName}.fileprovider",
                                        imageFile
                                    )
                                    if (!service.commitImage(imageUri, entry.type)) {
                                        // 如果输入框不支持图片，复制到系统剪贴板
                                        val clip = android.content.ClipData.newUri(
                                            context.contentResolver,
                                            "Clipboard Image",
                                            imageUri
                                        )
                                        (context.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager)
                                            .setPrimaryClip(clip)
                                    }
                                }
                            } catch (e: Exception) {
                                timber.log.Timber.e(e, "Failed to paste image")
                            }
                        } else {
                            // 文本类型
                            service.commitText(entry.text)
                        }
                    }
                    clipboardTimeoutJob?.cancel()
                    clipboardTimeoutJob = null
                    isClipboardFresh = false
                    evalIdleUiState()
                }
                setOnLongClickListener {
                    ClipboardManager.lastEntry?.let {
                        AppUtil.launchClipboardEdit(context, it.id, true)
                    }
                    true
                }
            }
        }
    }

    private fun startToolbarEditMode() {
        val allButtonsArray = mapOf(
            "undo" to idleUi.buttonsUi.undoButton,
            "redo" to idleUi.buttonsUi.redoButton,
            "cursorMove" to idleUi.buttonsUi.cursorMoveButton,
            "clipboard" to idleUi.buttonsUi.clipboardButton,
            "quickPhrase" to idleUi.buttonsUi.quickPhraseButton,
            "more" to idleUi.buttonsUi.moreButton,
            "voice" to idleUi.buttonsUi.voiceButton
        )

        // 屏蔽键盘切换、展开按钮等（可选，暂直接改变状态）
        isEditingToolbar = true
        idleUi.buttonsUi.isEditMode = true

        val hiddenPref = prefs.internal.buttonsBarHidden.getValue()
        val hiddens = if (hiddenPref.isEmpty()) emptyList() else hiddenPref.split(",")
        // 显示 Toolbar Edit Window
        val window = ToolbarEditWindow(allButtonsArray, hiddens) { action, addedKeys, newHiddenKeys ->
            when (action) {
                ToolbarEditWindow.Action.SAVE -> {
                    // 读取上方保留的 order
                    val newOrder = mutableListOf<String>()
                    val flexbox = idleUi.buttonsUi.root as com.google.android.flexbox.FlexboxLayout
                    for (i in 0 until flexbox.childCount) {
                        (flexbox.getChildAt(i).tag as? String)?.let { newOrder.add(it) }
                    }
                    prefs.internal.buttonsBarOrder.setValue(newOrder.joinToString(","))
                    prefs.internal.buttonsBarHidden.setValue(newHiddenKeys?.joinToString(",") ?: "")
                    exitToolbarEditMode()
                }
                ToolbarEditWindow.Action.RESET -> {
                    // 恢复成系统默认
                    prefs.internal.buttonsBarOrder.setValue("undo,redo,cursorMove,clipboard,quickPhrase,voice,more")
                    prefs.internal.buttonsBarHidden.setValue("")
                    exitToolbarEditMode()
                }
                ToolbarEditWindow.Action.CANCEL -> {
                    exitToolbarEditMode()
                }
                ToolbarEditWindow.Action.ADD_TO_TOP -> {
                    // 底下仓库图标点了加号，上浮
                    addedKeys?.firstOrNull()?.let {
                        idleUi.buttonsUi.catchAddedButtonFromBottom(it)
                    }
                }
            }
        }
        currentEditWindow = window
        windowManager.attachWindow(window)
        // 强制进入编辑模式后显示工具条本身
        idleUi.updateState(IdleUi.State.Toolbar)
    }

    private fun exitToolbarEditMode() {
        currentEditWindow = null
        isEditingToolbar = false
        idleUi.buttonsUi.isEditMode = false
        // 从 Prefs 重新装载 UI 到 idleUi
        idleUi.buttonsUi.loadButtonsFromPrefs()
        // 恢复键盘
        windowManager.attachWindow(KeyboardWindow)
        evalIdleUiState(true)
    }

    private val candidateUi by lazy {
        CandidateUi(context, theme, horizontalCandidate.view).apply {
            expandButton.apply {
                swipeEnabled = true
                swipeThresholdY = dp(HEIGHT.toFloat())
                onGestureListener = swipeDownHideKeyboardCallback
            }
        }
    }

    private val titleUi by lazy {
        TitleUi(context, theme)
    }

    private val voiceListeningUi by lazy {
        VoiceListeningUi(context, theme)
    }

    private var savedBarChildIndex = 0

    private val barStateMachine = KawaiiBarStateMachine.new {
        switchUiByState(it)
    }

    val expandButtonStateMachine = ExpandButtonStateMachine.new {
        when (it) {
            ClickToAttachWindow -> {
                setExpandButtonToAttach()
                setExpandButtonEnabled(true)
            }
            ClickToDetachWindow -> {
                setExpandButtonToDetach()
                setExpandButtonEnabled(true)
            }
            Hidden -> {
                setExpandButtonEnabled(false)
            }
        }
    }

    // set expand candidate button to create expand candidate
    private fun setExpandButtonToAttach() {
        candidateUi.expandButton.setOnClickListener {
            windowManager.attachWindow(
                when (expandedCandidateStyle) {
                    ExpandedCandidateStyle.Grid -> GridExpandedCandidateWindow()
                    ExpandedCandidateStyle.Flexbox -> FlexboxExpandedCandidateWindow()
                }
            )
        }
        candidateUi.expandButton.setIcon(R.drawable.ic_baseline_expand_more_24)
        candidateUi.expandButton.contentDescription = context.getString(R.string.expand_candidates_list)
    }

    // set expand candidate button to close expand candidate
    private fun setExpandButtonToDetach() {
        candidateUi.expandButton.setOnClickListener {
            windowManager.attachWindow(KeyboardWindow)
        }
        candidateUi.expandButton.setIcon(R.drawable.ic_baseline_expand_less_24)
        candidateUi.expandButton.contentDescription = context.getString(R.string.hide_candidates_list)
    }

    // should be used with setExpandButtonToAttach or setExpandButtonToDetach
    private fun setExpandButtonEnabled(enabled: Boolean) {
        candidateUi.expandButton.visibility = if (enabled) View.VISIBLE else View.INVISIBLE
    }

    private fun switchUiByState(state: KawaiiBarStateMachine.State) {
        val index = state.ordinal
        if (view.displayedChild == index) return

        // 普通状态切换不使用滑动动画
        view.inAnimation = null
        view.outAnimation = null

        val new = view.getChildAt(index)
        if (new != titleUi.root) {
            titleUi.setReturnButtonOnClickListener { }
            titleUi.setTitle("")
            titleUi.removeExtension()
        }
        view.displayedChild = index
    }

    override val view by lazy {
        ViewAnimator(context).apply {
            backgroundColor =
                if (ThemeManager.prefs.keyBorder.getValue()) android.graphics.Color.TRANSPARENT
                else theme.barColor
            
            add(idleUi.root, lParams(matchParent, matchParent))
            add(candidateUi.root, lParams(matchParent, matchParent))
            add(titleUi.root, lParams(matchParent, matchParent))
            add(voiceListeningUi.root, lParams(matchParent, matchParent))
        }
    }

    private var barBgColorAnimator: ValueAnimator? = null

    private fun setSlideAnimation() {
        // 统一左入右出效果
        val inAnim = TranslateAnimation(
            Animation.RELATIVE_TO_PARENT, -1.0f,
            Animation.RELATIVE_TO_PARENT, 0.0f,
            Animation.RELATIVE_TO_SELF, 0.0f,
            Animation.RELATIVE_TO_SELF, 0.0f
        ).apply {
            duration = 500
            interpolator = OvershootInterpolator(1.2f)
        }
        val outAnim = TranslateAnimation(
            Animation.RELATIVE_TO_PARENT, 0.0f,
            Animation.RELATIVE_TO_PARENT, 1.0f,
            Animation.RELATIVE_TO_SELF, 0.0f,
            Animation.RELATIVE_TO_SELF, 0.0f
        ).apply {
            duration = 400
            interpolator = DecelerateInterpolator()
        }
        view.inAnimation = inAnim
        view.outAnimation = outAnim
    }

    fun showVoiceListening() {
        savedBarChildIndex = view.displayedChild
        
        val startColor = (view.background as? ColorDrawable)?.color ?: theme.barColor
        val endColor = theme.accentKeyBackgroundColor
        
        barBgColorAnimator?.cancel()
        barBgColorAnimator = ValueAnimator.ofObject(ArgbEvaluator(), startColor, endColor).apply {
            duration = 300
            addUpdateListener { animator ->
                view.setBackgroundColor(animator.animatedValue as Int)
            }
            start()
        }
        
        setSlideAnimation()
        view.displayedChild = 3
    }

    fun hideVoiceListening() {
        if (view.displayedChild == 3) {
            val startColor = (view.background as? android.graphics.drawable.ColorDrawable)?.color ?: theme.accentKeyBackgroundColor
            val endColor = if (ThemeManager.prefs.keyBorder.getValue()) android.graphics.Color.TRANSPARENT
                          else theme.barColor
            
            barBgColorAnimator?.cancel()
            barBgColorAnimator = ValueAnimator.ofObject(ArgbEvaluator(), startColor, endColor).apply {
                duration = 300
                addUpdateListener { animator ->
                    view.setBackgroundColor(animator.animatedValue as Int)
                }
                start()
            }
            
            setSlideAnimation()
            view.displayedChild = savedBarChildIndex
        }
    }

    override fun onScopeSetupFinished(scope: DynamicScope) {
        ClipboardManager.lastEntry?.let {
            val now = System.currentTimeMillis()
            val clipboardTimeout = clipboardItemTimeout.getValue() * 1000L
            if (now - it.timestamp < clipboardTimeout) {
                onClipboardUpdateListener.onUpdate(it)
            }
        }
        ClipboardManager.addOnUpdateListener(onClipboardUpdateListener)
        clipboardSuggestion.registerOnChangeListener(onClipboardSuggestionUpdateListener)
        clipboardItemTimeout.registerOnChangeListener(onClipboardTimeoutUpdateListener)
    }

    override fun onStartInput(info: EditorInfo, capFlags: CapabilityFlags) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            idleUi.privateMode(info.imeOptions.hasFlag(EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING))
        }
        isCapabilityFlagsPassword = toolbarNumRowOnPassword && capFlags.has(CapabilityFlag.Password)
        isInlineSuggestionPresent = false
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            idleUi.inlineSuggestionsBar.clear()
        }
        voiceInputSubtype = InputMethodUtil.firstVoiceInput()
        val shouldShowVoiceInput =
            showVoiceInputButton && voiceInputSubtype != null && !capFlags.has(CapabilityFlag.Password)
        idleUi.setHideKeyboardIsVoiceInput(
            shouldShowVoiceInput,
            if (shouldShowVoiceInput) switchToVoiceInputCallback else hideKeyboardCallback
        )
        evalIdleUiState()
    }

    override fun onPreeditEmptyStateUpdate(empty: Boolean) {
        barStateMachine.push(PreeditUpdated, PreeditEmpty to empty)
    }

    override fun onCandidateUpdate(data: CandidateListEvent.Data) {
        barStateMachine.push(CandidatesUpdated, CandidateEmpty to data.candidates.isEmpty())
    }

    override fun onWindowAttached(window: InputWindow) {
        when (window) {
            is InputWindow.ExtendedInputWindow<*> -> {
                titleUi.setTitle(window.title)
                window.onCreateBarExtension()?.let { titleUi.addExtension(it, window.showTitle) }
                titleUi.setReturnButtonOnClickListener {
                    windowManager.attachWindow(KeyboardWindow)
                }
                barStateMachine.push(ExtendedWindowAttached)
            }
            else -> {}
        }
    }

    override fun onWindowDetached(window: InputWindow) {
        if (window is ToolbarEditWindow) {
            if (isEditingToolbar) {
                // 如果 ToolbarEditWindow 被外部（如点击空白处关闭）卸载，并且我们还在编辑模式中
                // 那么意味着它是取消/或者自然销毁，我们需要退出编辑模式
                exitToolbarEditMode()
            }
        }
        barStateMachine.push(WindowDetached)
    }

    private val suggestionSize by lazy {
        Size(ViewGroup.LayoutParams.WRAP_CONTENT, context.dp(HEIGHT))
    }

    private val directExecutor by lazy {
        Executor { it.run() }
    }

    @RequiresApi(Build.VERSION_CODES.R)
    fun handleInlineSuggestions(response: InlineSuggestionsResponse): Boolean {
        val suggestions = response.inlineSuggestions
        if (suggestions.isEmpty()) {
            isInlineSuggestionPresent = false
            evalIdleUiState()
            idleUi.inlineSuggestionsBar.clear()
            return true
        }
        var pinned: InlineSuggestion? = null
        val scrollable = mutableListOf<InlineSuggestion>()
        var extraPinnedCount = 0
        suggestions.forEach {
            if (it.info.isPinned) {
                if (pinned == null) {
                    pinned = it
                } else {
                    scrollable.add(extraPinnedCount++, it)
                }
            } else {
                scrollable.add(it)
            }
        }
        service.lifecycleScope.launch {
            idleUi.inlineSuggestionsBar.setPinnedView(
                pinned?.let { inflateInlineContentView(it) }
            )
        }
        service.lifecycleScope.launch {
            val views = scrollable.map { s ->
                service.lifecycleScope.async {
                    inflateInlineContentView(s)
                }
            }.awaitAll()
            idleUi.inlineSuggestionsBar.setScrollableViews(views)
        }
        isInlineSuggestionPresent = true
        evalIdleUiState()
        return true
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private suspend fun inflateInlineContentView(suggestion: InlineSuggestion): InlineContentView? {
        return suspendCoroutine { c ->
            // callback view might be null
            suggestion.inflate(context, suggestionSize, directExecutor) { v ->
                c.resume(v)
            }
        }
    }

    companion object {
        const val HEIGHT = 40
    }

    fun onKeyboardLayoutSwitched(isNumber: Boolean) {
        isKeyboardLayoutNumber = isNumber
        evalIdleUiState()
    }

}
