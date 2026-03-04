package org.fcitx.fcitx5.android.input.quickphrase.ui

import android.content.Context
import android.widget.LinearLayout
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.theme.Theme
import splitties.dimensions.dp
import splitties.views.dsl.core.Ui
import splitties.views.dsl.core.add
import splitties.views.dsl.core.horizontalLayout
import splitties.views.dsl.core.lParams
import splitties.views.dsl.core.matchParent
import splitties.views.dsl.core.verticalLayout
import splitties.views.dsl.core.wrapContent
import splitties.views.dsl.recyclerview.recyclerView
import splitties.views.dsl.core.button
import splitties.views.dsl.core.styles.AndroidStyles

class QuickPhraseUi(override val ctx: Context, private val theme: Theme) : Ui {

    val categoryList = recyclerView()
    
    val phraseList = recyclerView()

    private val androidStyles = AndroidStyles(ctx)

    val addCategoryButton = androidStyles.button.borderless {
        text = "+"
        setTextColor(theme.accentKeyBackgroundColor)
    }

    val addPhraseButton = androidStyles.button.borderless {
        text = "+"
        setTextColor(theme.accentKeyBackgroundColor)
    }

    val sortCategoryButton = androidStyles.button.borderless {
        text = "☰" // 也可以使用 ☰ 或其他符号
        setTextColor(theme.accentKeyBackgroundColor)
    }

    val sortPhraseButton = androidStyles.button.borderless {
        text = "☰" // 代表排序设置
        setTextColor(theme.accentKeyBackgroundColor)
    }

    val orderPhraseButton = androidStyles.button.borderless {
        text = "↓" // 代表降序或升序图标
        setTextColor(theme.accentKeyBackgroundColor)
    }

    private val leftPanel = verticalLayout {
        add(categoryList, lParams(matchParent, 0) {
            weight = 1f
        })
        add(horizontalLayout {
            add(sortCategoryButton, lParams(0, wrapContent) { weight = 1f })
            add(addCategoryButton, lParams(0, wrapContent) { weight = 1f })
        }, lParams(matchParent, wrapContent))
    }

    private val rightPanel = verticalLayout {
        add(phraseList, lParams(matchParent, 0) {
            weight = 1f
        })
    }

    override val root = verticalLayout {
        add(horizontalLayout {
            add(leftPanel, lParams(0, matchParent) {
                weight = 0.3f
            })
            add(rightPanel, lParams(0, matchParent) {
                weight = 0.7f
                marginStart = dp(8)
            })
        }, lParams(matchParent, matchParent) {
            weight = 1f
        })
    }

    val extension = horizontalLayout {
        add(sortPhraseButton, lParams(dp(40), dp(40)) { marginEnd = dp(4) })
        add(orderPhraseButton, lParams(dp(40), dp(40)) { marginEnd = dp(4) })
        add(addPhraseButton, lParams(dp(40), dp(40)))
    }
}
