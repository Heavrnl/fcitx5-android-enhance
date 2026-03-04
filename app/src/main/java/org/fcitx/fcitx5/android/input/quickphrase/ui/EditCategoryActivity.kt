package org.fcitx.fcitx5.android.input.quickphrase.ui

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.quickphrase.QuickPhraseManager
import org.fcitx.fcitx5.android.data.quickphrase.db.QuickPhraseCategory
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

class EditCategoryActivity : Activity() {

    private val scope: CoroutineScope = MainScope()
    private val dao = QuickPhraseManager.dao

    private lateinit var nameInput: EditText

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestWindowFeature(android.view.Window.FEATURE_NO_TITLE)
        window.attributes.gravity = Gravity.CENTER

        val categoryId = intent.getIntExtra(EXTRA_CATEGORY_ID, -1)
        val categoryName = intent.getStringExtra(EXTRA_CATEGORY_NAME)
        val isEdit = categoryId != -1

        val root = verticalLayout {
            setPaddingDp(24, 24, 24, 16)
            setBackgroundColor(resources.getColor(android.R.color.background_light, theme))


            addView(textView {
                text = context.getString(if (isEdit) R.string.quickphrase_edit_category else R.string.quickphrase_new_category)
                textSize = 20f
            }, lParams { bottomMargin = dp(16) })

            nameInput = editText {
                hint = getString(R.string.name)
                setSingleLine()
                if (isEdit) {
                    categoryName?.let { setText(it) }
                }
            }
            addView(nameInput, lParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.WRAP_CONTENT))

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
        inputMethodManager.showSoftInput(nameInput, InputMethodManager.SHOW_IMPLICIT)
    }

    private fun saveAndFinish() {
        val categoryId = intent.getIntExtra(EXTRA_CATEGORY_ID, -1)
        val sortOrder = intent.getIntExtra(EXTRA_CATEGORY_SORT_ORDER, -1)
        val name = nameInput.text.toString().trim()
        if (name.isNotEmpty()) {
            scope.launch(NonCancellable) {
                if (categoryId != -1) {
                    dao.updateCategory(QuickPhraseCategory(id = categoryId, name = name, sortOrder = sortOrder))
                } else {
                    val maxOrderId = dao.getAllCategories().maxOfOrNull { it.sortOrder } ?: 100
                    dao.insertCategory(QuickPhraseCategory(name = name, sortOrder = maxOrderId + 1))
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
        const val EXTRA_CATEGORY_NAME = "category_name"
        const val EXTRA_CATEGORY_SORT_ORDER = "category_sort_order"

        fun launchCreate(context: Context) {
            context.startActivity(Intent(context, EditCategoryActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            })
        }

        fun launchEdit(context: Context, category: QuickPhraseCategory) {
            context.startActivity(Intent(context, EditCategoryActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra(EXTRA_CATEGORY_ID, category.id)
                putExtra(EXTRA_CATEGORY_NAME, category.name)
                putExtra(EXTRA_CATEGORY_SORT_ORDER, category.sortOrder)
            })
        }
    }
}
