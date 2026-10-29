package com.jacktor.audio.panel.shared.service

import android.content.Context
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialProber
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.util.SerialInputOutputManager
import com.jacktor.audio.panel.shared.model.TelemetryPacket
import com.jacktor.audio.panel.shared.parser.TelemetryParser
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.io.IOException
import java.lang.StringBuilder

class UsbSerialManager(private val context: Context) : SerialInputOutputManager.Listener {

    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private var usbPort: UsbSerialPort? = null
    private var ioManager: SerialInputOutputManager? = null

    private val scope = CoroutineScope(Dispatchers.IO)

    private val _isConnected = MutableStateFlow(false)
    val isConnected: StateFlow<Boolean> = _isConnected.asStateFlow()

    private val _telemetryFlow = MutableSharedFlow<TelemetryPacket>(extraBufferCapacity = 64)
    val telemetryFlow: SharedFlow<TelemetryPacket> = _telemetryFlow.asSharedFlow()

    private val _statusMessage = MutableStateFlow("Disconnected")
    val statusMessage: StateFlow<String> = _statusMessage.asStateFlow()

    private val rxBuffer = StringBuilder()

    fun connect(): Boolean {
        val availableDrivers = UsbSerialProber.getDefaultProber().findAllDrivers(usbManager)
        if (availableDrivers.isEmpty()) {
            _statusMessage.value = "No USB Serial devices found"
            _isConnected.value = false
            return false
        }

        val driver: UsbSerialDriver = availableDrivers[0]
        val connection = usbManager.openDevice(driver.device)
        if (connection == null) {
            _statusMessage.value = "Permission denied or failed to open device"
            _isConnected.value = false
            return false
        }

        val port = driver.ports[0]
        try {
            port.open(connection)
            port.setParameters(921600, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
            port.dtr = true
            port.rts = true

            usbPort = port
            ioManager = SerialInputOutputManager(port, this).apply {
                start()
            }

            _isConnected.value = true
            _statusMessage.value = "Connected (${driver.device.deviceName})"

            // Request initial command list and telemetry snapshot
            sendCommand("{\"type\":\"get_commands\"}")
            return true
        } catch (e: IOException) {
            _statusMessage.value = "Error connecting: ${e.message}"
            disconnect()
            return false
        }
    }

    fun disconnect() {
        try {
            ioManager?.listener = null
            ioManager?.stop()
            ioManager = null
            usbPort?.close()
            usbPort = null
        } catch (e: Exception) {
            // ignore
        } finally {
            _isConnected.value = false
            _statusMessage.value = "Disconnected"
        }
    }

    fun sendCommand(jsonCommand: String) {
        val port = usbPort ?: return
        if (!_isConnected.value) return
        try {
            val bytes = (jsonCommand.trim() + "\n").toByteArray(Charsets.UTF_8)
            port.write(bytes, 1000)
        } catch (e: Exception) {
            _statusMessage.value = "TX Error: ${e.message}"
        }
    }

    override fun onNewData(data: ByteArray?) {
        if (data == null || data.isEmpty()) return
        val str = String(data, Charsets.UTF_8)
        synchronized(rxBuffer) {
            rxBuffer.append(str)
            var lineEndIndex: Int
            while (rxBuffer.indexOf("\n").also { lineEndIndex = it } >= 0) {
                val line = rxBuffer.substring(0, lineEndIndex)
                rxBuffer.delete(0, lineEndIndex + 1)
                scope.launch {
                    val packet = TelemetryParser.parseLine(line)
                    if (packet != null) {
                        _telemetryFlow.emit(packet)
                    }
                }
            }
        }
    }

    override fun onRunError(e: Exception?) {
        _statusMessage.value = "USB Error: ${e?.message}"
        scope.launch {
            disconnect()
        }
    }
}
