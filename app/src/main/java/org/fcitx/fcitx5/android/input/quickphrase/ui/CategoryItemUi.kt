package org.fcitx.fcitx5.android.input.quickphrase.ui

import android.content.Context
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.content.res.ColorStateList
import android.text.TextUtils
import org.fcitx.fcitx5.android.data.theme.Theme
import org.fcitx.fcitx5.android.input.keyboard.CustomGestureView
import splitties.dimensions.dp
import splitties.views.dsl.core.Ui
import splitties.views.dsl.core.add
import splitties.views.dsl.core.lParams
import splitties.views.dsl.core.matchParent
import splitties.views.dsl.core.textView
import splitties.views.dsl.core.wrapContent
import splitties.views.setPaddingDp

class CategoryItemUi(override val ctx: Context, private val theme: Theme) : Ui {

    val textView = textView {
        maxLines = 1
        ellipsize = TextUtils.TruncateAt.END
        setPaddingDp(8, 12, 8, 12)
        textSize = 14f
    }

    override val root = CustomGestureView(ctx).apply {
        isClickable = true
        layoutParams = android.view.ViewGroup.LayoutParams(
            android.view.ViewGroup.LayoutParams.MATCH_PARENT, 
            android.view.ViewGroup.LayoutParams.WRAP_CONTENT
        )
        textView.textAlignment = android.view.View.TEXT_ALIGNMENT_CENTER
        add(textView, lParams(matchParent, wrapContent))
    }

    fun bind(name: String, isSelected: Boolean) {
        textView.text = name
        if (isSelected) {
            textView.setTextColor(theme.accentKeyTextColor)
            root.background = GradientDrawable().apply {
                setColor(theme.genericActiveBackgroundColor)
            }
        } else {
            textView.setTextColor(theme.keyTextColor)
            root.background = null
        }

        root.foreground = RippleDrawable(
            ColorStateList.valueOf(theme.keyPressHighlightColor), null,
            GradientDrawable().apply {
                setColor(Color.WHITE)
            }
        )
    }
}
