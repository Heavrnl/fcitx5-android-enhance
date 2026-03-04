package org.fcitx.fcitx5.android.input.keyboard

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import com.google.mlkit.vision.digitalink.Ink
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.fcitx.fcitx5.android.input.handwriting.HandwritingEngine
import splitties.dimensions.dp
import kotlin.math.abs

@SuppressLint("ViewConstructor")
class HandwritingOverlayView(
    context: Context,
    private val engine: HandwritingEngine,
    private val onRecognized: (List<String>) -> Unit
) : View(context) {

    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop

    // 绘制相关
    private val paint = Paint().apply {
        color = Color.parseColor("#4A90E2") // fcitx 风格蓝
        isAntiAlias = true
        strokeWidth = dp(4f)
        style = Paint.Style.STROKE
        strokeJoin = Paint.Join.ROUND
        strokeCap = Paint.Cap.ROUND
    }
    private val path = Path()

    // 引擎数据采集
    private var inkBuilder = Ink.builder()
    private var strokeBuilder: Ink.Stroke.Builder? = null

    // 触摸状态
    private var isWriting = false
    private var startX = 0f
    private var startY = 0f

    // 延时识别逻辑
    private var recognizeJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.Main)
    private val recognizeDelayMs = 500L

    init {
        // ViewGroup 默认不调用 onDraw，必须得关掉以强制绘制手写轨迹
        setWillNotDraw(false)
        // 确保引擎初始化
        engine.initModel()
    }

    /**
     * 判断当前画板上是否还有未被识别结算的笔画。
     * 可以用来让外部拦截器知道当前是否正处于“连续手写”的会话中。
     */
    fun hasInk(): Boolean {
        // 如果 path 不为空（或 inkBuilder 中已构建过 stroke），则表示正处于手写过程中
        return !path.isEmpty
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawPath(path, paint)
    }

    fun resetRecognizeJob() {
        recognizeJob?.cancel()
    }

    fun startWriting(x: Float, y: Float, t: Long) {
        strokeBuilder = Ink.Stroke.builder()
        strokeBuilder?.addPoint(Ink.Point.create(x, y, t))
        path.moveTo(x, y)
    }

    fun continueWriting(x: Float, y: Float, t: Long) {
        path.lineTo(x, y)
        invalidate()
        strokeBuilder?.addPoint(Ink.Point.create(x, y, t))
    }

    fun endWriting(x: Float, y: Float, t: Long) {
        strokeBuilder?.addPoint(Ink.Point.create(x, y, t))
        strokeBuilder?.build()?.let { inkBuilder.addStroke(it) }
        strokeBuilder = null
        scheduleRecognition()
    }

    // 暴露一个强制取消的方法以防出错
    fun cancelWriting() {
        recognizeJob?.cancel()
        strokeBuilder = null
        clearCanvas()
    }

    private fun scheduleRecognition() {
        recognizeJob?.cancel()
        recognizeJob = scope.launch {
            delay(recognizeDelayMs)
            performRecognition()
        }
    }

    private suspend fun performRecognition() {
        val ink = inkBuilder.build()
        if (ink.strokes.isEmpty()) {
            onRecognized(emptyList()) // 让上层知道会话已经因为空而结束
            return
        }

        val results = engine.recognize(ink)
        
        // 无论有无结果都清空画板
        clearCanvas()
        
        // 无论结果是否为空，都回调给上层，以便上层恢复普通键盘状态
        onRecognized(results)
    }

    private fun clearCanvas() {
        path.reset()
        invalidate()
        inkBuilder = Ink.builder()
    }
}
