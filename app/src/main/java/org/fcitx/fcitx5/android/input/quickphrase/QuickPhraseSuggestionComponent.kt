package org.fcitx.fcitx5.android.input.quickphrase

import android.view.View
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.fcitx.fcitx5.android.core.FcitxEvent
import org.fcitx.fcitx5.android.data.quickphrase.QuickPhraseManager
import org.fcitx.fcitx5.android.data.quickphrase.db.QuickPhraseItem
import org.fcitx.fcitx5.android.data.theme.ThemeManager
import org.fcitx.fcitx5.android.input.broadcast.InputBroadcastReceiver
import org.fcitx.fcitx5.android.input.dependency.UniqueViewComponent
import org.fcitx.fcitx5.android.input.dependency.context
import org.fcitx.fcitx5.android.input.dependency.fcitx
import org.fcitx.fcitx5.android.input.dependency.inputMethodService
import org.fcitx.fcitx5.android.input.dependency.theme
import org.fcitx.fcitx5.android.input.quickphrase.ui.QuickPhraseSuggestionAdapter
import org.fcitx.fcitx5.android.input.quickphrase.ui.QuickPhraseSuggestionUi
import org.fcitx.fcitx5.android.core.FormattedText
import splitties.dimensions.dp
import timber.log.Timber

class QuickPhraseSuggestionComponent : UniqueViewComponent<QuickPhraseSuggestionComponent, View>(), InputBroadcastReceiver {

    private val context by manager.context()
    private val theme by manager.theme()
    private val service by manager.inputMethodService()
    private val fcitx by manager.fcitx()

    val ui by lazy { QuickPhraseSuggestionUi(context, theme) }
    override val view get() = ui.root

    private val dao = QuickPhraseManager.dao
    private var allItems = emptyList<QuickPhraseItem>()

    private val adapter by lazy {
        val radius = ThemeManager.prefs.clipboardEntryRadius.getValue().toFloat()
        QuickPhraseSuggestionAdapter(context, theme, context.dp(radius).toFloat()) { item ->
            service.lifecycleScope.launch {
                service.commitText(item.content)
                dao.updateItemLastUsedTime(item.id, System.currentTimeMillis())
            }
        }
    }

    override fun onScopeSetupFinished(scope: org.mechdancer.dependency.DynamicScope) {
        ui.suggestionList.layoutManager = LinearLayoutManager(context, RecyclerView.HORIZONTAL, false)
        ui.suggestionList.adapter = adapter
        service.lifecycleScope.launch {
            dao.getAllItemsFlow().collectLatest { items ->
                allItems = items.filter { it.shortcut.isNotBlank() }
                Timber.d("QuickPhraseSuggestion: Loaded ${allItems.size} items with shortcuts.")
                updateQuery(lastQuery)
            }
        }
    }

    private var lastQuery = ""

    override fun onInputPanelUpdate(data: FcitxEvent.InputPanelEvent.Data) {
        val raw = data.preedit.toString()
        val query = raw.replace(" ", "").replace("\'", "").lowercase()
        updateQuery(query)
    }

    override fun onClientPreeditUpdate(data: FormattedText) {
        val raw = data.toString()
        val query = raw.replace(" ", "").replace("\'", "").lowercase()
        updateQuery(query)
    }

    private fun updateQuery(query: String) {
        lastQuery = query
        if (query.isBlank()) {
            adapter.submitList(emptyList())
            ui.root.visibility = View.GONE
            return
        }
        val matched = allItems.filter { item ->
            val shortcuts = item.shortcut.split(",").map { it.trim().lowercase() }.filter { it.isNotEmpty() }
            shortcuts.any { it == query }
        }
        Timber.d("QuickPhraseSuggestion: query='$query', matched=${matched.size}")
        if (matched.isNotEmpty()) {
            adapter.submitList(matched.take(10))
            ui.root.visibility = View.VISIBLE
        } else {
            adapter.submitList(emptyList())
            ui.root.visibility = View.GONE
        }
    }
}
