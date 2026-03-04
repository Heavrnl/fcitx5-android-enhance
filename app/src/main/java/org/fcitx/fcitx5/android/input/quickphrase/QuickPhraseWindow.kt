package org.fcitx.fcitx5.android.input.quickphrase

import android.view.View
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import org.fcitx.fcitx5.android.data.quickphrase.QuickPhraseManager
import org.fcitx.fcitx5.android.data.quickphrase.db.QuickPhraseCategory
import org.fcitx.fcitx5.android.data.quickphrase.db.QuickPhraseItem
import org.fcitx.fcitx5.android.data.theme.ThemeManager
import org.fcitx.fcitx5.android.input.FcitxInputMethodService
import org.fcitx.fcitx5.android.input.dependency.inputMethodService
import org.fcitx.fcitx5.android.input.dependency.theme
import org.fcitx.fcitx5.android.input.keyboard.KeyboardWindow
import org.fcitx.fcitx5.android.input.quickphrase.ui.CategoryAdapter
import org.fcitx.fcitx5.android.input.quickphrase.ui.EditCategoryActivity
import org.fcitx.fcitx5.android.input.quickphrase.ui.EditPhraseActivity
import org.fcitx.fcitx5.android.input.quickphrase.ui.PhraseItemAdapter
import org.fcitx.fcitx5.android.input.quickphrase.ui.QuickPhraseUi
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.input.wm.InputWindow
import org.fcitx.fcitx5.android.input.wm.InputWindowManager
import org.mechdancer.dependency.manager.must
import splitties.dimensions.dp

class QuickPhraseWindow : InputWindow.ExtendedInputWindow<QuickPhraseWindow>() {
    
    private val theme by manager.theme()
    private val service: FcitxInputMethodService by manager.inputMethodService()
    private val windowManager: InputWindowManager by manager.must()
    private val dao = QuickPhraseManager.dao
    private val prefs = AppPrefs.getInstance().internal

    private var isSortingMode = false
    private var currentCatId: Int = -1

    private val categoryAdapter: CategoryAdapter by lazy {
        CategoryAdapter(theme, { category ->
            if (!isSortingMode) onCategorySelected(category)
        }, { category, view ->
            if (!isSortingMode && category.id != 1 && category.name != context.getString(R.string.quickphrase_category_default) && category.name != context.getString(R.string.quickphrase_category_recent)) {
                val popup = android.widget.PopupMenu(context, view)
                popup.menu.add(0, 1, 0, context.getString(org.fcitx.fcitx5.android.R.string.edit))
                popup.menu.add(0, 2, 0, context.getString(org.fcitx.fcitx5.android.R.string.delete))
                popup.setOnMenuItemClickListener { item ->
                    when (item.itemId) {
                        1 -> {
                            EditCategoryActivity.launchEdit(context, category)
                        }
                        2 -> {
                            service.lifecycleScope.launch {
                                dao.deleteCategoryAndItems(category)
                                if (categoryAdapter.selectedCategoryId == category.id) {
                                    val fallback = categoryAdapter.currentList.firstOrNull { it.id == 1 || it.name == context.getString(R.string.quickphrase_category_recent) } ?: categoryAdapter.currentList.firstOrNull()
                                    if (fallback != null) onCategorySelected(fallback)
                                }
                            }
                        }
                    }
                    true
                }
                popup.show()
            }
        })
    }

    private val phraseAdapter: PhraseItemAdapter by lazy {
        val radius = ThemeManager.prefs.clipboardEntryRadius.getValue().toFloat()
        PhraseItemAdapter(
            theme = theme,
            radius = context.dp(radius).toFloat(),
            onPaste = { item ->
                service.commitText(item.content)
                service.lifecycleScope.launch {
                    dao.updateItem(item.copy(lastUsed = System.currentTimeMillis()))
                }
            },
            onEdit = { item ->
                EditPhraseActivity.launchEdit(context, item.categoryId, item)
            },
            onDelete = { item ->
                service.lifecycleScope.launch {
                    dao.deleteItem(item)
                }
            }
        )
    }

    private val ui by lazy {
        QuickPhraseUi(context, theme).apply {
            categoryList.layoutManager = LinearLayoutManager(context)
            categoryList.adapter = categoryAdapter

            phraseList.layoutManager = androidx.recyclerview.widget.StaggeredGridLayoutManager(2, androidx.recyclerview.widget.StaggeredGridLayoutManager.VERTICAL)
            phraseList.adapter = phraseAdapter

            addCategoryButton.setOnClickListener {
                EditCategoryActivity.launchCreate(context)
            }

            addPhraseButton.setOnClickListener {
                var catId = categoryAdapter.selectedCategoryId
                if (catId == 1 || categoryAdapter.currentList.find { it.id == catId }?.name == context.getString(R.string.quickphrase_category_recent)) {
                    val defaultCat = categoryAdapter.currentList.find { it.name == context.getString(R.string.quickphrase_category_default) }
                    catId = defaultCat?.id ?: -1
                }
                EditPhraseActivity.launchEdit(context, catId, null)
            }

            sortCategoryButton.setOnClickListener {
                isSortingMode = !isSortingMode
                sortCategoryButton.text = if (isSortingMode) "✓" else "☰"
            }

            fun updateOrderButtonIcon() {
                orderPhraseButton.text = if (prefs.quickPhraseSortDesc.getValue()) "↓" else "↑"
            }
            updateOrderButtonIcon()

            orderPhraseButton.setOnClickListener {
                val newDesc = !prefs.quickPhraseSortDesc.getValue()
                prefs.quickPhraseSortDesc.setValue(newDesc)
                updateOrderButtonIcon()
                refreshCurrentCategory()
            }

            sortPhraseButton.setOnClickListener { view ->
                val popup = android.widget.PopupMenu(context, view)
                popup.menu.add(0, 0, 0, context.getString(R.string.quickphrase_sort_by_name))
                popup.menu.add(0, 1, 0, context.getString(R.string.quickphrase_sort_by_creation_time))
                popup.menu.add(0, 2, 0, context.getString(R.string.quickphrase_sort_by_modification_time))
                popup.menu.add(0, 3, 0, context.getString(R.string.quickphrase_sort_by_last_used))
                popup.setOnMenuItemClickListener { item ->
                    val sortKey = when (item.itemId) {
                        0 -> "name"
                        1 -> "id"
                        2 -> "lastModified"
                        3 -> "lastUsed"
                        else -> "lastUsed"
                    }
                    prefs.quickPhraseSortBy.setValue(sortKey)
                    refreshCurrentCategory()
                    true
                }
                popup.show()
            }

            val touchHelper = androidx.recyclerview.widget.ItemTouchHelper(object : androidx.recyclerview.widget.ItemTouchHelper.SimpleCallback(
                androidx.recyclerview.widget.ItemTouchHelper.UP or androidx.recyclerview.widget.ItemTouchHelper.DOWN, 0) {
                
                override fun isLongPressDragEnabled(): Boolean = isSortingMode

                override fun getDragDirs(
                    recyclerView: androidx.recyclerview.widget.RecyclerView,
                    viewHolder: androidx.recyclerview.widget.RecyclerView.ViewHolder
                ): Int {
                    val idx = viewHolder.adapterPosition
                    if (idx == androidx.recyclerview.widget.RecyclerView.NO_POSITION) return 0
                    val category = categoryAdapter.currentList[idx]
                    if (category.id == 1 || category.name == context.getString(R.string.quickphrase_category_recent)) return 0
                    return super.getDragDirs(recyclerView, viewHolder)
                }

                override fun onMove(
                    recyclerView: androidx.recyclerview.widget.RecyclerView,
                    viewHolder: androidx.recyclerview.widget.RecyclerView.ViewHolder,
                    target: androidx.recyclerview.widget.RecyclerView.ViewHolder
                ): Boolean {
                    val fromPos = viewHolder.adapterPosition
                    val toPos = target.adapterPosition
                    if (fromPos == androidx.recyclerview.widget.RecyclerView.NO_POSITION || toPos == androidx.recyclerview.widget.RecyclerView.NO_POSITION) return false
                    
                    val list = categoryAdapter.currentList.toMutableList()
                    val targetCat = list[toPos]
                    if (targetCat.id == 1 || targetCat.name == context.getString(R.string.quickphrase_category_recent)) return false
                    
                    val item = list.removeAt(fromPos)
                    list.add(toPos, item)
                    
                    list.forEachIndexed { index, category -> 
                        list[index] = category.copy(sortOrder = index)
                    }
                    
                    categoryAdapter.submitList(list)
                    service.lifecycleScope.launch {
                        list.forEach { dao.updateCategory(it) }
                    }
                    return true
                }

                override fun onSwiped(viewHolder: androidx.recyclerview.widget.RecyclerView.ViewHolder, direction: Int) {}
            })
            touchHelper.attachToRecyclerView(categoryList)
        }
    }

    override fun onCreateBarExtension(): View = ui.extension

    override fun onCreateView(): View = ui.root

    override val title: String by lazy {
        context.getString(org.fcitx.fcitx5.android.R.string.quickphrase)
    }

    override fun onAttached() {
        service.lifecycleScope.launch {
            dao.getAllCategoriesFlow().collectLatest { categories ->
                categoryAdapter.submitList(categories)
                if (categories.isNotEmpty() && categoryAdapter.selectedCategoryId == -1) {
                    onCategorySelected(categories.first())
                }
            }
        }
    }

    private fun onCategorySelected(category: QuickPhraseCategory) {
        categoryAdapter.selectedCategoryId = category.id
        currentCatId = category.id
        refreshCurrentCategory()
    }

    private fun refreshCurrentCategory() {
        if (currentCatId == -1) return
        service.lifecycleScope.launch {
            val flow = if (currentCatId == 1 || categoryAdapter.currentList.find { it.id == currentCatId }?.name == context.getString(R.string.quickphrase_category_recent)) {
                dao.getRecentItemsFlow(50)
            } else {
                dao.getItemsByCategoryIdFlow(currentCatId)
            }
            
            flow.collectLatest { rawItems ->
                 val sortBy = prefs.quickPhraseSortBy.getValue()
                 val sortDesc = prefs.quickPhraseSortDesc.getValue()

                 val sorted = when (sortBy) {
                     "name" -> if (sortDesc) rawItems.sortedByDescending { it.title?.takeIf { t -> t.isNotEmpty() } ?: it.content } else rawItems.sortedBy { it.title?.takeIf { t -> t.isNotEmpty() } ?: it.content }
                     "id" -> if (sortDesc) rawItems.sortedByDescending { it.id } else rawItems.sortedBy { it.id }
                     "lastModified" -> if (sortDesc) rawItems.sortedByDescending { it.lastModified } else rawItems.sortedBy { it.lastModified }
                     "lastUsed" -> if (sortDesc) rawItems.sortedByDescending { it.lastUsed } else rawItems.sortedBy { it.lastUsed }
                     else -> rawItems
                 }

                 phraseAdapter.submitList(sorted)
            }
        }
    }

    override fun onDetached() {
        phraseAdapter.onDetached()
    }
}
