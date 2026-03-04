/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.verification

import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.ManagedPreferenceEnum

/**
 * 验证码提取模式
 */
enum class VerificationCodeExtractionMode(override val stringRes: Int) : ManagedPreferenceEnum {
    Local(R.string.verification_code_mode_local),
    AI(R.string.verification_code_mode_ai);
}
