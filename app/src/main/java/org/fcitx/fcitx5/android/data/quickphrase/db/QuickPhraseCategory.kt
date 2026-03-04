package org.fcitx.fcitx5.android.data.quickphrase.db

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = QuickPhraseCategory.TABLE_NAME)
data class QuickPhraseCategory(
    @PrimaryKey(autoGenerate = true)
    val id: Int = 0,
    val name: String,
    val sortOrder: Int = 0
) {
    companion object {
        const val TABLE_NAME = "quickphrase_category"
    }
}
