package com.jacktor.audiopanel.model

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class TelemetryPacket(
    val type: String? = null,
    val rt: RealtimeTelemetry? = null,
    val hz1: SlowTelemetry? = null,
    val data: AnalyzerConfigData? = null,
    val origin: String? = null,
    val commands: List<String>? = null,
    val step: Int? = null,
    val msg: String? = null,
    val status: String? = null,
    @SerialName("smps_v") val smpsV: Float? = null,
    val lvl: String? = null,
    val ok: Boolean? = null,
    val changed: String? = null,
    val results: SetupReportResults? = null,
    val count: Int? = null,
    val devices: List<TempScannedSensor>? = null,
    val sensors: List<TempScannedSensor>? = null,
    val assigned: TempAssignedRoms? = null
)

@Serializable
data class TempScannedSensor(
    val index: Int = 0,
    val rom: String = "0000000000000000",
    val temp: Float? = null,
    val role: String = "unassigned"
)

@Serializable
data class TempAssignedRoms(
    @SerialName("class_ab") val classAb: String = "0000000000000000",
    @SerialName("class_d") val classD: String = "0000000000000000",
    val smps: String = "0000000000000000"
)

@Serializable
data class ThermalSensorNode(
    val temp: Float? = null,
    val rom: String = "0000000000000000"
)

@Serializable
data class ThermalData(
    @SerialName("class_ab") val classAb: ThermalSensorNode? = null,
    @SerialName("class_d") val classD: ThermalSensorNode? = null,
    val smps: ThermalSensorNode? = null
)

@Serializable
data class SetupReportResults(
    val rtc: String? = null,
    val ads1115: String? = null,
    val ds18b20: String? = null,
    val smps: String? = null,
    @SerialName("spk_protect") val spkProtect: String? = null,
    @SerialName("bluetooth_gaia") val bluetoothGaia: String? = null,
    @SerialName("bluetooth_gaia_rssi_dbm") val bluetoothGaiaRssiDbm: Int? = null
)

data class SetupLogLine(
    val id: Long,
    val timestamp: String,
    val step: Int?,
    val message: String,
    val status: String // "RUNNING", "PASS", "WARN", "FAIL", "SKIP", "INFO"
)

data class SetupTerminalState(
    val isActive: Boolean = false,
    val currentStep: Int = 0,
    val overallStatus: String = "IDLE", // "IDLE", "RUNNING", "PASS", "FAIL"
    val smpsV: Float? = null,
    val logs: List<SetupLogLine> = emptyList()
)

@Serializable
data class SerialBridgePortInfo(
    val port: String = "/dev/ttyUSB0",
    val name: String = "/dev/ttyUSB0"
)

@Serializable
data class SerialBridgeStatus(
    val running: Int = 0,
    val pid: Int = 0,
    val enabled: Int = 1,
    @SerialName("hw_port") val hwPort: String = "/dev/ttyUSB0",
    @SerialName("hw_exists") val hwExists: Int = 0,
    val baud: String = "921600",
    val mode: String = "tcp_pty",
    @SerialName("tcp_port") val tcpPort: Int = 7777,
    @SerialName("pty_link") val ptyLink: String = "/dev/ttyESP",
    @SerialName("unix_socket") val unixSocket: String = "/data/local/tmp/jacktor_telemetry.sock",
    @SerialName("clients_connected") val clientsConnected: Int = 0,
    @SerialName("unix_clients_connected") val unixClientsConnected: Int = 0,
    @SerialName("available_ports") val availablePorts: List<SerialBridgePortInfo> = emptyList()
) {
    val isRunning: Boolean get() = running != 0
    val isEnabled: Boolean get() = enabled != 0
    val isHwPresent: Boolean get() = hwExists != 0
    val baudInt: Int get() = baud.toIntOrNull() ?: 921600
}

@Serializable
data class KernelInfo(
    val release: String? = null,
    val version: String? = null,
    val display: String? = null
)

@Serializable
data class PanelStatusResponse(
    val success: Boolean = false,
    @SerialName("serial_bridge") val serialBridge: SerialBridgeStatus? = null,
    val ip: String? = null,
    val kernel: KernelInfo? = null,
    val device_model: String? = null,
    val uptime: String? = null,
    val uptime_sec: Long? = null,
    val uptime_id: String? = null
)

@Serializable
data class RealtimeTelemetry(
    val pwr: Boolean? = null,
    @SerialName("spk_pwr") val spkPwr: Boolean? = null,
    @SerialName("spk_fault") val spkFault: Boolean? = null,
    @SerialName("bt_en") val btEn: Boolean? = null,
    @SerialName("qcc_pwr") val qccPwr: Boolean? = null,
    @SerialName("spp_connected") val sppConnected: Boolean? = null,
    @SerialName("bt_rssi") val btRssi: Int? = null,
    @SerialName("bt_status") val btStatus: String? = null,
    @SerialName("qcc_fault") val qccFault: Boolean? = null,
    @SerialName("v_stby") val vStby: Float? = null,
    @SerialName("v_smps") val vSmps: Float? = null,
    @SerialName("heat_c") val heatC: Float? = null,
    @SerialName("heat_ab") val heatAb: Float? = null,
    @SerialName("heat_d") val heatD: Float? = null,
    @SerialName("heat_smps") val heatSmps: Float? = null,
    val mode: String? = "fft",
    @SerialName("bands_len") val bandsLen: Int = 32,
    val vu: Int = 0,
    @SerialName("vu_l") val vuL: Int = 0,
    @SerialName("vu_r") val vuR: Int = 0,
    @SerialName("gain_l") val gainL: Float = 1.0f,
    @SerialName("gain_r") val gainR: Float = 1.0f,
    @SerialName("noise_l") val noiseL: Float = 0.0f,
    @SerialName("noise_r") val noiseR: Float = 0.0f,
    val clipping: Boolean = false,
    @SerialName("clip_count") val clipCount: Int = 0,
    @SerialName("update_ms") val updateMs: Int = 50,
    @SerialName("bands_b64") val bandsB64: String? = null,
    @SerialName("bands_l_b64") val bandsLB64: String? = null,
    @SerialName("bands_r_b64") val bandsRB64: String? = null,
    val link: LinkStatus? = null,
    val input: String? = null,
    @SerialName("bt_state") val btState: String? = null,
    @SerialName("dc_l") val dcL: Int? = null,
    @SerialName("dc_r") val dcR: Int? = null,
    @SerialName("raw_l") val rawL: Int? = null,
    @SerialName("raw_r") val rawR: Int? = null,
    @SerialName("p2p_l") val p2pL: Int? = null,
    @SerialName("p2p_r") val p2pR: Int? = null
)

@Serializable
data class LinkStatus(
    val alive: Boolean = false,
    @SerialName("rx_ms") val rxMs: Long? = null,
    @SerialName("tx_ms") val txMs: Long? = null
)

@Serializable
data class SqwData(
    @SerialName("hw_connected") val hwConnected: Boolean = false,
    val pin: Int = 36,
    val status: String = "DISCONNECTED",
    val freq: Float = 0f
)

@Serializable
data class BluetoothTelemetry(
    @SerialName("qcc_pwr") val qccPwr: Boolean = false,
    @SerialName("spp_connected") val sppConnected: Boolean = false,
    @SerialName("spp_state") val sppState: String = "idle",
    val status: String = "off",
    val rssi: Int = 0,
    @SerialName("esp_bt_active") val espBtActive: Boolean = false,
    @SerialName("esp_tx_power") val espTxPower: String = "Standby",
    val codec: String = "--",
    @SerialName("target_mac") val targetMac: String = "",
    @SerialName("power_injected") val powerInjected: Boolean = false
)

@Serializable
data class SlowTelemetry(
    val time: String? = null,
    @SerialName("current_time") val currentTime: String? = null,
    @SerialName("fw_ver") val fwVer: String? = null,
    @SerialName("ota_ready") val otaReady: Boolean = false,
    val smps: SmpsData? = null,
    @SerialName("sleep_timer") val sleepTimer: Int = 0,
    @SerialName("v_stby") val vStby: Float = 0f,
    @SerialName("v_vdd") val vVdd: Float = 0f,
    @SerialName("v_esp") val vEsp: Float = 0f,
    @SerialName("heat_c") val heatC: Float? = null,
    @SerialName("heat_ab") val heatAb: Float? = null,
    @SerialName("heat_d") val heatD: Float? = null,
    @SerialName("heat_smps") val heatSmps: Float? = null,
    val thermal: ThermalData? = null,
    @SerialName("rtc_c") val rtcC: Float? = null,
    val sqw: SqwData? = null,
    val bt: BluetoothTelemetry? = null,
    val inputs: InputsData? = null,
    val states: SystemStates? = null,
    val errors: List<String> = emptyList(),
    val warnings: List<String> = emptyList(),
    val analyzer: AnalyzerConfigData? = null,
    val buzzer: BuzzerConfigData? = null,
    val nvs: NvsSettings? = null
)

@Serializable
data class AnalyzerConfigData(
    val mode: String = "fft",
    @SerialName("bands_len") val bandsLen: Int = 32,
    @SerialName("update_ms") val updateMs: Int = 50,
    val vu: Int = 0,
    @SerialName("vu_l") val vuL: Int = 0,
    @SerialName("vu_r") val vuR: Int = 0,
    @SerialName("gain_l") val gainL: Float = 1.0f,
    @SerialName("gain_r") val gainR: Float = 1.0f,
    @SerialName("noise_l") val noiseL: Float = 0.0f,
    @SerialName("noise_r") val noiseR: Float = 0.0f,
    @SerialName("dc_l") val dcL: Int? = null,
    @SerialName("dc_r") val dcR: Int? = null,
    @SerialName("p2p_l") val p2pL: Int? = null,
    @SerialName("p2p_r") val p2pR: Int? = null,
    val clipping: Boolean = false,
    @SerialName("clip_count") val clipCount: Int = 0
)

@Serializable
data class BuzzerConfigData(
    val enabled: Boolean = true,
    @SerialName("last_tone") val lastTone: String = "none",
    @SerialName("last_ms") val lastMs: Long = 0,
    @SerialName("quiet_now") val quietNow: Boolean = false
)

@Serializable
data class SmpsData(
    val v: Float = 0f,
    val stage: String? = "standby",
    val cutoff: Float = 38f,
    val recover: Float = 42f
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
    @SerialName("fan_duty") val fanDuty: Int = 512,
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
    @SerialName("smps_cut") val smpsCut: Float = 38f,
    @SerialName("smps_rec") val smpsRec: Float = 42f,
    @SerialName("smps_ov") val smpsOv: Float = 70f,
    @SerialName("pid_sp") val pidSp: Float = 45f,
    @SerialName("otp_thr") val otpThr: Float = 85f,
    @SerialName("bt_off_min") val btOffMin: Int = 5,
    @SerialName("sleep_timer") val sleepTimer: Int = 0,
    @SerialName("buzz_enabled") val buzzEnabled: Boolean = true,
    @SerialName("buzz_volume") val buzzVolume: Int = 80,
    @SerialName("bass_boost") val bassBoost: Boolean = false,
    @SerialName("audio_3d") val audio3d: Boolean = false,
    @SerialName("eq_preset") val eqPreset: Int = 0,
    @SerialName("ch_mode") val chMode: Int = 0,
    @SerialName("temp_ab_rom") val tempAbRom: String = "0000000000000000",
    @SerialName("temp_d_rom") val tempDRom: String = "0000000000000000",
    @SerialName("temp_smps_rom") val tempSmpsRom: String = "0000000000000000"
)
