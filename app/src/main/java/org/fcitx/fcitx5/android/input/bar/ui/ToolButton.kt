/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2023 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.input.bar.ui

import android.content.Context
import android.content.res.ColorStateList
import android.widget.ImageView
import androidx.annotation.ColorInt
import androidx.annotation.DrawableRes
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.theme.Theme
import org.fcitx.fcitx5.android.input.keyboard.CustomGestureView
import org.fcitx.fcitx5.android.utils.borderlessRippleDrawable
import org.fcitx.fcitx5.android.utils.circlePressHighlightDrawable
import splitties.dimensions.dp
import splitties.views.dsl.core.add
import splitties.views.dsl.core.imageView
import splitties.views.dsl.core.lParams
import splitties.views.dsl.core.wrapContent
import splitties.dimensions.dp
import android.widget.FrameLayout
import android.view.Gravity
import android.view.View
import android.widget.TextView

class ToolButton(context: Context) : CustomGestureView(context) {

    companion object {
        val disableAnimation by AppPrefs.getInstance().advanced.disableAnimation
    }

    val image = imageView {
        isClickable = false
        isFocusable = false
        setPadding(dp(10), dp(10), dp(10), dp(10))
        scaleType = ImageView.ScaleType.CENTER_INSIDE
    }

    val labelText = TextView(context).apply {
        textSize = 10f
        gravity = Gravity.CENTER_HORIZONTAL
        visibility = View.GONE
        maxLines = 1
        ellipsize = android.text.TextUtils.TruncateAt.END
    }

    constructor(context: Context, @DrawableRes icon: Int, theme: Theme) : this(context) {
        image.imageTintList = ColorStateList.valueOf(theme.altKeyTextColor)
        setIcon(icon)
        setPressHighlightColor(theme.keyPressHighlightColor)
        
        // 包含主图标
        addView(image, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT, Gravity.CENTER))
        
        addView(labelText, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT, Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL).apply {
            bottomMargin = dp(0)
        })
        
        // 挂载提示小角标
        editIcon.imageTintList = ColorStateList.valueOf(theme.altKeyTextColor)
        addView(editIcon, LayoutParams(dp(16), dp(16)).apply {
            gravity = Gravity.TOP or Gravity.END
            topMargin = dp(4)
            rightMargin = dp(4)
        })
    }

    private val editIcon = ImageView(context).apply {
        isClickable = false
        isFocusable = false
        visibility = View.GONE
        setPadding(dp(2), dp(2), dp(2), dp(2))
        // 默认用减号，表示移除
        setImageResource(R.drawable.ic_toolbar_edit_remove)
    }

    var isEditMode = false
        private set

    var onEditClickListener: (() -> Unit)? = null

    override fun performClick(): Boolean {
        if (isEditMode) {
            onEditClickListener?.invoke()
            return true
        }
        return super.performClick()
    }

    fun setEditMode(edit: Boolean, isDeletable: Boolean = true, theme: Theme? = null) {
        isEditMode = edit
        if (edit) {
            editIcon.visibility = View.VISIBLE
            editIcon.setImageResource(if (isDeletable) R.drawable.ic_toolbar_edit_remove else R.drawable.ic_toolbar_edit_add)
            editIcon.imageTintList = null
            if (!isDeletable && contentDescription != null) {
                labelText.text = contentDescription
                labelText.visibility = View.VISIBLE
                labelText.setTextColor(theme?.altKeyTextColor ?: android.graphics.Color.WHITE)
                (image.layoutParams as LayoutParams).gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
                (image.layoutParams as LayoutParams).topMargin = 0
            } else {
                labelText.visibility = View.GONE
                (image.layoutParams as LayoutParams).gravity = Gravity.CENTER
                (image.layoutParams as LayoutParams).topMargin = 0
            }
        } else {
            editIcon.visibility = View.GONE
            labelText.visibility = View.GONE
            (image.layoutParams as LayoutParams).gravity = Gravity.CENTER
            (image.layoutParams as LayoutParams).topMargin = 0
        }
    }

    fun setIcon(@DrawableRes icon: Int) {
        image.setImageResource(icon)
    }

    fun setPressHighlightColor(@ColorInt color: Int) {
        background = if (disableAnimation) {
            circlePressHighlightDrawable(color)
        } else {
            borderlessRippleDrawable(color, dp(20))
        }
    }
}
