/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2021-2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.clipboard

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import android.os.SystemClock
import androidx.annotation.Keep
import androidx.core.content.edit
import androidx.room.Room
import androidx.room.withTransaction
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.fcitx.fcitx5.android.data.clipboard.db.ClipboardDao
import org.fcitx.fcitx5.android.data.clipboard.db.ClipboardDatabase
import org.fcitx.fcitx5.android.data.clipboard.db.ClipboardEntry
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreference
import org.fcitx.fcitx5.android.data.syncclipboard.SyncLog
import org.fcitx.fcitx5.android.utils.WeakHashSet
import org.fcitx.fcitx5.android.utils.appContext
import org.fcitx.fcitx5.android.utils.clipboardManager
import timber.log.Timber
import java.io.File
import java.security.MessageDigest

object ClipboardManager : ClipboardManager.OnPrimaryClipChangedListener,
    CoroutineScope by CoroutineScope(SupervisorJob() + Dispatchers.Default) {
    private const val STATE_PREFS_NAME = "sync_clipboard_state"
    private const val KEY_LAST_CLIP_TS = "last_clip_ts"
    private const val KEY_LAST_CLIP_HASH = "last_clip_hash"

    private lateinit var clbDb: ClipboardDatabase
    private lateinit var clbDao: ClipboardDao
    private lateinit var context: Context
    private var statePrefs: SharedPreferences? = null
    private var initWallTimeMs: Long = 0L

    // 图片存储目录
    val imageDir: File
        get() = File(context.filesDir, "clipboard_images").also { it.mkdirs() }

    fun interface OnClipboardUpdateListener {
        fun onUpdate(entry: ClipboardEntry)
    }

    private val clipboardManager = appContext.clipboardManager

    private val mutex = Mutex()

    var itemCount: Int = 0
        private set

    private suspend fun updateItemCount() {
        itemCount = clbDao.itemCount()
    }

    private val onUpdateListeners = WeakHashSet<OnClipboardUpdateListener>()
    private val onLocalCopyListeners = WeakHashSet<OnClipboardUpdateListener>()

    var transformer: ((String) -> String)? = null

    fun addOnUpdateListener(listener: OnClipboardUpdateListener) {
        onUpdateListeners.add(listener)
    }

    fun removeOnUpdateListener(listener: OnClipboardUpdateListener) {
        onUpdateListeners.remove(listener)
    }

    /**
     * 仅订阅本机真实复制产生的新剪贴板条目，远端同步下发写入的条目不会触发此回调。
     */
    fun addOnLocalCopyListener(listener: OnClipboardUpdateListener) {
        onLocalCopyListeners.add(listener)
    }

    fun removeOnLocalCopyListener(listener: OnClipboardUpdateListener) {
        onLocalCopyListeners.remove(listener)
    }

    private val enabledPref = AppPrefs.getInstance().clipboard.clipboardListening

    @Keep
    private val enabledListener = ManagedPreference.OnChangeListener<Boolean> { _, value ->
        if (value) {
            clipboardManager.addPrimaryClipChangedListener(this)
        } else {
            clipboardManager.removePrimaryClipChangedListener(this)
        }
    }

    private val limitPref = AppPrefs.getInstance().clipboard.clipboardHistoryLimit

    @Keep
    private val limitListener = ManagedPreference.OnChangeListener<Int> { _, _ ->
        launch { removeOutdated() }
    }

    var lastEntry: ClipboardEntry? = null

    private fun updateLastEntry(entry: ClipboardEntry, notifyLocalCopy: Boolean) {
        lastEntry = entry
        onUpdateListeners.forEach { it.onUpdate(entry) }
        if (notifyLocalCopy) {
            onLocalCopyListeners.forEach { it.onUpdate(entry) }
        }
    }

    fun init(context: Context) {
        this.context = context
        this.initWallTimeMs = System.currentTimeMillis()
        this.statePrefs = runCatching {
            context.getSharedPreferences(STATE_PREFS_NAME, Context.MODE_PRIVATE)
        }.getOrNull()
        clbDb = Room
            .databaseBuilder(context, ClipboardDatabase::class.java, "clbdb")
            // allow wipe the database instead of crashing when downgrade
            .fallbackToDestructiveMigrationOnDowngrade(dropAllTables = true)
            .build()
        clbDao = clbDb.clipboardDao()
        enabledListener.onChange(enabledPref.key, enabledPref.getValue())
        enabledPref.registerOnChangeListener(enabledListener)
        limitListener.onChange(limitPref.key, limitPref.getValue())
        limitPref.registerOnChangeListener(limitListener)
        launch { updateItemCount() }
    }

    suspend fun get(id: Int) = clbDao.get(id)

    suspend fun haveUnpinned() = clbDao.haveUnpinned()

    fun allEntries() = clbDao.allEntries()

    suspend fun pin(id: Int) = clbDao.updatePinStatus(id, true)

    suspend fun unpin(id: Int) = clbDao.updatePinStatus(id, false)

    suspend fun updateText(id: Int, text: String) {
        lastEntry?.let {
            if (id == it.id) updateLastEntry(it.copy(text = text), notifyLocalCopy = false)
        }
        clbDao.updateText(id, text)
    }

    suspend fun delete(id: Int) {
        // 删除关联图片
        val entry = clbDao.get(id)
        if (entry?.imagePath?.isNotEmpty() == true) {
            File(entry.imagePath).delete()
        }
        clbDao.markAsDeleted(id)
        updateItemCount()
    }

    suspend fun deleteAll(skipPinned: Boolean = true): IntArray {
        val ids = if (skipPinned) {
            clbDao.findUnpinnedIds()
        } else {
            clbDao.findAllIds()
        }
        // 删除关联图片
        ids.forEach { id ->
            val entry = clbDao.get(id)
            if (entry?.imagePath?.isNotEmpty() == true) {
                File(entry.imagePath).delete()
            }
        }
        clbDao.markAsDeleted(*ids)
        updateItemCount()
        return ids
    }

    suspend fun undoDelete(vararg ids: Int) {
        clbDao.undoDelete(*ids)
        updateItemCount()
    }

    suspend fun realDelete() {
        clbDao.realDelete()
    }

    suspend fun nukeTable() {
        withContext(coroutineContext) {
            clbDb.clearAllTables()
            // 清除所有图片
            imageDir.listFiles()?.forEach { it.delete() }
            updateItemCount()
        }
    }

    /**
     * 插入图片条目（用于 SyncClipboard）
     */
    suspend fun insertImageEntry(entry: ClipboardEntry): ClipboardEntry? {
        return mutex.withLock {
            try {
                val insertedEntry = clbDb.withTransaction {
                    val rowId = clbDao.insert(entry)
                    removeOutdated()
                    clbDao.get(rowId) ?: entry
                }
                updateLastEntry(insertedEntry, notifyLocalCopy = false)
                updateItemCount()
                insertedEntry
            } catch (e: Exception) {
                Timber.e(e, "Failed to insert image entry")
                null
            }
        }
    }

    private var lastClipTimestamp = -1L
    private var lastClipHash = 0
    private var lastProcessedTextHash = ""

    // 记录后台因无焦点被系统拦截的剪贴板变更，以便解锁或弹出键盘时主动补读上传
    @Volatile
    private var pendingBackgroundClipSync = false

    @Volatile
    private var pendingMissedClipAt = 0L

    @Volatile
    private var lastCompensationCheckAt = 0L

    // 记录最近一次由本应用主动写入系统剪贴板的文本（远端同步或提取的验证码），
    // 防止紧随其后的系统回调或补偿逻辑重复入库或二次上传
    @Volatile
    private var lastSelfAppliedText: String? = null

    @Volatile
    private var lastSelfAppliedClipAt = 0L

    fun markRemoteTextApplied(text: String) {
        markSelfAppliedText(text)
    }

    fun markSelfAppliedText(text: String) {
        lastSelfAppliedText = text
        lastSelfAppliedClipAt = SystemClock.elapsedRealtime()
    }

    /**
     * 远端文本写入系统剪贴板后，同步写入输入法剪贴板历史，
     * 避免部分 ROM 禁止后台读取 primaryClip 时导致历史面板缺少同步记录。
     */
    suspend fun recordRemoteText(text: String) {
        recordTextDirectly(text, logTag = "recordRemoteText")
    }

    /**
     * 验证码写入系统剪贴板后直接落库（不走 onLocalCopyListeners），避免息屏时后台读剪贴板被系统拦截。
     */
    suspend fun recordVerificationCode(code: String) {
        markSelfAppliedText(code)
        recordTextDirectly(code, logTag = "recordVerificationCode")
    }

    private suspend fun recordTextDirectly(text: String, logTag: String) {
        if (text.isBlank()) return
        // 若刚好是刚才自己 setPrimaryClip 触发了系统剪贴板权限检查，尝试顺便对齐时间戳
        val sysTs = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            runCatching { clipboardManager.primaryClip?.description?.timestamp }.getOrNull() ?: 0L
        } else {
            0L
        }
        mutex.withLock {
            val now = System.currentTimeMillis()
            val textHash = sha256Hex(text)
            lastProcessedTextHash = textHash
            if (sysTs > 0L) {
                lastClipTimestamp = sysTs
                persistClipCursor(sysTs, textHash)
            } else {
                persistClipHashOnly(textHash)
            }
            try {
                val existing = clbDao.find(text, false)
                val entry = if (existing != null) {
                    clbDao.updateTime(existing.id, now)
                    existing.copy(timestamp = now)
                } else {
                    val newEntry = ClipboardEntry(text = text, timestamp = now)
                    val inserted = clbDb.withTransaction {
                        val rowId = clbDao.insert(newEntry)
                        removeOutdated()
                        clbDao.get(rowId) ?: newEntry
                    }
                    updateItemCount()
                    inserted
                }
                updateLastEntry(entry, notifyLocalCopy = false)
            } catch (e: Exception) {
                SyncLog.w("CB", "$logTag db failed: ${e.message}", e)
                updateLastEntry(ClipboardEntry(text = text, timestamp = now), notifyLocalCopy = false)
            }
        }
    }

    /**
     * 在输入法弹出、解锁等重新获得剪贴板访问权时机执行补偿检查：
     * 捕获之前因后台无焦点而被系统拦截的剪贴板变更，或进程重启期间新产生的数据。
     */
    fun checkAndSyncSystemClipboard(reason: String) {
        if (!enabledPref.getValue()) return
        val nowElapsed = SystemClock.elapsedRealtime()
        if (nowElapsed - lastCompensationCheckAt < 250L) return
        lastCompensationCheckAt = nowElapsed

        val clip = runCatching { clipboardManager.primaryClip }.getOrElse { e ->
            SyncLog.d("CB", "compensation($reason): primaryClip threw ${e.javaClass.simpleName}")
            return
        }
        if (clip == null) {
            SyncLog.d("CB", "compensation($reason): primaryClip=null")
            return
        }

        // 若刚才因自己 setPrimaryClip 触发的回调在后台被拦截，待补读时若仍是同一段文本则仅对齐游标，不回传
        val selfApplied = lastSelfAppliedText
        if (pendingBackgroundClipSync &&
            selfApplied != null &&
            pendingMissedClipAt - lastSelfAppliedClipAt in 0L..3_000L
        ) {
            val currentText = runCatching {
                if (clip.itemCount > 0) clip.getItemAt(0)?.text?.toString() else null
            }.getOrNull()
            if (currentText == selfApplied) {
                pendingBackgroundClipSync = false
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    val ts = clip.description.timestamp
                    if (ts > 0L) {
                        lastClipTimestamp = ts
                        persistClipCursor(ts, sha256Hex(selfApplied))
                    }
                }
                SyncLog.d("CB", "compensation($reason): matched just-applied self text, skipped upload")
                return
            }
        }

        val hadPending = pendingBackgroundClipSync
        pendingBackgroundClipSync = false
        handleClipData(clip, fromCompensation = true, forceUploadIfPending = hadPending, reason = reason)
    }

    override fun onPrimaryClipChanged() {
        // 注意：Android 10+ 部分 ROM 在软键盘未弹出时读取剪贴板会被拦截。
        // 被拦截时记下 pendingBackgroundClipSync，待解锁或弹出键盘时再补读上传。
        val clip = runCatching { clipboardManager.primaryClip }.getOrElse { e ->
            pendingBackgroundClipSync = true
            pendingMissedClipAt = SystemClock.elapsedRealtime()
            SyncLog.w("CB", "read primaryClip threw ${e.javaClass.simpleName}: ${e.message} -> marked pending", e)
            return
        }
        if (clip == null) {
            pendingBackgroundClipSync = true
            pendingMissedClipAt = SystemClock.elapsedRealtime()
            SyncLog.w("CB", "primaryClip=null (no focus / access denied by system) -> marked pending")
            return
        }
        pendingBackgroundClipSync = false
        handleClipData(clip, fromCompensation = false, forceUploadIfPending = false, reason = "listener")
    }

    private fun handleClipData(
        clip: ClipData,
        fromCompensation: Boolean,
        forceUploadIfPending: Boolean,
        reason: String
    ) {
        var shouldNotifyLocalCopy = true
        val clipTimestamp = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            clip.description.timestamp
        } else {
            0L
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            if (clipTimestamp == lastClipTimestamp && !forceUploadIfPending) {
                if (!fromCompensation) {
                    SyncLog.d("CB", "skip: duplicate event (ts=$clipTimestamp)")
                }
                return
            }
            if (fromCompensation && lastClipTimestamp == -1L) {
                // 进程刚重启后首次补偿读到系统剪贴板：比对持久化时间戳，防止把重启前的老剪贴板重新上传覆盖服务端
                val persistedTs = statePrefs?.getLong(KEY_LAST_CLIP_TS, 0L) ?: 0L
                val isNewSinceRestart = when {
                    forceUploadIfPending -> true
                    persistedTs > 0L -> clipTimestamp > persistedTs
                    else -> clipTimestamp >= initWallTimeMs
                }
                if (!isNewSinceRestart) {
                    shouldNotifyLocalCopy = false
                    SyncLog.d(
                        "CB",
                        "compensation($reason): existing pre-restart clip (ts=$clipTimestamp persisted=$persistedTs), sync locally only"
                    )
                }
            }
            lastClipTimestamp = clipTimestamp
        } else {
            val timestamp = System.currentTimeMillis()
            val hash = clip.hashCode()
            if (!fromCompensation && timestamp - lastClipTimestamp < 100L && hash == lastClipHash) {
                SyncLog.d("CB", "skip: duplicate event (legacy)")
                return
            }
            if (fromCompensation && lastClipTimestamp == -1L && !forceUploadIfPending) {
                shouldNotifyLocalCopy = false
            }
            lastClipTimestamp = timestamp
            lastClipHash = hash
        }

        val mimes = (0 until clip.description.mimeTypeCount)
            .joinToString(",") { clip.description.getMimeType(it) }
        SyncLog.d(
            "CB",
            "clip changed ($reason): mime=$mimes items=${clip.itemCount} notifyUpload=$shouldNotifyLocalCopy"
        )
        launch {
            mutex.withLock {
                // 尝试包含图片的处理
                val entry = ClipboardEntry.fromClipData(clip, context, imageDir, transformer)
                    ?: ClipboardEntry.fromClipData(clip, transformer)
                    ?: run {
                        SyncLog.w("CB", "skip: clip not parsable (unsupported mime)")
                        return@withLock
                    }

                // 图片类型直接插入
                if (entry.isImage) {
                    SyncLog.d("CB", "image entry: path=${entry.imagePath}")
                    try {
                        val insertedEntry = clbDb.withTransaction {
                            val rowId = clbDao.insert(entry)
                            removeOutdated()
                            clbDao.get(rowId) ?: entry
                        }
                        if (clipTimestamp > 0L) {
                            persistClipCursor(clipTimestamp, "")
                        }
                        updateLastEntry(insertedEntry, notifyLocalCopy = shouldNotifyLocalCopy)
                        updateItemCount()
                    } catch (exception: Exception) {
                        SyncLog.w("CB", "failed to insert image entry: $exception", exception)
                    }
                    return@withLock
                }

                // 文本类型处理
                if (entry.text.isBlank()) {
                    SyncLog.d("CB", "skip: blank text")
                    return@withLock
                }

                val textHash = sha256Hex(entry.text)
                // Android < O 或 timestamp 为 0 的设备，额外比对内存及持久化文本摘要避免重复上传
                if (fromCompensation && !forceUploadIfPending && clipTimestamp <= 0L) {
                    val persistedHash = statePrefs?.getString(KEY_LAST_CLIP_HASH, "") ?: ""
                    if (textHash == lastProcessedTextHash || persistedHash == textHash) {
                        shouldNotifyLocalCopy = false
                        if (lastProcessedTextHash == textHash) {
                            SyncLog.d("CB", "compensation($reason): same text hash on legacy api, skipped")
                            return@withLock
                        }
                    }
                }
                lastProcessedTextHash = textHash
                if (clipTimestamp > 0L) {
                    persistClipCursor(clipTimestamp, textHash)
                } else {
                    persistClipHashOnly(textHash)
                }

                try {
                    clbDao.find(entry.text, entry.sensitive)?.let {
                        SyncLog.d(
                            "CB",
                            "text duplicated in history (id=${it.id}, sensitive=${entry.sensitive}) ${SyncLog.textMeta(entry.text)}"
                        )
                        updateLastEntry(
                            it.copy(timestamp = entry.timestamp),
                            notifyLocalCopy = shouldNotifyLocalCopy
                        )
                        clbDao.updateTime(it.id, entry.timestamp)
                        return@withLock
                    }
                    val insertedEntry = clbDb.withTransaction {
                        val rowId = clbDao.insert(entry)
                        removeOutdated()
                        // new entry can be deleted immediately if clipboard limit == 0
                        clbDao.get(rowId) ?: entry
                    }
                    SyncLog.i(
                        "CB",
                        "text stored (id=${insertedEntry.id}, sensitive=${entry.sensitive}, upload=$shouldNotifyLocalCopy) ${SyncLog.textMeta(entry.text)}"
                    )
                    updateLastEntry(insertedEntry, notifyLocalCopy = shouldNotifyLocalCopy)
                    updateItemCount()
                } catch (exception: Exception) {
                    SyncLog.w("CB", "failed to update db, fallback to in-memory only: $exception", exception)
                    updateLastEntry(entry, notifyLocalCopy = shouldNotifyLocalCopy)
                }
            }
        }
    }

    private fun persistClipCursor(timestamp: Long, textHash: String) {
        statePrefs?.edit {
            putLong(KEY_LAST_CLIP_TS, timestamp)
            if (textHash.isNotEmpty()) {
                putString(KEY_LAST_CLIP_HASH, textHash)
            }
        }
    }

    private fun persistClipHashOnly(textHash: String) {
        if (textHash.isEmpty()) return
        statePrefs?.edit {
            putString(KEY_LAST_CLIP_HASH, textHash)
        }
    }

    private fun sha256Hex(text: String): String {
        val digest = MessageDigest.getInstance("SHA-256").digest(text.toByteArray(Charsets.UTF_8))
        return digest.joinToString("") { "%02x".format(it) }
    }

    private suspend fun removeOutdated() {
        val limit = limitPref.getValue()
        val unpinned = clbDao.getAllUnpinned()
        if (unpinned.size > limit) {
            // the last one we will keep
            val last = unpinned
                .sortedBy { it.id }
                .getOrNull(unpinned.size - limit)
            // 删除过期条目的图片
            val toDelete = unpinned.filter { it.timestamp < (last?.timestamp ?: System.currentTimeMillis()) }
            toDelete.forEach { entry ->
                if (entry.imagePath.isNotEmpty()) {
                    File(entry.imagePath).delete()
                }
            }
            // delete all unpinned before that, or delete all when limit <= 0
            clbDao.markUnpinnedAsDeletedEarlierThan(last?.timestamp ?: System.currentTimeMillis())
        }
    }

}
