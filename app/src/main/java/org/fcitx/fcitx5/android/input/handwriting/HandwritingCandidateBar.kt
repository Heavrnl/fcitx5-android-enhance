package org.fcitx.fcitx5.android.input.handwriting

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.drawable.ShapeDrawable
import android.graphics.drawable.shapes.RectShape
import android.view.ViewGroup
import androidx.core.view.updateLayoutParams
import androidx.recyclerview.widget.RecyclerView
import com.google.android.flexbox.FlexDirection
import com.google.android.flexbox.FlexWrap
import com.google.android.flexbox.FlexboxLayoutManager
import splitties.dimensions.dp
import splitties.views.dsl.core.matchParent
import splitties.views.dsl.core.wrapContent
import splitties.views.setPaddingDp
import android.content.res.Configuration
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.theme.Theme
import org.fcitx.fcitx5.android.input.candidates.CandidateItemUi
import org.fcitx.fcitx5.android.input.candidates.CandidateViewHolder
import org.fcitx.fcitx5.android.input.candidates.expanded.decoration.FlexboxVerticalDecoration
import org.fcitx.fcitx5.android.input.candidates.horizontal.HorizontalCandidateMode
import kotlin.math.max

@SuppressLint("ViewConstructor")
class HandwritingCandidateBar(
    context: Context,
    private val theme: Theme,
    private val onCandidateSelected: (String, Int) -> Unit
) : RecyclerView(context) {

    private val dividerDrawable by lazy {
        ShapeDrawable(RectShape()).apply {
            val intrinsicSize = max(1, context.dp(1))
            intrinsicWidth = intrinsicSize
            intrinsicHeight = intrinsicSize
            paint.color = theme.dividerColor
        }
    }

    private var candidates: List<String> = emptyList()
    private var layoutMinWidth = 0
    private var layoutFlexGrow = 1f

    private val fillStyle by AppPrefs.getInstance().keyboard.horizontalCandidateStyle
    private val maxSpanCountPref by lazy {
        AppPrefs.getInstance().keyboard.run {
            if (context.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT)
                expandedCandidateGridSpanCount
            else
                expandedCandidateGridSpanCountLandscape
        }
    }

    init {
        setBackgroundColor(theme.barColor)
        itemAnimator = null

        layoutManager = object : FlexboxLayoutManager(context) {
            override fun canScrollVertically() = false
            override fun canScrollHorizontally() = true
        }.apply {
            flexDirection = FlexDirection.ROW
            flexWrap = FlexWrap.NOWRAP
        }

        adapter = HandwritingCandidateAdapter()
        addItemDecoration(FlexboxVerticalDecoration(dividerDrawable))
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (fillStyle == HorizontalCandidateMode.AutoFillWidth) {
            val maxSpanCount = maxSpanCountPref.getValue()
            layoutMinWidth = w / maxSpanCount - dividerDrawable.intrinsicWidth
        }
    }

    @SuppressLint("NotifyDataSetChanged")
    fun setCandidates(results: List<String>) {
        candidates = results
        val maxSpanCount = maxSpanCountPref.getValue()
        when (fillStyle) {
            HorizontalCandidateMode.NeverFillWidth -> {
                layoutMinWidth = 0
                layoutFlexGrow = 0f
            }
            HorizontalCandidateMode.AutoFillWidth -> {
                layoutMinWidth = width / maxSpanCount - dividerDrawable.intrinsicWidth
                layoutFlexGrow = if (candidates.size < maxSpanCount) 0f else 1f
            }
            HorizontalCandidateMode.AlwaysFillWidth -> {
                layoutMinWidth = 0
                layoutFlexGrow = 1f
            }
        }

        adapter?.notifyDataSetChanged()
        scrollToPosition(0)
    }

    private inner class HandwritingCandidateAdapter : RecyclerView.Adapter<CandidateViewHolder>() {

        init {
            setHasStableIds(true)
        }

        override fun getItemCount() = candidates.size

        override fun getItemId(position: Int) = candidates[position].hashCode().toLong()

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): CandidateViewHolder {
            val ui = CandidateItemUi(parent.context, theme)
            ui.root.apply {
                minimumWidth = dp(40)
                setPaddingDp(10, 0, 10, 0)
                layoutParams = FlexboxLayoutManager.LayoutParams(wrapContent, matchParent)
            }
            return CandidateViewHolder(ui)
        }

        override fun onBindViewHolder(holder: CandidateViewHolder, position: Int) {
            val text = candidates[position]
            holder.ui.text.text = text
            holder.text = text
            holder.idx = position

            holder.itemView.updateLayoutParams<FlexboxLayoutManager.LayoutParams> {
                minWidth = layoutMinWidth
                flexGrow = layoutFlexGrow
            }

            holder.itemView.setOnClickListener {
                onCandidateSelected(text, holder.idx)
            }
        }

        override fun onViewRecycled(holder: CandidateViewHolder) {
            holder.itemView.setOnClickListener(null)
            super.onViewRecycled(holder)
        }
    }
}
