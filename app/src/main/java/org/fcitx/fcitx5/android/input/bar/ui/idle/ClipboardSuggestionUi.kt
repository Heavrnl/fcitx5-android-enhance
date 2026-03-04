/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2023 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.input.bar.ui.idle

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.drawable.RippleDrawable
import android.graphics.drawable.StateListDrawable
import android.text.TextUtils
import android.view.Gravity
import android.widget.FrameLayout
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.theme.Theme
import org.fcitx.fcitx5.android.data.theme.ThemeManager
import org.fcitx.fcitx5.android.input.keyboard.CustomGestureView
import org.fcitx.fcitx5.android.input.keyboard.radiusDrawable
import org.fcitx.fcitx5.android.utils.rippleDrawable
import splitties.dimensions.dp
import splitties.resources.drawable
import splitties.views.dsl.constraintlayout.centerInParent
import splitties.views.dsl.constraintlayout.centerVertically
import splitties.views.dsl.constraintlayout.constraintLayout
import splitties.views.dsl.constraintlayout.endOfParent
import splitties.views.dsl.constraintlayout.endToStartOf
import splitties.views.dsl.constraintlayout.lParams
import splitties.views.dsl.constraintlayout.matchConstraints
import splitties.views.dsl.constraintlayout.startOfParent
import splitties.views.dsl.constraintlayout.startToEndOf
import splitties.views.dsl.core.Ui
import splitties.views.dsl.core.add
import splitties.views.dsl.core.horizontalLayout
import splitties.views.dsl.core.imageView
import splitties.views.dsl.core.lParams
import splitties.views.dsl.core.matchParent
import splitties.views.dsl.core.textView
import splitties.views.dsl.core.verticalMargin
import splitties.views.dsl.core.wrapContent
import splitties.views.imageDrawable

class ClipboardSuggestionUi(override val ctx: Context, private val theme: Theme) : Ui {

    private val keyBorder by ThemeManager.prefs.keyBorder
    private val keyRipple by ThemeManager.prefs.keyRippleEffect

    val icon = imageView {
        imageDrawable = drawable(R.drawable.ic_clipboard)!!.apply {
            setTint(theme.altKeyTextColor)
        }
    }

    val openLinkButton = imageView {
        visibility = android.view.View.GONE
        imageDrawable = drawable(R.drawable.ic_baseline_open_in_browser_24)!!.apply {
            setTint(theme.altKeyTextColor)
        }
        setPadding(dp(10), dp(10), dp(10), dp(10))
        background = rippleDrawable(theme.keyPressHighlightColor)
    }

    val image = imageView {
        visibility = android.view.View.GONE
        scaleType = android.widget.ImageView.ScaleType.CENTER_CROP
    }

    val text = textView {
        isSingleLine = true
        maxWidth = dp(120)
        ellipsize = TextUtils.TruncateAt.END
        setTextColor(theme.altKeyTextColor)
    }

    // 使用 horizontalLayout 让 icon 和 text 作为一个整体水平居中
    private val layout = horizontalLayout {
        gravity = Gravity.CENTER_VERTICAL
        val spacing = dp(4)
        // 添加水平内边距以确保内容不贴边
        val horizontalPadding = dp(8)
        setPadding(horizontalPadding, 0, horizontalPadding, 0)
        
        add(icon, lParams(dp(20), dp(20)) {
            marginEnd = spacing
        })
        add(image, lParams(dp(20), dp(20)) {
            marginEnd = spacing
        })
        add(text, lParams(wrapContent, wrapContent))
    }

    val suggestionView = CustomGestureView(ctx).apply {
        // 使用 FrameLayout.LayoutParams 设置 gravity 让 layout 在 suggestionView 中居中
        add(layout, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.MATCH_PARENT,
            Gravity.CENTER
        ))
        
        // Apply key border style if enabled
        if (keyBorder) {
            // 使用和键盘按键一致的背景颜色 keyBackgroundColor
            // 固定胶囊形状，使用大圆角 (类似 Gboard)
            val capsuleRadius = ctx.dp(50).toFloat() // 大圆角实现胶囊效果
            
            // 简单的圆角背景，不带阴影
            background = radiusDrawable(capsuleRadius, theme.keyBackgroundColor)
            
            // Foreground: press highlight or ripple
            foreground = if (keyRipple) {
                RippleDrawable(
                    ColorStateList.valueOf(theme.keyPressHighlightColor), null,
                    radiusDrawable(capsuleRadius, android.graphics.Color.WHITE)
                )
            } else {
                StateListDrawable().apply {
                    addState(
                        intArrayOf(android.R.attr.state_pressed),
                        radiusDrawable(capsuleRadius, theme.keyPressHighlightColor)
                    )
                }
            }
        } else {
            background = rippleDrawable(theme.keyPressHighlightColor)
        }
    }

    override val root = constraintLayout {
        add(openLinkButton, lParams(dp(40), dp(40)) {
            startOfParent()
            endToStartOf(suggestionView)
            centerVertically()
            horizontalChainStyle = androidx.constraintlayout.widget.ConstraintLayout.LayoutParams.CHAIN_PACKED
            rightMargin = dp(8) // Space between button and suggestion
        })

        add(suggestionView, lParams(wrapContent, matchConstraints) {
            startToEndOf(openLinkButton)
            endOfParent()
            centerVertically()
            verticalMargin = dp(4)
        })
    }
}
