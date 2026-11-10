package com.jacktor.audiopanel.service

import com.jacktor.audiopanel.model.SerialBridgeStatus
import com.jacktor.audiopanel.parser.TelemetryParser
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.BufferedReader
import java.io.File
import java.io.InputStreamReader

object SerialBridgeShell {
    private const val API_BIN = "/data/adb/modules/jacktor-audio-framework/system/bin/jacktor-api"
    private const val BIN_PATH = "/data/adb/modules/jacktor-audio-framework/system/bin/serial-bridge"
    private const val CONF_PATH = "/data/adb/modules/jacktor-audio-framework/config/serial_bridge.conf"

    suspend fun runRootCommand(cmd: String): String = withContext(Dispatchers.IO) {
        try {
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", cmd))
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            val output = StringBuilder()
            var line: String?
            while (reader.readLine().also { line = it } != null) {
                output.append(line).append('\n')
            }
            process.waitFor()
            output.toString().trim()
        } catch (_: Exception) {
            ""
        }
    }

    suspend fun queryStatus(): SerialBridgeStatus {
        val raw = runRootCommand("($API_BIN serial_bridge_control status || $BIN_PATH status || serial-bridge status) 2>/dev/null")
        return TelemetryParser.parseSerialBridgeStatus(raw) ?: SerialBridgeStatus()
    }

    suspend fun controlDaemon(action: String): SerialBridgeStatus {
        runRootCommand("($API_BIN serial_bridge_control $action || $BIN_PATH $action || serial-bridge $action) >/dev/null 2>&1")
        return queryStatus()
    }

    suspend fun setPort(port: String): SerialBridgeStatus {
        runRootCommand("($API_BIN serial_bridge_control set-port $port || $BIN_PATH set-port $port || serial-bridge set-port $port) >/dev/null 2>&1")
        return queryStatus()
    }

    suspend fun setBaud(baud: Int): SerialBridgeStatus {
        runRootCommand("($API_BIN serial_bridge_control set-baud $baud || $BIN_PATH set-baud $baud || serial-bridge set-baud $baud) >/dev/null 2>&1")
        return queryStatus()
    }

    suspend fun updateConfigKey(key: String, value: String, restartDaemon: Boolean = true): SerialBridgeStatus {
        val script = buildString {
            append("mkdir -p /data/adb/modules/jacktor-audio-framework/config 2>/dev/null; ")
            append("touch $CONF_PATH 2>/dev/null; ")
            append("if grep -q '^$key=' $CONF_PATH 2>/dev/null; then ")
            append("sed -i 's/^$key=.*/$key=$value/' $CONF_PATH; ")
            append("else echo '$key=$value' >> $CONF_PATH; fi; ")
            if (restartDaemon) {
                append("($BIN_PATH restart || serial-bridge restart) >/dev/null 2>&1")
            }
        }
        runRootCommand(script)
        return queryStatus()
    }

    /**
     * Directly test if USB serial hardware (/dev/ttyUSB0, /dev/ttyUSB*, /dev/ttyACM*) exists
     * in the Linux devfs or sysfs.
     */
    fun checkUsbHardwarePresent(configuredPort: String? = null): Boolean {
        if (!configuredPort.isNullOrBlank() && File(configuredPort).exists()) {
            return true
        }
        if (File("/dev/ttyUSB0").exists() || File("/sys/class/tty/ttyUSB0").exists()) return true
        if (File("/dev/ttyUSB1").exists() || File("/sys/class/tty/ttyUSB1").exists()) return true
        if (File("/dev/ttyACM0").exists() || File("/sys/class/tty/ttyACM0").exists()) return true

        val sysTty = File("/sys/class/tty")
        if (sysTty.exists()) {
            val list = sysTty.list()
            if (list != null && list.any { it.startsWith("ttyUSB") || it.startsWith("ttyACM") }) {
                return true
            }
        }
        return false
    }
}
