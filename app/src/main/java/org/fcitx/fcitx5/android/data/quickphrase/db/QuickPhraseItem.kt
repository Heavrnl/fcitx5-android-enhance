package org.fcitx.fcitx5.android.data.quickphrase.db

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = QuickPhraseItem.TABLE_NAME)
data class QuickPhraseItem(
    @PrimaryKey(autoGenerate = true)
    val id: Int = 0,
    val categoryId: Int,
    val title: String?,
    val content: String,
    val shortcut: String = "",
    val lastModified: Long = System.currentTimeMillis(),
    val lastUsed: Long = 0
) {
    companion object {
        const val TABLE_NAME = "quickphrase_item"
    }
}
