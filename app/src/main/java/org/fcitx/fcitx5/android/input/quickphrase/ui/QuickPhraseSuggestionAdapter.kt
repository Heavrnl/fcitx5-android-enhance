package org.fcitx.fcitx5.android.input.quickphrase.ui

import android.content.Context
import android.graphics.drawable.GradientDrawable
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import org.fcitx.fcitx5.android.data.quickphrase.db.QuickPhraseItem
import org.fcitx.fcitx5.android.data.theme.Theme
import splitties.dimensions.dp
import splitties.views.dsl.core.textView

class QuickPhraseSuggestionAdapter(
    private val context: Context,
    private val theme: Theme,
    private val radius: Float,
    private val onClick: (QuickPhraseItem) -> Unit
) : RecyclerView.Adapter<QuickPhraseSuggestionAdapter.VH>() {

    private var items: List<QuickPhraseItem> = emptyList()

    fun submitList(newItems: List<QuickPhraseItem>) {
        items = newItems
        notifyDataSetChanged()
    }

    override fun getItemCount() = items.size

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val tv = context.textView {
            setSingleLine()
            setPadding(context.dp(8), context.dp(4), context.dp(8), context.dp(4))
            setTextColor(theme.accentKeyTextColor)
            background = GradientDrawable().apply {
                cornerRadius = radius
                setColor(theme.accentKeyBackgroundColor)
            }
        }
        val lp = ViewGroup.MarginLayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            marginEnd = context.dp(8)
        }
        tv.layoutParams = lp
        return VH(tv)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val item = items[position]
        val title = item.title?.takeIf { it.isNotEmpty() } ?: item.content
        holder.tv.text = if (title.length > 15) title.take(15) + "..." else title
        holder.tv.setOnClickListener { onClick(item) }
    }

    class VH(val tv: TextView) : RecyclerView.ViewHolder(tv)
}
