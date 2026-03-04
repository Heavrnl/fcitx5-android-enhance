/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.syncclipboard

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * SyncClipboard.json 数据模型
 * 参考: https://github.com/Jeric-X/SyncClipboard
 */
@Serializable
data class SyncClipboardData(
    @SerialName("type")
    val type: String = "Text",
    @SerialName("text")
    val text: String = "",
    @SerialName("dataName")
    val dataName: String = "",
    @SerialName("hash")
    val hash: String = "",
    @SerialName("hasData")
    val hasData: Boolean = false,
    @SerialName("size")
    val size: Long = 0
) {
    companion object {
        const val TYPE_TEXT = "Text"
        const val TYPE_IMAGE = "Image"
        const val TYPE_FILE = "File"
        const val TYPE_GROUP = "Group"

        fun text(content: String) = SyncClipboardData(
            type = TYPE_TEXT,
            text = content
        )

        fun image(hash: String, filename: String) = SyncClipboardData(
            type = TYPE_IMAGE,
            hash = hash,
            dataName = filename,
            hasData = true
        )
    }
}
