/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2023 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.input.bar.ui.idle

import android.content.Context
import android.content.ClipData
import android.transition.ChangeBounds
import android.transition.TransitionManager
import android.view.DragEvent
import android.view.View
import androidx.annotation.DrawableRes
import com.google.android.flexbox.AlignItems
import com.google.android.flexbox.FlexboxLayout
import com.google.android.flexbox.JustifyContent
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.theme.Theme
import org.fcitx.fcitx5.android.input.bar.ui.ToolButton
import splitties.dimensions.dp
import splitties.views.dsl.core.Ui
import splitties.views.dsl.core.view

class ButtonsBarUi(override val ctx: Context, private val theme: Theme) : Ui {

    // 当用户长按任意非编辑态下的按钮时触发该回调，用以通知外部组件切换为编辑模式拉起编辑抽屉
    var onEditActionRequested: (() -> Unit)? = null
    // 当编辑模式下（且不是长按时而是单点）点击一个 ToolButton 时触发
    var onButtonRemoved: ((String) -> Unit)? = null

    var isEditMode: Boolean = false
        set(value) {
            field = value
            allButtons.values.forEach { 
                it.setEditMode(value, true, theme)
                it.alpha = 1f // 进出编辑模式均恢复透明度
            }
        }

    override val root = view(::FlexboxLayout) {
        alignItems = AlignItems.CENTER
        justifyContent = JustifyContent.SPACE_AROUND
    }

    private fun handleRemoveRequest(tag: String) {
        if ((root as FlexboxLayout).childCount <= 1) return
        onButtonRemoved?.invoke(tag)
    }

    private fun toolButton(@DrawableRes icon: Int, idString: String) = ToolButton(ctx, icon, theme).also { btn ->
        btn.tag = idString
        btn.onEditClickListener = {
            handleRemoveRequest(idString)
        }
    }

    val undoButton = toolButton(R.drawable.ic_baseline_undo_24, "undo").apply {
        contentDescription = ctx.getString(R.string.undo)
    }

    val redoButton = toolButton(R.drawable.ic_baseline_redo_24, "redo").apply {
        contentDescription = ctx.getString(R.string.redo)
    }

    val cursorMoveButton = toolButton(R.drawable.ic_cursor_move, "cursorMove").apply {
        contentDescription = ctx.getString(R.string.text_editing)
    }

    val clipboardButton = toolButton(R.drawable.ic_clipboard, "clipboard").apply {
        contentDescription = ctx.getString(R.string.clipboard)
    }

    val quickPhraseButton = toolButton(R.drawable.ic_baseline_chat_24, "quickPhrase").apply {
        contentDescription = ctx.getString(R.string.quickphrase)
    }

    val moreButton = toolButton(R.drawable.ic_baseline_more_horiz_24, "more").apply {
        contentDescription = ctx.getString(R.string.status_area)
    }

    val voiceButton = toolButton(R.drawable.ic_baseline_keyboard_voice_24, "voice").apply {
        contentDescription = ctx.getString(R.string.switch_to_voice_input)
    }

    private val allButtons by lazy {
        mapOf(
            "undo" to undoButton,
            "redo" to redoButton,
            "cursorMove" to cursorMoveButton,
            "clipboard" to clipboardButton,
            "quickPhrase" to quickPhraseButton,
            "more" to moreButton,
            "voice" to voiceButton
        )
    }

    private val prefs = org.fcitx.fcitx5.android.data.prefs.AppPrefs.getInstance()

    init {
        loadButtonsFromPrefs()
        setupDragAndDrop()
    }

    fun loadButtonsFromPrefs() {
        // 清除现有的所有按钮视图
        root.removeAllViews()
        val orderString = prefs.internal.buttonsBarOrder.getValue()
        val orders = if (orderString.isEmpty()) emptyList() else orderString.split(",")
        // 按照设置的顺序进行布局加载
        orders.forEach { key ->
            allButtons[key]?.let { button ->
                // 先安全脱离原本 Parent
                (button.parent as? android.view.ViewGroup)?.removeView(button)
                button.onEditClickListener = {
                    handleRemoveRequest(key)
                }
                val size = ctx.dp(40)
                root.addView(button, FlexboxLayout.LayoutParams(size, size))
            }
        }
        
        // （如果在默认值外还有新的内建组件，但配置没包含，不再直接强制塞在最后，我们假设新加入的直接丢去仓库就行，这里不用处理。但这为了兼容性，如果发现未配置且也不在仓库的，我们统一追加上）
        val hiddenStr = prefs.internal.buttonsBarHidden.getValue()
        val hiddenOrders = if (hiddenStr.isEmpty()) emptyList() else hiddenStr.split(",")
        allButtons.forEach { (key, button) ->
            if (!orders.contains(key) && !hiddenOrders.contains(key)) {
                if (button.parent == null) {
                    val size = ctx.dp(40)
                    button.onEditClickListener = {
                        handleRemoveRequest(key)
                    }
                    root.addView(button, FlexboxLayout.LayoutParams(size, size))
                }
            }
        }
    }

    private fun setupDragAndDrop() {
        allButtons.values.forEach { button ->
            button.setOnLongClickListener { v ->
                if (!isEditMode) {
                    // 非编辑模式下长按，我们拉起编辑模式，不执行拖拽。
                    v.performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS)
                    onEditActionRequested?.invoke()
                    true
                } else {
                    // 编辑模式下长按，启动拖拽。
                    val flexbox = root as FlexboxLayout
                    if (flexbox.indexOfChild(v) != -1 && flexbox.childCount <= 1) {
                        return@setOnLongClickListener true // 保护仅剩的一个图标不被拖出
                    }
                    val shadow = View.DragShadowBuilder(v)
                    if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.N) {
                        v.startDragAndDrop(ClipData.newPlainText("toolbar_button", v.tag as String), shadow, v, 0)
                    } else {
                        @Suppress("DEPRECATION")
                        v.startDrag(ClipData.newPlainText("toolbar_button", v.tag as String), shadow, v, 0)
                    }
                    v.alpha = 0f // 拖动时视口内容设透明
                    true
                }
            }
        }

        root.setOnDragListener { v, event ->
            val flexbox = v as? FlexboxLayout ?: return@setOnDragListener false
            when (event.action) {
                DragEvent.ACTION_DRAG_STARTED -> {
                    // 仅当是编辑模式中才可以允许接收拖拽（包含从外面仓库拖上来的）
                    isEditMode
                }
                DragEvent.ACTION_DRAG_ENTERED -> true
                DragEvent.ACTION_DRAG_LOCATION -> {
                    val draggedView = event.localState as? View ?: return@setOnDragListener false
                    val draggedTag = draggedView.tag as? String ?: return@setOnDragListener false
                        
                    val x = event.x
                    var hoverIndex = -1
                    for (i in 0 until flexbox.childCount) {
                        val child = flexbox.getChildAt(i)
                        val childCenterX = child.left + child.width / 2
                        if (x < childCenterX) {
                            hoverIndex = i
                            break
                        }
                    }
                        if (hoverIndex == -1) {
                            hoverIndex = flexbox.childCount
                        }
                        
                        val currentIndex = flexbox.indexOfChild(draggedView)
                        if (currentIndex != hoverIndex) {
                            if (currentIndex != -1) {
                                TransitionManager.beginDelayedTransition(flexbox, ChangeBounds().apply { duration = 150 })
                                // 内部重新排布
                                flexbox.removeView(draggedView)
                                val insertIndex = if (hoverIndex > currentIndex) hoverIndex - 1 else hoverIndex
                                flexbox.addView(draggedView, insertIndex)
                            } else {
                                // 从外面进来的
                                if (flexbox.childCount < 7) {
                                    TransitionManager.beginDelayedTransition(flexbox, ChangeBounds().apply { duration = 150 })
                                    (draggedView.parent as? android.view.ViewGroup)?.removeView(draggedView)
                                    draggedView.alpha = 0f // 保持影子状态透明，等 ActionDrop 恢复
                                    flexbox.addView(draggedView, hoverIndex)
                                }
                            }
                        }
                    true
                }
                DragEvent.ACTION_DROP -> {
                    val draggedView = event.localState as? View ?: return@setOnDragListener false
                    val draggedTag = draggedView.tag as? String ?: return@setOnDragListener false
                    draggedView.alpha = 1f
                    
                    val currentIndex = flexbox.indexOfChild(draggedView)
                    if (isEditMode && currentIndex != -1) {
                        (draggedView as? ToolButton)?.setEditMode(true, isDeletable = true, theme = theme)
                        draggedView.layoutParams = FlexboxLayout.LayoutParams(ctx.dp(40), ctx.dp(40))
                    }

                    true
                }
                DragEvent.ACTION_DRAG_ENDED -> {
                    // 这有可能意味着拖出去了，或者拖放完成，恢复透明度
                    val draggedView = event.localState as? View 
                    // 对于跨区域（通过 ClipData）的拖放，localState可能为null或不一致，安全起见我们遍历所有的 allButtons 强制设 1f
                    allButtons.values.forEach { it.alpha = 1f }
                    true
                }
                DragEvent.ACTION_DRAG_EXITED -> {
                    true
                }
                else -> false
            }
        }
    }

    fun catchAddedButtonFromBottom(tag: String) {
        val btn = allButtons[tag] ?: return
        val flexbox = root as FlexboxLayout
        if (flexbox.childCount >= 7) return
        if (flexbox.indexOfChild(btn) == -1) {
            (btn.parent as? android.view.ViewGroup)?.removeView(btn)
            btn.setEditMode(true, isDeletable = true, theme = theme) // 上面为减号
            btn.layoutParams = FlexboxLayout.LayoutParams(ctx.dp(40), ctx.dp(40))
            flexbox.addView(btn)

            btn.onEditClickListener = {
                handleRemoveRequest(tag)
            }
        }
    }
}
