package com.jacktor.audiopanel.utils

/**
 * Berkas Definisi Teks Bahasa Indonesia (Murni Terpisah)
 */
object StringsId : AppDictionary {
    override val tabAudioMedia: String = "AUDIO & MEDIA"
    override val tabSystemAmp: String = "SISTEM & AMP"

    override val channelStereo: String = "STEREO (L/R)"
    override val channelLeft: String = "KIRI (L-CH)"
    override val channelRight: String = "KANAN (R-CH)"
    override val channelMono: String = "MONO (L+R)"
    override val channelModeTitle: String = "Routing Kanal Audio (R/L)"
    override val channelModeDesc: String = "Perutean kanal audio stereo (Stereo, Kiri, Kanan, Mono downmix)."

    override fun channelButtonText(mode: Int): String {
        return when (mode) {
            1 -> "CH: KIRI (L)"
            2 -> "CH: KANAN (R)"
            3 -> "CH: MONO"
            else -> "CH: STEREO"
        }
    }

    override val settingsTabPanel: String = "PANEL & SISTEM"
    override val settingsTabAudio: String = "AUDIO & DSP"
    override val settingsTabAmp: String = "AMPLIFIER & BT"
    override val settingsTabAnalyzer: String = "ANALYZER & ADC"
    override val settingsTabThermal: String = "TERMAL & HW"

    override val nowPlaying: String = "MEMUTAR MUSIK"
    override val audioStandby: String = "AUDIO STANDBY"
    override val noTrackTitle: String = "Tidak Ada Musik Diputar"
    override val noTrackArtist: String = "Audio Engine Standby"
    override val bassBoost: String = "BASS BOOST"
    override val sound3d: String = "3D AUDIO"
    override val pairing: String = "PAIRING"

    override val confirmDangerTitle: String = "Konfirmasi Tindakan Berbahaya"
    override val enterPassword: String = "Masukkan Password Sistem (root):"
    override val cancel: String = "BATAL"
    override val confirm: String = "KONFIRMASI"
    override val forgotPassword: String = "Lupa Password?"
    override val resetPasswordDefault: String = "Reset ke Default (furry2026)"

    override val appLanguageTitle: String = "BAHASA APLIKASI (APP LANGUAGE)"
    override val selectLanguage: String = "Pilih Bahasa Antarmuka Panel:"

    // Daya Layar & Auto-Standby
    override val screenPowerTitle: String = "DAYA LAYAR & AUTO-STANDBY"
    override val screenPowerModeLabel: String = "Mode Daya Layar Selalu Hidup:"
    override val screenModeAlways: String = "Selalu Menyala (24/7)"
    override val screenModeNormal: String = "Normal (1 Menit)"
    override val screenModeAuto: String = "Auto (Amp Aktif)"
    override val screenModeAutoDesc: String = "Auto: Layar selalu hidup saat amplifier runtime, dan kembali ke normal (1 menit tidur) saat standby."
    override val lockscreenDisabledLabel: String = "Nonaktifkan Kunci Layar Android (Instant Display)"

    // Panel & System Settings
    override val panelKernelTitle: String = "SISTEM PANEL & KERNEL"
    override val orientationResolutionTitle: String = "ORIENTASI & RESOLUSI LAYAR"
    override val panelRotationLabel: String = "Rotasi Tampilan Panel:"
    override val resolutionDensityPresetLabel: String = "Preset Resolusi & Kepadatan Layar (DPI):"
    override val batteryCpuTitle: String = "DAYA BATERAI & PROFIL PERFORMA CPU"
    override val batteryBypassLabel: String = "24/7 Battery AC Bypass (Proteksi Baterai & Bypass Charger)"
    override val usbAutosuspendLabel: String = "USB Autosuspend (Anti-Dropout Audio Buffer)"
    override val cpuProfileLabel: String = "Profil Performa CPU & Thermal Governor:"
    override val sshAdminTitle: String = "LAYANAN ADMINISTRASI OPENSSH (PORT 22)"
    override val sshServiceLabel: String = "Layanan OpenSSH Daemon (Port 22)"
    override val sshAutostartLabel: String = "Mulai Otomatis Saat Booting (service.sh)"
    override val launcherBootTitle: String = "DEFAULT HOME LAUNCHER & TERMINAL BOOT"
    override val showBootTerminalLabel: String = "Tampilkan Terminal Boot Saat Menyalakan Amp"
    override val securityPasswordTitle: String = "KEAMANAN & KATA SANDI (ROOT & SSH)"
}
