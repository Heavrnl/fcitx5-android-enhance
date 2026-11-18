/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2026 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.syncclipboard

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import org.fcitx.fcitx5.android.FcitxApplication
import org.fcitx.fcitx5.android.data.prefs.AppPrefs

/**
 * 接收开机解锁完成（BOOT_COMPLETED）及应用更新替换（MY_PACKAGE_REPLACED）广播，
 * 在进程尚未因弹出软键盘而启动时，提前恢复剪贴板同步与屏幕状态监听。
 */
class SyncClipboardBootReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action ?: return
        if (action != Intent.ACTION_BOOT_COMPLETED && action != Intent.ACTION_MY_PACKAGE_REPLACED) {
            return
        }
        if (FcitxApplication.getInstance().isDirectBootMode) {
            return
        }
        org.fcitx.fcitx5.android.data.verification.VerificationCodeManager.ensureListenerBound()
        val prefs = AppPrefs.getInstance().syncClipboard
        if (!prefs.enabled.getValue()) {
            return
        }
        SyncLog.i("Sync", "boot/package receiver triggered: action=$action")
        SyncClipboardManager.onBootCompleted()
    }
}
