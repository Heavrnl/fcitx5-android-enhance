package org.fcitx.fcitx5.android.data.quickphrase.db

import androidx.room.Database
import androidx.room.RoomDatabase

@Database(
    entities = [QuickPhraseCategory::class, QuickPhraseItem::class],
    version = 2,
    exportSchema = false
)
abstract class QuickPhraseDatabase : RoomDatabase() {
    abstract fun quickPhraseDao(): QuickPhraseDao
}
