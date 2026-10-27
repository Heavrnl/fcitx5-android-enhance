/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2025 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.ui.main

import android.os.Bundle
import android.view.Menu
import android.view.ViewGroup
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts.CreateDocument
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updateLayoutParams
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.launch
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.syncclipboard.SyncLog
import org.fcitx.fcitx5.android.databinding.ActivitySyncLogBinding
import org.fcitx.fcitx5.android.ui.main.log.LogView
import org.fcitx.fcitx5.android.utils.DeviceInfo
import org.fcitx.fcitx5.android.utils.iso8601UTCDateTime
import org.fcitx.fcitx5.android.utils.item
import org.fcitx.fcitx5.android.utils.toast
import splitties.resources.styledColor
import splitties.views.topPadding

/**
 * 查看/导出剪贴板同步的本地持久化日志（[SyncLog]）。
 *
 * 与 "实时日志"(LogActivity) 的区别：这里读的是落到 `files/logs/` 的文件，
 * 进程被杀、logcat 被冲掉之后依然能看，是"同步偶尔失效"的事后取证入口。
 */
class SyncLogActivity : AppCompatActivity() {

    private lateinit var logView: LogView
    private lateinit var binding: ActivitySyncLogBinding
    private lateinit var launcher: ActivityResultLauncher<String>

    /** 界面只渲染最后这么多行，避免几千行一次性塞进 RecyclerView */
    private val displayLineLimit = 4000

    private fun registerLauncher() {
        launcher = registerForActivityResult(CreateDocument("text/plain")) { uri ->
            if (uri == null) return@registerForActivityResult
            lifecycleScope.launch(NonCancellable + Dispatchers.IO) {
                runCatching {
                    contentResolver.openOutputStream(uri)!!.use { stream ->
                        stream.bufferedWriter().use { writer ->
                            writer.write(DeviceInfo.get(this@SyncLogActivity))
                            writer.write(SyncLog.snapshot())
                        }
                    }
                }.let { toast(it) }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        binding = ActivitySyncLogBinding.inflate(layoutInflater)
        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { _, windowInsets ->
            val statusBars = windowInsets.getInsets(WindowInsetsCompat.Type.statusBars())
            val navBars = windowInsets.getInsets(WindowInsetsCompat.Type.navigationBars())
            binding.root.updateLayoutParams<ViewGroup.MarginLayoutParams> {
                leftMargin = navBars.left
                rightMargin = navBars.right
            }
            binding.toolbar.topPadding = statusBars.top
            binding.logView.setBottomPadding(navBars.bottom)
            windowInsets
        }
        setContentView(binding.root)
        logView = binding.logView
        setSupportActionBar(binding.toolbar)
        supportActionBar?.apply {
            setDisplayHomeAsUpEnabled(true)
            setTitle(R.string.sync_log)
        }
        registerLauncher()
        reload()
    }

    private fun reload() {
        logView.clear()
        supportActionBar?.subtitle = SyncLog.sizeSummary()
        val snapshot = SyncLog.snapshot()
        if (snapshot.isBlank()) {
            logView.append("(empty) ${SyncLog.dir?.absolutePath ?: "-"}")
            return
        }
        val lines = snapshot.lineSequence().toList()
        lines.takeLast(displayLineLimit).forEach { logView.append(it) }
        if (lines.size > displayLineLimit) {
            logView.append("… (${lines.size - displayLineLimit} older lines hidden; export to get the full log)")
        }
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        val iconTint = styledColor(android.R.attr.colorControlNormal)
        menu.item(R.string.refresh, R.drawable.ic_baseline_sync_24, iconTint, true) {
            reload()
        }
        menu.item(R.string.clear, R.drawable.ic_baseline_delete_24, iconTint, true) {
            AlertDialog.Builder(this)
                .setMessage(R.string.sync_log_clear_confirm)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    SyncLog.clear()
                    reload()
                }
                .show()
        }
        menu.item(R.string.export, R.drawable.ic_baseline_save_24, iconTint, true) {
            launcher.launch("${packageName}-sync-log-${iso8601UTCDateTime()}.txt")
        }
        return true
    }
}
