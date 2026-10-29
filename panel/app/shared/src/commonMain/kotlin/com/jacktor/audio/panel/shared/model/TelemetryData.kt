package com.jacktor.audio.panel.shared.model

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class TelemetryPacket(
    val type: String? = null,
    val rt: RealtimeTelemetry? = null,
    val hz1: SlowTelemetry? = null,
    val origin: String? = null,
    val commands: List<String>? = null
)

@Serializable
data class RealtimeTelemetry(
    val mode: String? = "off",
    @SerialName("bands_len") val bandsLen: Int = 32,
    val vu: Int = 0,
    @SerialName("vu_l") val vuL: Int = 0,
    @SerialName("vu_r") val vuR: Int = 0,
    val clipping: Boolean = false,
    @SerialName("clip_count") val clipCount: Int = 0,
    @SerialName("update_ms") val updateMs: Int = 50,
    @SerialName("bands_b64") val bandsB64: String? = null,
    val link: LinkStatus? = null,
    val input: String? = null,
    @SerialName("bt_state") val btState: String? = null
)

@Serializable
data class LinkStatus(
    val alive: Boolean = false,
    @SerialName("rx_ms") val rxMs: Long? = null,
    @SerialName("tx_ms") val txMs: Long? = null
)

@Serializable
data class SlowTelemetry(
    val time: String? = null,
    @SerialName("fw_ver") val fwVer: String? = null,
    @SerialName("ota_ready") val otaReady: Boolean = false,
    val smps: SmpsData? = null,
    @SerialName("sleep_timer") val sleepTimer: Int = 0,
    @SerialName("v_stby") val vStby: Float = 0f,
    @SerialName("v_vdd") val vVdd: Float = 0f,
    @SerialName("v_esp") val vEsp: Float = 0f,
    @SerialName("heat_c") val heatC: Float? = null,
    @SerialName("rtc_c") val rtcC: Float? = null,
    val inputs: InputsData? = null,
    val states: SystemStates? = null,
    val errors: List<String> = emptyList(),
    val warnings: List<String> = emptyList(),
    val nvs: NvsSettings? = null
)

@Serializable
data class SmpsData(
    val v: Float = 0f,
    val stage: String? = "standby",
    val cutoff: Float = 18f,
    val recover: Float = 20f
)

@Serializable
data class InputsData(
    val bt: Boolean = false
)

@Serializable
data class SystemStates(
    val on: Boolean = false,
    val standby: Boolean = true,
    val initializing: Boolean = false,
    @SerialName("bt_failed_perm") val btFailedPerm: Boolean = false
)

@Serializable
data class NvsSettings(
    @SerialName("fan_mode") val fanMode: Int = 0,
    @SerialName("fan_mode_str") val fanModeStr: String = "auto",
    @SerialName("fan_duty") val fanDuty: Int = 50,
    @SerialName("amp_mode") val ampMode: String = "normal",
    @SerialName("audio_relay") val audioRelay: Boolean = true,
    @SerialName("spk_pwr") val spkPwr: Boolean = true,
    @SerialName("bt_en") val btEn: Boolean = true,
    @SerialName("bt_autooff") val btAutooff: Long = 300000,
    @SerialName("auto_sense_en") val autoSenseEn: Boolean = true,
    @SerialName("auto_sense_timeout") val autoSenseTimeout: Int = 60,
    @SerialName("auto_sense_thresh") val autoSenseThresh: Int = 10,
    @SerialName("btn_lock") val btnLock: Boolean = false,
    @SerialName("smps_bypass") val smpsBypass: Boolean = false,
    @SerialName("smps_cut") val smpsCut: Float = 18f,
    @SerialName("smps_rec") val smpsRec: Float = 20f,
    @SerialName("buzz_enabled") val buzzEnabled: Boolean = true,
    @SerialName("buzz_volume") val buzzVolume: Int = 80
)
