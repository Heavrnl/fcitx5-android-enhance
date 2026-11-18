/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2026 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.utils

import android.annotation.SuppressLint
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.PowerManager
import android.provider.Settings
import androidx.core.content.getSystemService
import org.fcitx.fcitx5.android.R

/**
 * 电池优化白名单检测与设置跳转辅助，供剪贴板同步与验证码提取设置页共用。
 */
object BatteryOptimizationHelper {

    fun isIgnoringBatteryOptimizations(context: Context): Boolean {
        val pm = context.getSystemService<PowerManager>() ?: return false
        return pm.isIgnoringBatteryOptimizations(context.packageName)
    }

    fun getStatusSummary(context: Context): String {
        return if (isIgnoringBatteryOptimizations(context)) {
            context.getString(R.string.sync_clipboard_battery_unrestricted)
        } else {
            context.getString(R.string.sync_clipboard_battery_restricted)
        }
    }

    fun handleBatteryPreferenceClick(context: Context) {
        if (isIgnoringBatteryOptimizations(context)) {
            openBatterySettingsFallback(context)
        } else {
            AlertDialog.Builder(context)
                .setTitle(R.string.sync_clipboard_battery_dialog_title)
                .setMessage(R.string.sync_clipboard_battery_dialog_message)
                .setPositiveButton(R.string.sync_clipboard_battery_dialog_confirm) { _, _ ->
                    requestIgnoreBatteryOptimizations(context)
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }
    }

    @SuppressLint("BatteryLife")
    private fun requestIgnoreBatteryOptimizations(context: Context) {
        val pkgUri = Uri.parse("package:${context.packageName}")
        val intents = listOf(
            Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, pkgUri),
            Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS),
            Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, pkgUri)
        )
        startFirstAvailable(context, intents)
    }

    private fun openBatterySettingsFallback(context: Context) {
        val pkgUri = Uri.parse("package:${context.packageName}")
        val intents = listOf(
            Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, pkgUri),
            Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)
        )
        startFirstAvailable(context, intents)
    }

    private fun startFirstAvailable(context: Context, intents: List<Intent>) {
        for (intent in intents) {
            if (runCatching { context.startActivity(intent); true }.getOrDefault(false)) {
                return
            }
        }
    }
}
