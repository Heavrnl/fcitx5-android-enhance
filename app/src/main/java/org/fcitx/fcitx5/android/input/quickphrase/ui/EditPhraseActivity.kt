package org.fcitx.fcitx5.android.input.quickphrase.ui

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.view.inputmethod.InputMethodManager
import android.widget.ArrayAdapter
import android.widget.EditText
import android.widget.Spinner
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.quickphrase.QuickPhraseManager
import org.fcitx.fcitx5.android.data.quickphrase.db.QuickPhraseCategory
import org.fcitx.fcitx5.android.data.quickphrase.db.QuickPhraseItem
import org.fcitx.fcitx5.android.utils.inputMethodManager
import splitties.dimensions.dp
import splitties.views.dsl.core.button
import splitties.views.dsl.core.editText
import splitties.views.dsl.core.horizontalLayout
import splitties.views.dsl.core.lParams
import splitties.views.dsl.core.margin
import splitties.views.dsl.core.textView
import splitties.views.dsl.core.verticalLayout
import splitties.views.setPaddingDp

class EditPhraseActivity : Activity() {

    private val scope: CoroutineScope = MainScope()
    private val dao = QuickPhraseManager.dao

    private lateinit var titleInput: EditText
    private lateinit var shortcutInput: EditText
    private lateinit var contentInput: EditText
    private lateinit var categorySpinner: Spinner

    private var categoryId: Int = -1
    private var itemId: Int = -1
    private var categories = emptyList<QuickPhraseCategory>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestWindowFeature(android.view.Window.FEATURE_NO_TITLE)
        window.attributes.gravity = Gravity.CENTER

        categoryId = intent.getIntExtra(EXTRA_CATEGORY_ID, -1)
        itemId = intent.getIntExtra(EXTRA_ITEM_ID, -1)

        val isEdit = itemId != -1

        val root = verticalLayout {
            setPaddingDp(24, 24, 24, 16)
            setBackgroundColor(resources.getColor(android.R.color.background_light, theme))


            addView(textView {
                text = context.getString(if (isEdit) R.string.quickphrase_edit_phrase else R.string.quickphrase_new_phrase)
                textSize = 20f
            }, lParams { bottomMargin = dp(16) })

            titleInput = editText {
                hint = context.getString(R.string.quickphrase_title_hint)
                setSingleLine()
                if (isEdit) {
                    intent.getStringExtra(EXTRA_ITEM_TITLE)?.let { setText(it) }
                }
            }
            addView(titleInput, lParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.WRAP_CONTENT))

            shortcutInput = editText {
                hint = context.getString(R.string.quickphrase_shortcut_hint)
                setSingleLine()
                if (isEdit) {
                    intent.getStringExtra(EXTRA_ITEM_SHORTCUT)?.let { setText(it) }
                }
            }
            addView(shortcutInput, lParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.WRAP_CONTENT) { topMargin = dp(8) })

            contentInput = editText {
                hint = getString(R.string.quickphrase_phrase)
                if (isEdit) {
                    intent.getStringExtra(EXTRA_ITEM_CONTENT)?.let { setText(it) }
                }
            }
            addView(contentInput, lParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.WRAP_CONTENT) { topMargin = dp(8) })

            categorySpinner = Spinner(this@EditPhraseActivity)
            addView(categorySpinner, lParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.WRAP_CONTENT) { topMargin = dp(8) })

            addView(horizontalLayout {
                gravity = Gravity.END
                addView(button {
                    text = getString(android.R.string.cancel)
                    setOnClickListener { finish() }
                    setBackgroundResource(android.R.color.transparent)
                }, lParams { rightMargin = dp(8) })
                addView(button {
                    text = getString(android.R.string.ok)
                    setOnClickListener { saveAndFinish() }
                    setBackgroundResource(android.R.color.transparent)
                })
            }, lParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.WRAP_CONTENT) {
                topMargin = dp(16)
            })
        }

        setContentView(root)
        window.setLayout((resources.displayMetrics.widthPixels * 0.85).toInt(), android.view.ViewGroup.LayoutParams.WRAP_CONTENT)

        scope.launch {
            categories = dao.getAllCategories().filter { it.id != 1 && it.name != getString(R.string.quickphrase_category_recent) }
            val adapter = ArrayAdapter(
                this@EditPhraseActivity,
                android.R.layout.simple_spinner_item,
                categories.map { it.name }
            )
            adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
            categorySpinner.adapter = adapter

            val selectedIndex = categories.indexOfFirst { it.id == categoryId }
            if (selectedIndex != -1) {
                categorySpinner.setSelection(selectedIndex)
            }
        }

        inputMethodManager.showSoftInput(contentInput, InputMethodManager.SHOW_IMPLICIT)
    }

    private fun saveAndFinish() {
        val title = titleInput.text.toString().trim()
        val shortcut = shortcutInput.text.toString().trim()
        val content = contentInput.text.toString().trim()
        val selectedCatIndex = categorySpinner.selectedItemPosition
        val selectedCatId = if (selectedCatIndex in categories.indices) categories[selectedCatIndex].id else categoryId
        
        if (content.isNotEmpty() && selectedCatId != -1) {
            scope.launch(NonCancellable) {
                if (itemId != -1) {
                    // We need to fetch it from the previous category ID to ensure it is found in cases where we don't have global cache
                    val item = dao.getItemsByCategoryId(categoryId).find { it.id == itemId }
                    if (item != null) {
                        dao.updateItem(item.copy(categoryId = selectedCatId, title = title, content = content, shortcut = shortcut, lastModified = System.currentTimeMillis()))
                    }
                } else {
                    dao.insertItem(
                        QuickPhraseItem(
                            categoryId = selectedCatId,
                            title = title,
                            content = content,
                            shortcut = shortcut,
                            lastModified = System.currentTimeMillis(),
                            lastUsed = 0
                        )
                    )
                }
            }
        }
        finish()
    }

    override fun onDestroy() {
        super.onDestroy()
        scope.cancel()
    }

    companion object {
        const val EXTRA_CATEGORY_ID = "category_id"
        const val EXTRA_ITEM_ID = "item_id"
        const val EXTRA_ITEM_TITLE = "item_title"
        const val EXTRA_ITEM_CONTENT = "item_content"
        const val EXTRA_ITEM_SHORTCUT = "item_shortcut"

        fun launchEdit(context: Context, categoryId: Int, item: QuickPhraseItem?) {
            context.startActivity(Intent(context, EditPhraseActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra(EXTRA_CATEGORY_ID, categoryId)
                if (item != null) {
                    putExtra(EXTRA_ITEM_ID, item.id)
                    putExtra(EXTRA_ITEM_TITLE, item.title)
                    putExtra(EXTRA_ITEM_CONTENT, item.content)
                    putExtra(EXTRA_ITEM_SHORTCUT, item.shortcut)
                }
            })
        }
    }
}
