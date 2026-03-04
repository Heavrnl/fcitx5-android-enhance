package org.fcitx.fcitx5.android.input.quickphrase.ui

import android.view.ViewGroup
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import org.fcitx.fcitx5.android.data.quickphrase.db.QuickPhraseCategory
import org.fcitx.fcitx5.android.data.theme.Theme

class CategoryAdapter(
    private val theme: Theme,
    private val onCategoryClick: (QuickPhraseCategory) -> Unit,
    private val onCategoryLongClick: (QuickPhraseCategory, android.view.View) -> Unit
) : ListAdapter<QuickPhraseCategory, CategoryAdapter.ViewHolder>(diffCallback) {

    companion object {
        private val diffCallback = object : DiffUtil.ItemCallback<QuickPhraseCategory>() {
            override fun areItemsTheSame(
                oldItem: QuickPhraseCategory,
                newItem: QuickPhraseCategory
            ): Boolean = oldItem.id == newItem.id

            override fun areContentsTheSame(
                oldItem: QuickPhraseCategory,
                newItem: QuickPhraseCategory
            ): Boolean = oldItem == newItem
        }
    }

    var selectedCategoryId: Int = -1
        set(value) {
            val old = field
            field = value
            val oldIndex = currentList.indexOfFirst { it.id == old }
            if (oldIndex != -1) notifyItemChanged(oldIndex)
            val newIndex = currentList.indexOfFirst { it.id == value }
            if (newIndex != -1) notifyItemChanged(newIndex)
        }

    class ViewHolder(val ui: CategoryItemUi) : RecyclerView.ViewHolder(ui.root)

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        return ViewHolder(CategoryItemUi(parent.context, theme))
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val category = getItem(position)
        holder.ui.bind(category.name, category.id == selectedCategoryId)
        holder.ui.root.setOnClickListener {
            onCategoryClick(category)
        }
        holder.ui.root.setOnLongClickListener {
            onCategoryLongClick(category, holder.ui.root)
            true
        }
    }
}
