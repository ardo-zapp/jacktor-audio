package com.jacktor.audiopanel.service

import android.content.Context
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import com.jacktor.audiopanel.model.TelemetryPacket
import com.jacktor.audiopanel.parser.TelemetryParser
import com.jacktor.audiopanel.repository.AmplifierRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket

/**
 * TelemetryConnectionManager handles real-time full-duplex communication with the ESP32 amplifier
 * through the native `serial-bridge` multiplexer daemon (TCP 127.0.0.1:7777 or Unix Socket).
 */
class TelemetryConnectionManager(
    private val context: Context,
    private val repository: AmplifierRepository? = null
) {

    private val TAG = "TelemetryConnMgr"

    enum class ConnectionMode {
        JACKTOR_API,        // Connect to native jacktor-api SSE streaming (http://127.0.0.1/cgi-bin/jacktor-api?action=stream)
        SERIAL_BRIDGE_UNIX, // Direct Unix socket fallback (/data/local/tmp/jacktor_telemetry.sock)
        SERIAL_BRIDGE_TCP   // Direct TCP fallback (127.0.0.1:7777)
    }

    private val prefs = context.getSharedPreferences("JacktorPanelPrefs", Context.MODE_PRIVATE)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private var connectionJob: Job? = null
    private var heartbeatJob: Job? = null

    @Volatile
    private var activeInputStream: InputStream? = null

    @Volatile
    private var activeOutputStream: OutputStream? = null

    private var tcpSocket: Socket? = null
    private var unixSocket: LocalSocket? = null

    private val _isConnected = MutableStateFlow(false)
    val isConnected: StateFlow<Boolean> = _isConnected.asStateFlow()

    private val _isUsbPresent = MutableStateFlow(SerialBridgeShell.checkUsbHardwarePresent())
    val isUsbPresent: StateFlow<Boolean> = _isUsbPresent.asStateFlow()

    private val _isAmpAlive = MutableStateFlow(false)
    val isAmpAlive: StateFlow<Boolean> = _isAmpAlive.asStateFlow()

    private val _telemetryFlow = MutableSharedFlow<TelemetryPacket>(
        replay = 2,
        extraBufferCapacity = 128,
        onBufferOverflow = BufferOverflow.DROP_OLDEST
    )
    val telemetryFlow: SharedFlow<TelemetryPacket> = _telemetryFlow.asSharedFlow()

    private val _statusMessage = MutableStateFlow("Connecting to Jacktor API...")
    val statusMessage: StateFlow<String> = _statusMessage.asStateFlow()

    private val _connectionMode = MutableStateFlow<ConnectionMode?>(null)
    val connectionMode: StateFlow<ConnectionMode?> = _connectionMode.asStateFlow()

    @Volatile
    private var lastPacketReceivedTimestampMs: Long = 0L

    private val rxBuffer = StringBuilder(8192)
    private val txLock = Any()

    val tcpHost: String
        get() = prefs.getString("sb_tcp_host", "127.0.0.1")?.trim()?.ifEmpty { "127.0.0.1" } ?: "127.0.0.1"

    val tcpPort: Int
        get() = prefs.getInt("sb_tcp_port", 7777).let { if (it in 1..65535) it else 7777 }

    val unixSocketPath: String
        get() = prefs.getString("sb_unix_socket", "/data/local/tmp/jacktor_telemetry.sock")
            ?: "/data/local/tmp/jacktor_telemetry.sock"

    val preferredMode: ConnectionMode
        get() {
            val saved = prefs.getString("sb_app_conn_mode", "API") ?: "API"
            return when {
                saved.contains("UNIX", ignoreCase = true) -> ConnectionMode.SERIAL_BRIDGE_UNIX
                saved.contains("TCP", ignoreCase = true) -> ConnectionMode.SERIAL_BRIDGE_TCP
                else -> ConnectionMode.JACKTOR_API
            }
        }

    init {
        connectToSerialBridge()
    }

    private fun ensureBridgeDaemonRunning() {
        try {
            Runtime.getRuntime().exec(
                arrayOf(
                    "su", "-c",
                    "/data/adb/modules/jacktor-audio-framework/system/bin/jacktor-api serial_bridge_control start >/dev/null 2>&1 || serial-bridge start >/dev/null 2>&1"
                )
            )
        } catch (_: Exception) {
        }
    }

    private fun preloadCachedTelemetry() {
        try {
            val cacheFile = File("/data/local/tmp/amp_telemetry.json")
            if (cacheFile.exists() && cacheFile.canRead()) {
                val text = cacheFile.readText()
                val pkt = TelemetryParser.parseLine(text)
                if (pkt != null) {
                    repository?.processPacket(pkt)
                    _telemetryFlow.tryEmit(pkt)
                }
            }
        } catch (_: Exception) {
        }
    }

    fun connectToSerialBridge() {
        connectionJob?.cancel()
        heartbeatJob?.cancel()
        closeSocketsOnly()

        connectionJob = scope.launch {
            preloadCachedTelemetry()
            var failCount = 0

            while (isActive) {
                if (isMaintenanceMode()) {
                    _isConnected.value = false
                    _statusMessage.value = "MAINTENANCE: Firmware update in progress..."
                    delay(3000)
                    continue
                }

                val modeToTry = if (failCount > 0 && failCount % 3 == 0) {
                    if (preferredMode == ConnectionMode.JACKTOR_API) {
                        ConnectionMode.SERIAL_BRIDGE_UNIX
                    } else {
                        ConnectionMode.JACKTOR_API
                    }
                } else {
                    preferredMode
                }

                _connectionMode.value = modeToTry

                val success = when (modeToTry) {
                    ConnectionMode.JACKTOR_API -> connectAndReadApiStream()
                    ConnectionMode.SERIAL_BRIDGE_UNIX -> connectAndReadUnix()
                    ConnectionMode.SERIAL_BRIDGE_TCP -> connectAndReadTcp()
                }

                if (!success) {
                    failCount++
                    if (failCount == 1 || failCount % 4 == 0) {
                        ensureBridgeDaemonRunning()
                    }
                    delay(1000)
                } else {
                    failCount = 0
                    delay(500)
                }
            }
        }
    }

    private suspend fun connectAndReadApiStream(): Boolean {
        return try {
            _statusMessage.value = "Connecting to Jacktor API (http://127.0.0.1:80)..."
            val url = java.net.URL("http://127.0.0.1/cgi-bin/jacktor-api?action=stream")
            val conn = (url.openConnection() as java.net.HttpURLConnection).apply {
                connectTimeout = 3000
                readTimeout = 12000
                requestMethod = "GET"
                setRequestProperty("Accept", "text/event-stream")
            }
            conn.connect()

            if (conn.responseCode != 200) {
                conn.disconnect()
                return false
            }

            val stream = conn.inputStream
            activeInputStream = stream
            _isConnected.value = true
            _statusMessage.value = "ONLINE via Jacktor API"

            startHeartbeatLoop()
            sendInitialHandshake()

            val reader = java.io.BufferedReader(java.io.InputStreamReader(stream, Charsets.UTF_8))
            var line: String?
            var latestRtLine: String? = null

            while (kotlinx.coroutines.currentCoroutineContext().isActive && _isConnected.value) {
                line = reader.readLine() ?: break
                val trimmed = line.trim()
                if (trimmed.startsWith("data: ")) {
                    val rawJson = trimmed.substring(6).trim()
                    if (rawJson.isNotEmpty()) {
                        lastPacketReceivedTimestampMs = System.currentTimeMillis()
                        _isAmpAlive.value = true

                        if (rawJson.contains("\"rt\":")) {
                            latestRtLine = rawJson
                        } else {
                            val packet = TelemetryParser.parseLine(rawJson)
                            if (packet != null) {
                                repository?.processPacket(packet)
                                _telemetryFlow.tryEmit(packet)
                            }
                        }
                    }
                } else if (trimmed.isEmpty() && latestRtLine != null) {
                    val packet = TelemetryParser.parseLine(latestRtLine)
                    if (packet != null) {
                        repository?.processPacket(packet)
                        _telemetryFlow.tryEmit(packet)
                    }
                    latestRtLine = null
                }
            }
            true
        } catch (e: Exception) {
            Log.w(TAG, "Jacktor API stream error: ${e.message}")
            _isConnected.value = false
            _statusMessage.value = "Jacktor API offline — reconnecting..."
            false
        } finally {
            heartbeatJob?.cancel()
            closeSocketsOnly()
            _isConnected.value = false
        }
    }

    private suspend fun connectAndReadTcp(): Boolean {
        val host = tcpHost
        val port = tcpPort
        return try {
            _statusMessage.value = "Connecting to serial-bridge ($host:$port)..."
            val socket = Socket()
            socket.connect(InetSocketAddress(host, port), 2500)
            socket.soTimeout = 8000
            socket.tcpNoDelay = true
            socket.keepAlive = true

            tcpSocket = socket
            activeInputStream = socket.getInputStream()
            activeOutputStream = socket.getOutputStream()

            _isConnected.value = true
            _statusMessage.value = "ONLINE via serial-bridge TCP ($host:$port)"

            startHeartbeatLoop()
            sendInitialHandshake()

            readStreamLoop(activeInputStream!!)
            true
        } catch (e: Exception) {
            Log.w(TAG, "TCP serial-bridge error: ${e.message}")
            _isConnected.value = false
            _statusMessage.value = "serial-bridge ($host:$port) offline — reconnecting..."
            false
        } finally {
            heartbeatJob?.cancel()
            closeSocketsOnly()
            _isConnected.value = false
        }
    }

    private suspend fun connectAndReadUnix(): Boolean {
        val path = unixSocketPath
        return try {
            _statusMessage.value = "Connecting to serial-bridge Unix ($path)..."
            val socket = LocalSocket()
            socket.connect(LocalSocketAddress(path, LocalSocketAddress.Namespace.FILESYSTEM))
            socket.soTimeout = 8000

            unixSocket = socket
            activeInputStream = socket.inputStream
            activeOutputStream = socket.outputStream

            _isConnected.value = true
            _statusMessage.value = "ONLINE via serial-bridge UNIX ($path)"

            startHeartbeatLoop()
            sendInitialHandshake()

            readStreamLoop(activeInputStream!!)
            true
        } catch (e: Exception) {
            Log.w(TAG, "Unix serial-bridge error: ${e.message}")
            _isConnected.value = false
            _statusMessage.value = "serial-bridge Unix offline — reconnecting..."
            false
        } finally {
            heartbeatJob?.cancel()
            closeSocketsOnly()
            _isConnected.value = false
        }
    }

    private fun sendInitialHandshake() {
        writeRawLine("{\"type\":\"ping\"}")
        writeRawLine("{\"type\":\"analyzer\",\"cmd\":\"get\"}")
        writeRawLine("{\"type\":\"get_commands\"}")
    }

    private fun updateDetailedStatusMessage(usbPresent: Boolean, ampAlive: Boolean) {
        if (!_isConnected.value) {
            val target = if (_connectionMode.value == ConnectionMode.SERIAL_BRIDGE_UNIX) unixSocketPath else "$tcpHost:$tcpPort"
            _statusMessage.value = "OFFLINE — serial-bridge ($target) disconnected"
            return
        }

        val transport = if (_connectionMode.value == ConnectionMode.SERIAL_BRIDGE_UNIX) "UNIX" else "TCP"
        _statusMessage.value = when {
            usbPresent && ampAlive -> "ONLINE — Bridge: $transport | USB: OK | Amp: Active"
            usbPresent && !ampAlive -> "WARN — Bridge: $transport | USB: OK | Amp: No Ping / Off"
            !usbPresent -> "WARN — Bridge: $transport | USB: Disconnected | Amp: Offline"
            else -> "ONLINE via serial-bridge $transport"
        }
    }

    private fun startHeartbeatLoop() {
        heartbeatJob?.cancel()
        heartbeatJob = scope.launch {
            while (isActive && _isConnected.value) {
                val now = System.currentTimeMillis()
                val usbPresent = SerialBridgeShell.checkUsbHardwarePresent()
                _isUsbPresent.value = usbPresent

                // Heartbeat ping to MCU
                writeRawLine("{\"type\":\"ping\"}")

                // Evaluate whether the MCU has responded within timeout window
                val timeSinceLast = if (lastPacketReceivedTimestampMs > 0L) now - lastPacketReceivedTimestampMs else Long.MAX_VALUE
                val ampAlive = usbPresent && (timeSinceLast <= 3500L)
                val prevAlive = _isAmpAlive.value
                _isAmpAlive.value = ampAlive

                if (prevAlive && !ampAlive) {
                    // ESP32 turned off, disconnected, or reset button pressed
                    repository?.clearAlertsAndErrors()
                } else if (!prevAlive && ampAlive) {
                    // ESP32 rebooted or reconnected
                    repository?.resetMcuSession()
                }

                updateDetailedStatusMessage(usbPresent, ampAlive)

                delay(1500)
            }
        }
    }

    private fun readStreamLoop(input: InputStream) {
        val buffer = ByteArray(4096)
        synchronized(rxBuffer) { rxBuffer.setLength(0) }

        while (_isConnected.value) {
            val bytesRead = input.read(buffer)
            if (bytesRead > 0) {
                processIncomingBytes(buffer, bytesRead)
            } else if (bytesRead < 0) {
                break
            }
        }
    }

    private fun processIncomingBytes(buffer: ByteArray, bytesRead: Int) {
        val now = System.currentTimeMillis()
        val chunk = String(buffer, 0, bytesRead, Charsets.UTF_8)
        var latestRtLine: String? = null
        synchronized(rxBuffer) {
            rxBuffer.append(chunk)
            var nlIdx: Int
            while (rxBuffer.indexOf("\n").also { nlIdx = it } >= 0) {
                val rawLine = rxBuffer.substring(0, nlIdx).trim()
                rxBuffer.delete(0, nlIdx + 1)
                if (rawLine.isNotEmpty() && rawLine.contains('{')) {
                    lastPacketReceivedTimestampMs = now
                    _isAmpAlive.value = true
                    _isUsbPresent.value = true

                    if (rawLine.contains("\"rt\":")) {
                        // Coalesce burst of 30Hz rt packets within the same TCP read chunk to eliminate backlog
                        latestRtLine = rawLine
                    } else {
                        // Priority fast-path for setup_progress, setup_report, hz1, log, analyzer, ack
                        val packet = TelemetryParser.parseLine(rawLine)
                        if (packet != null) {
                            repository?.processPacket(packet)
                            _telemetryFlow.tryEmit(packet)
                        }
                    }
                }
            }
            if (rxBuffer.length > 16384) {
                rxBuffer.setLength(0)
            }
        }
        latestRtLine?.let { rtLine ->
            val packet = TelemetryParser.parseLine(rtLine)
            if (packet != null) {
                repository?.processPacket(packet)
                _telemetryFlow.tryEmit(packet)
            }
        }
    }

    private fun isMaintenanceMode(): Boolean {
        return File("/data/local/tmp/jacktor_flashing.lock").exists()
    }

    private fun writeRawLine(cmd: String): Boolean {
        val out = activeOutputStream ?: return false
        if (!_isConnected.value) return false
        return try {
            val clean = cmd.trim()
            if (clean.isEmpty()) return false
            val payload = (clean + "\n").toByteArray(Charsets.UTF_8)
            synchronized(txLock) {
                out.write(payload)
                out.flush()
            }
            true
        } catch (e: Exception) {
            Log.w(TAG, "TX failed: ${e.message}")
            _isConnected.value = false
            false
        }
    }

    fun sendCommand(jsonCommand: String) {
        scope.launch {
            val clean = jsonCommand.trim()
            if (clean.isEmpty()) return@launch

            // 1. Direct Socket Output Stream (Sub-millisecond latency & zero process fork!)
            if (_isConnected.value && writeRawLine(clean)) {
                return@launch
            }

            // 2. Direct High-Speed HTTP to jacktor-api (Port 80)
            try {
                val encoded = java.net.URLEncoder.encode(clean, "UTF-8")
                val url = java.net.URL("http://127.0.0.1/cgi-bin/jacktor-api?action=amp_cmd&cmd=$encoded")
                val conn = (url.openConnection() as java.net.HttpURLConnection).apply {
                    connectTimeout = 800
                    readTimeout = 800
                    requestMethod = "GET"
                    useCaches = false
                }
                val code = conn.responseCode
                conn.disconnect()
                if (code == 200) return@launch
            } catch (_: Exception) {
            }

            // 3. Fallback to native jacktor-api CLI
            try {
                val safeCmd = clean.replace("'", "")
                Runtime.getRuntime().exec(
                    arrayOf("su", "-c", "/data/adb/modules/jacktor-audio-framework/system/bin/jacktor-api amp_cmd '$safeCmd' >/dev/null 2>&1")
                )
            } catch (_: Exception) {
            }
        }
    }

    private fun closeSocketsOnly() {
        _isAmpAlive.value = false
        activeInputStream = null
        activeOutputStream = null
        try {
            tcpSocket?.close()
        } catch (_: Exception) {
        }
        tcpSocket = null

        try {
            unixSocket?.close()
        } catch (_: Exception) {
        }
        unixSocket = null
    }

    fun disconnect() {
        connectionJob?.cancel()
        heartbeatJob?.cancel()
        closeSocketsOnly()
        _isConnected.value = false
        _isAmpAlive.value = false
        _connectionMode.value = null
        _statusMessage.value = "Disconnected"
    }

    fun refreshConnection() {
        connectToSerialBridge()
    }
}
