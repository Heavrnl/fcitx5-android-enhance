/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.verification

import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import timber.log.Timber

/**
 * 通知监听服务
 * 监听所有通知，当发现包含验证码关键词的通知时，触发验证码提取
 */
class VerificationCodeNotificationListener : NotificationListenerService() {

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        if (sbn == null) return

        try {
            val notification = sbn.notification ?: return
            val extras = notification.extras ?: return

            // 获取通知标题和内容
            val title = extras.getCharSequence("android.title")?.toString() ?: ""
            val text = extras.getCharSequence("android.text")?.toString() ?: ""
            val bigText = extras.getCharSequence("android.bigText")?.toString() ?: ""

            // 合并所有文本内容
            val fullText = buildString {
                if (title.isNotBlank()) append(title).append(" ")
                if (text.isNotBlank()) append(text).append(" ")
                if (bigText.isNotBlank()) append(bigText)
            }.trim()

            if (fullText.isBlank()) return

            Timber.d("Notification received from ${sbn.packageName}: $fullText")

            // 检查是否包含验证码关键词并处理
            if (VerificationCodeManager.containsKeywords(fullText)) {
                Timber.d("Verification code keywords detected, processing...")
                VerificationCodeManager.processNotification(fullText)
            }

        } catch (e: Exception) {
            Timber.e(e, "Error processing notification")
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        // 不需要处理通知移除事件
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        Timber.d("Notification listener connected")
    }

    override fun onListenerDisconnected() {
        super.onListenerDisconnected()
        Timber.d("Notification listener disconnected")
    }
}
