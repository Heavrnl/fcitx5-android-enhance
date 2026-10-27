/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2025 Fcitx5 for Android Contributors
 */

package org.fcitx.fcitx5.android.utils

import android.util.Log
import org.fcitx.fcitx5.android.BuildConfig
import org.fcitx.fcitx5.android.data.syncclipboard.SyncLogTree
import timber.log.Timber

class VerboseTree : Timber.DebugTree() {
    override fun log(priority: Int, tag: String?, message: String, t: Throwable?) {
        super.log(priority, "[${Thread.currentThread().name}] $tag", message, t)
    }
}

class ConciseTree : Timber.Tree() {
    // "tag" is only available when calling with Timber.tag().log(), which we didn't
    override fun log(priority: Int, tag: String?, message: String, t: Throwable?) {
        if (priority < Log.INFO) return
        Log.println(priority, "[${Thread.currentThread().name}]", message)
    }
}

fun Timber.Forest.setupForest(verbose: Boolean) {
    if (treeCount > 0) {
        uprootAll()
    }
    plant(if (BuildConfig.DEBUG || verbose) VerboseTree() else ConciseTree())
    // 剪贴板同步的日志额外落盘（release 包也写，且不受 ConciseTree 的 INFO 过滤影响）。
    // 位置：files/logs/clipboard_sync*.log，入口：设置 -> 剪贴板同步 -> 剪贴板同步日志
    plant(SyncLogTree())
}
