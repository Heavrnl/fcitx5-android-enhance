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
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreference
import org.fcitx.fcitx5.android.data.prefs.ManagedPreferenceFragment

class SyncClipboardSettingsFragment : ManagedPreferenceFragment(AppPrefs.getInstance().syncClipboard) {

    private val prefs = AppPrefs.getInstance().syncClipboard

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        if (!isGranted) {
            // Permission denied, disable the feature
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

    override fun onResume() {
        super.onResume()
        // Check permission status when returning to settings
        if (prefs.screenshotDetection.getValue() && !hasMediaPermission()) {
            showPermissionDialog()
        }
    }

    override fun onDestroy() {
        prefs.screenshotDetection.unregisterOnChangeListener(screenshotDetectionListener)
        super.onDestroy()
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
                // User cancelled, disable the feature
                prefs.screenshotDetection.setValue(false)
            }
            .setCancelable(false)
            .show()
    }
}
