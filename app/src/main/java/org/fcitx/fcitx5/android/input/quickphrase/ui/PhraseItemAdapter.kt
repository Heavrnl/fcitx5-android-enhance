package org.fcitx.fcitx5.android.input.quickphrase.ui

import android.os.Build
import android.view.ViewGroup
import android.widget.PopupMenu
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.quickphrase.db.QuickPhraseItem
import org.fcitx.fcitx5.android.data.theme.Theme
import org.fcitx.fcitx5.android.utils.DeviceUtil
import splitties.resources.styledColor

class PhraseItemAdapter(
    private val theme: Theme,
    private val radius: Float,
    private val onPaste: (QuickPhraseItem) -> Unit,
    private val onEdit: (QuickPhraseItem) -> Unit,
    private val onDelete: (QuickPhraseItem) -> Unit
) : ListAdapter<QuickPhraseItem, PhraseItemAdapter.ViewHolder>(diffCallback) {

    companion object {
        private val diffCallback = object : DiffUtil.ItemCallback<QuickPhraseItem>() {
            override fun areItemsTheSame(
                oldItem: QuickPhraseItem,
                newItem: QuickPhraseItem
            ): Boolean = oldItem.id == newItem.id

            override fun areContentsTheSame(
                oldItem: QuickPhraseItem,
                newItem: QuickPhraseItem
            ): Boolean = oldItem == newItem
        }
    }

    private var popupMenu: PopupMenu? = null

    class ViewHolder(val ui: PhraseItemUi) : RecyclerView.ViewHolder(ui.root)

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        return ViewHolder(PhraseItemUi(parent.context, theme, radius))
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val item = getItem(position)
        holder.ui.bind(item.title, item.content)

        holder.ui.root.setOnClickListener {
            onPaste(item)
        }

        holder.ui.root.setOnLongClickListener {
            val popup = PopupMenu(holder.ui.ctx, holder.ui.root)
            val menu = popup.menu
            val iconTint = holder.ui.ctx.styledColor(android.R.attr.colorControlNormal)

            menu.add(0, 1, 0, R.string.edit).apply {
                setIcon(R.drawable.ic_baseline_edit_24)
                icon?.setTint(iconTint)
            }
            menu.add(0, 2, 0, R.string.delete).apply {
                setIcon(R.drawable.ic_baseline_delete_24)
                icon?.setTint(iconTint)
            }
            
            popup.setOnMenuItemClickListener { menuItem ->
                when (menuItem.itemId) {
                    1 -> {
                        onEdit(item)
                        true
                    }
                    2 -> {
                        onDelete(item)
                        true
                    }
                    else -> false
                }
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && !DeviceUtil.isSamsungOneUI && !DeviceUtil.isFlyme) {
                popup.setForceShowIcon(true)
            }
            popup.setOnDismissListener {
                if (it === popupMenu) popupMenu = null
            }
            popupMenu?.dismiss()
            popupMenu = popup
            popup.show()
            true
        }
    }

    fun onDetached() {
        popupMenu?.dismiss()
        popupMenu = null
    }
}
