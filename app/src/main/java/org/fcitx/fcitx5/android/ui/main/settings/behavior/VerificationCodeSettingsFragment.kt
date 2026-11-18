/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.ui.main.settings.behavior

import android.app.AlertDialog
import android.app.AppOpsManager
import android.content.ClipData
import android.content.ComponentName
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Process
import android.provider.Settings
import androidx.core.content.getSystemService
import androidx.preference.Preference
import androidx.preference.PreferenceScreen
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreference
import org.fcitx.fcitx5.android.data.prefs.ManagedPreferenceFragment
import org.fcitx.fcitx5.android.data.verification.VerificationCodeManager
import org.fcitx.fcitx5.android.data.verification.VerificationCodeNotificationListener
import org.fcitx.fcitx5.android.utils.BatteryOptimizationHelper
import org.fcitx.fcitx5.android.utils.clipboardManager
import org.fcitx.fcitx5.android.utils.setup
import org.fcitx.fcitx5.android.utils.toast

class VerificationCodeSettingsFragment : ManagedPreferenceFragment(AppPrefs.getInstance().verificationCode) {

    private val prefs = AppPrefs.getInstance().verificationCode

    private var batteryPreference: Preference? = null
    private var sensitivePermPreference: Preference? = null

    private val enabledListener = ManagedPreference.OnChangeListener<Boolean> { _, enabled ->
        if (enabled && !isNotificationAccessGranted()) {
            showNotificationAccessDialog()
        }
    }

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        super.onCreatePreferences(savedInstanceState, rootKey)
        prefs.enabled.registerOnChangeListener(enabledListener)
    }

    override fun onPreferenceUiCreated(screen: PreferenceScreen) {
        super.onPreferenceUiCreated(screen)
        val ctx = requireContext()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
            sensitivePermPreference = Preference(ctx).apply {
                setup(ctx.getString(R.string.verification_code_sensitive_permission_title), onClick = {
                    showSensitivePermissionDialog()
                })
            }.also { screen.addPreference(it) }
        }

        batteryPreference = Preference(ctx).apply {
            setup(ctx.getString(R.string.sync_clipboard_battery_optimization), onClick = {
                BatteryOptimizationHelper.handleBatteryPreferenceClick(requireContext())
            })
        }.also { screen.addPreference(it) }

        updateDiagnosticSummaries()
    }

    override fun onResume() {
        super.onResume()
        if (prefs.enabled.getValue() && !isNotificationAccessGranted()) {
            showNotificationAccessDialog()
        } else {
            VerificationCodeManager.ensureListenerBound()
        }
        updateDiagnosticSummaries()
    }

    override fun onDestroyView() {
        batteryPreference = null
        sensitivePermPreference = null
        super.onDestroyView()
    }

    override fun onDestroy() {
        prefs.enabled.unregisterOnChangeListener(enabledListener)
        super.onDestroy()
    }

    private fun updateDiagnosticSummaries() {
        batteryPreference?.summary = BatteryOptimizationHelper.getStatusSummary(requireContext())
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
            sensitivePermPreference?.summary = if (isSensitiveNotificationAllowed()) {
                getString(R.string.verification_code_sensitive_permission_ok)
            } else {
                getString(R.string.verification_code_sensitive_permission_blocked)
            }
        }
    }

    private fun isSensitiveNotificationAllowed(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM) return true
        val ctx = requireContext()
        val appOps = ctx.getSystemService<AppOpsManager>() ?: return true
        return runCatching {
            val mode = appOps.unsafeCheckOpNoThrow(
                "android:receive_sensitive_notifications",
                Process.myUid(),
                ctx.packageName
            )
            mode == AppOpsManager.MODE_ALLOWED
        }.getOrDefault(false)
    }

    private fun showSensitivePermissionDialog() {
        val ctx = requireContext()
        val adbCommand = "adb shell cmd appops set --user 0 ${ctx.packageName} RECEIVE_SENSITIVE_NOTIFICATIONS allow"
        AlertDialog.Builder(ctx)
            .setTitle(R.string.verification_code_sensitive_permission_title)
            .setMessage(getString(R.string.verification_code_sensitive_permission_dialog_msg, adbCommand))
            .setPositiveButton(R.string.copy_command) { _, _ ->
                runCatching {
                    ctx.clipboardManager.setPrimaryClip(ClipData.newPlainText("adb", adbCommand))
                    ctx.toast(R.string.copied_to_clipboard)
                }
            }
            .setNegativeButton(android.R.string.ok, null)
            .show()
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
            val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
                data = Uri.parse("package:${requireContext().packageName}")
            }
            startActivity(intent)
        }
    }
}
