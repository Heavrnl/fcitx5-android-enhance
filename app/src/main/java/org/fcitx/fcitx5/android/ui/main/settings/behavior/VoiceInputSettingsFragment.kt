package org.fcitx.fcitx5.android.ui.main.settings.behavior

import android.os.Bundle
import android.text.method.LinkMovementMethod
import android.widget.TextView
import androidx.core.text.HtmlCompat
import androidx.preference.PreferenceFragmentCompat
import org.fcitx.fcitx5.android.R
import org.fcitx.fcitx5.android.data.prefs.AppPrefs
import org.fcitx.fcitx5.android.data.prefs.ManagedPreferenceFragment

class VoiceInputSettingsFragment : ManagedPreferenceFragment(AppPrefs.getInstance().voiceInput) {
    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        super.onCreatePreferences(savedInstanceState, rootKey)
        // 为 API Key 设置带可点击链接的 summary
        findPreference<androidx.preference.Preference>("dashscope_api_key")?.apply {
            // 先清除已有的 SummaryProvider，否则直接 setSummary 会抛异常
            summaryProvider = null
            val displayUrl = "https://modelstudio.console.alibabacloud.com"
            val targetUrl = "https://modelstudio.console.alibabacloud.com/ap-southeast-1/?tab=dashboard#/api-key"
            summary = HtmlCompat.fromHtml(
                getString(R.string.dashscope_api_key_summary)
                    .replace(displayUrl, "<a href=\"$targetUrl\">$displayUrl</a>"),
                HtmlCompat.FROM_HTML_MODE_COMPACT
            )
        }
    }

    override fun onStart() {
        super.onStart()
        // 使 summary 中的链接可点击
        view?.let { v ->
            v.post {
                val recyclerView = listView ?: return@post
                for (i in 0 until recyclerView.childCount) {
                    val holder = recyclerView.getChildViewHolder(recyclerView.getChildAt(i))
                    val summaryView = holder.itemView.findViewById<TextView>(android.R.id.summary)
                    summaryView?.movementMethod = LinkMovementMethod.getInstance()
                }
            }
        }
    }
}
