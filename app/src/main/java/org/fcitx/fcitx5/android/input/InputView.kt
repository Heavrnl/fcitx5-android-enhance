/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2025 Fcitx5 for Android Contributors
 */

package org.fcitx.fcitx5.android.input

import android.annotation.SuppressLint
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.Outline
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.MotionEvent
import android.view.ViewOutlineProvider
import android.view.WindowInsets
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InlineSuggestionsResponse
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import androidx.annotation.Keep
import androidx.annotation.RequiresApi
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.core.view.updateLayoutParams
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.core.CapabilityFlags
import org.fcitx.fcitx5.android.core.FcitxEvent
import org.fcitx.fcitx5.android.daemon.FcitxConnection
import org.fcitx.fcitx5.android.daemon.launchOnReady
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreferenceProvider
import org.fcitx.fcitx5.android.data.theme.Theme
import org.fcitx.fcitx5.android.data.theme.ThemeManager
import org.fcitx.fcitx5.android.input.bar.KawaiiBarComponent
import org.fcitx.fcitx5.android.input.broadcast.InputBroadcaster
import org.fcitx.fcitx5.android.input.broadcast.PreeditEmptyStateComponent
import org.fcitx.fcitx5.android.input.broadcast.PunctuationComponent
import org.fcitx.fcitx5.android.input.broadcast.ReturnKeyDrawableComponent
import org.fcitx.fcitx5.android.input.candidates.horizontal.HorizontalCandidateComponent
import org.fcitx.fcitx5.android.input.keyboard.CommonKeyActionListener
import org.fcitx.fcitx5.android.input.keyboard.KeyboardWindow
import org.fcitx.fcitx5.android.input.picker.emojiPicker
import org.fcitx.fcitx5.android.input.picker.emoticonPicker
import org.fcitx.fcitx5.android.input.handwriting.HandwritingCandidateBar
import org.fcitx.fcitx5.android.input.picker.symbolPicker
import org.fcitx.fcitx5.android.input.popup.PopupComponent
import org.fcitx.fcitx5.android.input.preedit.PreeditComponent
import org.fcitx.fcitx5.android.input.wm.InputWindowManager
import org.fcitx.fcitx5.android.utils.unset
import org.mechdancer.dependency.DynamicScope
import org.mechdancer.dependency.manager.wrapToUniqueComponent
import org.mechdancer.dependency.plusAssign
import splitties.dimensions.dp
import splitties.views.dsl.constraintlayout.above
import splitties.views.dsl.constraintlayout.below
import splitties.views.dsl.constraintlayout.bottomOfParent
import splitties.views.dsl.constraintlayout.centerHorizontally
import splitties.views.dsl.constraintlayout.centerInParent
import splitties.views.dsl.constraintlayout.centerVertically
import splitties.views.dsl.constraintlayout.constraintLayout
import splitties.views.dsl.constraintlayout.endOfParent
import splitties.views.dsl.constraintlayout.endToStartOf
import splitties.views.dsl.constraintlayout.lParams
import splitties.views.dsl.constraintlayout.startOfParent
import splitties.views.dsl.constraintlayout.startToEndOf
import splitties.views.dsl.constraintlayout.topOfParent
import splitties.views.dsl.core.add
import splitties.views.dsl.core.imageView
import splitties.views.dsl.core.matchParent
import splitties.views.dsl.core.view
import splitties.views.dsl.core.withTheme
import splitties.views.dsl.core.wrapContent
import splitties.views.imageDrawable

@SuppressLint("ViewConstructor")
class InputView(
    service: FcitxInputMethodService,
    fcitx: FcitxConnection,
    theme: Theme
) : BaseInputView(service, fcitx, theme) {

    private val keyBorder by ThemeManager.prefs.keyBorder

    private val customBackground = imageView {
        scaleType = ImageView.ScaleType.CENTER_CROP
    }

    private val placeholderOnClickListener = OnClickListener { }

    // use clickable view as padding, so MotionEvent can be split to padding view and keyboard view
    private val leftPaddingSpace = FrameLayout(context).apply {
        setOnClickListener(placeholderOnClickListener)
    }
    private val rightPaddingSpace = FrameLayout(context).apply {
        setOnClickListener(placeholderOnClickListener)
    }
    private val bottomPaddingSpace = view(::View) {
        // height as keyboardBottomPadding
        // bottomMargin as WindowInsets (Navigation Bar) offset
        setOnClickListener(placeholderOnClickListener)
    }

    // 单手模式相关
    private val internalPrefs = AppPrefs.getInstance().internal
    private val oneHandedModePref = internalPrefs.oneHandedMode
    private val oneHandedLastSidePref = internalPrefs.oneHandedLastSide

    // 单手模式侧边面板
    private val oneHandedPanel = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL
        gravity = Gravity.CENTER
        visibility = View.GONE
    }

    // 悬浮键盘相关
    private val floatingPref = internalPrefs.floatingKeyboardEnabled
    internal var isFloatingMode = if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
        floatingPref.getValue()
    } else {
        false
    }
    private var floatingOffsetX = 0f
    private var floatingOffsetY = 0f
    // 记录是否处于无候选词状态（preedit 为空）
    private var isPreeditEmpty = true

    // 拖拽手柄（小横线 + 大触摸区域）
    // 注意：dragHandle 放入 innerWrapper 的顶部区域，与 preedit 互斥显示
    @SuppressLint("ClickableViewAccessibility")
    internal val dragHandle = FrameLayout(context).apply {
        var defaultColor = theme.altKeyTextColor
        var targetColor = theme.accentKeyBackgroundColor
        val lineDrawable = GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            cornerRadius = dp(2f)
            setColor(defaultColor)
        }
        val line = View(context).apply {
            background = lineDrawable
        }
        val lineParams = FrameLayout.LayoutParams(dp(40), dp(4)).apply {
            gravity = Gravity.CENTER
        }
        addView(line, lineParams)
        visibility = View.GONE
        var lastTouchX = 0f
        var lastTouchY = 0f
        var colorAnimator: android.animation.ValueAnimator? = null
        
        fun animateColor(fromColor: Int, toColor: Int) {
            colorAnimator?.cancel()
            colorAnimator = android.animation.ValueAnimator.ofArgb(fromColor, toColor).apply {
                duration = 200
                addUpdateListener { animator ->
                    lineDrawable.setColor(animator.animatedValue as Int)
                }
                start()
            }
        }

        setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    lastTouchX = event.rawX
                    lastTouchY = event.rawY
                    animateColor(defaultColor, targetColor)
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = event.rawX - lastTouchX
                    val dy = event.rawY - lastTouchY
                    floatingOffsetX += dx
                    floatingOffsetY += dy
                    // 整体移动 innerWrapper，由于 dragHandle 现为 innerWrapper 的子元素，它也会跟随移动
                    innerWrapper.translationX = floatingOffsetX
                    innerWrapper.translationY = floatingOffsetY
                    lastTouchX = event.rawX
                    lastTouchY = event.rawY
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    animateColor(targetColor, defaultColor)
                    true
                }
                else -> false
            }
        }
    }

    // 创建侧边面板按钮
    private fun createOneHandedPanelButtons() {
        oneHandedPanel.removeAllViews()
        val accentColor = theme.accentKeyBackgroundColor
        val btnSize = dp(44)
        val iconPadding = dp(10)

        val currentMode = oneHandedModePref.getValue()

        // 切换到另一手按钮
        val switchBtn = ImageView(context).apply {
            if (currentMode == "right") {
                setImageResource(R.drawable.ic_onehand_left_24)
                contentDescription = context.getString(R.string.one_handed_left)
            } else {
                setImageResource(R.drawable.ic_onehand_right_24)
                contentDescription = context.getString(R.string.one_handed_right)
            }
            setPadding(iconPadding, iconPadding, iconPadding, iconPadding)
            drawable.setTint(accentColor)
            background = android.graphics.drawable.GradientDrawable().apply {
                shape = android.graphics.drawable.GradientDrawable.OVAL
                setColor(Color.WHITE)
            }
            setOnClickListener {
                // 左 ↔ 右 切换
                val newMode = if (currentMode == "right") "left" else "right"
                setOneHandedMode(newMode)
            }
        }

        // 全键盘按钮
        val fullBtn = ImageView(context).apply {
            setImageResource(R.drawable.ic_baseline_keyboard_24)
            contentDescription = context.getString(R.string.full_keyboard)
            setPadding(iconPadding, iconPadding, iconPadding, iconPadding)
            drawable.setTint(accentColor)
            background = android.graphics.drawable.GradientDrawable().apply {
                shape = android.graphics.drawable.GradientDrawable.OVAL
                setColor(Color.WHITE)
            }
            setOnClickListener {
                setOneHandedMode("off")
            }
        }

        val btnParams = LinearLayout.LayoutParams(btnSize, btnSize).apply {
            setMargins(0, dp(8), 0, dp(8))
        }
        oneHandedPanel.addView(switchBtn, btnParams)
        oneHandedPanel.addView(fullBtn, btnParams)
    }

    // 设置单手模式
    fun setOneHandedMode(mode: String) {
        if (mode != "off") {
            oneHandedLastSidePref.setValue(mode)
        }
        oneHandedModePref.setValue(mode)
        updateKeyboardSize()
    }

    // 切换悬浮键盘模式
    fun toggleFloatingMode() {
        isFloatingMode = !isFloatingMode
        if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            floatingPref.setValue(isFloatingMode)
        }
        floatingOffsetX = 0f
        floatingOffsetY = 0f
        applyFloatingState(isFloatingMode)
        updateKeyboardSize()
        requestLayout()
        windowManager.attachWindow(keyboardWindow)
    }

    // 悬浮缩放比例
    private val floatingScale = 0.75f

    // 应用或移除悬浮视觉效果
    private fun applyFloatingState(floating: Boolean) {
        if (floating) {
            // 等比例缩小 wrapper
            innerWrapper.scaleX = floatingScale
            innerWrapper.scaleY = floatingScale
            // 缩放轴心设为底部中心
            innerWrapper.pivotX = innerWrapper.width / 2f
            innerWrapper.pivotY = innerWrapper.height.toFloat()
            // 设置 elevation 产生阴影效果
            innerWrapper.elevation = dp(8f)
            popup.root.elevation = dp(16f) // 气泡也得具有更高的Z轴高度以防止被键盘盖住
            innerWrapper.outlineProvider = object : ViewOutlineProvider() {
                override fun getOutline(view: View, outline: Outline) {
                    outline.setRoundRect(0, 0, view.width, view.height, dp(12f))
                }
            }
            innerWrapper.clipToOutline = true
            // 为 topArea 赋予悬浮特有背景色（键盘底色）
            topArea?.setBackgroundColor(theme.backgroundColor)
            // 默认向上偏移一段距离，让键盘明显脱离底部
            val defaultOffsetY = if (floatingOffsetY == 0f) -dp(80f) else floatingOffsetY
            floatingOffsetY = defaultOffsetY
            innerWrapper.translationX = floatingOffsetX
            innerWrapper.translationY = floatingOffsetY
            // 隐藏底部占位
            bottomPaddingSpace.visibility = View.GONE
            // 显示拖拽手柄
            updateDragHandleVisibility()
        } else {
            // 恢复原始比例
            innerWrapper.scaleX = 1f
            innerWrapper.scaleY = 1f
            // 清除 elevation 和圆角
            innerWrapper.elevation = 0f
            popup.root.elevation = 0f
            innerWrapper.outlineProvider = ViewOutlineProvider.BACKGROUND
            innerWrapper.clipToOutline = false
            // 恢复 topArea 的背景为透明
            topArea?.background = null
            // 重置位移
            innerWrapper.translationX = 0f
            innerWrapper.translationY = 0f
            // 恢复底部占位
            bottomPaddingSpace.visibility = View.VISIBLE
            // 隐藏拖拽手柄
            dragHandle.visibility = View.GONE
        }
    }

    // 更新拖拽手柄及候选词栏可见性：悬浮模式且无候选词时显示手柄，否则显示候选区
    private fun updateDragHandleVisibility() {
        if (isFloatingMode) {
            topArea?.visibility = View.VISIBLE
            if (isPreeditEmpty) {
                // 无输入：显示手柄，隐藏候选区
                dragHandle.visibility = View.VISIBLE
                preedit.ui.root.visibility = View.GONE
            } else {
                // 有输入：显示候选区，隐藏手柄
                dragHandle.visibility = View.GONE
                preedit.ui.root.visibility = View.VISIBLE
            }
        } else {
            // 普通模式：完全隐藏手柄，并清除 topArea 的特有背景。
            // 候选区正常按需显示
            dragHandle.visibility = View.GONE
            preedit.ui.root.visibility = View.VISIBLE
            // 普通模式不需要强制 topArea 占用视口和背景色
            topArea?.background = null
            topArea?.visibility = View.VISIBLE
        }
    }

    // 供外部（PreeditEmptyState）调用来通知候选词状态变化
    fun onPreeditEmptyStateChanged(empty: Boolean) {
        isPreeditEmpty = empty
        updateDragHandleVisibility()
    }

    private val scope = DynamicScope()
    private val themedContext = context.withTheme(R.style.Theme_InputViewTheme)
    private val broadcaster = InputBroadcaster()
    private val popup = PopupComponent()
    private val punctuation = PunctuationComponent()
    private val returnKeyDrawable = ReturnKeyDrawableComponent()
    private val preeditEmptyState = PreeditEmptyStateComponent()
    private val preedit = PreeditComponent()
    private val commonKeyActionListener = CommonKeyActionListener()
    private val windowManager = InputWindowManager()
    private val kawaiiBar = KawaiiBarComponent()
    private val quickPhraseSuggestion = org.fcitx.fcitx5.android.input.quickphrase.QuickPhraseSuggestionComponent()
    private val horizontalCandidate = HorizontalCandidateComponent()
    private val keyboardWindow = KeyboardWindow()
    private val symbolPicker = symbolPicker()
    private val emojiPicker = emojiPicker()
    private val emoticonPicker = emoticonPicker()

    private var onHandwritingCandidateSelected: ((String, Int) -> Unit)? = null

    val handwritingCandidateBar = HandwritingCandidateBar(context, theme) { text, index ->
        onHandwritingCandidateSelected?.invoke(text, index)
    }.apply { 
        visibility = View.GONE 
    }

    fun updateHandwritingCandidates(candidates: List<String>, listener: ((String, Int) -> Unit)?) {
        onHandwritingCandidateSelected = listener
        if (candidates.isEmpty()) {
            handwritingCandidateBar.visibility = View.GONE
        } else {
            handwritingCandidateBar.setCandidates(candidates)
            handwritingCandidateBar.visibility = View.VISIBLE
        }
    }

    private fun setupScope() {
        scope += this@InputView.wrapToUniqueComponent()
        scope += service.wrapToUniqueComponent()
        scope += fcitx.wrapToUniqueComponent()
        scope += theme.wrapToUniqueComponent()
        scope += themedContext.wrapToUniqueComponent()
        scope += broadcaster
        scope += popup
        scope += punctuation
        scope += returnKeyDrawable
        scope += preeditEmptyState
        scope += preedit
        scope += commonKeyActionListener
        scope += windowManager
        scope += kawaiiBar
        scope += quickPhraseSuggestion
        scope += horizontalCandidate
        broadcaster.onScopeSetupFinished(scope)
    }

    private val keyboardPrefs = AppPrefs.getInstance().keyboard

    private val focusChangeResetKeyboard by keyboardPrefs.focusChangeResetKeyboard

    private val keyboardHeightPercent = keyboardPrefs.keyboardHeightPercent
    private val keyboardHeightPercentLandscape = keyboardPrefs.keyboardHeightPercentLandscape
    private val keyboardSidePadding = keyboardPrefs.keyboardSidePadding
    private val keyboardSidePaddingLandscape = keyboardPrefs.keyboardSidePaddingLandscape
    private val keyboardSidePaddingRight = keyboardPrefs.keyboardSidePaddingRight
    private val keyboardSidePaddingRightLandscape = keyboardPrefs.keyboardSidePaddingRightLandscape
    private val keyboardBottomPadding = keyboardPrefs.keyboardBottomPadding
    private val keyboardBottomPaddingLandscape = keyboardPrefs.keyboardBottomPaddingLandscape

    private val keyboardSizePrefs = listOf(
        keyboardHeightPercent,
        keyboardHeightPercentLandscape,
        keyboardSidePadding,
        keyboardSidePaddingLandscape,
        keyboardSidePaddingRight,
        keyboardSidePaddingRightLandscape,
        keyboardBottomPadding,
        keyboardBottomPaddingLandscape,
    )

    private val keyboardHeightPx: Int
        get() {
            // 如果处于悬浮模式，则无论横竖屏都强制拿手机长边(竖屏高度)与竖屏配置乘算
            if (isFloatingMode) {
                val percent = keyboardHeightPercent.getValue()
                val portraitHeight = kotlin.math.max(resources.displayMetrics.widthPixels, resources.displayMetrics.heightPixels)
                return portraitHeight * percent / 100
            }
            val percent = when (resources.configuration.orientation) {
                Configuration.ORIENTATION_LANDSCAPE -> keyboardHeightPercentLandscape
                else -> keyboardHeightPercent
            }.getValue()
            return resources.displayMetrics.heightPixels * percent / 100
        }

    // 左侧 padding（复用原 sidePadding）
    private val keyboardLeftPaddingPx: Int
        get() {
            val value = when (resources.configuration.orientation) {
                Configuration.ORIENTATION_LANDSCAPE -> keyboardSidePaddingLandscape
                else -> keyboardSidePadding
            }.getValue()
            return dp(value)
        }

    // 右侧 padding
    private val keyboardRightPaddingPx: Int
        get() {
            val value = when (resources.configuration.orientation) {
                Configuration.ORIENTATION_LANDSCAPE -> keyboardSidePaddingRightLandscape
                else -> keyboardSidePaddingRight
            }.getValue()
            return dp(value)
        }

    private val keyboardBottomPaddingPx: Int
        get() {
            val value = when (resources.configuration.orientation) {
                Configuration.ORIENTATION_LANDSCAPE -> keyboardBottomPaddingLandscape
                else -> keyboardBottomPadding
            }.getValue()
            return dp(value)
        }

    @Keep
    private val onKeyboardSizeChangeListener = ManagedPreferenceProvider.OnChangeListener { key ->
        if (keyboardSizePrefs.any { it.key == key }) {
            updateKeyboardSize()
        }
    }

    val keyboardView: View
    internal lateinit var innerWrapper: ConstraintLayout
    private var topArea: ConstraintLayout? = null

    init {
        // MUST call before any operation
        setupScope()

        // restore punctuation mapping in case of InputView recreation
        fcitx.launchOnReady {
            punctuation.updatePunctuationMapping(it.statusAreaActionsCached)
        }

        // make sure KeyboardWindow's view has been created before it receives any broadcast
        windowManager.addEssentialWindow(keyboardWindow, createView = true)
        windowManager.addEssentialWindow(symbolPicker)
        windowManager.addEssentialWindow(emojiPicker)
        windowManager.addEssentialWindow(emoticonPicker)
        // show KeyboardWindow by default
        windowManager.attachWindow(KeyboardWindow)

        broadcaster.onImeUpdate(fcitx.runImmediately { inputMethodEntryCached })

        customBackground.imageDrawable = theme.backgroundDrawable(keyBorder)

        keyboardView = constraintLayout {
            // allow MotionEvent to be delivered to keyboard while pressing on padding views.
            // although it should be default for apps targeting Honeycomb (3.0, API 11) and higher,
            // but it's not the case on some devices ... just set it here
            isMotionEventSplittingEnabled = true
            add(customBackground, lParams {
                centerVertically()
                centerHorizontally()
            })
            add(quickPhraseSuggestion.ui.root, lParams(matchParent, wrapContent) {
                topOfParent()
                centerHorizontally()
            })
            add(kawaiiBar.view, lParams(matchParent, dp(KawaiiBarComponent.HEIGHT)) {
                below(quickPhraseSuggestion.ui.root)
                centerHorizontally()
            })
            add(handwritingCandidateBar, lParams(matchParent, dp(KawaiiBarComponent.HEIGHT)) {
                below(quickPhraseSuggestion.ui.root)
                centerHorizontally()
            })
            add(leftPaddingSpace, lParams {
                below(kawaiiBar.view)
                startOfParent()
                bottomOfParent()
            })
            add(rightPaddingSpace, lParams {
                below(kawaiiBar.view)
                endOfParent()
                bottomOfParent()
            })
            add(windowManager.view, lParams {
                below(kawaiiBar.view)
                above(bottomPaddingSpace)
                /**
                 * set start and end constrain in [updateKeyboardSize]
                 */
            })
            add(bottomPaddingSpace, lParams {
                startToEndOf(leftPaddingSpace)
                endToStartOf(rightPaddingSpace)
                bottomOfParent()
            })
        }

        // 实例化 innerWrapper
        innerWrapper = constraintLayout {
            // 当候选词栏出现或消失造成尺寸变化时，实时调整缩放中心为新底部，避免悬浮键盘基石发生平移
            addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
                if (isFloatingMode) {
                    pivotX = width / 2f
                    pivotY = height.toFloat()
                }
            }
            // 设置统一背景色（随主题）在悬浮时形成完整的视觉边界
            if (isFloatingMode) {
                background = ColorDrawable(theme.backgroundColor)
            }

            // 顶部区域：容纳 preedit 或者 拖拽手柄
            topArea = constraintLayout {
                id = View.generateViewId()
                add(preedit.ui.root, lParams(matchParent, wrapContent) {
                    centerInParent()
                })
                add(dragHandle, lParams(matchParent, dp(40)) { // 加高手柄区域使其像个真正的标题栏
                    centerInParent()
                })
            }

            add(topArea!!, lParams(matchParent, wrapContent) {
                topOfParent()
                centerHorizontally()
            })
            add(keyboardView, lParams(matchParent, wrapContent) {
                below(topArea!!)
                centerHorizontally()
                bottomOfParent()
            })
        }

        updateKeyboardSize()

        val initialWrapperWidth = if (isFloatingMode) {
            val metrics = resources.displayMetrics
            kotlin.math.min(metrics.widthPixels, metrics.heightPixels)
        } else {
            matchParent
        }
        
        add(innerWrapper, lParams(initialWrapperWidth, wrapContent) {
            centerHorizontally()
            bottomOfParent()
        })
        add(popup.root, lParams(matchParent, matchParent) {
            centerVertically()
            centerHorizontally()
        })

        keyboardPrefs.registerOnChangeListener(onKeyboardSizeChangeListener)

        // 应用初始悬浮状态
        if (isFloatingMode) {
            innerWrapper.post {
                floatingOffsetX = 0f
                floatingOffsetY = 0f
                applyFloatingState(true)
                requestLayout()
            }
        }
    }

    fun updateKeyboardSize() {
        if (innerWrapper.layoutParams != null) {
            if (isFloatingMode) {
                // 在悬浮模式下，强制锁定键盘渲染层的实际宽度为手机“竖屏宽度”（长宽中的短边）
                val metrics = resources.displayMetrics
                val portraitWidth = kotlin.math.min(metrics.widthPixels, metrics.heightPixels)
                innerWrapper.updateLayoutParams { width = portraitWidth }
            } else {
                innerWrapper.updateLayoutParams { width = android.view.ViewGroup.LayoutParams.MATCH_PARENT }
            }
        }
        windowManager.view.updateLayoutParams {
            height = keyboardHeightPx
        }
        if (isFloatingMode) {
            bottomPaddingSpace.visibility = View.GONE
            bottomPaddingSpace.updateLayoutParams { height = 0 }
        } else {
            bottomPaddingSpace.updateLayoutParams {
                height = keyboardBottomPaddingPx
            }
        }

        // 悬浮模式下跳过单手模式和 padding 逻辑
        if (isFloatingMode) {
            leftPaddingSpace.visibility = GONE
            rightPaddingSpace.visibility = GONE
            oneHandedPanel.visibility = View.GONE
            (oneHandedPanel.parent as? ViewGroup)?.removeView(oneHandedPanel)
            windowManager.view.updateLayoutParams<LayoutParams> {
                startToEnd = unset
                endToStart = unset
                startOfParent()
                endOfParent()
            }
            preedit.ui.root.setPadding(0, 0, 0, 0)
            kawaiiBar.view.setPadding(0, 0, 0, 0)
            return
        }

        // 计算有效的左右 padding（考虑单手模式）
        val oneHandedMode = oneHandedModePref.getValue()
        val displayWidth = resources.displayMetrics.widthPixels
        val oneHandedPadding = (displayWidth * 0.25f).toInt()

        var leftPadding = keyboardLeftPaddingPx
        var rightPadding = keyboardRightPaddingPx

        val landscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        if (landscape) {
            val pad = dp(35)
            leftPadding += pad
            rightPadding += pad
        }

        // 单手模式覆盖 side padding
        when (oneHandedMode) {
            "right" -> {
                // 右手模式：键盘靠右，左侧留白
                leftPadding = oneHandedPadding
                rightPadding = 0
            }
            "left" -> {
                // 左手模式：键盘靠左，右侧留白
                leftPadding = 0
                rightPadding = oneHandedPadding
            }
        }

        // 管理单手面板显示
        if (oneHandedMode != "off") {
            createOneHandedPanelButtons()
            // 将面板放到正确的 paddingSpace 中
            (oneHandedPanel.parent as? ViewGroup)?.removeView(oneHandedPanel)
            oneHandedPanel.visibility = View.VISIBLE
            if (oneHandedMode == "right") {
                leftPaddingSpace.addView(oneHandedPanel, FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT
                ))
            } else {
                rightPaddingSpace.addView(oneHandedPanel, FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT
                ))
            }
        } else {
            oneHandedPanel.visibility = View.GONE
            (oneHandedPanel.parent as? ViewGroup)?.removeView(oneHandedPanel)
        }

        if (leftPadding == 0 && rightPadding == 0) {
            // 两侧都无 padding，隐藏 space view
            leftPaddingSpace.visibility = GONE
            rightPaddingSpace.visibility = GONE
            windowManager.view.updateLayoutParams<LayoutParams> {
                startToEnd = unset
                endToStart = unset
                startOfParent()
                endOfParent()
            }
        } else {
            // 分别设置左右 padding
            if (leftPadding > 0) {
                leftPaddingSpace.visibility = VISIBLE
                leftPaddingSpace.updateLayoutParams {
                    width = leftPadding
                }
            } else {
                leftPaddingSpace.visibility = GONE
            }
            if (rightPadding > 0) {
                rightPaddingSpace.visibility = VISIBLE
                rightPaddingSpace.updateLayoutParams {
                    width = rightPadding
                }
            } else {
                rightPaddingSpace.visibility = GONE
            }
            windowManager.view.updateLayoutParams<LayoutParams> {
                if (leftPadding > 0) {
                    startToStart = unset
                    startToEndOf(leftPaddingSpace)
                } else {
                    startToEnd = unset
                    startOfParent()
                }
                if (rightPadding > 0) {
                    endToEnd = unset
                    endToStartOf(rightPaddingSpace)
                } else {
                    endToStart = unset
                    endOfParent()
                }
            }
        }
        // 单手模式下 kawaiiBar 和 preedit 也跟随缩进
        preedit.ui.root.setPadding(leftPadding, 0, rightPadding, 0)
        kawaiiBar.view.setPadding(leftPadding, 0, rightPadding, 0)
    }

    override fun onApplyWindowInsets(insets: WindowInsets): WindowInsets {
        bottomPaddingSpace.updateLayoutParams<LayoutParams> {
            bottomMargin = getNavBarBottomInset(insets)
        }
        return insets
    }

    /**
     * called when [InputView] is about to show, or restart
     */
    fun startInput(info: EditorInfo, capFlags: CapabilityFlags, restarting: Boolean = false) {
        broadcaster.onStartInput(info, capFlags)
        returnKeyDrawable.updateDrawableOnEditorInfo(info)
        if (focusChangeResetKeyboard || !restarting) {
            windowManager.attachWindow(KeyboardWindow)
        }
    }

    override fun onStartHandleFcitxEvent() {
        val inputPanelData = fcitx.runImmediately { inputPanelCached }
        val inputMethodEntry = fcitx.runImmediately { inputMethodEntryCached }
        val statusAreaActions = fcitx.runImmediately { statusAreaActionsCached }
        arrayOf(
            FcitxEvent.InputPanelEvent(inputPanelData),
            FcitxEvent.IMChangeEvent(inputMethodEntry),
            FcitxEvent.StatusAreaEvent(
                FcitxEvent.StatusAreaEvent.Data(statusAreaActions, inputMethodEntry)
            )
        ).forEach { handleFcitxEvent(it) }
    }

    override fun handleFcitxEvent(it: FcitxEvent<*>) {
        when (it) {
            is FcitxEvent.CandidateListEvent -> {
                broadcaster.onCandidateUpdate(it.data)
                // 候选词为空时悬浮手柄可见
                onPreeditEmptyStateChanged(it.data.candidates.isEmpty())
            }
            is FcitxEvent.ClientPreeditEvent -> {
                preeditEmptyState.updatePreeditEmptyState(clientPreedit = it.data)
                broadcaster.onClientPreeditUpdate(it.data)
            }
            is FcitxEvent.InputPanelEvent -> {
                preeditEmptyState.updatePreeditEmptyState(preedit = it.data.preedit)
                broadcaster.onInputPanelUpdate(it.data)
            }
            is FcitxEvent.IMChangeEvent -> {
                broadcaster.onImeUpdate(it.data)
            }
            is FcitxEvent.StatusAreaEvent -> {
                punctuation.updatePunctuationMapping(it.data.actions)
                broadcaster.onStatusAreaUpdate(it.data.actions)
            }
            else -> {}
        }
    }

    fun updateSelection(start: Int, end: Int) {
        broadcaster.onSelectionUpdate(start, end)
    }

    @RequiresApi(Build.VERSION_CODES.R)
    fun handleInlineSuggestions(response: InlineSuggestionsResponse): Boolean {
        return kawaiiBar.handleInlineSuggestions(response)
    }

    override fun onDetachedFromWindow() {
        keyboardPrefs.unregisterOnChangeListener(onKeyboardSizeChangeListener)
        // clear DynamicScope, implies that InputView should not be attached again after detached.
        scope.clear()
        super.onDetachedFromWindow()
    }

}
