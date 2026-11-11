package com.jacktor.audiopanel.utils

import android.content.Context
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
 * Universal Dual-Language Dictionary Interface
 */
interface AppDictionary {
    val tabAudioMedia: String
    val tabSystemAmp: String
    val channelStereo: String
    val channelLeft: String
    val channelRight: String
    val channelMono: String
    val channelModeTitle: String
    val channelModeDesc: String
    fun channelButtonText(mode: Int): String

    val settingsTabPanel: String
    val settingsTabAudio: String
    val settingsTabAmp: String
    val settingsTabAnalyzer: String
    val settingsTabThermal: String

    val nowPlaying: String
    val audioStandby: String
    val noTrackTitle: String
    val noTrackArtist: String
    val bassBoost: String
    val sound3d: String
    val pairing: String

    val confirmDangerTitle: String
    val enterPassword: String
    val cancel: String
    val confirm: String
    val forgotPassword: String
    val resetPasswordDefault: String

    val appLanguageTitle: String
    val selectLanguage: String

    // Screen Power & Auto-Standby
    val screenPowerTitle: String
    val screenPowerModeLabel: String
    val screenModeAlways: String
    val screenModeNormal: String
    val screenModeAuto: String
    val screenModeAutoDesc: String
    val lockscreenDisabledLabel: String

    // Panel & System Settings
    val panelKernelTitle: String
    val orientationResolutionTitle: String
    val panelRotationLabel: String
    val resolutionDensityPresetLabel: String
    val batteryCpuTitle: String
    val batteryBypassLabel: String
    val usbAutosuspendLabel: String
    val cpuProfileLabel: String
    val sshAdminTitle: String
    val sshServiceLabel: String
    val sshAutostartLabel: String
    val launcherBootTitle: String
    val showBootTerminalLabel: String
    val securityPasswordTitle: String
}

/**
 * Universal Dual-Language Gateway for Jacktor Panel UI
 */
object PanelStrings {
    fun current(lang: AppLanguage = LanguageController.currentLanguage.value): AppDictionary {
        return if (lang == AppLanguage.INDONESIAN) StringsId else StringsEn
    }

    fun get(id: String, en: String, lang: AppLanguage = LanguageController.currentLanguage.value): String {
        return if (lang == AppLanguage.INDONESIAN) id else en
    }

    // Tabs
    fun tabAudioMedia(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).tabAudioMedia
    fun tabSystemAmp(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).tabSystemAmp

    // Channel Routing (R/L)
    fun channelStereo(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).channelStereo
    fun channelLeft(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).channelLeft
    fun channelRight(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).channelRight
    fun channelMono(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).channelMono
    fun channelModeTitle(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).channelModeTitle
    fun channelModeDesc(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).channelModeDesc
    fun channelButtonText(mode: Int, lang: AppLanguage = LanguageController.currentLanguage.value): String = current(lang).channelButtonText(mode)

    // Settings Tabs
    fun settingsTabPanel(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).settingsTabPanel
    fun settingsTabAudio(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).settingsTabAudio
    fun settingsTabAmp(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).settingsTabAmp
    fun settingsTabAnalyzer(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).settingsTabAnalyzer
    fun settingsTabThermal(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).settingsTabThermal

    // Media & Playback Card
    fun nowPlaying(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).nowPlaying
    fun audioStandby(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).audioStandby
    fun noTrackTitle(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).noTrackTitle
    fun noTrackArtist(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).noTrackArtist
    fun bassBoost(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).bassBoost
    fun sound3d(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).sound3d
    fun pairing(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).pairing

    // Dialog & Dangerous Actions
    fun confirmDangerTitle(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).confirmDangerTitle
    fun enterPassword(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).enterPassword
    fun cancel(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).cancel
    fun confirm(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).confirm
    fun forgotPassword(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).forgotPassword
    fun resetPasswordDefault(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).resetPasswordDefault

    // Language Section in Settings
    fun appLanguageTitle(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).appLanguageTitle
    fun selectLanguage(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).selectLanguage

    // Screen Power & Standby
    fun screenPowerTitle(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).screenPowerTitle
    fun screenPowerModeLabel(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).screenPowerModeLabel
    fun screenModeAlways(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).screenModeAlways
    fun screenModeNormal(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).screenModeNormal
    fun screenModeAuto(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).screenModeAuto
    fun screenModeAutoDesc(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).screenModeAutoDesc
    fun lockscreenDisabledLabel(lang: AppLanguage = LanguageController.currentLanguage.value) = current(lang).lockscreenDisabledLabel
}
