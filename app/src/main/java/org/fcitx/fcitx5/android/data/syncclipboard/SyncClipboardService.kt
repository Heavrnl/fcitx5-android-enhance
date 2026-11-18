/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2026 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.syncclipboard

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.ui.main.MainActivity
import org.fcitx.fcitx5.android.ui.main.settings.SettingsRoute
import org.fcitx.fcitx5.android.utils.notificationManager

/**
 * 可选的剪贴板同步后台保活前台服务。
 *
 * 设计原则：
 * 1. 不持有任何 WakeLock，也不执行任何独立定时器，息屏后由 SyncClipboardManager 主动断开全部连接与心跳。
 * 2. 使用最低优先级静默通知渠道（IMPORTANCE_MIN），不震动、不响铃、不在状态栏打扰用户。
 * 3. 仅作为进程优先级提升手段，在亮屏未弹出软键盘时防止部分 ROM 冻结网络或杀死进程。
 */
class SyncClipboardService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        isRunning = true
        createNotificationChannel(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Android 8+ 通过 startForegroundService 启动后必须立即调用 startForeground，
        // 即使随后判断功能已关闭需要 stopSelf，也必须先挂上通知再移除，避免 ForegroundServiceDidNotStartInTimeException
        val notification = buildNotification(this, SyncClipboardManager.syncState.value)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }

        val prefs = AppPrefs.getInstance().syncClipboard
        if (!prefs.enabled.getValue() || !prefs.foregroundService.getValue()) {
            stopForegroundCompat()
            stopSelf()
            return START_NOT_STICKY
        }

        return START_STICKY
    }

    override fun onDestroy() {
        isRunning = false
        stopForegroundCompat()
        super.onDestroy()
    }

    @Suppress("DEPRECATION")
    private fun stopForegroundCompat() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE)
        } else {
            stopForeground(true)
        }
    }

    companion object {
        private const val CHANNEL_ID = "sync-clipboard-keep-alive"
        private const val NOTIFICATION_ID = 0x5379

        @Volatile
        var isRunning: Boolean = false
            private set

        @Volatile
        private var lastNotifiedState: SyncClipboardManager.SyncState? = null

        fun start(context: Context) {
            val prefs = AppPrefs.getInstance().syncClipboard
            if (!prefs.enabled.getValue() || !prefs.foregroundService.getValue()) return
            val intent = Intent(context, SyncClipboardService::class.java)
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    ContextCompat.startForegroundService(context, intent)
                } else {
                    context.startService(intent)
                }
            } catch (e: Exception) {
                // Android 12+ 在无电池豁免的严格后台场景可能抛出 ForegroundServiceStartNotAllowedException，
                // 不向上崩溃，等待用户打开应用或弹出软键盘时自动补拉起
                SyncLog.w("SVC", "startForegroundService failed (${e.javaClass.simpleName}): ${e.message}")
            }
        }

        fun stop(context: Context) {
            lastNotifiedState = null
            runCatching {
                context.stopService(Intent(context, SyncClipboardService::class.java))
            }
        }

        fun updateNotificationIfRunning(context: Context, state: SyncClipboardManager.SyncState) {
            if (!isRunning) return
            if (lastNotifiedState == state) return
            lastNotifiedState = state
            runCatching {
                createNotificationChannel(context)
                context.notificationManager.notify(
                    NOTIFICATION_ID,
                    buildNotification(context, state)
                )
            }
        }

        private fun createNotificationChannel(context: Context) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val channel = NotificationChannel(
                    CHANNEL_ID,
                    context.getString(R.string.sync_clipboard_service_channel_name),
                    NotificationManager.IMPORTANCE_MIN
                ).apply {
                    setShowBadge(false)
                    enableLights(false)
                    enableVibration(false)
                    setSound(null, null)
                }
                context.notificationManager.createNotificationChannel(channel)
            }
        }

        private fun buildNotification(
            context: Context,
            state: SyncClipboardManager.SyncState
        ): Notification {
            lastNotifiedState = state
            val contentText = when (state) {
                SyncClipboardManager.SyncState.Connected ->
                    context.getString(R.string.sync_clipboard_service_text_connected)
                SyncClipboardManager.SyncState.Connecting ->
                    context.getString(R.string.sync_clipboard_service_text_connecting)
                SyncClipboardManager.SyncState.WaitingNetwork ->
                    context.getString(R.string.sync_clipboard_service_text_waiting_network)
                SyncClipboardManager.SyncState.PausedScreenOff ->
                    context.getString(R.string.sync_clipboard_service_text_paused)
                SyncClipboardManager.SyncState.Disabled ->
                    context.getString(R.string.sync_clipboard_service_text_idle)
            }
            val launchIntent = Intent(context, MainActivity::class.java).apply {
                action = Intent.ACTION_RUN
                putExtra(MainActivity.EXTRA_SETTINGS_ROUTE, SettingsRoute.SyncClipboard)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            }
            val pendingIntent = PendingIntent.getActivity(
                context,
                0,
                launchIntent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
            return NotificationCompat.Builder(context, CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_baseline_sync_24)
                .setContentTitle(context.getString(R.string.sync_clipboard_service_title))
                .setContentText(contentText)
                .setPriority(NotificationCompat.PRIORITY_MIN)
                .setCategory(NotificationCompat.CATEGORY_SERVICE)
                .setSilent(true)
                .setOngoing(true)
                .setShowWhen(false)
                .setContentIntent(pendingIntent)
                .build()
        }
    }
}
