package org.fcitx.fcitx5.android.data.quickphrase.db

import androidx.room.Dao
import androidx.room.Delete
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import androidx.room.Update
import kotlinx.coroutines.flow.Flow

@Dao
interface QuickPhraseDao {
    @Query("SELECT * FROM quickphrase_category ORDER BY sortOrder ASC")
    suspend fun getAllCategories(): List<QuickPhraseCategory>

    @Query("SELECT * FROM quickphrase_category ORDER BY sortOrder ASC")
    fun getAllCategoriesFlow(): Flow<List<QuickPhraseCategory>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertCategory(category: QuickPhraseCategory): Long

    @Update
    suspend fun updateCategory(category: QuickPhraseCategory)

    @Delete
    suspend fun deleteCategory(category: QuickPhraseCategory)

    @Query("DELETE FROM quickphrase_item WHERE categoryId = :categoryId")
    suspend fun deleteItemsByCategoryId(categoryId: Int)

    @androidx.room.Transaction
    suspend fun deleteCategoryAndItems(category: QuickPhraseCategory) {
        deleteItemsByCategoryId(category.id)
        deleteCategory(category)
    }

    @Query("SELECT * FROM quickphrase_item WHERE categoryId = :categoryId ORDER BY lastUsed DESC")
    suspend fun getItemsByCategoryId(categoryId: Int): List<QuickPhraseItem>

    @Query("SELECT * FROM quickphrase_item WHERE categoryId = :categoryId ORDER BY lastUsed DESC")
    fun getItemsByCategoryIdFlow(categoryId: Int): Flow<List<QuickPhraseItem>>

    @Query("SELECT * FROM quickphrase_item WHERE lastUsed > 0 ORDER BY lastUsed DESC LIMIT :limit")
    fun getRecentItemsFlow(limit: Int): Flow<List<QuickPhraseItem>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertItem(item: QuickPhraseItem): Long

    @Update
    suspend fun updateItem(item: QuickPhraseItem)

    @Delete
    suspend fun deleteItem(item: QuickPhraseItem)
    
    @Query("UPDATE ${QuickPhraseItem.TABLE_NAME} SET lastUsed = :timestamp WHERE id = :itemId")
    suspend fun updateItemLastUsedTime(itemId: Int, timestamp: Long)

    @Query("SELECT COUNT(*) FROM ${QuickPhraseCategory.TABLE_NAME}")
    suspend fun getCategoryCount(): Int

    @Query("SELECT * FROM ${QuickPhraseItem.TABLE_NAME} ORDER BY lastUsed DESC")
    fun getAllItemsFlow(): Flow<List<QuickPhraseItem>>
}
