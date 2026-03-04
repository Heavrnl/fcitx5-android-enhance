package org.fcitx.fcitx5.android.input.voice

import android.animation.ArgbEvaluator
import android.animation.ValueAnimator
import android.view.animation.OvershootInterpolator
import android.content.Context
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.content.res.ColorStateList
import android.widget.ImageView
import androidx.core.view.setPadding
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.theme.Theme
import splitties.dimensions.dp
import splitties.views.backgroundColor
import splitties.views.dsl.constraintlayout.centerHorizontally
import splitties.views.dsl.constraintlayout.centerVertically
import splitties.views.dsl.constraintlayout.constraintLayout
import splitties.views.dsl.constraintlayout.lParams
import splitties.views.dsl.constraintlayout.topToBottomOf
import splitties.views.dsl.constraintlayout.startOfParent
import splitties.views.dsl.constraintlayout.endOfParent
import splitties.views.dsl.constraintlayout.topOfParent
import splitties.views.dsl.constraintlayout.bottomOfParent
import splitties.views.dsl.core.Ui
import splitties.views.dsl.core.add
import splitties.views.dsl.core.imageView
import splitties.views.dsl.core.textView
import splitties.views.dsl.core.wrapContent

class VoiceUi(override val ctx: Context, private val theme: Theme) : Ui {

    val modeSwitchButton = textView {
        text = "" // 窗口代码中初始化
        setTextColor(0xFFFFFFFF.toInt())
        textSize = 14f
        setPadding(dp(12))
        // 添加圆角背景，使其看起来像按钮
        background = android.graphics.drawable.GradientDrawable().apply {
            shape = android.graphics.drawable.GradientDrawable.RECTANGLE
            cornerRadius = dp(8).toFloat()
            setColor(theme.genericActiveBackgroundColor)
        }
    }

    val settingsButton = imageView {
        setImageResource(R.drawable.ic_baseline_settings_24)
        imageTintList = android.content.res.ColorStateList.valueOf(theme.keyTextColor)
        setPadding(dp(16))
    }

    val recordButton = imageView {
        setImageResource(R.drawable.ic_baseline_keyboard_voice_24)
        imageTintList = ColorStateList.valueOf(0xFFFFFFFF.toInt())
        
        val content = GradientDrawable().apply {
            shape = GradientDrawable.OVAL
            setColor(theme.accentKeyBackgroundColor)
        }
        val mask = GradientDrawable().apply {
            shape = GradientDrawable.OVAL
            setColor(0xFFFFFFFF.toInt())
        }
        background = RippleDrawable(ColorStateList.valueOf(0x40FFFFFF), content, mask)
        
        scaleType = ImageView.ScaleType.FIT_CENTER
        setPadding(dp(16))
    }

    val statusText = textView {
        text = "点击开始说话"
        setTextColor(theme.keyTextColor)
        textSize = 18f
    }

    // 右下角删除按键
    val deleteButton = imageView {
        setImageResource(R.drawable.ic_baseline_backspace_24)
        imageTintList = android.content.res.ColorStateList.valueOf(theme.altKeyTextColor)
        background = android.graphics.drawable.GradientDrawable().apply {
            shape = android.graphics.drawable.GradientDrawable.OVAL
            setColor(theme.altKeyBackgroundColor)
        }
        scaleType = ImageView.ScaleType.FIT_CENTER
        setPadding(dp(14))
    }

    override val root = constraintLayout {
        backgroundColor = theme.barColor
        
        add(modeSwitchButton, lParams(wrapContent, wrapContent) {
            topOfParent()
            startOfParent()
        })
        
        add(settingsButton, lParams(wrapContent, wrapContent) {
            topOfParent()
            endOfParent()
        })
        
        add(recordButton, lParams(dp(100), dp(100)) {
            centerHorizontally()
            centerVertically()
        })
        add(statusText, lParams(wrapContent, wrapContent) {
            topToBottomOf(recordButton, dp(24))
            centerHorizontally()
        })

        add(deleteButton, lParams(dp(56), dp(56)) {
            bottomOfParent(dp(16))
            endOfParent(dp(16))
        })
    }

    private var colorAnimator: ValueAnimator? = null

    fun setRecordingState(isRecording: Boolean, animate: Boolean = true) {
        val nextBgColor = if (isRecording) 0xFFE53935.toInt() else theme.accentKeyBackgroundColor
        
        if (!animate) {
            colorAnimator?.cancel()
            recordButton.animate().cancel()
            ((recordButton.background as? RippleDrawable)?.getDrawable(0) as? GradientDrawable)?.setColor(nextBgColor)
            recordButton.scaleX = 1f
            recordButton.scaleY = 1f
            return
        }

        val currentBgColor = ((recordButton.background as? RippleDrawable)?.getDrawable(0) as? GradientDrawable)?.color?.defaultColor ?: theme.accentKeyBackgroundColor

        colorAnimator?.cancel()

        // 颜色切换增加弹性插值器
        val bounceInterpolator = OvershootInterpolator(1.5f)

        colorAnimator = ValueAnimator.ofObject(ArgbEvaluator(), currentBgColor, nextBgColor).apply {
            duration = 400
            interpolator = bounceInterpolator
            addUpdateListener { animator ->
                val color = animator.animatedValue as Int
                ((recordButton.background as? RippleDrawable)?.getDrawable(0) as? GradientDrawable)?.setColor(color)
            }
            start()
        }

        // Q弹缩放反馈
        recordButton.animate()
            .scaleX(1.15f)
            .scaleY(1.15f)
            .setDuration(200)
            .setInterpolator(OvershootInterpolator(3.0f))
            .withEndAction {
                recordButton.animate()
                    .scaleX(1.0f)
                    .scaleY(1.0f)
                    .setDuration(250)
                    .setInterpolator(OvershootInterpolator(1.5f))
                    .start()
            }
            .start()
    }
}
