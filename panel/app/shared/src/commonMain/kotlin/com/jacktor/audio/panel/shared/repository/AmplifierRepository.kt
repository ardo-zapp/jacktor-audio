package com.jacktor.audio.panel.shared.repository

import com.jacktor.audio.panel.shared.model.NvsSettings
import com.jacktor.audio.panel.shared.model.RealtimeTelemetry
import com.jacktor.audio.panel.shared.model.SlowTelemetry
import com.jacktor.audio.panel.shared.model.TelemetryPacket
import com.jacktor.audio.panel.shared.parser.TelemetryParser
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

    fun processPacket(packet: TelemetryPacket) {
        packet.rt?.let { rt ->
            _realtimeData.value = rt
            rt.bandsB64?.let { b64 ->
                _spectrumBands.value = TelemetryParser.decodeBase64Bands(b64, rt.bandsLen)
            }
        }

        packet.hz1?.let { slow ->
            _slowData.value = slow
            slow.nvs?.let { nvs ->
                _nvsSettings.value = nvs
            }
        }
    }
}
