package com.jacktor.audiopanel.utils

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.staticCompositionLocalOf

/**
 * Supported Languages for Jacktor Android Panel
 */
enum class AppLanguage(val code: String, val displayName: String, val flag: String) {
    INDONESIAN("id", "Bahasa Indonesia", "🇮🇩"),
    ENGLISH("en", "English", "🇬🇧");

    companion object {
        fun fromCode(code: String?): AppLanguage {
            return when (code?.lowercase()) {
                "en" -> ENGLISH
                else -> INDONESIAN
            }
        }
    }
}

/**
 * Dynamic Language Controller for Compose
 */
object LanguageController {
    val currentLanguage = mutableStateOf(AppLanguage.INDONESIAN)

    fun init(context: Context) {
        val prefs = context.getSharedPreferences("JacktorPanelPrefs", Context.MODE_PRIVATE)
        val code = prefs.getString("app_language", "id")
        currentLanguage.value = AppLanguage.fromCode(code)
    }

    fun setLanguage(context: Context, lang: AppLanguage) {
        currentLanguage.value = lang
        val prefs = context.getSharedPreferences("JacktorPanelPrefs", Context.MODE_PRIVATE)
        prefs.edit().putString("app_language", lang.code).apply()
    }
}

val LocalAppLanguage = staticCompositionLocalOf { AppLanguage.INDONESIAN }

/**
 * Universal Dual-Language Dictionary for Jacktor Panel UI
 */
object PanelStrings {
    fun get(id: String, en: String, lang: AppLanguage = LanguageController.currentLanguage.value): String {
        return if (lang == AppLanguage.INDONESIAN) id else en
    }

    // Tabs
    fun tabAudioMedia(lang: AppLanguage = LanguageController.currentLanguage.value) = get("AUDIO & MEDIA", "AUDIO & MEDIA", lang)
    fun tabSystemAmp(lang: AppLanguage = LanguageController.currentLanguage.value) = get("SISTEM & AMP", "SYSTEM & AMP", lang)

    // Channel Routing (R/L)
    fun channelStereo(lang: AppLanguage = LanguageController.currentLanguage.value) = get("STEREO (L/R)", "STEREO (L/R)", lang)
    fun channelLeft(lang: AppLanguage = LanguageController.currentLanguage.value) = get("KIRI (L-CH)", "LEFT (L-CH)", lang)
    fun channelRight(lang: AppLanguage = LanguageController.currentLanguage.value) = get("KANAN (R-CH)", "RIGHT (R-CH)", lang)
    fun channelMono(lang: AppLanguage = LanguageController.currentLanguage.value) = get("MONO (L+R)", "MONO (L+R)", lang)
    fun channelModeTitle(lang: AppLanguage = LanguageController.currentLanguage.value) = get("Routing Kanal Audio (R/L)", "Audio Channel Routing (R/L)", lang)
    fun channelModeDesc(lang: AppLanguage = LanguageController.currentLanguage.value) = get("Perutean kanal audio stereo (Stereo, Kiri, Kanan, Mono downmix).", "Stereo channel routing configuration (Stereo, Left, Right, Mono downmix).", lang)

    fun channelButtonText(mode: Int, lang: AppLanguage = LanguageController.currentLanguage.value): String {
        return when (mode) {
            1 -> get("CH: KIRI (L)", "CH: LEFT (L)", lang)
            2 -> get("CH: KANAN (R)", "CH: RIGHT (R)", lang)
            3 -> get("CH: MONO", "CH: MONO", lang)
            else -> "CH: STEREO"
        }
    }

    // Settings Tabs
    fun settingsTabPanel(lang: AppLanguage = LanguageController.currentLanguage.value) = get("PANEL & SISTEM", "PANEL & SYSTEM", lang)
    fun settingsTabAudio(lang: AppLanguage = LanguageController.currentLanguage.value) = get("AUDIO & DSP", "AUDIO & DSP", lang)
    fun settingsTabAmp(lang: AppLanguage = LanguageController.currentLanguage.value) = get("AMPLIFIER & BT", "AMPLIFIER & BT", lang)
    fun settingsTabAnalyzer(lang: AppLanguage = LanguageController.currentLanguage.value) = get("ANALYZER & ADC", "ANALYZER & ADC", lang)
    fun settingsTabThermal(lang: AppLanguage = LanguageController.currentLanguage.value) = get("TERMAL & HW", "THERMAL & HW", lang)

    // Media & Playback Card
    fun nowPlaying(lang: AppLanguage = LanguageController.currentLanguage.value) = get("MEMUTAR MUSIK", "NOW PLAYING", lang)
    fun audioStandby(lang: AppLanguage = LanguageController.currentLanguage.value) = get("AUDIO STANDBY", "AUDIO STANDBY", lang)
    fun noTrackTitle(lang: AppLanguage = LanguageController.currentLanguage.value) = get("Tidak Ada Musik Diputar", "No Music Playing", lang)
    fun noTrackArtist(lang: AppLanguage = LanguageController.currentLanguage.value) = get("Audio Engine Standby", "Audio Engine Standby", lang)
    fun bassBoost(lang: AppLanguage = LanguageController.currentLanguage.value) = "BASS BOOST"
    fun sound3d(lang: AppLanguage = LanguageController.currentLanguage.value) = "3D AUDIO"
    fun pairing(lang: AppLanguage = LanguageController.currentLanguage.value) = get("PAIRING", "PAIRING", lang)

    // Dialog & Dangerous Actions
    fun confirmDangerTitle(lang: AppLanguage = LanguageController.currentLanguage.value) = get("Konfirmasi Tindakan Berbahaya", "Dangerous Action Confirmation", lang)
    fun enterPassword(lang: AppLanguage = LanguageController.currentLanguage.value) = get("Masukkan Password Sistem (root):", "Enter System Password (root):", lang)
    fun cancel(lang: AppLanguage = LanguageController.currentLanguage.value) = get("BATAL", "CANCEL", lang)
    fun confirm(lang: AppLanguage = LanguageController.currentLanguage.value) = get("KONFIRMASI", "CONFIRM", lang)
    fun forgotPassword(lang: AppLanguage = LanguageController.currentLanguage.value) = get("Lupa Password?", "Forgot Password?", lang)
    fun resetPasswordDefault(lang: AppLanguage = LanguageController.currentLanguage.value) = get("Reset ke Default (furry2026)", "Reset to Default (furry2026)", lang)

    // Language Section in Settings
    fun appLanguageTitle(lang: AppLanguage = LanguageController.currentLanguage.value) = get("BAHASA APLIKASI (APP LANGUAGE)", "APP LANGUAGE (BAHASA APLIKASI)", lang)
    fun selectLanguage(lang: AppLanguage = LanguageController.currentLanguage.value) = get("Pilih Bahasa Antarmuka Panel:", "Select Panel Interface Language:", lang)
}
