package com.jacktor.audio.panel.shared.parser

import com.jacktor.audio.panel.shared.model.TelemetryPacket
import kotlinx.serialization.json.Json
import kotlin.io.encoding.Base64
import kotlin.io.encoding.ExperimentalEncodingApi

object TelemetryParser {
    val json = Json {
        ignoreUnknownKeys = true
        isLenient = true
        coerceInputValues = true
    }

    fun parseLine(line: String): TelemetryPacket? {
        val trimmed = line.trim()
        if (!trimmed.startsWith("{") || !trimmed.endsWith("}")) return null
        return try {
            json.decodeFromString<TelemetryPacket>(trimmed)
        } catch (e: Exception) {
            null
        }
    }

    @OptIn(ExperimentalEncodingApi::class)
    fun decodeBase64Bands(b64: String?, expectedLen: Int = 32): IntArray {
        if (b64.isNullOrEmpty()) return IntArray(expectedLen)
        return try {
            val bytes = Base64.decode(b64)
            IntArray(bytes.size) { i -> bytes[i].toInt() and 0xFF }
        } catch (e: Exception) {
            IntArray(expectedLen)
        }
    }

    fun buildPowerCommand(on: Boolean, bypass: Boolean = false): String {
        return if (bypass) {
            "{\"type\":\"cmd\",\"cmd\":{\"power\":$on,\"bypass\":true}}"
        } else {
            "{\"type\":\"cmd\",\"cmd\":{\"power\":$on}}"
        }
    }

    fun buildMediaCommand(action: String, value: Any? = null): String {
        return when (action) {
            "play" -> "{\"type\":\"cmd\",\"cmd\":{\"play\":true}}"
            "pause" -> "{\"type\":\"cmd\",\"cmd\":{\"pause\":true}}"
            "next" -> "{\"type\":\"cmd\",\"cmd\":{\"next\":true}}"
            "prev" -> "{\"type\":\"cmd\",\"cmd\":{\"prev\":true}}"
            "volup" -> "{\"type\":\"cmd\",\"cmd\":{\"volup\":true}}"
            "voldown" -> "{\"type\":\"cmd\",\"cmd\":{\"voldown\":true}}"
            "setpreset" -> "{\"type\":\"cmd\",\"cmd\":{\"setpreset\":${value ?: 0}}}"
            "changevolume" -> "{\"type\":\"cmd\",\"cmd\":{\"changevolume\":${value ?: 50}}}"
            "setbass" -> "{\"type\":\"cmd\",\"cmd\":{\"setbass\":${value ?: 0}}}"
            "set3d" -> "{\"type\":\"cmd\",\"cmd\":{\"set3d\":${value ?: 0}}}"
            "enterpairing" -> "{\"type\":\"cmd\",\"cmd\":{\"enterpairing\":true}}"
            else -> "{\"type\":\"cmd\",\"cmd\":{\"$action\":true}}"
        }
    }

    fun buildNvsConfigCommand(key: String, value: Any): String {
        val valStr = when (value) {
            is Boolean -> value.toString()
            is Number -> value.toString()
            else -> "\"$value\""
        }
        return "{\"type\":\"cmd\",\"cmd\":{\"$key\":$valStr}}"
    }
}
