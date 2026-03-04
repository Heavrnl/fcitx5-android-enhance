/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.input.clipboard

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.text.TextUtils
import android.view.View
import android.widget.ImageView
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.theme.Theme
import org.fcitx.fcitx5.android.input.keyboard.CustomGestureView
import splitties.dimensions.dp
import splitties.resources.drawable
import splitties.views.dsl.constraintlayout.bottomOfParent
import splitties.views.dsl.constraintlayout.centerHorizontally
import splitties.views.dsl.constraintlayout.centerVertically
import splitties.views.dsl.constraintlayout.constraintLayout
import splitties.views.dsl.constraintlayout.endOfParent
import splitties.views.dsl.constraintlayout.lParams
import splitties.views.dsl.constraintlayout.startOfParent
import splitties.views.dsl.constraintlayout.topOfParent
import splitties.views.dsl.core.Ui
import splitties.views.dsl.core.add
import splitties.views.dsl.core.imageView
import splitties.views.dsl.core.lParams
import splitties.views.dsl.core.matchParent
import splitties.views.dsl.core.textView
import splitties.views.dsl.core.wrapContent
import splitties.views.imageDrawable
import splitties.views.setPaddingDp
import timber.log.Timber
import java.io.File

class ClipboardEntryUi(override val ctx: Context, private val theme: Theme, radius: Float) : Ui {

    val textView = textView {
        minLines = 1
        maxLines = 4
        textSize = 14f
        setPaddingDp(8, 4, 8, 4)
        ellipsize = TextUtils.TruncateAt.END
        setTextColor(theme.keyTextColor)
    }

    // 新增图片视图
    val imagePreview = imageView {
        scaleType = ImageView.ScaleType.CENTER_CROP
        visibility = View.GONE
    }

    val pin = imageView {
        imageDrawable = drawable(R.drawable.ic_baseline_push_pin_24)!!.apply {
            setTint(theme.altKeyTextColor)
            setAlpha(0.3f)
        }
    }



    val layout = constraintLayout {
        add(textView, lParams(matchParent, wrapContent) {
            centerVertically()
        })
        // 给图片预览设置固定高度，防止在 RecyclerView 中高度塌陷
        add(imagePreview, lParams(matchParent, dp(120)) {
            topOfParent()
            bottomOfParent()
            centerHorizontally()
        })
        add(pin, lParams(dp(12), dp(12)) {
            bottomOfParent(dp(2))
            endOfParent(dp(2))
        })
    }

    override val root = CustomGestureView(ctx).apply {
        isClickable = true
        minimumHeight = dp(30)
        foreground = RippleDrawable(
            ColorStateList.valueOf(theme.keyPressHighlightColor), null,
            GradientDrawable().apply {
                cornerRadius = radius
                setColor(Color.WHITE)
            }
        )
        background = GradientDrawable().apply {
            cornerRadius = radius
            setColor(theme.clipboardEntryColor)
        }
        add(layout, lParams(matchParent, matchParent))
    }

    /**
     * 设置文本条目
     */
    fun setEntry(text: String, pinned: Boolean) {
        textView.text = text
        textView.visibility = View.VISIBLE
        imagePreview.visibility = View.GONE
        imagePreview.setImageBitmap(null)
        pin.visibility = if (pinned) View.VISIBLE else View.GONE
    }

    /**
     * 设置图片条目
     */
    fun setImageEntry(imagePath: String, pinned: Boolean) {
        textView.visibility = View.GONE
        imagePreview.visibility = View.VISIBLE
        pin.visibility = if (pinned) View.VISIBLE else View.GONE

        // 加载图片
        try {
            val file = File(imagePath)
            if (file.exists()) {
                // 使用 inSampleSize 缩小图片以节省内存
                val options = BitmapFactory.Options().apply {
                    inJustDecodeBounds = true
                }
                BitmapFactory.decodeFile(imagePath, options)
                
                // 计算缩放比例（目标大小约256px）
                val targetSize = 256
                var sampleSize = 1
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
                val bitmap = BitmapFactory.decodeFile(imagePath, decodeOptions)
                imagePreview.setImageBitmap(bitmap)
            } else {
                // 文件不存在，显示占位符
                val placeholder = ctx.drawable(R.drawable.ic_baseline_image_24)
                placeholder?.setTint(theme.altKeyTextColor)
                imagePreview.setImageDrawable(placeholder)
            }
        } catch (e: Exception) {
            Timber.e(e, "Failed to load image: $imagePath")
            val errorPlaceholder = ctx.drawable(R.drawable.ic_baseline_image_24)
            errorPlaceholder?.setTint(theme.altKeyTextColor)
            imagePreview.setImageDrawable(errorPlaceholder)
        }
    }

    /**
     * 回收图片资源
     */
    fun recycle() {
        val drawable = imagePreview.drawable
        if (drawable is android.graphics.drawable.BitmapDrawable) {
            drawable.bitmap?.recycle()
        }
        imagePreview.setImageBitmap(null)
    }
}