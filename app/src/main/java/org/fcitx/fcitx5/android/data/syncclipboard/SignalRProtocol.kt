/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: Copyright 2024 Fcitx5 for Android Contributors
 */
package org.fcitx.fcitx5.android.data.syncclipboard

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.JsonElement

/**
 * SignalR JSON 协议数据模型
 * 参考: https://github.com/dotnet/aspnetcore/blob/main/src/SignalR/docs/specs/HubProtocol.md
 */

/**
 * SignalR 握手请求
 */
@Serializable
data class SignalRHandshakeRequest(
    val protocol: String = "json",
    val version: Int = 1
)

/**
 * SignalR 握手响应
 */
@Serializable
data class SignalRHandshakeResponse(
    val error: String? = null
)

/**
 * SignalR 消息类型
 */
object SignalRMessageType {
    const val INVOCATION = 1
    const val STREAM_ITEM = 2
    const val COMPLETION = 3
    const val STREAM_INVOCATION = 4
    const val CANCEL_INVOCATION = 5
    const val PING = 6
    const val CLOSE = 7
}

/**
 * SignalR 调用消息
 */
@Serializable
data class SignalRInvocationMessage(
    val type: Int,
    val target: String? = null,
    val arguments: List<JsonElement>? = null,
    val invocationId: String? = null
)

/**
 * SignalR Ping 消息
 */
@Serializable
data class SignalRPingMessage(
    val type: Int = SignalRMessageType.PING
)

/**
 * SignalR Close 消息
 */
@Serializable
data class SignalRCloseMessage(
    val type: Int = SignalRMessageType.CLOSE,
    val error: String? = null,
    val allowReconnect: Boolean? = null
)

/**
 * 服务器推送的 ProfileDto
 * 对应 SyncClipboard.Shared.ProfileDto
 */
@Serializable
data class ProfileDto(
    @SerialName("type")
    val type: String = "Text",
    @SerialName("hash")
    val hash: String? = null,
    @SerialName("text")
    val text: String? = null,
    @SerialName("hasData")
    val hasData: Boolean = false,
    @SerialName("dataName")
    val dataName: String? = null,
    @SerialName("size")
    val size: Long = 0
) {
    companion object {
        const val TYPE_TEXT = "Text"
        const val TYPE_IMAGE = "Image"
        const val TYPE_FILE = "File"
        const val TYPE_GROUP = "Group"
    }
}
