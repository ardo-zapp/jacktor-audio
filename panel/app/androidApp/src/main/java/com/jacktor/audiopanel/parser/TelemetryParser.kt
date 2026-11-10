package com.jacktor.audiopanel.parser

import android.util.Base64
import com.jacktor.audiopanel.model.SerialBridgeStatus
import com.jacktor.audiopanel.model.TelemetryPacket
import kotlinx.serialization.json.Json

object TelemetryParser {
    val json = Json {
        ignoreUnknownKeys = true
        isLenient = true
        coerceInputValues = true
    }

    fun parseLine(line: String): TelemetryPacket? {
        val trimmed = line.trim()
        val start = trimmed.indexOf('{')
        val end = trimmed.lastIndexOf('}')
        if (start < 0 || end <= start) return null
        val jsonSlice = trimmed.substring(start, end + 1)
        return try {
            json.decodeFromString<TelemetryPacket>(jsonSlice)
        } catch (_: Exception) {
            null
        }
    }

    fun parseSerialBridgeStatus(jsonText: String): SerialBridgeStatus? {
        val trimmed = jsonText.trim()
        val start = trimmed.indexOf('{')
        val end = trimmed.lastIndexOf('}')
        if (start < 0 || end <= start) return null
        return try {
            json.decodeFromString<SerialBridgeStatus>(trimmed.substring(start, end + 1))
        } catch (_: Exception) {
            null
        }
    }

    fun decodeBase64Bands(b64: String?, expectedLen: Int = 32): IntArray {
        if (b64.isNullOrEmpty()) return IntArray(expectedLen)
        return try {
            val bytes = Base64.decode(b64, Base64.DEFAULT)
            IntArray(bytes.size) { i -> bytes[i].toInt() and 0xFF }
        } catch (_: Exception) {
            IntArray(expectedLen)
        }
    }

    fun buildPowerCommand(on: Boolean, bypass: Boolean = false): String {
        return if (bypass) {
            "{\"type\":\"cmd\",\"cmd\":{\"power\":{\"state\":$on,\"bypass\":true}}}"
        } else {
            "{\"type\":\"cmd\",\"cmd\":{\"power\":$on}}"
        }
    }

    fun buildMediaCommand(action: String, value: Any? = null): String {
        return when (action) {
            "play" -> "{\"type\":\"cmd\",\"cmd\":{\"play\":true}}"
            "pause" -> "{\"type\":\"cmd\",\"cmd\":{\"pause\":true}}"
            "stop" -> "{\"type\":\"cmd\",\"cmd\":{\"stop\":true}}"
            "next" -> "{\"type\":\"cmd\",\"cmd\":{\"next\":true}}"
            "prev" -> "{\"type\":\"cmd\",\"cmd\":{\"prev\":true}}"
            "volup" -> "{\"type\":\"cmd\",\"cmd\":{\"volup\":true}}"
            "voldown" -> "{\"type\":\"cmd\",\"cmd\":{\"voldown\":true}}"
            "setpreset", "seteq" -> "{\"type\":\"cmd\",\"cmd\":{\"setpreset\":${value ?: 0}}}"
            "changevolume" -> "{\"type\":\"cmd\",\"cmd\":{\"changevolume\":${value ?: 50}}}"
            "setbass" -> "{\"type\":\"cmd\",\"cmd\":{\"setbass\":${value ?: 0}}}"
            "set3d" -> "{\"type\":\"cmd\",\"cmd\":{\"set3d\":${value ?: 0}}}"
            "setchannel", "channel_mode", "ch_mode" -> "{\"type\":\"cmd\",\"cmd\":{\"setchannel\":${value ?: 0}}}"
            "enterpairing" -> "{\"type\":\"cmd\",\"cmd\":{\"enterpairing\":true}}"
            "deletepdl", "clearpdl" -> "{\"type\":\"cmd\",\"cmd\":{\"deletepdl\":true}}"
            "qcc_reboot", "qccreboot" -> "{\"type\":\"cmd\",\"cmd\":{\"reboot\":true}}"
            else -> "{\"type\":\"cmd\",\"cmd\":{\"$action\":true}}"
        }
    }

    fun buildSimpleCommand(action: String): String {
        return when (action) {
            "calibrate" -> "{\"type\":\"analyzer\",\"cmd\":\"calibrate\"}"
            "ana-get" -> "{\"type\":\"analyzer\",\"cmd\":\"get\"}"
            "setup", "selftest" -> "{\"type\":\"cmd\",\"cmd\":{\"setup\":true}}"
            "factory-reset", "factory_reset", "nvs-reset", "nvs_reset" -> "{\"type\":\"cmd\",\"cmd\":{\"factory_reset\":true}}"
            "stats" -> "{\"type\":\"cmd\",\"cmd\":{\"stats\":true}}"
            "deviceinfo", "status" -> "{\"type\":\"cmd\",\"cmd\":{\"deviceinfo\":true}}"
            "ping" -> "{\"type\":\"ping\"}"
            "reboot", "mcu_reboot" -> "{\"type\":\"cmd\",\"cmd\":{\"mcu_reboot\":true}}"
            "tone-click" -> "tone-click"
            "tone-test" -> "tone-test"
            else -> action
        }
    }

    fun buildNvsConfigCommand(key: String, value: Any): String {
        // Map UI setting keys to exact ESP32 comms_handlers.cpp keys
        val mappedKey = when (key) {
            "bt_en" -> "bt"
            else -> key
        }
        val valStr = when (value) {
            is Boolean -> value.toString()
            is Number -> value.toString()
            else -> "\"$value\""
        }
        return "{\"type\":\"cmd\",\"cmd\":{\"$mappedKey\":$valStr}}"
    }

    fun buildTuneCommand(pidSetpoint: Float? = null, otpThresh: Float? = null): String {
        val parts = mutableListOf<String>()
        if (pidSetpoint != null) parts.add("\"pid_setpoint\":$pidSetpoint")
        if (otpThresh != null) parts.add("\"otp_thresh\":$otpThresh")
        return "{\"type\":\"cmd\",\"cmd\":{\"tune\":{${parts.joinToString(",")}}}}"
    }

    fun buildRtcSyncCommand(epochSeconds: Long): String {
        return "{\"type\":\"cmd\",\"cmd\":{\"rtc_set_epoch\":$epochSeconds}}"
    }

    fun buildAnalyzerModeCommand(mode: String, bands: Int, updateMs: Int): String {
        return "{\"type\":\"analyzer\",\"cmd\":\"set\",\"mode\":\"$mode\",\"bands\":$bands,\"update_ms\":$updateMs}"
    }

    fun buildAnalyzerParamsCommand(gainL: Float, gainR: Float, noiseL: Float, noiseR: Float): String {
        return "{\"type\":\"analyzer\",\"cmd\":\"set_params\",\"gain_l\":$gainL,\"gain_r\":$gainR,\"noise_l\":$noiseL,\"noise_r\":$noiseR}"
    }
}
