/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2025 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.syncclipboard

import android.content.Context
import android.os.Build
import android.os.Process
import android.util.Log
import timber.log.Timber
import java.io.File
import java.security.MessageDigest
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/**
 * 剪贴板同步专用的本地持久化日志。
 *
 * 为什么需要它：
 *  - 项目里原本只有 Timber -> logcat，logcat 环形缓冲满了/进程重启就全没了，
 *    而"同步偶尔失效"恰恰是过后才发现的，事后无法取证。
 *  - release 包里 [org.fcitx.fcitx5.android.utils.ConciseTree] 会把 DEBUG/VERBOSE 级别
 *    直接丢掉，而同步链路里几乎所有决策点都是 Timber.d，等于正式版根本没有诊断信息。
 *
 * 设计要点：
 *  - 同步写入（不排队），保证崩溃/被杀之前已经落盘，且多线程下时间顺序不乱。
 *  - 单文件 256KB 滚动，保留 3 个历史文件（总计约 1MB），不会无限增长。
 *  - **不记录剪贴板内容明文**，只记录长度 + md5 前 16 位，避免日志泄露隐私。
 *
 * 日志文件位置：`files/logs/clipboard_sync*.log`
 * 查看入口：设置 -> 剪贴板同步 -> 剪贴板同步日志
 */
object SyncLog {

    /** logcat 里这些日志统一挂在 "SyncLog" 之下，方便 `adb logcat -s` */
    private const val LOGCAT_TAG = "SyncLog"

    private const val DIR_NAME = "logs"
    private const val BASE_NAME = "clipboard_sync"
    private const val EXT = ".log"

    /** 单个文件上限，超过就滚动 */
    private const val MAX_BYTES = 256L * 1024

    /** 保留的历史文件个数 */
    const val KEEP_ARCHIVES = 3

    /** 查看/导出时最多取多少字符（约 800KB），避免一次性读爆内存 */
    private const val MAX_SNAPSHOT_CHARS = 800_000

    /** 单条日志上限，防止异常堆栈/超长消息把单行撑爆 */
    private const val MAX_MESSAGE_CHARS = 4_000

    private val lock = ReentrantLock()

    @Volatile
    private var logDir: File? = null

    private val fullTimeFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)

    val isReady: Boolean
        get() = logDir != null

    /** 日志目录，未初始化时为 null */
    val dir: File?
        get() = logDir

    // ------------------------------------------------------------------ 初始化

    /**
     * 必须在 [Timber] 种树之前调用（见 FcitxApplication.onCreate），
     * 否则启动早期的日志会被丢掉。
     */
    fun init(context: Context) {
        if (logDir != null) return
        // 优先写应用外部目录：/sdcard/Android/data/<pkg>/files/logs
        // adb 可以直接 pull（内部目录 filesDir 只有 root / debuggable 包才读得到，
        // 出问题时正是需要"不改动现场就把日志取出来"）。
        val d = runCatching { context.getExternalFilesDir(null) }.getOrNull()
            ?.let { ext -> File(ext, DIR_NAME).takeIf { it.mkdirs() || it.isDirectory } }
            ?: File(context.filesDir, DIR_NAME).apply { runCatching { mkdirs() } }
        logDir = d
        migrateLegacyLogs(context, d)

        val versionName = runCatching {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName
        }.getOrNull() ?: "?"

        val header = buildString {
            appendLine("==================== SESSION START ====================")
            appendLine("time    : ${fullTimeFormat.format(Date())}")
            appendLine("pid     : ${Process.myPid()}")
            appendLine("version : $versionName")
            appendLine("device  : ${Build.MANUFACTURER} ${Build.MODEL} / Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})")
            appendLine("privacy : clipboard text is NOT logged; only len + md5(len) are recorded")
            appendLine("levels  : V/D/I/W/E")
            appendLine("======================================================")
        }
        appendText(header)
    }

    /**
     * 老版本把日志写在内部目录（只有 root / debuggable 包读得到）。
     * 切换到外部目录时把旧日志搬过来，这样升级后第一次启动就能把
     * 之前那次"失效现场"的日志一起导出，不会丢。
     */
    private fun migrateLegacyLogs(context: Context, target: File) {
        runCatching {
            val legacy = File(context.filesDir, DIR_NAME)
            if (!legacy.isDirectory) return
            if (legacy.canonicalPath == target.canonicalPath) return
            // 目标目录已经有日志就不再迁移，避免把新日志覆盖掉
            if ((target.listFiles()?.size ?: 0) > 0) return
            legacy.listFiles()?.forEach { src ->
                if (src.isFile) runCatching { src.copyTo(File(target, src.name), overwrite = true) }
            }
        }
    }

    /**
     * 会话开始后补写一次配置快照（此时 AppPrefs 才可用）。
     * 排查时第一件事就是确认"当时到底开没开、连的是哪台服务器"。
     */
    fun logConfigSnapshot(enabled: Boolean, serverUrl: String, username: String, password: String) {
        val host = runCatching { java.net.URI(serverUrl.trim()).host }.getOrNull() ?: serverUrl
        i(
            "Sync",
            "config: enabled=$enabled url=$host user=${username.ifBlank { "<empty>" }} " +
                "password=${if (password.isBlank()) "<empty>" else "set(len=${password.length})"}"
        )
    }

    // ------------------------------------------------------------------ 对外写入 API

    fun d(tag: String, message: String) = emit(Log.DEBUG, tag, message, null)

    fun i(tag: String, message: String) = emit(Log.INFO, tag, message, null)

    fun w(tag: String, message: String, t: Throwable? = null) = emit(Log.WARN, tag, message, t)

    fun e(tag: String, message: String, t: Throwable? = null) = emit(Log.ERROR, tag, message, t)

    private fun emit(priority: Int, tag: String, message: String, t: Throwable?) {
        if (logDir == null) return
        // logcat 也给一份，方便 adb 在线看；不走 Timber，避免和 SyncLogTree 重复写文件
        runCatching { Log.println(priority, "$LOGCAT_TAG/$tag", message) }
        write(priority, tag, message, t)
    }

    /**
     * 供 [SyncLogTree] 调用：把既有的 Timber 调用（Timber.d("SyncClipboard: ...")）也收进文件
     */
    internal fun write(priority: Int, tag: String, message: String, t: Throwable?) {
        val d = logDir ?: return
        val line = buildString {
            append(fullTimeFormat.format(Date()))
            append(' ')
            append(priorityLetter(priority))
            append('/')
            append(tag.ifEmpty { "Sync" })
            append(' ')
            val thread = Thread.currentThread().name
            if (thread.length <= 24) {
                append('[').append(thread).append("] ")
            }
            if (message.length > MAX_MESSAGE_CHARS) {
                append(message, 0, MAX_MESSAGE_CHARS).append("…(+${message.length - MAX_MESSAGE_CHARS})")
            } else {
                append(message)
            }
            append('\n')
            t?.let { append(stackTraceOf(it)) }
        }
        appendText(line)
    }

    private fun appendText(text: String) {
        val d = logDir ?: return
        lock.withLock {
            runCatching {
                var file = currentFile(d)
                if (file.length() > MAX_BYTES) {
                    rotate(d)
                    file = currentFile(d)
                    // 兜底：万一 renameTo 失败（同一目录下极少见），强制截断，
                    // 保证文件大小一定是有限的，不会无限膨胀。
                    if (file.length() > MAX_BYTES) {
                        file.writeText("${fullTimeFormat.format(Date())} W/Sync log truncated (rotate failed)\n")
                    }
                }
                file.appendText(text, Charsets.UTF_8)
            }
        }
    }

    private fun currentFile(d: File) = File(d, "$BASE_NAME$EXT")

    private fun rotate(d: File) {
        File(d, "$BASE_NAME.$KEEP_ARCHIVES$EXT").delete()
        for (i in KEEP_ARCHIVES - 1 downTo 1) {
            val from = File(d, "$BASE_NAME.$i$EXT")
            if (from.exists()) {
                from.renameTo(File(d, "$BASE_NAME.${i + 1}$EXT"))
            }
        }
        currentFile(d).renameTo(File(d, "$BASE_NAME.1$EXT"))
    }

    // ------------------------------------------------------------------ 读取 / 清理

    /** 按时间顺序拼出全部日志（历史文件在前，当前文件在后），用于界面展示与导出 */
    fun snapshot(): String {
        val d = logDir ?: return ""
        val sb = StringBuilder()
        for (i in KEEP_ARCHIVES downTo 1) {
            val f = File(d, "$BASE_NAME.$i$EXT")
            if (f.exists()) {
                runCatching { sb.append(f.readText(Charsets.UTF_8)) }
            }
        }
        runCatching { sb.append(currentFile(d).readText(Charsets.UTF_8)) }
        val s = sb.toString()
        return if (s.length > MAX_SNAPSHOT_CHARS) {
            "…(older lines truncated)\n" + s.takeLast(MAX_SNAPSHOT_CHARS)
        } else {
            s
        }
    }

    /** 日志总大小，用于在界面上提示 */
    fun totalSize(): Long {
        val d = logDir ?: return 0
        return allFiles(d).sumOf { runCatching { it.length() }.getOrDefault(0L) }
    }

    fun allFiles(): List<File> {
        val d = logDir ?: return emptyList()
        return allFiles(d)
    }

    private fun allFiles(d: File): List<File> =
        (KEEP_ARCHIVES downTo 1).map { File(d, "$BASE_NAME.$it$EXT") } + currentFile(d)

    /**
     * 供界面展示的大小摘要，例如 `2 file(s) · 412 KB / limit 1.0 MB`。
     * 上限 = 单文件上限 × (历史份数 + 当前文件)。
     */
    fun sizeSummary(): String {
        val files = allFiles().count { it.exists() }
        return "$files file(s) · ${formatSize(totalSize())} / limit ~${formatSize(MAX_BYTES * (KEEP_ARCHIVES + 1))}"
    }

    private fun formatSize(bytes: Long): String = when {
        bytes >= 1024 * 1024 -> String.format(Locale.US, "%.1f MB", bytes / 1024.0 / 1024.0)
        bytes >= 1024 -> String.format(Locale.US, "%.0f KB", bytes / 1024.0)
        else -> "$bytes B"
    }

    fun clear() {
        val d = logDir ?: return
        lock.withLock {
            allFiles(d).forEach { runCatching { it.delete() } }
        }
        // 清空后补一条标记，避免下次看到空白文件以为是故障
        appendText("${fullTimeFormat.format(Date())} I/Sync log cleared by user\n")
    }

    // ------------------------------------------------------------------ 工具

    fun textMeta(text: String?): String {
        if (text == null) return "text=null"
        return "len=${text.length} md5=${md5(text).take(16)} lines=${text.count { it == '\n' } + 1}"
    }

    fun md5(s: String): String = runCatching {
        MessageDigest.getInstance("MD5")
            .digest(s.toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it) }
    }.getOrDefault("?")

    private fun priorityLetter(priority: Int) = when (priority) {
        Log.VERBOSE -> 'V'
        Log.DEBUG -> 'D'
        Log.INFO -> 'I'
        Log.WARN -> 'W'
        Log.ERROR -> 'E'
        Log.ASSERT -> 'F'
        else -> '?'
    }

    private fun stackTraceOf(t: Throwable): String =
        t.stackTraceToString().lineSequence()
            .take(12)
            .joinToString("") { "    $it\n" }
}

/**
 * 把同步相关模块的 Timber 调用也落盘。
 *
 * 这样无需改动所有既有 `Timber.d(...)`，`release` 包里本来被
 * [org.fcitx.fcitx5.android.utils.ConciseTree] 丢掉的 DEBUG 日志也能进文件。
 */
class SyncLogTree : Timber.Tree() {

    private val tags = listOf("SyncLog", "SyncClipboard", "SignalR", "ScreenshotDetector")
    private val messagePrefixes =
        listOf("SyncClipboard", "ScreenshotDetector", "[CB]", "[WS]", "[UP]", "[DOWN]", "[HTTP]")

    override fun log(priority: Int, tag: String?, message: String, t: Throwable?) {
        if (!SyncLog.isReady) return
        if (priority < Log.DEBUG) return
        val tg = tag ?: ""
        val isSync = tags.any { tg.startsWith(it) } || messagePrefixes.any { message.startsWith(it) }
        if (!isSync) return
        SyncLog.write(priority, tg.ifEmpty { "Timber" }, message, t)
    }
}
