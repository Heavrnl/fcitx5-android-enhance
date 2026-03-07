/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2025 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.input.keyboard

import org.fcitx.fcitx5.android.data.InputFeedbacks

/**
 * 占位间隔键，用于分离键盘布局中间的空白区域。
 * 不绘制任何内容，不响应任何触摸事件。
 */
class SpacerDef(
    percentWidth: Float = 0.08f
) : KeyDef(
    appearance = Appearance.Text(
        displayText = "",
        textSize = 0f,
        percentWidth = percentWidth,
        variant = Appearance.Variant.Normal,
        border = Appearance.Border.Off,
        margin = false,
        viewId = -1,
        soundEffect = InputFeedbacks.SoundEffect.Standard
    ),
    behaviors = emptySet()
)
