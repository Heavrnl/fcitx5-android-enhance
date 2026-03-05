/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2025 Fcitx5 for Android Contributors
 */

package org.fcitx.fcitx5.android.input.candidates.horizontal

import android.graphics.Canvas
import android.graphics.Rect
import android.graphics.drawable.Drawable
import android.graphics.drawable.ShapeDrawable
import android.graphics.drawable.shapes.RectShape
import android.view.View
import android.widget.PopupMenu
import androidx.core.text.bold
import androidx.core.text.buildSpannedString
import androidx.core.text.color
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.launch
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.core.FcitxEvent
import org.fcitx.fcitx5.android.daemon.launchOnReady
import org.fcitx.fcitx5.android.data.InputFeedbacks
import org.fcitx.fcitx5.android.input.bar.ExpandButtonStateMachine.BooleanKey.ExpandedCandidatesEmpty
import org.fcitx.fcitx5.android.input.bar.ExpandButtonStateMachine.TransitionEvent.ExpandedCandidatesUpdated
import org.fcitx.fcitx5.android.input.bar.KawaiiBarComponent
import org.fcitx.fcitx5.android.input.broadcast.InputBroadcastReceiver
import org.fcitx.fcitx5.android.input.candidates.CandidateViewHolder
import org.fcitx.fcitx5.android.input.dependency.UniqueViewComponent
import org.fcitx.fcitx5.android.input.dependency.context
import org.fcitx.fcitx5.android.input.dependency.fcitx
import org.fcitx.fcitx5.android.input.dependency.inputMethodService
import org.fcitx.fcitx5.android.input.dependency.theme
import org.fcitx.fcitx5.android.utils.item
import org.mechdancer.dependency.manager.must
import splitties.dimensions.dp
import splitties.resources.styledColor
import kotlin.math.max

class HorizontalCandidateComponent :
    UniqueViewComponent<HorizontalCandidateComponent, RecyclerView>(), InputBroadcastReceiver {

    private val service by manager.inputMethodService()
    private val context by manager.context()
    private val fcitx by manager.fcitx()
    private val theme by manager.theme()
    private val bar: KawaiiBarComponent by manager.must()

    // 每次加载更多候选词的批次大小
    private val PAGE_SIZE = 20

    // 是否正在加载更多候选词
    private var isLoadingMore = false

    // 展开窗口是否已打开，打开时停止更新 offset，避免候选词栏滑动干扰展开窗口
    var isExpandedWindowAttached = false

    // 候选词更新后是否需要在首次 layout 完成时刷新展开按钮状态
    // 使用标志位确保只在候选词数据更新后的首次 layout 触发，滚动不影响
    private var needsRefreshExpanded = false

    // Since expanded candidate window is created once the expand button was clicked,
    // we need to replay the last offset
    private val _expandedCandidateOffset = MutableSharedFlow<Int>(
        replay = 1,
        onBufferOverflow = BufferOverflow.DROP_OLDEST
    )

    val expandedCandidateOffset = _expandedCandidateOffset.asSharedFlow()

    private fun refreshExpanded(candidateCount: Int) {
        _expandedCandidateOffset.tryEmit(candidateCount)
        bar.expandButtonStateMachine.push(
            ExpandedCandidatesUpdated,
            ExpandedCandidatesEmpty to (adapter.total == candidateCount)
        )
    }

    val adapter: HorizontalCandidateViewAdapter by lazy {
        object : HorizontalCandidateViewAdapter(theme) {
            override fun onBindViewHolder(holder: CandidateViewHolder, position: Int) {
                super.onBindViewHolder(holder, position)
                holder.itemView.setOnClickListener {
                    fcitx.launchOnReady { it.select(holder.idx) }
                }
                holder.itemView.setOnLongClickListener {
                    showCandidateActionMenu(holder)
                    true
                }
            }

            override fun onViewRecycled(holder: CandidateViewHolder) {
                holder.itemView.setOnClickListener(null)
                holder.itemView.setOnLongClickListener(null)
                super.onViewRecycled(holder)
            }
        }
    }

    // 使用水平方向的 LinearLayoutManager，支持流畅的水平滚动
    val layoutManager: LinearLayoutManager by lazy {
        object : LinearLayoutManager(context, HORIZONTAL, false) {
            override fun onLayoutCompleted(state: RecyclerView.State?) {
                super.onLayoutCompleted(state)
                // 只在候选词数据更新后的首次 layout 触发 refreshExpanded
                // childCount 是屏幕上实际可见的候选词数量（与原来 FlexboxLayoutManager 行为一致）
                if (needsRefreshExpanded) {
                    needsRefreshExpanded = false
                    refreshExpanded(childCount)
                }
            }
        }
    }

    private val dividerDrawable by lazy {
        ShapeDrawable(RectShape()).apply {
            val intrinsicSize = max(1, context.dp(1))
            intrinsicWidth = intrinsicSize
            intrinsicHeight = intrinsicSize
            paint.color = theme.dividerColor
        }
    }

    /**
     * 适用于 LinearLayoutManager 的候选词竖线分隔线装饰器
     */
    private inner class CandidateVerticalDecoration(val drawable: Drawable) : RecyclerView.ItemDecoration() {
        override fun getItemOffsets(
            outRect: Rect,
            view: View,
            parent: RecyclerView,
            state: RecyclerView.State
        ) {
            when (parent.layoutDirection) {
                View.LAYOUT_DIRECTION_LTR -> {
                    outRect.right = drawable.intrinsicWidth
                }
                View.LAYOUT_DIRECTION_RTL -> {
                    outRect.left = drawable.intrinsicWidth
                }
                else -> {
                    outRect.set(0, 0, 0, 0)
                }
            }
        }

        override fun onDraw(c: Canvas, parent: RecyclerView, state: RecyclerView.State) {
            val lm = parent.layoutManager ?: return
            for (i in 0 until lm.childCount) {
                val view = lm.getChildAt(i) ?: continue
                val lp = view.layoutParams as RecyclerView.LayoutParams
                val left: Int
                val right: Int
                when (parent.layoutDirection) {
                    View.LAYOUT_DIRECTION_LTR -> {
                        left = view.right + lp.rightMargin
                        right = left + drawable.intrinsicWidth
                    }
                    View.LAYOUT_DIRECTION_RTL -> {
                        right = view.left + lp.leftMargin
                        left = right - drawable.intrinsicWidth
                    }
                    else -> {
                        left = view.left
                        right = left + drawable.intrinsicWidth
                    }
                }
                val top = view.top - lp.topMargin
                val bottom = view.bottom + lp.bottomMargin
                // 分隔线上下各缩进一点
                val vInset = parent.dp(8)
                drawable.setBounds(left, top + vInset, right, bottom - vInset)
                drawable.draw(c)
            }
        }
    }

    /**
     * 滚动监听器：
     * 1. 实时记录用户最后可见的候选词位置，供展开窗口使用
     * 2. 滚动到末尾时自动加载更多候选词
     */
    private val scrollListener = object : RecyclerView.OnScrollListener() {
        override fun onScrolled(recyclerView: RecyclerView, dx: Int, dy: Int) {
            super.onScrolled(recyclerView, dx, dy)

            val lm = layoutManager
            val lastVisibleItem = lm.findLastVisibleItemPosition()

            // 展开窗口未打开时，更新 offset 为用户最后看到的候选词之后
            // 展开窗口已打开时不更新，避免候选词栏滑动干扰展开窗口内容
            if (lastVisibleItem >= 0 && !isExpandedWindowAttached) {
                _expandedCandidateOffset.tryEmit(lastVisibleItem + 1)
            }

            // 当还有更多候选词，且已滑到接近末尾时，加载更多
            if (!isLoadingMore) {
                val totalItemCount = lm.itemCount
                val hasMore = adapter.total < 0 || totalItemCount < adapter.total
                if (hasMore && lastVisibleItem >= totalItemCount - 3) {
                    loadMoreCandidates()
                }
            }
        }
    }

    /**
     * 从 fcitx 加载更多候选词并追加到 adapter
     */
    private fun loadMoreCandidates() {
        if (isLoadingMore) return
        isLoadingMore = true
        val offset = adapter.candidates.size
        service.lifecycleScope.launch {
            try {
                val moreCandidates = fcitx.runOnReady {
                    getCandidates(offset, PAGE_SIZE)
                }
                if (moreCandidates.isNotEmpty()) {
                    adapter.appendCandidates(moreCandidates)
                    // 注意：不调用 refreshExpanded，展开窗口的 offset 始终基于初始候选词数量
                }
            } finally {
                isLoadingMore = false
            }
        }
    }

    override val view by lazy {
        RecyclerView(context).apply {
            id = R.id.candidate_view
            itemAnimator = null
            adapter = this@HorizontalCandidateComponent.adapter
            layoutManager = this@HorizontalCandidateComponent.layoutManager
            addItemDecoration(CandidateVerticalDecoration(dividerDrawable))
            addOnScrollListener(scrollListener)
        }
    }

    override fun onCandidateUpdate(data: FcitxEvent.CandidateListEvent.Data) {
        val candidates = data.candidates
        val total = data.total
        // 重置加载状态
        isLoadingMore = false
        // 标记需要在下一次 layout 完成后刷新展开按钮状态
        needsRefreshExpanded = true
        adapter.updateCandidates(candidates, total)
        // 滚动回起始位置
        view.scrollToPosition(0)
        // 候选词为空时 layout 不会被触发，手动处理
        if (candidates.isEmpty()) {
            needsRefreshExpanded = false
            refreshExpanded(0)
        }
    }

    private fun triggerCandidateAction(idx: Int, actionIdx: Int) {
        fcitx.runIfReady { triggerCandidateAction(idx, actionIdx) }
    }

    private var candidateActionMenu: PopupMenu? = null

    fun showCandidateActionMenu(holder: CandidateViewHolder) {
        val idx = holder.idx
        val text = holder.text
        val view = holder.ui.root
        candidateActionMenu?.dismiss()
        candidateActionMenu = null
        service.lifecycleScope.launch {
            val actions = fcitx.runOnReady { getCandidateActions(idx) }
            if (actions.isEmpty()) return@launch
            InputFeedbacks.hapticFeedback(view, longPress = true)
            candidateActionMenu = PopupMenu(context, view).apply {
                menu.add(buildSpannedString {
                    bold {
                        color(context.styledColor(android.R.attr.colorAccent)) {
                            append(text)
                        }
                    }
                }).apply {
                    isEnabled = false
                }
                actions.forEach { action ->
                    menu.item(action.text) {
                        triggerCandidateAction(idx, action.id)
                    }
                }
                setOnDismissListener {
                    candidateActionMenu = null
                }
                show()
            }
        }
    }
}