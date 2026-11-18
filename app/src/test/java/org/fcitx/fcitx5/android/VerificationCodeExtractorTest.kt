/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2026 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android

import org.fcitx.fcitx5.android.data.verification.VerificationCodeExtractor
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class VerificationCodeExtractorTest {

    @Test
    fun `extracts numeric code from english notification without matching words`() {
        val text = "SMS Your verification code is 682941, valid for 5 minutes."
        assertEquals("682941", VerificationCodeExtractor.extractCodeLocal(text))
    }

    @Test
    fun `extracts numeric code from chinese notification`() {
        val text = "【测试平台】您的登录验证码是 682941，5分钟内有效。"
        assertEquals("682941", VerificationCodeExtractor.extractCodeLocal(text))
    }

    @Test
    fun `extracts numeric code from mixed english brand and chinese text`() {
        val text = "【GitHub】您的验证码为 520131，切勿泄露他人"
        assertEquals("520131", VerificationCodeExtractor.extractCodeLocal(text))
    }

    @Test
    fun `extracts mixed alphanumeric code cleanly`() {
        val text = "Your login OTP is A9F3K2, expires in 10 min."
        assertEquals("A9F3K2", VerificationCodeExtractor.extractCodeLocal(text))
    }

    @Test
    fun `ignores long order numbers and extracts closest code`() {
        val text = "订单号 20261118998877，验证码123456（10分钟内有效）"
        assertEquals("123456", VerificationCodeExtractor.extractCodeLocal(text))
    }

    @Test
    fun `returns null when no keyword present`() {
        val text = "您的快递已到达菜鸟驿站，取件码 991201"
        assertNull(VerificationCodeExtractor.extractCodeLocal(text))
    }
}
