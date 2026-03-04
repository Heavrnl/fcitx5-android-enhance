/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2025 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.input.bar.ui.idle

import android.content.ClipData
import android.content.Context
import android.transition.ChangeBounds
import android.transition.TransitionManager
import android.view.DragEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.annotation.DrawableRes
import com.google.android.flexbox.AlignItems
import com.google.android.flexbox.FlexDirection
import com.google.android.flexbox.FlexWrap
import com.google.android.flexbox.FlexboxLayout
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.theme.Theme
import org.fcitx.fcitx5.android.input.bar.ui.ToolButton
import splitties.dimensions.dp
import splitties.views.backgroundColor
import splitties.views.dsl.core.Ui
import splitties.views.dsl.core.add
import splitties.views.dsl.core.button
import splitties.views.dsl.core.lParams
import splitties.views.dsl.core.margin
import splitties.views.dsl.core.matchParent
import splitties.views.dsl.core.textView
import splitties.views.dsl.core.verticalLayout
import splitties.views.dsl.core.view
import splitties.views.dsl.core.wrapContent
import splitties.views.gravityCenter
import splitties.views.padding

class ToolbarEditUi(override val ctx: Context, private val theme: Theme) : Ui {

    val flexbox = view(::FlexboxLayout) {
        flexDirection = FlexDirection.ROW
        flexWrap = FlexWrap.WRAP
        alignItems = AlignItems.FLEX_START
        padding = dp(16)
    }

    private fun createBtnBg(color: Int): android.graphics.drawable.Drawable {
        val rippleColor = android.content.res.ColorStateList.valueOf(
            androidx.core.graphics.ColorUtils.setAlphaComponent(
                theme.genericActiveForegroundColor, 64
            )
        )
        val shape = android.graphics.drawable.GradientDrawable().apply {
            shape = android.graphics.drawable.GradientDrawable.OVAL
            setColor(color)
        }
        return android.graphics.drawable.RippleDrawable(rippleColor, shape, null)
    }

    val btnReset = android.widget.ImageView(ctx).apply {
        contentDescription = "恢复默认"
        setImageResource(R.drawable.ic_baseline_replay_24)
        imageTintList = android.content.res.ColorStateList.valueOf(theme.keyTextColor)
        background = createBtnBg(androidx.core.graphics.ColorUtils.setAlphaComponent(theme.keyTextColor, 24))
        setPadding(ctx.dp(12), ctx.dp(12), ctx.dp(12), ctx.dp(12))
        scaleType = android.widget.ImageView.ScaleType.CENTER_INSIDE
    }

    val btnSave = android.widget.ImageView(ctx).apply {
        contentDescription = ctx.getString(android.R.string.ok)
        setImageResource(R.drawable.ic_baseline_check_24)
        imageTintList = android.content.res.ColorStateList.valueOf(theme.genericActiveForegroundColor)
        background = createBtnBg(theme.genericActiveBackgroundColor)
        setPadding(ctx.dp(12), ctx.dp(12), ctx.dp(12), ctx.dp(12))
        scaleType = android.widget.ImageView.ScaleType.CENTER_INSIDE
    }

    val btnCancel = android.widget.ImageView(ctx).apply {
        contentDescription = ctx.getString(android.R.string.cancel)
        setImageResource(R.drawable.ic_baseline_close_24)
        imageTintList = android.content.res.ColorStateList.valueOf(theme.keyTextColor)
        background = createBtnBg(androidx.core.graphics.ColorUtils.setAlphaComponent(theme.keyTextColor, 24))
        setPadding(ctx.dp(12), ctx.dp(12), ctx.dp(12), ctx.dp(12))
        scaleType = android.widget.ImageView.ScaleType.CENTER_INSIDE
    }

    private val scrollContent = view(::ScrollView) {
        isFillViewport = true
        add(flexbox, lParams(matchParent, wrapContent))
    }

    private val bottomBar = view(::LinearLayout) {
        orientation = LinearLayout.HORIZONTAL
        gravity = android.view.Gravity.CENTER_VERTICAL or android.view.Gravity.END
        setPadding(dp(8), dp(4), dp(8), dp(4))

        add(btnReset, lParams(ctx.dp(48), ctx.dp(48)) {
            rightMargin = ctx.dp(16)
        })
        add(btnCancel, lParams(ctx.dp(48), ctx.dp(48)) {
            rightMargin = ctx.dp(16)
        })
        add(btnSave, lParams(ctx.dp(48), ctx.dp(48)))
    }

    override val root = verticalLayout {
        backgroundColor = theme.keyBackgroundColor
        add(scrollContent, lParams(matchParent, 0) {
            weight = 1f
        })
        add(bottomBar, lParams(matchParent, wrapContent))
    }

    // 这些是在仓库里等待被挑上去的闲置图标。由外层控制它们进出 flexbox 
    val hiddenButtons = mutableMapOf<String, ToolButton>()

    fun populateHiddenButtons(buttons: Map<String, ToolButton>) {
        hiddenButtons.clear()
        hiddenButtons.putAll(buttons)
        renderHiddenButtons()
    }

    private fun renderHiddenButtons() {
        flexbox.removeAllViews()
        hiddenButtons.values.forEach { btn ->
            (btn.parent as? ViewGroup)?.removeView(btn)
            btn.setEditMode(edit = true, isDeletable = false, theme = theme) // 下方为加号
            flexbox.addView(btn, FlexboxLayout.LayoutParams(ctx.dp(56), ctx.dp(56)).apply {
                margin = ctx.dp(8)
            })
        }
    }
}
