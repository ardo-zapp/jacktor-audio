package com.jacktor.audio.panel.shared.parser

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

class TelemetryParserTest {

    @Test
    fun testParseRealtimeTelemetry() {
        val jsonStr = """{"type":"telemetry","rt":{"mode":"fft","bands_len":32,"vu":120,"vu_l":110,"vu_r":130,"clipping":false,"clip_count":0,"update_ms":50,"bands_b64":"AQIDBAU=","link":{"alive":true},"input":"AUX","bt_state":"bt"}}"""
        val packet = TelemetryParser.parseLine(jsonStr)
        assertNotNull(packet)
        assertEquals("telemetry", packet.type)
        assertNotNull(packet.rt)
        assertEquals("fft", packet.rt?.mode)
        assertEquals(110, packet.rt?.vuL)
        assertEquals(130, packet.rt?.vuR)

        val bands = TelemetryParser.decodeBase64Bands(packet.rt?.bandsB64)
        assertEquals(5, bands.size)
        assertEquals(1, bands[0])
        assertEquals(2, bands[1])
        assertEquals(5, bands[4])
    }

    @Test
    fun testBuildCommands() {
        val pwrCmd = TelemetryParser.buildPowerCommand(true, true)
        assertTrue(pwrCmd.contains("\"power\":true"))
        assertTrue(pwrCmd.contains("\"bypass\":true"))

        val mediaCmd = TelemetryParser.buildMediaCommand("play")
        assertTrue(mediaCmd.contains("\"play\":true"))

        val fanCmd = TelemetryParser.buildNvsConfigCommand("fan_mode", "auto")
        assertTrue(fanCmd.contains("\"fan_mode\":\"auto\""))
    }
}
