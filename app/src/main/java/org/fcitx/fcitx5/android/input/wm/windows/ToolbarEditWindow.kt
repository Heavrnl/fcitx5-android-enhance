/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2025 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.input.wm.windows

import android.content.ClipData
import android.transition.ChangeBounds
import android.transition.TransitionManager
import android.view.DragEvent
import android.view.View
import android.widget.ScrollView
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.input.bar.ui.ToolButton
import org.fcitx.fcitx5.android.input.bar.ui.idle.ToolbarEditUi
import org.fcitx.fcitx5.android.input.dependency.theme
import org.fcitx.fcitx5.android.input.wm.InputWindow
import org.mechdancer.dependency.manager.must
import timber.log.Timber

/**
 * 这是一个包含被隐藏按钮的仓库视图。
 * 它会在点击 "保存"、"恢复默认" 或 "取消" 时，通过 Callback 向 KawaiiBarComponent 等报告结果并销毁。
 */
class ToolbarEditWindow(
    private val allAvailableButtons: Map<String, ToolButton>,
    private val initialHiddenKeys: List<String>,
    private val onActionComplete: (Action, newVisibleKeys: List<String>?, newHiddenKeys: List<String>?) -> Unit
) : InputWindow.SimpleInputWindow<ToolbarEditWindow>() {

    enum class Action {
        SAVE, RESET, CANCEL, ADD_TO_TOP
    }
    private val theme by manager.theme()
    private val prefs = AppPrefs.getInstance()

    private val ui by lazy {
        ToolbarEditUi(context, theme).apply {
            val hiddens = allAvailableButtons.filterKeys { initialHiddenKeys.contains(it) }
            populateHiddenButtons(hiddens)

            // 保存
            btnSave.setOnClickListener {
                onActionComplete(Action.SAVE, null, getCurrentHidden())
            }

            // 重置
            btnReset.setOnClickListener {
                onActionComplete(Action.RESET, null, null)
            }

            // 取消
            btnCancel.setOnClickListener {
                onActionComplete(Action.CANCEL, null, null)
            }

            setupDragAndDrop()
            setupClickToAdd()
        }
    }

    private fun ToolbarEditUi.getCurrentHidden(): List<String> {
        val finalHidden = mutableListOf<String>()
        for (i in 0 until flexbox.childCount) {
            (flexbox.getChildAt(i).tag as? String)?.let { finalHidden.add(it) }
        }
        return finalHidden
    }



    override fun onCreateView(): View = ui.root

    private fun ToolbarEditUi.setupDragAndDrop() {
        hiddenButtons.values.forEach { button ->
            button.setOnLongClickListener { v ->
                val shadow = View.DragShadowBuilder(v)
                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.N) {
                    v.startDragAndDrop(ClipData.newPlainText("toolbar_button", v.tag as String), shadow, v, 0)
                } else {
                    @Suppress("DEPRECATION")
                    v.startDrag(ClipData.newPlainText("toolbar_button", v.tag as String), shadow, v, 0)
                }
                v.alpha = 0f
                true
            }
        }

        // 接收从自己或者上面拖下来的
        flexbox.setOnDragListener { v, event ->
            when (event.action) {
                DragEvent.ACTION_DRAG_STARTED -> true
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
                            TransitionManager.beginDelayedTransition(flexbox, ChangeBounds().apply { duration = 150 })
                            if (currentIndex != -1) {
                                // 内部重新排布
                                flexbox.removeView(draggedView)
                                val insertIndex = if (hoverIndex > currentIndex) hoverIndex - 1 else hoverIndex
                                flexbox.addView(draggedView, insertIndex)
                            } else {
                                // 从外面进来的
                                (draggedView.parent as? android.view.ViewGroup)?.removeView(draggedView)
                                draggedView.alpha = 0f
                                (draggedView as? ToolButton)?.setEditMode(true, isDeletable = false) // 进入此区变成 “+”
                                flexbox.addView(draggedView, hoverIndex)
                                hiddenButtons[draggedTag] = draggedView as ToolButton
                            }
                        }
                    true
                }
                DragEvent.ACTION_DROP -> {
                    val draggedView = event.localState as? View ?: return@setOnDragListener false
                    draggedView.alpha = 1f
                    true
                }
                DragEvent.ACTION_DRAG_ENDED -> {
                    val draggedView = event.localState as? View
                    allAvailableButtons.values.forEach { it.alpha = 1f }
                    true
                }
                DragEvent.ACTION_DRAG_EXITED -> true
                else -> false
            }
        }
    }

    private fun ToolbarEditUi.setupClickToAdd() {
        hiddenButtons.values.forEach { button ->
            button.onEditClickListener = {
                val tag = button.tag as? String
                if (tag != null) {
                    onActionComplete(Action.ADD_TO_TOP, listOf(tag), null)
                }
            }
        }
    }

    // 暴露一个给外部动态调用的，当顶部直接点击减号时，把它丢进仓库中
    fun catchDroppedButtonFromTop(tag: String) {
        val btn = allAvailableButtons[tag] ?: return
        if (ui.flexbox.indexOfChild(btn) == -1) {
            (btn.parent as? android.view.ViewGroup)?.removeView(btn)
            btn.setEditMode(true, isDeletable = false, theme = theme) // 变为加号
            btn.layoutParams = com.google.android.flexbox.FlexboxLayout.LayoutParams(context.resources.displayMetrics.density.toInt() * 56, context.resources.displayMetrics.density.toInt() * 56).apply { 
                setMargins(context.resources.displayMetrics.density.toInt() * 8, context.resources.displayMetrics.density.toInt() * 8, context.resources.displayMetrics.density.toInt() * 8, context.resources.displayMetrics.density.toInt() * 8)
            }
            ui.flexbox.addView(btn)
            ui.hiddenButtons[tag] = btn
            btn.onEditClickListener = {        
                onActionComplete(Action.ADD_TO_TOP, listOf(tag), null)
            }
        }
    }

    override fun onAttached() {
    }

    override fun onDetached() {
    }
}
