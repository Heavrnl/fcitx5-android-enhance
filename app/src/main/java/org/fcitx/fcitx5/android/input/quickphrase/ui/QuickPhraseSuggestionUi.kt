package org.fcitx.fcitx5.android.input.quickphrase.ui

import android.content.Context
import android.view.View
import org.fcitx.fcitx5.android.data.theme.Theme
import splitties.dimensions.dp
import splitties.views.dsl.constraintlayout.constraintLayout
import splitties.views.dsl.constraintlayout.lParams
import splitties.views.dsl.core.matchParent
import splitties.views.dsl.core.Ui
import splitties.views.dsl.core.add
import splitties.views.dsl.recyclerview.recyclerView
import splitties.views.dsl.core.wrapContent

class QuickPhraseSuggestionUi(override val ctx: Context, theme: Theme) : Ui {
    val suggestionList = recyclerView {
        clipToPadding = false
        setPadding(dp(8), dp(4), dp(8), dp(4))
    }

    override val root = ctx.constraintLayout {
        visibility = View.GONE
        add(suggestionList, lParams(matchParent, wrapContent))
    }
}
