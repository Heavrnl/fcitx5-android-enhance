/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2025 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.input.bar.ui

import android.content.Context
import android.graphics.Typeface
import android.view.Gravity
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.theme.Theme
import splitties.dimensions.dp
import splitties.views.dsl.core.Ui

/**
 * 长按空格语音输入时，在工具栏显示的"正在聆听..."状态 UI
 */
class VoiceListeningUi(
    override val ctx: Context,
    private val theme: Theme
) : Ui {

    // 麦克风图标
    private val micIcon = ImageView(ctx).apply {
        setImageResource(R.drawable.ic_baseline_keyboard_voice_24)
        setColorFilter(0xFFFFFFFF.toInt())
        layoutParams = LinearLayout.LayoutParams(dp(24), dp(24)).apply {
            gravity = Gravity.CENTER_VERTICAL
            marginEnd = dp(8)
        }
    }

    // "正在聆听..."文字
    private val listeningText = TextView(ctx).apply {
        text = "正在聆听..."
        setTextColor(0xFFFFFFFF.toInt())
        textSize = 14f
        typeface = Typeface.defaultFromStyle(Typeface.BOLD)
        layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ).apply {
            gravity = Gravity.CENTER_VERTICAL
        }
    }

    override val root = LinearLayout(ctx).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER
        addView(micIcon)
        addView(listeningText)
    }
}
