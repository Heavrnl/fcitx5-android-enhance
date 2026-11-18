/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.ui.main.settings.behavior

import android.Manifest
import android.app.AlertDialog
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.View
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.preference.Preference
import androidx.preference.PreferenceScreen
import kotlinx.coroutines.launch
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreference
import org.fcitx.fcitx5.android.data.prefs.ManagedPreferenceFragment
import org.fcitx.fcitx5.android.data.syncclipboard.SyncClipboardManager
import org.fcitx.fcitx5.android.ui.main.SyncLogActivity
import org.fcitx.fcitx5.android.utils.BatteryOptimizationHelper
import org.fcitx.fcitx5.android.utils.InputMethodUtil
import org.fcitx.fcitx5.android.utils.addPreference
import org.fcitx.fcitx5.android.utils.setup
import org.fcitx.fcitx5.android.utils.startActivity
import org.fcitx.fcitx5.android.utils.toast

class SyncClipboardSettingsFragment : ManagedPreferenceFragment(AppPrefs.getInstance().syncClipboard) {

    private val prefs = AppPrefs.getInstance().syncClipboard

    private var statusPreference: Preference? = null
    private var batteryPreference: Preference? = null

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        if (!isGranted) {
            prefs.screenshotDetection.setValue(false)
        }
    }

    private val screenshotDetectionListener = ManagedPreference.OnChangeListener<Boolean> { _, enabled ->
        if (enabled && !hasMediaPermission()) {
            requestMediaPermission()
        }
    }

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        super.onCreatePreferences(savedInstanceState, rootKey)
        prefs.screenshotDetection.registerOnChangeListener(screenshotDetectionListener)
    }

    override fun onPreferenceUiCreated(screen: PreferenceScreen) {
        super.onPreferenceUiCreated(screen)
        val ctx = requireContext()

        statusPreference = Preference(ctx).apply {
            order = -1
            setup(ctx.getString(R.string.sync_clipboard_status), onClick = {
                if (prefs.enabled.getValue()) {
                    SyncClipboardManager.triggerManualSync()
                    ctx.toast(R.string.sync_clipboard_manual_sync_triggered)
                }
            })
        }.also { screen.addPreference(it) }

        batteryPreference = Preference(ctx).apply {
            setup(ctx.getString(R.string.sync_clipboard_battery_optimization), onClick = {
                BatteryOptimizationHelper.handleBatteryPreferenceClick(requireContext())
            })
        }.also { screen.addPreference(it) }

        // 排查"同步偶尔失效"的入口：查看落盘的本地日志（进程被杀后依然保留）
        screen.addPreference(R.string.sync_log, R.string.sync_log_desc) {
            startActivity<SyncLogActivity>()
        }

        updateStatusSummary(SyncClipboardManager.syncState.value)
        updateBatterySummary()
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        viewLifecycleOwner.lifecycleScope.launch {
            viewLifecycleOwner.repeatOnLifecycle(Lifecycle.State.STARTED) {
                SyncClipboardManager.syncState.collect { state ->
                    updateStatusSummary(state)
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        if (prefs.screenshotDetection.getValue() && !hasMediaPermission()) {
            showPermissionDialog()
        }
        updateStatusSummary(SyncClipboardManager.syncState.value)
        updateBatterySummary()
    }

    override fun onDestroyView() {
        statusPreference = null
        batteryPreference = null
        super.onDestroyView()
    }

    override fun onDestroy() {
        prefs.screenshotDetection.unregisterOnChangeListener(screenshotDetectionListener)
        super.onDestroy()
    }

    private fun updateStatusSummary(state: SyncClipboardManager.SyncState) {
        val pref = statusPreference ?: return
        val base = when (state) {
            SyncClipboardManager.SyncState.Disabled -> getString(R.string.sync_clipboard_status_disabled)
            SyncClipboardManager.SyncState.Connected -> getString(R.string.sync_clipboard_status_connected)
            SyncClipboardManager.SyncState.Connecting -> getString(R.string.sync_clipboard_status_connecting)
            SyncClipboardManager.SyncState.WaitingNetwork -> getString(R.string.sync_clipboard_status_waiting_network)
            SyncClipboardManager.SyncState.PausedScreenOff -> getString(R.string.sync_clipboard_status_paused_screen_off)
        }
        val summary = if (prefs.enabled.getValue() && !InputMethodUtil.isSelected()) {
            base + getString(R.string.sync_clipboard_status_not_default_ime_suffix)
        } else {
            base
        }
        pref.summary = summary
    }

    private fun updateBatterySummary() {
        batteryPreference?.summary = BatteryOptimizationHelper.getStatusSummary(requireContext())
    }

    private fun hasMediaPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ContextCompat.checkSelfPermission(
                requireContext(),
                Manifest.permission.READ_MEDIA_IMAGES
            ) == PackageManager.PERMISSION_GRANTED
        } else {
            ContextCompat.checkSelfPermission(
                requireContext(),
                Manifest.permission.READ_EXTERNAL_STORAGE
            ) == PackageManager.PERMISSION_GRANTED
        }
    }

    private fun requestMediaPermission() {
        val permission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            Manifest.permission.READ_MEDIA_IMAGES
        } else {
            Manifest.permission.READ_EXTERNAL_STORAGE
        }

        if (shouldShowRequestPermissionRationale(permission)) {
            showPermissionDialog()
        } else {
            permissionLauncher.launch(permission)
        }
    }

    private fun showPermissionDialog() {
        AlertDialog.Builder(requireContext())
            .setTitle(R.string.sync_clipboard_media_permission_title)
            .setMessage(R.string.sync_clipboard_media_permission_desc)
            .setPositiveButton(R.string.grant_permission) { _, _ ->
                val permission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    Manifest.permission.READ_MEDIA_IMAGES
                } else {
                    Manifest.permission.READ_EXTERNAL_STORAGE
                }
                permissionLauncher.launch(permission)
            }
            .setNegativeButton(android.R.string.cancel) { _, _ ->
                prefs.screenshotDetection.setValue(false)
            }
            .setCancelable(false)
            .show()
    }
}
