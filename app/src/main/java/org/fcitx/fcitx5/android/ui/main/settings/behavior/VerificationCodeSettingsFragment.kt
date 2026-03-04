/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.ui.main.settings.behavior

import android.app.AlertDialog
import android.content.ComponentName
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import androidx.core.app.NotificationManagerCompat
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreference
import org.fcitx.fcitx5.android.data.prefs.ManagedPreferenceFragment
import org.fcitx.fcitx5.android.data.verification.VerificationCodeNotificationListener

class VerificationCodeSettingsFragment : ManagedPreferenceFragment(AppPrefs.getInstance().verificationCode) {

    private val prefs = AppPrefs.getInstance().verificationCode

    private val enabledListener = ManagedPreference.OnChangeListener<Boolean> { _, enabled ->
        if (enabled && !isNotificationAccessGranted()) {
            showNotificationAccessDialog()
        }
    }

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        super.onCreatePreferences(savedInstanceState, rootKey)
        prefs.enabled.registerOnChangeListener(enabledListener)
    }

    override fun onResume() {
        super.onResume()
        // 检查权限状态
        if (prefs.enabled.getValue() && !isNotificationAccessGranted()) {
            showNotificationAccessDialog()
        }
    }

    override fun onDestroy() {
        prefs.enabled.unregisterOnChangeListener(enabledListener)
        super.onDestroy()
    }

    private fun isNotificationAccessGranted(): Boolean {
        val componentName = ComponentName(requireContext(), VerificationCodeNotificationListener::class.java)
        val enabledListeners = Settings.Secure.getString(
            requireContext().contentResolver,
            "enabled_notification_listeners"
        )
        return enabledListeners?.contains(componentName.flattenToString()) == true
    }

    private fun showNotificationAccessDialog() {
        AlertDialog.Builder(requireContext())
            .setTitle(R.string.verification_code_notification_access)
            .setMessage(R.string.verification_code_notification_access_desc)
            .setPositiveButton(R.string.verification_code_grant_access) { _, _ ->
                openNotificationAccessSettings()
            }
            .setNegativeButton(android.R.string.cancel) { _, _ ->
                // 用户取消，关闭功能
                prefs.enabled.setValue(false)
            }
            .setCancelable(false)
            .show()
    }

    private fun openNotificationAccessSettings() {
        try {
            val intent = Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
            startActivity(intent)
        } catch (e: Exception) {
            // 备选方案：打开应用详情
            val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
                data = android.net.Uri.parse("package:${requireContext().packageName}")
            }
            startActivity(intent)
        }
    }
}
