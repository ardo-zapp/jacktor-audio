package com.jacktor.audiopanel.repository

import android.util.Log
import com.jacktor.audiopanel.model.NvsSettings
import com.jacktor.audiopanel.model.RealtimeTelemetry
import com.jacktor.audiopanel.model.SetupLogLine
import com.jacktor.audiopanel.model.SetupTerminalState
import com.jacktor.audiopanel.model.SlowTelemetry
import com.jacktor.audiopanel.model.TelemetryPacket
import com.jacktor.audiopanel.parser.TelemetryParser
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class AmplifierRepository {

    private val _realtimeData = MutableStateFlow(RealtimeTelemetry())
    val realtimeData: StateFlow<RealtimeTelemetry> = _realtimeData.asStateFlow()

    private val _spectrumBands = MutableStateFlow(IntArray(32))
    val spectrumBands: StateFlow<IntArray> = _spectrumBands.asStateFlow()

    private val _slowData = MutableStateFlow(SlowTelemetry())
    val slowData: StateFlow<SlowTelemetry> = _slowData.asStateFlow()

    private val _nvsSettings = MutableStateFlow(NvsSettings())
    val nvsSettings: StateFlow<NvsSettings> = _nvsSettings.asStateFlow()

    private val _scannedTempSensors = MutableStateFlow<List<com.jacktor.audiopanel.model.TempScannedSensor>>(emptyList())
    val scannedTempSensors: StateFlow<List<com.jacktor.audiopanel.model.TempScannedSensor>> = _scannedTempSensors.asStateFlow()

    private val _isPowerOn = MutableStateFlow(false)
    val isPowerOn: StateFlow<Boolean> = _isPowerOn.asStateFlow()

    private val _setupState = MutableStateFlow(SetupTerminalState())
    val setupState: StateFlow<SetupTerminalState> = _setupState.asStateFlow()

    private var nextLogId = 1L
    private var lastKnownPower: Boolean? = null
    private var lastKnownInitializing: Boolean = false
    private var latestTimeStr: String = "00:00:00"
    @Volatile
    private var powerOnCommandTimestampMs: Long = 0L
    @Volatile
    private var hasSeenMcuPowerConfirm: Boolean = false

    private fun currentTimeBadge(): String {
        val raw = _slowData.value.time ?: latestTimeStr
        val tIdx = raw.indexOf('T')
        if (tIdx >= 0 && raw.length >= tIdx + 9) {
            return raw.substring(tIdx + 1, tIdx + 9)
        }
        return latestTimeStr
    }

    @Synchronized
    fun clearAlertsAndErrors() {
        val cur = _slowData.value
        if (cur.errors.isNotEmpty() || cur.warnings.isNotEmpty()) {
            _slowData.value = cur.copy(errors = emptyList(), warnings = emptyList())
            Log.i("JacktorTerminal", "clearAlertsAndErrors: Cleared errors and warnings")
        }
    }

    @Synchronized
    fun resetMcuSession() {
        clearAlertsAndErrors()
        val cur = _slowData.value
        _slowData.value = cur.copy(
            errors = emptyList(),
            warnings = emptyList(),
            states = cur.states?.copy(on = false, standby = true, initializing = false)
        )
        _isPowerOn.value = false
        lastKnownPower = null
        lastKnownInitializing = false
        _setupState.value = SetupTerminalState(isActive = false, overallStatus = "IDLE", currentStep = 0, logs = emptyList())
        _realtimeData.value = RealtimeTelemetry()
        _spectrumBands.value = IntArray(32)
        Log.i("JacktorTerminal", "resetMcuSession: Cleared alerts, errors, and terminal logs for MCU reboot/reset")
    }

    @Synchronized
    fun startPowerOnTerminal(initialMsg: String = "Power-On Command Issued — Waiting for MCU Self-Test...") {
        val cur = _setupState.value
        if (cur.isActive && cur.overallStatus == "RUNNING") return
        powerOnCommandTimestampMs = System.currentTimeMillis()
        hasSeenMcuPowerConfirm = false
        val firstLine = SetupLogLine(
            id = nextLogId++,
            timestamp = currentTimeBadge(),
            step = 0,
            message = initialMsg,
            status = "RUNNING"
        )
        _isPowerOn.value = true
        lastKnownPower = true
        _setupState.value = SetupTerminalState(
            isActive = true,
            currentStep = 0,
            overallStatus = "RUNNING",
            smpsV = null,
            logs = listOf(firstLine)
        )
        Log.i("JacktorTerminal", "startPowerOnTerminal: $initialMsg")
    }

    @Synchronized
    fun dismissSetupTerminal() {
        powerOnCommandTimestampMs = 0L
        _setupState.value = _setupState.value.copy(isActive = false)
        Log.i("JacktorTerminal", "dismissSetupTerminal")
    }

    @Synchronized
    private fun appendTerminalLog(
        step: Int?,
        message: String,
        status: String,
        smpsV: Float? = null,
        overallOverride: String? = null
    ) {
        val cur = _setupState.value
        val baseLogs = if (!cur.isActive || (step == 0 && status == "RUNNING" && cur.overallStatus != "RUNNING")) {
            emptyList()
        } else {
            cur.logs
        }

        val lastLine = baseLogs.lastOrNull()
        val updatedLogs = if (lastLine != null && lastLine.step == step && lastLine.message == message && lastLine.status == status) {
            baseLogs
        } else {
            (baseLogs + SetupLogLine(
                id = nextLogId++,
                timestamp = currentTimeBadge(),
                step = step,
                message = message,
                status = status
            )).takeLast(120)
        }

        val newStep = if (step != null && step > cur.currentStep) step else cur.currentStep
        val newSmpsV = smpsV ?: cur.smpsV
        val newOverall = overallOverride ?: when {
            status == "FAIL" -> "FAIL"
            step == 6 && status == "PASS" && (
                message.contains("Bluetooth GAIA", ignoreCase = true) ||
                    message.contains("Skipping check", ignoreCase = true)
                ) -> "PASS"
            cur.overallStatus == "IDLE" || !cur.isActive -> "RUNNING"
            else -> cur.overallStatus
        }

        _setupState.value = SetupTerminalState(
            isActive = true,
            currentStep = newStep,
            overallStatus = newOverall,
            smpsV = newSmpsV,
            logs = updatedLogs
        )
        Log.i("JacktorTerminal", "STEP=$newStep [$status] OVERALL=$newOverall: $message")
    }

    @Synchronized
    fun processPacket(packet: TelemetryPacket) {
        val pktType = packet.type ?: ""

        when (pktType) {
            "setup_progress", "selftest_progress" -> {
                hasSeenMcuPowerConfirm = true
                _isPowerOn.value = true
                lastKnownPower = true
                val step = packet.step ?: 0
                if (step == 0) {
                    clearAlertsAndErrors()
                }
                val msg = packet.msg ?: ""
                val status = (packet.status ?: "RUNNING").uppercase()
                appendTerminalLog(step = step, message = msg, status = status, smpsV = packet.smpsV)
                return
            }

            "setup_report", "selftest_report" -> {
                hasSeenMcuPowerConfirm = true
                _isPowerOn.value = true
                lastKnownPower = true
                val repStatus = (packet.status ?: "PASS").uppercase()
                val res = packet.results
                if (res != null) {
                    val rssiSuffix = if (res.bluetoothGaiaRssiDbm != null) " (${res.bluetoothGaiaRssiDbm} dBm)" else ""
                    val summaryLine1 = "DIAG REPORT: RTC=${(res.rtc ?: "-").uppercase()} | ADS1115=${(res.ads1115 ?: "-").uppercase()} | DS18B20=${(res.ds18b20 ?: "-").uppercase()}"
                    val summaryLine2 = "DIAG REPORT: SMPS=${(res.smps ?: "-").uppercase()} | SPK=${(res.spkProtect ?: "-").uppercase()} | BT_GAIA=${(res.bluetoothGaia ?: "-").uppercase()}$rssiSuffix"
                    appendTerminalLog(step = 6, message = summaryLine1, status = "INFO")
                    appendTerminalLog(step = 6, message = summaryLine2, status = "INFO")
                }
                val finalMsg = if (repStatus == "PASS") {
                    "All Self-Test Diagnostics Passed [ PASS ] — Entering Runtime Mode"
                } else {
                    "Self-Test Diagnostics Failed [ FAIL ] — Protection Active"
                }
                appendTerminalLog(
                    step = 6,
                    message = finalMsg,
                    status = repStatus,
                    overallOverride = if (repStatus == "PASS") "PASS" else "FAIL"
                )
                return
            }

            "log" -> {
                val msg = packet.msg ?: ""
                if (msg.contains("[BOOT]", ignoreCase = true) ||
                    msg.contains("ResetReason", ignoreCase = true) ||
                    msg.contains("Clearing latched error", ignoreCase = true) ||
                    msg.contains("reboot...", ignoreCase = true)) {
                    resetMcuSession()
                }
                val cur = _setupState.value
                if (cur.isActive && cur.overallStatus == "RUNNING") {
                    if (msg.isNotBlank()) {
                        val lvl = (packet.lvl ?: "info").uppercase()
                        val st = when (lvl) {
                            "ERROR", "ERR" -> "FAIL"
                            "WARN", "WARNING" -> "WARN"
                            else -> "INFO"
                        }
                        appendTerminalLog(step = cur.currentStep, message = msg, status = st)
                    }
                }
                return
            }

            "analyzer" -> {
                packet.data?.let { an ->
                    _slowData.value = _slowData.value.copy(analyzer = an)
                    val curRt = _realtimeData.value
                    _realtimeData.value = curRt.copy(
                        mode = an.mode,
                        bandsLen = an.bandsLen,
                        updateMs = an.updateMs,
                        gainL = an.gainL,
                        gainR = an.gainR,
                        noiseL = an.noiseL,
                        noiseR = an.noiseR,
                        dcL = an.dcL ?: curRt.dcL,
                        dcR = an.dcR ?: curRt.dcR,
                        p2pL = an.p2pL ?: curRt.p2pL,
                        p2pR = an.p2pR ?: curRt.p2pR
                    )
                }
                return
            }

            "temp_scan_result" -> {
                _scannedTempSensors.value = packet.devices ?: packet.sensors ?: emptyList()
                packet.assigned?.let { asg ->
                    _nvsSettings.value = _nvsSettings.value.copy(
                        tempAbRom = asg.classAb,
                        tempDRom = asg.classD,
                        tempSmpsRom = asg.smps
                    )
                }
                return
            }

            "ack" -> {
                return
            }
        }

        val nowMs = System.currentTimeMillis()
        val inPowerOnGraceWindow = (powerOnCommandTimestampMs > 0L) &&
            (nowMs - powerOnCommandTimestampMs < 5000L) &&
            !hasSeenMcuPowerConfirm

        packet.rt?.let { rt ->
            val terminalBusy = _setupState.value.isActive && _setupState.value.overallStatus == "RUNNING"

            // Skip 30Hz spectrum & slowData copies while full-screen boot terminal is actively running
            // so 100% of UI frame time goes to instant terminal rendering
            if (!terminalBusy) {
                _realtimeData.value = rt
                if (!rt.bandsB64.isNullOrEmpty()) {
                    _spectrumBands.value = TelemetryParser.decodeBase64Bands(rt.bandsB64, rt.bandsLen.coerceAtLeast(8))
                } else if (_spectrumBands.value.any { it != 0 }) {
                    _spectrumBands.value = IntArray(rt.bandsLen.coerceAtLeast(32))
                }

                val curSlow = _slowData.value
                val updatedSmps = (curSlow.smps ?: com.jacktor.audiopanel.model.SmpsData()).copy(
                    v = rt.vSmps ?: curSlow.smps?.v ?: 0f,
                    stage = curSlow.smps?.stage ?: if (rt.pwr == true) "armed" else "standby"
                )
                _slowData.value = curSlow.copy(
                    smps = updatedSmps,
                    vStby = rt.vStby ?: curSlow.vStby,
                    heatC = rt.heatC ?: curSlow.heatC,
                    heatAb = rt.heatAb ?: curSlow.heatAb,
                    heatD = rt.heatD ?: curSlow.heatD,
                    heatSmps = rt.heatSmps ?: curSlow.heatSmps
                )
            }

            rt.pwr?.let { pwrNow ->
                if (pwrNow) {
                    hasSeenMcuPowerConfirm = true
                }
                if (pwrNow || !inPowerOnGraceWindow) {
                    val prev = lastKnownPower
                    lastKnownPower = pwrNow
                    _isPowerOn.value = pwrNow
                    if (pwrNow && prev == false && !_setupState.value.isActive) {
                        startPowerOnTerminal("Hardware Power-On Detected — Starting Self-Test...")
                    } else if (!pwrNow && prev == true) {
                        lastKnownInitializing = false
                        if (_setupState.value.overallStatus != "FAIL") {
                            dismissSetupTerminal()
                        }
                    }
                }
            }
        }

        packet.hz1?.let { slow ->
            slow.time?.let { latestTimeStr = it }
            _slowData.value = slow
            slow.nvs?.let { nvs ->
                _nvsSettings.value = nvs
            }
            slow.states?.let { st ->
                if (st.on) {
                    hasSeenMcuPowerConfirm = true
                }
                if (st.on || !inPowerOnGraceWindow) {
                    val prevPwr = lastKnownPower
                    val prevInit = lastKnownInitializing
                    lastKnownPower = st.on
                    lastKnownInitializing = st.initializing
                    _isPowerOn.value = st.on

                    if (st.on && (st.initializing || prevPwr == false)) {
                        if (!_setupState.value.isActive && st.initializing) {
                            startPowerOnTerminal("Amplifier Initializing — Running Power-On Self-Test...")
                        }
                    } else if (st.on && !st.initializing && prevInit) {
                        val cur = _setupState.value
                        if (cur.isActive && cur.overallStatus == "RUNNING") {
                            appendTerminalLog(
                                step = 6,
                                message = "System Self-Test Complete [ PASS ] — Entering Runtime Mode",
                                status = "PASS",
                                overallOverride = "PASS"
                            )
                        }
                    } else if (!st.on) {
                        if (_setupState.value.isActive && _setupState.value.overallStatus != "FAIL") {
                            dismissSetupTerminal()
                        }
                    }
                }
            }
        }
    }
}
