package org.fcitx.fcitx5.android.input.keyboard

import android.annotation.SuppressLint
import android.content.Context
import android.content.res.Configuration
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import androidx.core.content.edit
import androidx.core.view.setPadding
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.theme.ThemeManager
import org.fcitx.fcitx5.android.input.wm.InputWindow
import org.fcitx.fcitx5.android.input.wm.InputWindowManager
import org.mechdancer.dependency.manager.must
import kotlin.math.max
import kotlin.math.min

class KeyboardResizeWindow : InputWindow.SimpleInputWindow<KeyboardResizeWindow>() {

    private val keyboardPrefs = AppPrefs.getInstance().keyboard
    private val windowManager: InputWindowManager by manager.must()

    private val isLandscape: Boolean
        get() = context.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE

    private val heightPercentPref
        get() = if (isLandscape) keyboardPrefs.keyboardHeightPercentLandscape else keyboardPrefs.keyboardHeightPercent
    private val sidePaddingPref
        get() = if (isLandscape) keyboardPrefs.keyboardSidePaddingLandscape else keyboardPrefs.keyboardSidePadding
    private val rightPaddingPref
        get() = if (isLandscape) keyboardPrefs.keyboardSidePaddingRightLandscape else keyboardPrefs.keyboardSidePaddingRight
    private val bottomPaddingPref
        get() = if (isLandscape) keyboardPrefs.keyboardBottomPaddingLandscape else keyboardPrefs.keyboardBottomPadding

    private var textKeyboard: TextKeyboard? = null

    // 绘制四周模糊阴影背景的层及边框
    class GridOverlayView(context: Context, private val accentColor: Int) : View(context) {
        
        // 外部背景色
        private val dimPaint = Paint().apply {
            color = Color.parseColor("#44000000") // 较浅透明黑
            style = Paint.Style.FILL
        }

        // 边框画笔
        private val borderPaint = Paint().apply {
            color = accentColor
            strokeWidth = 2f * context.resources.displayMetrics.density
            style = Paint.Style.STROKE
        }

        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            // 绘制全屏透明黑色
            canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), dimPaint)

            val w = width.toFloat()
            val h = height.toFloat()

            // 绘制外部边界框
            canvas.drawRect(0f, 0f, w, h, borderPaint)
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onCreateView(): View {
        val theme = ThemeManager.activeTheme
        textKeyboard = TextKeyboard(context, theme).apply {
            onAttach()
        }

        val frameLayout = FrameLayout(context)
        
        // 1. 真实键盘层
        frameLayout.addView(textKeyboard, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        // 2. 网格遮罩层，阻断所有下层点击
        val overlay = GridOverlayView(context, theme.accentKeyBackgroundColor).apply {
            isClickable = true
        }
        frameLayout.addView(overlay, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        // 3. 尺寸限制参数
        val displayMetrics = context.resources.displayMetrics
        val displayHeight = displayMetrics.heightPixels
        val displayWidth = displayMetrics.widthPixels
        
        // 最大最小限制 
        // 键盘最小高度：不可低于屏幕的 25% (防止 UI 缩出界)
        val minHeightPercent = 25
        val maxHeightPercent = 40 // 最高不可突破 40%

        // 键盘最小宽度限制：最小保留 70% 的屏幕宽度
        val minWidthPx = (displayWidth * 0.7f).toInt()
        val maxSidePadding = (displayWidth - minWidthPx) / 2

        // 上下移动（BottomPadding）最高限制为屏幕高度的 50%
        val maxBottomPadding = displayHeight / 2

        // 4. 重置、移动、完成按钮容器
        val btnGroup = LinearLayout(context).apply {
            gravity = Gravity.CENTER
            orientation = LinearLayout.HORIZONTAL
            
            // 使用 Fcitx5 强调色
            val accentColor = theme.accentKeyBackgroundColor

            // DP 工具方法
            fun dp(value: Int): Int = (value * context.resources.displayMetrics.density).toInt()

            val btnReset = ImageView(context).apply {
                setImageResource(R.drawable.ic_baseline_replay_24)
                setPadding(dp(12))
                drawable.setTint(accentColor)
                background = android.graphics.drawable.GradientDrawable().apply {
                    shape = android.graphics.drawable.GradientDrawable.OVAL
                    setColor(Color.WHITE)
                }
                setOnClickListener {
                    heightPercentPref.sharedPreferences.edit { remove(heightPercentPref.key) }
                    sidePaddingPref.sharedPreferences.edit { remove(sidePaddingPref.key) }
                    rightPaddingPref.sharedPreferences.edit { remove(rightPaddingPref.key) }
                    bottomPaddingPref.sharedPreferences.edit { remove(bottomPaddingPref.key) }
                }
            }

            val btnMove = ImageView(context).apply {
                setImageResource(R.drawable.ic_cursor_move)
                setPadding(dp(16))
                drawable.setTint(Color.WHITE)
                background = android.graphics.drawable.GradientDrawable().apply {
                    shape = android.graphics.drawable.GradientDrawable.OVAL
                    setColor(accentColor)
                }
                var startY = 0f
                var startX = 0f
                var startBottomPadding = 0
                var startLeftPadding = 0
                var startRightPadding = 0
                setOnTouchListener { _, event ->
                    when (event.action) {
                        MotionEvent.ACTION_DOWN -> {
                            startY = event.rawY
                            startX = event.rawX
                            startBottomPadding = bottomPaddingPref.getValue()
                            startLeftPadding = sidePaddingPref.getValue()
                            startRightPadding = rightPaddingPref.getValue()
                            true
                        }
                        MotionEvent.ACTION_MOVE -> {
                            // 垂直移动
                            val deltaY = event.rawY - startY
                            val deltaYDp = px2dp(deltaY)
                            val proposedPadding = startBottomPadding - deltaYDp
                            val newBottomPadding = min(px2dp(maxBottomPadding.toFloat()), max(0, proposedPadding))
                            bottomPaddingPref.setValue(newBottomPadding)

                            // 水平移动：同时等量调整左右 padding，保持总和不变以维持键盘宽度
                            val deltaX = event.rawX - startX
                            val deltaXDp = px2dp(deltaX)
                            val totalPadding = startLeftPadding + startRightPadding
                            // 向右移动：左侧 padding 增加，右侧 padding 减少
                            var newLeft = startLeftPadding + deltaXDp
                            var newRight = startRightPadding - deltaXDp
                            // clamp 到 [0, totalPadding]，保持总和不变
                            if (newLeft < 0) {
                                newLeft = 0
                                newRight = totalPadding
                            }
                            if (newRight < 0) {
                                newRight = 0
                                newLeft = totalPadding
                            }
                            sidePaddingPref.setValue(newLeft)
                            rightPaddingPref.setValue(newRight)
                            true
                        }
                        else -> false
                    }
                }
            }

            val btnDone = ImageView(context).apply {
                setImageResource(R.drawable.ic_baseline_check_24)
                setPadding(dp(12))
                drawable.setTint(accentColor)
                background = android.graphics.drawable.GradientDrawable().apply {
                    shape = android.graphics.drawable.GradientDrawable.OVAL
                    setColor(Color.WHITE)
                }
                setOnClickListener {
                    windowManager.attachWindow(KeyboardWindow)
                }
            }

            val smallBtnParams = { LinearLayout.LayoutParams(dp(48), dp(48)).apply {
                setMargins(dp(16), dp(8), dp(16), dp(8))
            } }

            val bigBtnParams = { LinearLayout.LayoutParams(dp(64), dp(64)).apply {
                setMargins(dp(16), dp(8), dp(16), dp(8))
            } }

            addView(btnReset, smallBtnParams())
            addView(btnMove, bigBtnParams()) // 中间按钮明显较大
            addView(btnDone, smallBtnParams())
        }


        fun dp(value: Int): Int = (value * context.resources.displayMetrics.density).toInt()

        // 5. 辅助工具：提取创建边缘短线的函数
        fun createDragHandle(isVertical: Boolean): View {
            return View(context).apply {
                setBackgroundColor(theme.accentKeyBackgroundColor)
            }
        }

        // Top Drag Handle (短线居中)
        val topDragContainer = FrameLayout(context)
        val topDrag = createDragHandle(false)
        var startYTop = 0f
        var startHeightPercent = 0
        topDragContainer.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    startYTop = event.rawY
                    startHeightPercent = heightPercentPref.getValue()
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val deltaY = event.rawY - startYTop
                    val deltaPercent = (-deltaY / displayHeight * 100).toInt()
                    // Android 默认限制范围通常在 25 到 90 之间，我们在赋值前做强硬拦截
                    val newPercent = min(maxHeightPercent, max(minHeightPercent, startHeightPercent + deltaPercent))
                    if (newPercent in minHeightPercent..maxHeightPercent) {
                        heightPercentPref.setValue(newPercent)
                    }
                    true
                }
                else -> false
            }
        }
        topDragContainer.apply {
            addView(topDrag, FrameLayout.LayoutParams(dp(40), dp(4)).apply { gravity = Gravity.CENTER_HORIZONTAL or Gravity.TOP })
        }
        frameLayout.addView(topDragContainer, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48)).apply {
            gravity = Gravity.TOP
        })

        // Left side Drag Handle 
        val startDragContainer = FrameLayout(context)
        val startDrag = createDragHandle(true)
        var startXLeft = 0f
        var startLeftPadding = 0
        var startRightPaddingForLeft = 0 // 记录拖拽开始时的右侧 padding，用于计算最大值
        startDragContainer.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    startXLeft = event.rawX
                    startLeftPadding = sidePaddingPref.getValue()
                    startRightPaddingForLeft = rightPaddingPref.getValue()
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val deltaX = event.rawX - startXLeft
                    val deltaDp = px2dp(deltaX)
                    // 左侧拖拽只影响左侧 padding
                    val maxTotalPaddingDp = px2dp(maxSidePadding.toFloat() * 2)
                    val maxLeftDp = maxTotalPaddingDp - startRightPaddingForLeft
                    val newPadding = min(maxLeftDp, max(0, startLeftPadding + deltaDp))
                    sidePaddingPref.setValue(newPadding)
                    true
                }
                else -> false
            }
        }
        startDragContainer.apply {
            addView(startDrag, FrameLayout.LayoutParams(dp(4), dp(40)).apply { gravity = Gravity.CENTER_VERTICAL or Gravity.START })
        }
        frameLayout.addView(startDragContainer, FrameLayout.LayoutParams(dp(48), ViewGroup.LayoutParams.MATCH_PARENT).apply {
            gravity = Gravity.START
        })

        // Right side Drag Handle
        val endDragContainer = FrameLayout(context)
        val endDrag = createDragHandle(true)
        var startXRight = 0f
        var startRightPadding = 0
        var startLeftPaddingForRight = 0 // 记录拖拽开始时的左侧 padding，用于计算最大值
        endDragContainer.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    startXRight = event.rawX
                    startRightPadding = rightPaddingPref.getValue()
                    startLeftPaddingForRight = sidePaddingPref.getValue()
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val deltaX = event.rawX - startXRight
                    // 右侧拖拽只影响右侧 padding，往左拖（负 deltaX）意味着 padding 增大
                    val deltaDp = px2dp(-deltaX)
                    val maxTotalPaddingDp = px2dp(maxSidePadding.toFloat() * 2)
                    val maxRightDp = maxTotalPaddingDp - startLeftPaddingForRight
                    val newPadding = min(maxRightDp, max(0, startRightPadding + deltaDp))
                    rightPaddingPref.setValue(newPadding)
                    true
                }
                else -> false
            }
        }
        endDragContainer.apply {
            addView(endDrag, FrameLayout.LayoutParams(dp(4), dp(40)).apply { gravity = Gravity.CENTER_VERTICAL or Gravity.END })
        }
        frameLayout.addView(endDragContainer, FrameLayout.LayoutParams(dp(48), ViewGroup.LayoutParams.MATCH_PARENT).apply {
            gravity = Gravity.END
        })

        // 按钮组最后添加，确保 z-order 最高，点击优先级高于拖拽 handle
        frameLayout.addView(btnGroup, FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            gravity = Gravity.CENTER
        })

        return frameLayout
    }

    private fun px2dp(px: Float): Int {
        return (px / context.resources.displayMetrics.density).toInt()
    }

    override fun onAttached() {}

    override fun onDetached() {
        textKeyboard?.onDetach()
        textKeyboard = null
    }
}
