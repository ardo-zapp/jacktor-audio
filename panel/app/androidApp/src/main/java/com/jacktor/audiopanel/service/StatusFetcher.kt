package com.jacktor.audiopanel.service

import android.content.Context
import android.util.Log
import com.jacktor.audiopanel.model.SerialBridgeStatus
import com.jacktor.audiopanel.parser.TelemetryParser
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.net.Inet4Address
import java.net.NetworkInterface

/**
 * StatusFetcher periodically queries the system status directly:
 * - serial-bridge daemon state & hardware presence
 * - active panel IP address
 */
class StatusFetcher(private val context: Context) {

    private val TAG = "StatusFetcher"
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var fetchJob: Job? = null

    private val _serialBridgeStatus = MutableStateFlow<SerialBridgeStatus?>(null)
    val serialBridgeStatus: StateFlow<SerialBridgeStatus?> = _serialBridgeStatus.asStateFlow()

    private val _panelIp = MutableStateFlow<String?>(null)
    val panelIp: StateFlow<String?> = _panelIp.asStateFlow()

    fun start() {
        fetchJob?.cancel()
        fetchJob = scope.launch {
            fetchStatus()
            while (isActive) {
                delay(3000) // Poll every 3 seconds
                if (isActive) fetchStatus()
            }
        }
    }

    fun stop() {
        fetchJob?.cancel()
        fetchJob = null
    }

    private suspend fun fetchStatus() {
        try {
            // 1. Fetch via Jacktor API HTTP localhost (Port 80)
            var apiFetched = false
            try {
                val url = java.net.URL("http://127.0.0.1/cgi-bin/jacktor-api?action=status")
                val conn = (url.openConnection() as java.net.HttpURLConnection).apply {
                    connectTimeout = 1500
                    readTimeout = 1500
                    requestMethod = "GET"
                }
                if (conn.responseCode == 200) {
                    val body = conn.inputStream.bufferedReader().use { it.readText() }
                    conn.disconnect()
                    val json = org.json.JSONObject(body)
                    if (json.optBoolean("success", false)) {
                        val ip = json.optString("ip", "")
                        if (ip.isNotEmpty() && !ip.startsWith("127.")) {
                            _panelIp.value = ip
                        }
                        val sbObj = json.optJSONObject("serial_bridge")
                        if (sbObj != null) {
                            val status = TelemetryParser.parseSerialBridgeStatus(sbObj.toString())
                            if (status != null) {
                                val hasUsb = SerialBridgeShell.checkUsbHardwarePresent(status.hwPort)
                                val resolved = if (hasUsb && status.hwExists == 0) status.copy(hwExists = 1) else status
                                _serialBridgeStatus.value = resolved
                                apiFetched = true
                            }
                        }
                    }
                }
            } catch (_: Exception) {
            }

            // 2. Direct query fallback if API not ready
            if (!apiFetched) {
                val sbStatus = SerialBridgeShell.queryStatus()
                val hasUsb = SerialBridgeShell.checkUsbHardwarePresent(sbStatus.hwPort)
                val resolvedStatus = if (hasUsb && sbStatus.hwExists == 0) {
                    sbStatus.copy(hwExists = 1)
                } else {
                    sbStatus
                }
                _serialBridgeStatus.value = resolvedStatus

                val ip = getActivePanelIp()
                if (ip != null) {
                    _panelIp.value = ip
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Status fetch error: ${e.message}")
        }
    }

    private fun getActivePanelIp(): String? {
        return try {
            val interfaces = NetworkInterface.getNetworkInterfaces()
            for (iface in interfaces) {
                if (iface.isLoopback || !iface.isUp) continue
                for (addr in iface.inetAddresses) {
                    if (!addr.isLoopbackAddress && addr is Inet4Address) {
                        val host = addr.hostAddress
                        if (!host.isNullOrEmpty() && !host.startsWith("127.")) {
                            return host
                        }
                    }
                }
            }
            null
        } catch (_: Exception) {
            null
        }
    }
}
