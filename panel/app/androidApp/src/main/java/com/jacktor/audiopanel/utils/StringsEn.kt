package com.jacktor.audiopanel.utils

/**
 * English Language Definition File (Purely Separated)
 */
object StringsEn : AppDictionary {
    override val tabAudioMedia: String = "AUDIO & MEDIA"
    override val tabSystemAmp: String = "SYSTEM & AMP"

    override val channelStereo: String = "STEREO (L/R)"
    override val channelLeft: String = "LEFT (L-CH)"
    override val channelRight: String = "RIGHT (R-CH)"
    override val channelMono: String = "MONO (L+R)"
    override val channelModeTitle: String = "Audio Channel Routing (R/L)"
    override val channelModeDesc: String = "Stereo channel routing configuration (Stereo, Left, Right, Mono downmix)."

    override fun channelButtonText(mode: Int): String {
        return when (mode) {
            1 -> "CH: LEFT (L)"
            2 -> "CH: RIGHT (R)"
            3 -> "CH: MONO"
            else -> "CH: STEREO"
        }
    }

    override val settingsTabPanel: String = "PANEL & SYSTEM"
    override val settingsTabAudio: String = "AUDIO & DSP"
    override val settingsTabAmp: String = "AMPLIFIER & BT"
    override val settingsTabAnalyzer: String = "ANALYZER & ADC"
    override val settingsTabThermal: String = "THERMAL & HW"

    override val nowPlaying: String = "NOW PLAYING"
    override val audioStandby: String = "AUDIO STANDBY"
    override val noTrackTitle: String = "No Music Playing"
    override val noTrackArtist: String = "Audio Engine Standby"
    override val bassBoost: String = "BASS BOOST"
    override val sound3d: String = "3D AUDIO"
    override val pairing: String = "PAIRING"

    override val confirmDangerTitle: String = "Dangerous Action Confirmation"
    override val enterPassword: String = "Enter System Password (root):"
    override val cancel: String = "CANCEL"
    override val confirm: String = "CONFIRM"
    override val forgotPassword: String = "Forgot Password?"
    override val resetPasswordDefault: String = "Reset to Default (furry2026)"

    override val appLanguageTitle: String = "APP LANGUAGE (BAHASA APLIKASI)"
    override val selectLanguage: String = "Select Panel Interface Language:"

    // Screen Power & Auto-Standby
    override val screenPowerTitle: String = "SCREEN POWER & AUTO-STANDBY"
    override val screenPowerModeLabel: String = "Screen Always-On Mode:"
    override val screenModeAlways: String = "Always On (24/7)"
    override val screenModeNormal: String = "Normal (1m)"
    override val screenModeAuto: String = "Auto (Amp Active)"
    override val screenModeAutoDesc: String = "Auto: Screen stays awake during amplifier runtime, and reverts to normal 1m sleep timeout when standby."
    override val lockscreenDisabledLabel: String = "Disable Android Lock Screen (Instant Display)"

    // Panel & System Settings
    override val panelKernelTitle: String = "PANEL SYSTEM & KERNEL"
    override val orientationResolutionTitle: String = "DISPLAY ORIENTATION & RESOLUTION"
    override val panelRotationLabel: String = "Panel Display Rotation:"
    override val resolutionDensityPresetLabel: String = "Resolution & Density Presets (DPI):"
    override val batteryCpuTitle: String = "BATTERY POWER & CPU PERFORMANCE PROFILE"
    override val batteryBypassLabel: String = "24/7 Battery AC Bypass (Battery Protection & Charger Bypass)"
    override val usbAutosuspendLabel: String = "USB Autosuspend (Anti-Dropout Audio Buffer)"
    override val cpuProfileLabel: String = "CPU Performance Profile & Thermal Governor:"
    override val sshAdminTitle: String = "OPENSSH ADMINISTRATION SERVICE (PORT 22)"
    override val sshServiceLabel: String = "OpenSSH Daemon Service (Port 22)"
    override val sshAutostartLabel: String = "Auto-start on Boot (service.sh)"
    override val launcherBootTitle: String = "DEFAULT HOME LAUNCHER & BOOT TERMINAL"
    override val showBootTerminalLabel: String = "Show Boot Terminal on Amplifier Power-On"
    override val securityPasswordTitle: String = "SECURITY & PASSWORDS (ROOT & SSH)"
}
