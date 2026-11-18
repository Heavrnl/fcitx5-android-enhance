/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.verification

import android.app.KeyguardManager
import android.content.ClipData
import android.content.ComponentName
import android.os.Build
import android.os.PowerManager
import android.os.SystemClock
import android.provider.Settings
import android.service.notification.NotificationListenerService
import android.widget.Toast
import androidx.annotation.Keep
import androidx.core.content.getSystemService
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.clipboard.ClipboardManager
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreference
import org.fcitx.fcitx5.android.data.syncclipboard.SyncClipboardManager
import org.fcitx.fcitx5.android.data.syncclipboard.SyncLog
import org.fcitx.fcitx5.android.utils.appContext
import org.fcitx.fcitx5.android.utils.clipboardManager
import org.json.JSONArray
import org.json.JSONObject
import timber.log.Timber
import java.io.BufferedReader
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL

/**
 * 纯 Kotlin 验证码提取工具（无 Android Framework 依赖，便于单元测试与复用）。
 */
object VerificationCodeExtractor {

    val DEFAULT_KEYWORDS = listOf(
        "验证码", "校验码", "检验码", "确认码", "激活码", "动态码", "安全码",
        "验证代码", "校验代码", "检验代码", "激活代码", "确认代码", "动态代码", "安全代码",
        "登入码", "认证码", "识别码", "短信口令", "动态密码", "交易码", "上网密码", "随机码", "动态口令",
        "驗證碼", "校驗碼", "檢驗碼", "確認碼", "激活碼", "動態碼",
        "驗證代碼", "校驗代碼", "檢驗代碼", "確認代碼", "激活代碼", "動態代碼",
        "登入碼", "認證碼", "識別碼",
        "code", "otp", "one-time password", "verification", "auth", "authentication",
        "pin", "security", "access", "token",
        "短信验证", "短信验證", "短信校验", "短信校驗",
        "手机验证", "手機驗證", "手机校验", "手機校驗",
        "验证短信", "驗證短信", "验证信息", "驗證信息",
        "一次性密码", "一次性密碼", "临时密码", "臨時密碼",
        "授权码", "授權碼", "授权密码", "授權密碼",
        "二步验证", "二步驗證", "两步验证", "兩步驗證",
        "mfa", "2fa", "two-factor", "multi-factor",
        "passcode", "pass code", "secure code", "security code",
        "tac", "tan", "transaction authentication number",
        "验证邮件", "驗證郵件", "确认邮件", "確認郵件",
        "一次性验证码", "一次性驗證碼", "单次有效", "單次有效",
        "临时口令", "臨時口令", "临时验证码", "臨時驗證碼"
    )

    /**
     * 验证码正则：严格限定在独立字母数字词边界内匹配：
     * - 4-8 位纯数字，或
     * - 4-8 位同时包含字母与数字的混合码（避免跨空格误匹配普通英文单词或截断长订单号）。
     */
    private val CODE_PATTERN =
        """(?<![a-zA-Z0-9])(?:(?=[a-zA-Z0-9]*[a-zA-Z])(?=[a-zA-Z0-9]*\d)[a-zA-Z0-9]{4,8}|\d{4,8})(?![a-zA-Z0-9])""".toRegex()

    fun containsKeywords(text: String, keywordList: List<String>): Boolean {
        val lowerText = text.lowercase()
        return keywordList.any { keyword -> keyword.isNotBlank() && keyword.lowercase() in lowerText }
    }

    /**
     * 本地智能提取验证码：找到离触发关键词最近的独立候选码。
     */
    fun extractCodeLocal(text: String, keywordList: List<String> = DEFAULT_KEYWORDS): String? {
        val keywordPositions = mutableListOf<Int>()
        for (keyword in keywordList) {
            if (keyword.isBlank()) continue
            var index = text.indexOf(keyword, ignoreCase = true)
            while (index != -1) {
                keywordPositions.add(index)
                index = text.indexOf(keyword, index + keyword.length, ignoreCase = true)
            }
        }
        if (keywordPositions.isEmpty()) return null

        val candidates = CODE_PATTERN.findAll(text).toList()
        if (candidates.isEmpty()) return null

        var closestCode: String? = null
        var closestDistance = Int.MAX_VALUE
        for (match in candidates) {
            val codeStart = match.range.first
            val code = match.value
            for (keywordPos in keywordPositions) {
                val distance = kotlin.math.abs(codeStart - keywordPos)
                if (distance < closestDistance) {
                    closestDistance = distance
                    closestCode = code
                }
            }
        }
        return closestCode
    }
}

/**
 * 验证码提取管理器
 * 从通知中提取验证码并复制到剪贴板，同时联动剪贴板同步上传（支持息屏状态直通上传）。
 */
object VerificationCodeManager : CoroutineScope by CoroutineScope(SupervisorJob() + Dispatchers.Default) {

    private const val DUPLICATE_WINDOW_MS = 5_000L

    private val prefs by lazy { AppPrefs.getInstance().verificationCode }
    private val clipboardManager by lazy { appContext.clipboardManager }

    // 当前使用的关键词列表
    private var keywords: List<String> = VerificationCodeExtractor.DEFAULT_KEYWORDS

    // 验证码提取后的回调
    var onCodeExtracted: ((String) -> Unit)? = null

    @Volatile
    private var lastExtractedCode: String? = null

    @Volatile
    private var lastExtractedAt = 0L

    @Keep
    private val enabledListener = ManagedPreference.OnChangeListener<Boolean> { _, enabled ->
        Timber.d("VerificationCode feature ${if (enabled) "enabled" else "disabled"}")
    }

    @Keep
    private val keywordsListener = ManagedPreference.OnChangeListener<String> { _, _ ->
        loadKeywords()
    }

    fun init() {
        prefs.enabled.registerOnChangeListener(enabledListener)
        prefs.keywords.registerOnChangeListener(keywordsListener)
        loadKeywords()
        ensureListenerBound()
    }

    /**
     * 覆盖安装更新或系统异常解绑后，在用户已授予通知监听权限的前提下主动请求重新绑定，
     * 避免出现重启/升级后通知监听假死。
     */
    fun ensureListenerBound() {
        if (!prefs.enabled.getValue()) return
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) return
        runCatching {
            val componentName = ComponentName(appContext, VerificationCodeNotificationListener::class.java)
            val enabledListeners = Settings.Secure.getString(
                appContext.contentResolver,
                "enabled_notification_listeners"
            )
            if (enabledListeners?.contains(componentName.flattenToString()) == true) {
                NotificationListenerService.requestRebind(componentName)
            }
        }
    }

    private fun loadKeywords() {
        val keywordsStr = prefs.keywords.getValue()
        keywords = if (keywordsStr.isNotBlank()) {
            keywordsStr.split(",").map { it.trim() }.filter { it.isNotBlank() }
        } else {
            VerificationCodeExtractor.DEFAULT_KEYWORDS
        }
        Timber.d("Loaded ${keywords.size} keywords")
    }

    fun getKeywords(): List<String> = keywords.toList()

    fun setKeywords(newKeywords: List<String>) {
        keywords = newKeywords.filter { it.isNotBlank() }
    }

    fun addKeyword(keyword: String) {
        if (keyword.isNotBlank() && !keywords.contains(keyword)) {
            keywords = keywords + keyword
        }
    }

    fun removeKeyword(keyword: String) {
        keywords = keywords - keyword
    }

    fun resetKeywords() {
        keywords = VerificationCodeExtractor.DEFAULT_KEYWORDS
    }

    /**
     * 检查文本是否包含验证码关键词
     */
    fun containsKeywords(text: String): Boolean {
        if (!prefs.enabled.getValue()) return false
        return VerificationCodeExtractor.containsKeywords(text, keywords)
    }

    /**
     * 处理通知文本，提取验证码
     */
    fun processNotification(text: String) {
        if (!prefs.enabled.getValue()) return
        if (!containsKeywords(text)) return

        launch {
            val code = extractCode(text) ?: return@launch
            if (code.lowercase() == "none") return@launch

            val now = SystemClock.elapsedRealtime()
            if (code == lastExtractedCode && now - lastExtractedAt < DUPLICATE_WINDOW_MS) {
                SyncLog.d("OTP", "skip duplicate notification code within ${DUPLICATE_WINDOW_MS}ms (len=${code.length})")
                return@launch
            }
            lastExtractedCode = code
            lastExtractedAt = now

            val unlockedInteractive = isDeviceUnlockedAndInteractive()
            SyncLog.i("OTP", "extracted code len=${code.length} interactive=$unlockedInteractive")

            // 1. 预先标记并直通触发同步上传（支持息屏下不依赖系统剪贴板回调直接上传）
            ClipboardManager.markSelfAppliedText(code)
            SyncClipboardManager.uploadTextForWakeup(code, "verification_code")

            // 2. 写入系统剪贴板；仅在亮屏已解锁时弹 Toast 和触发输入框自动填充，避免息屏/锁屏误填
            withContext(Dispatchers.Main) {
                copyToClipboard(code)
                if (unlockedInteractive) {
                    showToast(appContext.getString(R.string.verification_code_extracted, code))
                    if (prefs.autoFill.getValue()) {
                        onCodeExtracted?.invoke(code)
                    }
                } else {
                    Timber.d("Device locked or screen off: skip toast/auto-fill, synced via clipboard")
                }
            }

            // 3. 同步记录到输入法剪贴板历史
            ClipboardManager.recordVerificationCode(code)
        }
    }

    private suspend fun extractCode(text: String): String? {
        return when (prefs.extractionMode.getValue()) {
            VerificationCodeExtractionMode.Local -> extractCodeLocal(text, keywords)
            VerificationCodeExtractionMode.AI -> extractCodeAI(text) ?: extractCodeLocal(text, keywords)
        }
    }

    /**
     * 本地智能提取验证码
     */
    internal fun extractCodeLocal(
        text: String,
        keywordList: List<String> = VerificationCodeExtractor.DEFAULT_KEYWORDS
    ): String? {
        val code = VerificationCodeExtractor.extractCodeLocal(text, keywordList)
        if (code == null) {
            Timber.d("No keywords or code candidates matched in text")
        } else {
            Timber.d("Extracted code '$code'")
        }
        return code
    }

    private suspend fun extractCodeAI(text: String): String? {
        val apiUrl = prefs.openaiApiUrl.getValue()
        val apiKey = prefs.openaiApiKey.getValue()
        val model = prefs.openaiModel.getValue()
        val promptTemplate = prefs.openaiPrompt.getValue()

        if (apiUrl.isBlank() || apiKey.isBlank()) {
            Timber.w("AI extraction: missing API configuration")
            return null
        }

        val desensitizedText = desensitizeText(text)
        val prompt = promptTemplate.replace("{input_text}", desensitizedText)

        return withContext(Dispatchers.IO) {
            try {
                val requestBody = JSONObject().apply {
                    put("model", model)
                    put("messages", JSONArray().apply {
                        put(JSONObject().apply {
                            put("role", "user")
                            put("content", prompt)
                        })
                    })
                    put("temperature", 0.1)
                    put("max_tokens", 100)
                }

                val url = URL("${apiUrl.trimEnd('/')}/chat/completions")
                val connection = url.openConnection() as HttpURLConnection
                connection.apply {
                    requestMethod = "POST"
                    setRequestProperty("Authorization", "Bearer $apiKey")
                    setRequestProperty("Content-Type", "application/json")
                    connectTimeout = 30000
                    readTimeout = 30000
                    doOutput = true
                }

                OutputStreamWriter(connection.outputStream).use { writer ->
                    writer.write(requestBody.toString())
                    writer.flush()
                }

                if (connection.responseCode == HttpURLConnection.HTTP_OK) {
                    val responseBody = connection.inputStream.bufferedReader().use(BufferedReader::readText)
                    val json = JSONObject(responseBody)
                    val content = json.getJSONArray("choices")
                        .getJSONObject(0)
                        .getJSONObject("message")
                        .getString("content")
                        .trim()

                    if (content.lowercase() != "none" && content.isNotBlank()) {
                        content
                    } else null
                } else {
                    Timber.e("AI extraction failed: ${connection.responseCode}")
                    null
                }
            } catch (e: Exception) {
                Timber.e(e, "AI extraction error")
                null
            }
        }
    }

    private fun desensitizeText(text: String): String {
        var result = text
        result = result.replace(Regex("""\b(?:\d{1,3}\.){3}\d{1,3}\b"""), "***.***.***.***")
        result = result.replace(Regex("""https?://\S+"""), "http://****")
        result = result.replace(Regex("""\b\d{10,11}\b"""), "**********")
        result = result.replace(Regex("""\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b"""), "****@****.***")
        result = result.replace(Regex("""\b\d{13,19}\b"""), "********************")
        return result
    }

    private fun isDeviceUnlockedAndInteractive(): Boolean {
        val pm = appContext.getSystemService<PowerManager>()
        val km = appContext.getSystemService<KeyguardManager>()
        val interactive = pm?.isInteractive ?: true
        val locked = km?.isKeyguardLocked ?: false
        return interactive && !locked
    }

    private fun copyToClipboard(code: String) {
        try {
            val clip = ClipData.newPlainText("VerificationCode", code)
            clipboardManager.setPrimaryClip(clip)
            Timber.d("Verification code copied to clipboard: $code")
        } catch (e: Exception) {
            Timber.e(e, "Failed to copy verification code to clipboard")
        }
    }

    private fun showToast(message: String) {
        runCatching {
            Toast.makeText(appContext, message, Toast.LENGTH_SHORT).show()
        }
    }
}
