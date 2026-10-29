package com.jacktor.audio.panel

import android.content.Context
import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Apps
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Usb
import androidx.compose.material.icons.filled.UsbOff
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.jacktor.audio.panel.shared.parser.TelemetryParser
import com.jacktor.audio.panel.shared.repository.AmplifierRepository
import com.jacktor.audio.panel.shared.service.UsbSerialManager
import com.jacktor.audio.panel.ui.components.AppDrawerDialog
import com.jacktor.audio.panel.ui.components.MediaControlToolbar
import com.jacktor.audio.panel.ui.components.SpectrumAnalyzerView
import com.jacktor.audio.panel.ui.components.StereoVuMeterView
import com.jacktor.audio.panel.ui.components.TelemetryGaugeView
import com.jacktor.audio.panel.ui.theme.IndustrialBackground
import com.jacktor.audio.panel.ui.theme.IndustrialError
import com.jacktor.audio.panel.ui.theme.IndustrialPrimary
import com.jacktor.audio.panel.ui.theme.IndustrialSecondary
import com.jacktor.audio.panel.ui.theme.IndustrialSurface
import com.jacktor.audio.panel.ui.theme.IndustrialTertiary
import com.jacktor.audio.panel.ui.theme.IndustrialTheme
import com.jacktor.audio.panel.utils.AppInfo
import com.jacktor.audio.panel.utils.AppLauncherManager
import kotlinx.coroutines.flow.collectLatest

class MainActivity : ComponentActivity() {

    companion object {
        var usbSerialManager: UsbSerialManager? = null
    }

    private val repository = AmplifierRepository()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val usbMgr = UsbSerialManager(this)
        usbSerialManager = usbMgr

        setContent {
            IndustrialTheme {
                val isConnected by usbMgr.isConnected.collectAsState()
                val statusMsg by usbMgr.statusMessage.collectAsState()

                val realtimeData by repository.realtimeData.collectAsState()
                val spectrumBands by repository.spectrumBands.collectAsState()
                val slowData by repository.slowData.collectAsState()

                var showAppDrawer by remember { mutableStateOf(false) }
                var installedApps by remember { mutableStateOf<List<AppInfo>>(emptyList()) }

                LaunchedEffect(Unit) {
                    usbMgr.telemetryFlow.collectLatest { packet ->
                        repository.processPacket(packet)
                    }
                }

                DashboardScreen(
                    isConnected = isConnected,
                    statusMsg = statusMsg,
                    realtimeData = realtimeData,
                    spectrumBands = spectrumBands,
                    slowData = slowData,
                    onConnectClick = {
                        if (isConnected) usbMgr.disconnect() else usbMgr.connect()
                    },
                    onOpenAppDrawer = {
                        installedApps = AppLauncherManager.getInstalledApps(this@MainActivity)
                        showAppDrawer = true
                    },
                    onOpenSettings = {
                        startActivity(Intent(this@MainActivity, SettingsActivity::class.java))
                    },
                    onCommand = { action, valParam ->
                        val cmdStr = when (action) {
                            "power" -> TelemetryParser.buildPowerCommand(valParam as Boolean)
                            "pwr_bypass" -> TelemetryParser.buildPowerCommand(true, bypass = true)
                            else -> TelemetryParser.buildMediaCommand(action, valParam)
                        }
                        usbMgr.sendCommand(cmdStr)
                    }
                )

                if (showAppDrawer) {
                    AppDrawerDialog(
                        apps = installedApps,
                        onDismiss = { showAppDrawer = false },
                        onAppClick = { pkg ->
                            showAppDrawer = false
                            AppLauncherManager.launchApp(this@MainActivity, pkg)
                        }
                    )
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        usbSerialManager?.disconnect()
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DashboardScreen(
    isConnected: Boolean,
    statusMsg: String,
    realtimeData: com.jacktor.audio.panel.shared.model.RealtimeTelemetry,
    spectrumBands: IntArray,
    slowData: com.jacktor.audio.panel.shared.model.SlowTelemetry,
    onConnectClick: () -> Unit,
    onOpenAppDrawer: () -> Unit,
    onOpenSettings: () -> Unit,
    onCommand: (String, Any?) -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = "JACKTOR LAUNCHER",
                            fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.Bold,
                            color = IndustrialPrimary,
                            fontSize = 16.sp
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Box(
                            modifier = Modifier
                                .background(
                                    if (isConnected) IndustrialTertiary else IndustrialError,
                                    RoundedCornerShape(4.dp)
                                )
                                .padding(horizontal = 6.dp, vertical = 2.dp)
                        ) {
                            Text(
                                text = if (isConnected) "ONLINE" else "OFFLINE",
                                color = Color.Black,
                                fontSize = 10.sp,
                                fontWeight = FontWeight.Bold,
                                fontFamily = FontFamily.Monospace
                            )
                        }
                    }
                },
                actions = {
                    // App Drawer Button
                    OutlinedButton(
                        onClick = onOpenAppDrawer,
                        shape = RoundedCornerShape(4.dp),
                        modifier = Modifier.height(32.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Apps,
                            contentDescription = "Apps",
                            tint = IndustrialPrimary,
                            modifier = Modifier.height(16.dp)
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = "APPS",
                            color = IndustrialPrimary,
                            fontWeight = FontWeight.Bold,
                            fontSize = 10.sp,
                            fontFamily = FontFamily.Monospace
                        )
                    }

                    Spacer(modifier = Modifier.width(8.dp))

                    Button(
                        onClick = onConnectClick,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (isConnected) IndustrialError else IndustrialPrimary
                        ),
                        shape = RoundedCornerShape(4.dp),
                        modifier = Modifier.height(32.dp)
                    ) {
                        Icon(
                            imageVector = if (isConnected) Icons.Default.UsbOff else Icons.Default.Usb,
                            contentDescription = "USB Connect",
                            tint = Color.Black
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = if (isConnected) "DISCONNECT" else "CONNECT USB",
                            color = Color.Black,
                            fontWeight = FontWeight.Bold,
                            fontSize = 10.sp,
                            fontFamily = FontFamily.Monospace
                        )
                    }

                    IconButton(onClick = onOpenSettings) {
                        Icon(Icons.Default.Settings, contentDescription = "Settings", tint = IndustrialPrimary)
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = IndustrialSurface)
            )
        },
        containerColor = IndustrialBackground
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(12.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Status Banner
            Text(
                text = "STATUS: $statusMsg",
                color = IndustrialSecondary,
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace
            )

            // Spectrum Analyzer
            SpectrumAnalyzerView(
                bands = spectrumBands,
                mode = realtimeData.mode ?: "fft",
                modifier = Modifier.fillMaxWidth()
            )

            // Stereo VU Meter
            StereoVuMeterView(
                vuL = realtimeData.vuL,
                vuR = realtimeData.vuR,
                clipping = realtimeData.clipping,
                modifier = Modifier.fillMaxWidth()
            )

            // Telemetry Gauges
            TelemetryGaugeView(
                slowData = slowData,
                modifier = Modifier.fillMaxWidth()
            )

            // GAIA Media Controls & Power Toolbar
            MediaControlToolbar(
                onCommand = onCommand,
                isPowerOn = slowData.states?.on ?: false,
                modifier = Modifier.fillMaxWidth()
            )
        }
    }
}
