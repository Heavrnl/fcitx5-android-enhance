/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.verification

import android.content.ClipData
import android.widget.Toast
import androidx.annotation.Keep
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreference
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
 * 验证码提取管理器
 * 从通知中提取验证码并复制到剪贴板
 */
object VerificationCodeManager : CoroutineScope by CoroutineScope(SupervisorJob() + Dispatchers.Default) {

    private val prefs = AppPrefs.getInstance().verificationCode
    private val clipboardManager = appContext.clipboardManager

    // 默认触发关键词列表
    private val DEFAULT_KEYWORDS = listOf(
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

    // 当前使用的关键词列表
    private var keywords: List<String> = DEFAULT_KEYWORDS

    // 验证码提取后的回调
    var onCodeExtracted: ((String) -> Unit)? = null

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
    }

    private fun loadKeywords() {
        // 从设置中读取关键词（逗号分隔）
        val keywordsStr = prefs.keywords.getValue()
        keywords = if (keywordsStr.isNotBlank()) {
            keywordsStr.split(",").map { it.trim() }.filter { it.isNotBlank() }
        } else {
            DEFAULT_KEYWORDS
        }
        Timber.d("Loaded ${keywords.size} keywords")
    }

    /**
     * 获取当前关键词列表
     */
    fun getKeywords(): List<String> = keywords.toList()

    /**
     * 设置关键词列表
     */
    fun setKeywords(newKeywords: List<String>) {
        keywords = newKeywords.filter { it.isNotBlank() }
    }

    /**
     * 添加关键词
     */
    fun addKeyword(keyword: String) {
        if (keyword.isNotBlank() && !keywords.contains(keyword)) {
            keywords = keywords + keyword
        }
    }

    /**
     * 删除关键词
     */
    fun removeKeyword(keyword: String) {
        keywords = keywords - keyword
    }

    /**
     * 重置为默认关键词
     */
    fun resetKeywords() {
        keywords = DEFAULT_KEYWORDS
    }

    /**
     * 检查文本是否包含验证码关键词
     */
    fun containsKeywords(text: String): Boolean {
        if (!prefs.enabled.getValue()) return false
        val lowerText = text.lowercase()
        return keywords.any { keyword -> keyword.lowercase() in lowerText }
    }

    /**
     * 处理通知文本，提取验证码
     */
    fun processNotification(text: String) {
        if (!prefs.enabled.getValue()) return
        if (!containsKeywords(text)) return

        launch {
            val code = extractCode(text)
            if (code != null && code.lowercase() != "none") {
                withContext(Dispatchers.Main) {
                    copyToClipboard(code)
                    showToast(appContext.getString(R.string.verification_code_extracted, code))
                    
                    // 触发自动填充回调
                    if (prefs.autoFill.getValue()) {
                        onCodeExtracted?.invoke(code)
                    }
                }
            }
        }
    }

    /**
     * 根据设置提取验证码
     */
    private suspend fun extractCode(text: String): String? {
        return when (prefs.extractionMode.getValue()) {
            VerificationCodeExtractionMode.Local -> extractCodeLocal(text)
            VerificationCodeExtractionMode.AI -> extractCodeAI(text) ?: extractCodeLocal(text)
        }
    }

    /**
     * 本地智能提取验证码
     * 使用距离算法：找到离关键词最近的验证码候选
     */
    private fun extractCodeLocal(text: String): String? {
        // 验证码匹配正则：4-8位纯数字 或 包含字母的4-8位混合码
        val codePattern = """((?=[a-zA-Z].*\d|\d.*[a-zA-Z])[a-zA-Z0-9]{4,8})|(\d{4,8})""".toRegex()
        
        // 找到所有关键词的位置
        val keywordPositions = mutableListOf<Int>()
        for (keyword in keywords) {
            var index = text.indexOf(keyword, ignoreCase = true)
            while (index != -1) {
                keywordPositions.add(index)
                index = text.indexOf(keyword, index + keyword.length, ignoreCase = true)
            }
        }
        
        // 如果没有找到关键词，返回 null
        if (keywordPositions.isEmpty()) {
            Timber.d("No keywords found in text")
            return null
        }
        
        // 找到所有可能的验证码候选
        val candidates = codePattern.findAll(text).toList()
        if (candidates.isEmpty()) {
            Timber.d("No code candidates found in text")
            return null
        }
        
        // 找到距离关键词最近的验证码
        var closestCode: String? = null
        var closestDistance = Int.MAX_VALUE
        
        for (match in candidates) {
            val codeStart = match.range.first
            val code = match.value
            
            // 计算与所有关键词的最小距离
            for (keywordPos in keywordPositions) {
                val distance = kotlin.math.abs(codeStart - keywordPos)
                if (distance < closestDistance) {
                    closestDistance = distance
                    closestCode = code
                }
            }
        }
        
        if (closestCode != null) {
            Timber.d("Extracted code '$closestCode' with distance $closestDistance from keyword")
        }
        
        return closestCode
    }

    /**
     * 使用 AI API 提取验证码
     */
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

    /**
     * 脱敏处理：隐藏敏感信息
     */
    private fun desensitizeText(text: String): String {
        var result = text
        // 替换 IP 地址
        result = result.replace(Regex("""\b(?:\d{1,3}\.){3}\d{1,3}\b"""), "***.***.***.***")
        // 替换 URL
        result = result.replace(Regex("""https?://\S+"""), "http://****")
        // 替换手机号码
        result = result.replace(Regex("""\b\d{10,11}\b"""), "**********")
        // 替换邮箱
        result = result.replace(Regex("""\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b"""), "****@****.***")
        // 替换信用卡号码
        result = result.replace(Regex("""\b\d{13,19}\b"""), "********************")
        return result
    }

    /**
     * 复制验证码到剪贴板
     */
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
        Toast.makeText(appContext, message, Toast.LENGTH_SHORT).show()
    }
}

