package com.jacktor.audiopanel

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.view.KeyEvent
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredHeight
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Apps
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Usb
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.jacktor.audiopanel.model.RealtimeTelemetry
import com.jacktor.audiopanel.model.SerialBridgeStatus
import com.jacktor.audiopanel.model.SlowTelemetry
import com.jacktor.audiopanel.parser.TelemetryParser
import com.jacktor.audiopanel.repository.AmplifierRepository
import com.jacktor.audiopanel.service.MediaNotificationListenerService
import com.jacktor.audiopanel.service.MediaTrackInfo
import com.jacktor.audiopanel.service.StatusFetcher
import com.jacktor.audiopanel.service.TelemetryConnectionManager
import com.jacktor.audiopanel.service.TelemetryConnectionManager.ConnectionMode
import com.jacktor.audiopanel.ui.components.AppDrawerDialog
import com.jacktor.audiopanel.ui.components.MediaPlaybackCard
import com.jacktor.audiopanel.ui.components.SetupTerminalFragment
import com.jacktor.audiopanel.ui.components.SpectrumAnalyzerView
import com.jacktor.audiopanel.ui.components.StereoVuMeterView
import com.jacktor.audiopanel.ui.components.SystemInfoCard
import com.jacktor.audiopanel.ui.components.TelemetryGaugeView
import com.jacktor.audiopanel.ui.theme.IndustrialBackground
import com.jacktor.audiopanel.ui.theme.IndustrialError
import com.jacktor.audiopanel.ui.theme.IndustrialPrimary
import com.jacktor.audiopanel.ui.theme.IndustrialSecondary
import com.jacktor.audiopanel.ui.theme.IndustrialSurface
import com.jacktor.audiopanel.ui.theme.IndustrialTertiary
import com.jacktor.audiopanel.ui.theme.IndustrialTheme
import com.jacktor.audiopanel.ui.theme.JetBrainsMono
import com.jacktor.audiopanel.utils.AppInfo
import com.jacktor.audiopanel.utils.AppLauncherManager
import com.jacktor.audiopanel.utils.LanguageController
import com.jacktor.audiopanel.utils.PanelStrings
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    companion object {
        var connectionManager: TelemetryConnectionManager? = null
        var repositoryInstance: AmplifierRepository? = null
    }

    private val repository = AmplifierRepository()
    private var partialWakeLock: PowerManager.WakeLock? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private var multicastLock: WifiManager.MulticastLock? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        repositoryInstance = repository

        // 24/7 Background Network & CPU WakeLock to guarantee WebUI, SSH & API services when screen is dark
        try {
            val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
            partialWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "JacktorAudioPanel:PartialWakeLock").apply {
                setReferenceCounted(false)
                acquire()
            }
            val wm = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            wifiLock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "JacktorAudioPanel:WifiLock").apply {
                setReferenceCounted(false)
                acquire()
            }
            multicastLock = wm.createMulticastLock("JacktorAudioPanel:MulticastLock").apply {
                setReferenceCounted(false)
                acquire()
            }
        } catch (_: Exception) {
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
            setShowWhenLocked(true)
            setTurnScreenOn(true)
        } else {
            @Suppress("DEPRECATION")
            window.addFlags(
                WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or
                    WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON
            )
        }

        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // Handle back button - stay on home launcher dashboard
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (repository.setupState.value.isActive && repository.setupState.value.overallStatus != "RUNNING") {
                    repository.dismissSetupTerminal()
                }
            }
        })

        LanguageController.init(this)
        val prefs = getSharedPreferences("JacktorPanelPrefs", Context.MODE_PRIVATE)
        val connMgr = TelemetryConnectionManager(this, repository)
        connectionManager = connMgr
        val statusFetcher = StatusFetcher(this)

        setContent {
            IndustrialTheme {
                val currentLang by LanguageController.currentLanguage
                val isConnected by connMgr.isConnected.collectAsState()
                val statusMsg by connMgr.statusMessage.collectAsState()
                val connectionMode by connMgr.connectionMode.collectAsState()

                val isPowerOn by repository.isPowerOn.collectAsState()
                val setupState by repository.setupState.collectAsState()

                // Status fetcher for panel-level info (serial-bridge hardware, IP, etc.)
                val serialBridgeStatus by statusFetcher.serialBridgeStatus.collectAsState()
                val panelIp by statusFetcher.panelIp.collectAsState()
                val connUsbPresent by connMgr.isUsbPresent.collectAsState()
                val isAmpAliveByConn by connMgr.isAmpAlive.collectAsState()

                // Media state from notification listener service
                val mediaTrack by MediaNotificationListenerService.mediaState.collectAsState()
                var coverBitmap by remember { mutableStateOf<Bitmap?>(null) }

                LaunchedEffect(mediaTrack) {
                    coverBitmap = MediaNotificationListenerService.getCover(this@MainActivity)
                }

                // Debounced USB hardware status (hysteresis: hold true for 2s after loss)
                val isUsbOkRaw = connUsbPresent || (serialBridgeStatus?.isHwPresent == true)
                val usbHwPresentDebounced = remember { mutableStateOf(isUsbOkRaw) }
                LaunchedEffect(isUsbOkRaw) {
                    if (isUsbOkRaw) {
                        usbHwPresentDebounced.value = true
                    } else if (usbHwPresentDebounced.value) {
                        delay(2000)
                        if (!isUsbOkRaw) {
                            usbHwPresentDebounced.value = false
                        }
                    }
                }

                var showAppDrawer by remember { mutableStateOf(false) }
                var installedApps by remember { mutableStateOf<List<AppInfo>>(emptyList()) }
                val configuredDelaySec = prefs.getInt("terminal_pass_delay_sec", 3).coerceIn(1, 15)
                var passCountdownSec by remember { mutableIntStateOf(configuredDelaySec) }

                // Auto-return from Black Terminal Fragment to Main Dashboard a few seconds after PASS (or FAIL)
                LaunchedEffect(setupState.isActive, setupState.overallStatus) {
                    val delayTarget = prefs.getInt("terminal_pass_delay_sec", 3).coerceIn(1, 15)
                    if (setupState.isActive && setupState.overallStatus == "PASS") {
                        for (sec in delayTarget downTo 1) {
                            passCountdownSec = sec
                            delay(1000L)
                        }
                        repository.dismissSetupTerminal()
                    } else if (setupState.isActive && setupState.overallStatus == "FAIL") {
                        val failDelay = (delayTarget + 3).coerceAtLeast(5)
                        for (sec in failDelay downTo 1) {
                            passCountdownSec = sec
                            delay(1000L)
                        }
                        repository.dismissSetupTerminal()
                    } else {
                        passCountdownSec = delayTarget
                    }
                }

                // Start status fetcher
                LaunchedEffect(Unit) {
                    statusFetcher.start()
                }

                val showBootTerminal = prefs.getBoolean("show_boot_terminal", true)

                Box(modifier = Modifier.fillMaxSize()) {
                    if (setupState.isActive && showBootTerminal) {
                        // Render ONLY SetupTerminalFragment during Power-On Self-Test
                        SetupTerminalFragment(
                            setupState = setupState,
                            countdownSeconds = passCountdownSec,
                            onDismiss = { repository.dismissSetupTerminal() },
                            modifier = Modifier.fillMaxSize()
                        )
                    } else {
                        val realtimeData by repository.realtimeData.collectAsState()
                        val spectrumBands by repository.spectrumBands.collectAsState()
                        val slowData by repository.slowData.collectAsState()

                        // Amp MCU link liveness
                        val isAmpOkRaw = isAmpAliveByConn || (realtimeData.link?.alive == true)
                        val ampLinkAliveDebounced = remember { mutableStateOf(isAmpOkRaw) }
                        LaunchedEffect(isAmpOkRaw) {
                            if (isAmpOkRaw) {
                                ampLinkAliveDebounced.value = true
                            } else if (ampLinkAliveDebounced.value) {
                                delay(2000)
                                if (!isAmpOkRaw) {
                                    ampLinkAliveDebounced.value = false
                                }
                            }
                        }

                        DashboardScreen(
                            isConnected = isConnected,
                            statusMsg = statusMsg,
                            connectionMode = connectionMode,
                            realtimeData = realtimeData,
                            spectrumBands = spectrumBands,
                            slowData = slowData,
                            isPowerOn = isPowerOn || (slowData.states?.on == true),
                            serialBridgeStatus = serialBridgeStatus,
                            panelIp = panelIp,
                            usbHwPresentDebounced = usbHwPresentDebounced.value,
                            ampLinkAliveDebounced = ampLinkAliveDebounced.value,
                            trackInfo = mediaTrack,
                            coverBitmap = coverBitmap,
                            onReconnectClick = {
                                connMgr.refreshConnection()
                            },
                            onOpenAppDrawer = {
                                installedApps = AppLauncherManager.getInstalledApps(this@MainActivity)
                                showAppDrawer = true
                            },
                            onOpenSettings = {
                                startActivity(Intent(this@MainActivity, SettingsActivity::class.java))
                            },
                            onCommand = { action, valParam ->
                                when (action) {
                                    "power" -> {
                                        val targetOn = valParam as Boolean
                                        if (targetOn) {
                                            if (prefs.getBoolean("show_boot_terminal", true)) {
                                                repository.startPowerOnTerminal("Power-On Command (pwr-on) Sent — Initializing...")
                                            }
                                        } else {
                                            repository.dismissSetupTerminal()
                                        }
                                        connMgr.sendCommand(TelemetryParser.buildPowerCommand(targetOn))
                                    }
                                    "pwr_bypass" -> {
                                        if (prefs.getBoolean("show_boot_terminal", true)) {
                                            repository.startPowerOnTerminal("Power-On Bypass Command (pwr-on --bypass) Sent — Initializing...")
                                        }
                                        connMgr.sendCommand(TelemetryParser.buildPowerCommand(true, bypass = true))
                                    }
                                    "setchannel" -> {
                                        connMgr.sendCommand(TelemetryParser.buildMediaCommand(action, valParam))
                                        CoroutineScope(Dispatchers.IO).launch {
                                            JacktorApiBridge.execute("channel_mode", mapOf("mode" to (valParam ?: 0).toString()))
                                        }
                                    }
                                    else -> {
                                        connMgr.sendCommand(TelemetryParser.buildMediaCommand(action, valParam))
                                    }
                                }
                            }
                        )
                    }
                }

                if (showAppDrawer) {
                    AppDrawerDialog(
                        apps = installedApps,
                        onDismiss = { showAppDrawer = false },
                        onAppClick = { pkg ->
                            showAppDrawer = false
                            AppLauncherManager.launchApp(this@MainActivity, pkg)
                        },
                        onRefreshApps = {
                            installedApps = AppLauncherManager.getInstalledApps(this@MainActivity)
                        }
                    )
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
    }

    override fun onDestroy() {
        super.onDestroy()
        try {
            partialWakeLock?.let { if (it.isHeld) it.release() }
            wifiLock?.let { if (it.isHeld) it.release() }
            multicastLock?.let { if (it.isHeld) it.release() }
        } catch (_: Exception) {
        }
        connectionManager?.disconnect()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            return true
        }
        return super.onKeyDown(keyCode, event)
    }
}

@Composable
fun StatusBadge(
    label: String,
    isOk: Boolean,
    icon: ImageVector,
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null
) {
    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier
            .requiredHeight(30.dp)
            .background(
                color = if (isOk) IndustrialTertiary else IndustrialError,
                shape = RoundedCornerShape(4.dp)
            )
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .padding(horizontal = 8.dp)
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center
        ) {
            Icon(
                imageVector = icon,
                contentDescription = label,
                tint = Color.Black,
                modifier = Modifier.size(17.dp)
            )
            Spacer(modifier = Modifier.width(5.dp))
            Text(
                text = label,
                color = Color.Black,
                fontSize = 12.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = JetBrainsMono,
                lineHeight = 12.sp,
                textAlign = TextAlign.Center
            )
        }
    }
}

@Composable
fun IndustrialTabButton(
    label: String,
    icon: ImageVector,
    isSelected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier
            .requiredHeight(30.dp)
            .background(
                color = if (isSelected) Color(0xFF162536) else Color.Transparent,
                shape = RoundedCornerShape(4.dp)
            )
            .border(
                width = 1.dp,
                color = if (isSelected) IndustrialPrimary else Color(0xFF263238),
                shape = RoundedCornerShape(4.dp)
            )
            .clickable(onClick = onClick)
            .padding(horizontal = 10.dp)
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center
        ) {
            Icon(
                imageVector = icon,
                contentDescription = label,
                tint = if (isSelected) IndustrialPrimary else Color(0xFF75879B),
                modifier = Modifier.size(16.dp)
            )
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                text = label,
                color = if (isSelected) IndustrialPrimary else Color(0xFF8090A0),
                fontSize = 11.5.sp,
                fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Medium,
                fontFamily = JetBrainsMono,
                lineHeight = 11.5.sp,
                textAlign = TextAlign.Center
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DashboardScreen(
    @Suppress("UNUSED_PARAMETER") isConnected: Boolean,
    statusMsg: String,
    connectionMode: ConnectionMode?,
    realtimeData: RealtimeTelemetry,
    spectrumBands: IntArray,
    slowData: SlowTelemetry,
    isPowerOn: Boolean,
    serialBridgeStatus: SerialBridgeStatus?,
    panelIp: String?,
    usbHwPresentDebounced: Boolean,
    ampLinkAliveDebounced: Boolean,
    trackInfo: MediaTrackInfo,
    coverBitmap: Bitmap?,
    onReconnectClick: () -> Unit,
    onOpenAppDrawer: () -> Unit,
    onOpenSettings: () -> Unit,
    onCommand: (String, Any?) -> Unit
) {
    // Tab State: 0 = AUDIO & MEDIA, 1 = SYSTEM & AMPLIFIER
    var selectedTab by rememberSaveable { mutableIntStateOf(0) }

    Scaffold(
        topBar = {
            androidx.compose.material3.Surface(
                color = IndustrialSurface,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(44.dp)
                    .border(width = 1.dp, color = Color(0xFF21262D))
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(horizontal = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "JACKTOR",
                        fontFamily = JetBrainsMono,
                        fontWeight = FontWeight.Bold,
                        color = IndustrialPrimary,
                        fontSize = 16.sp,
                        maxLines = 1
                    )

                    Spacer(modifier = Modifier.width(10.dp))

                    // Hardware Telemetry Badges
                    val isBridgeActive = serialBridgeStatus?.isRunning == true
                    StatusBadge(
                        label = if (isBridgeActive) "BRG" else "NO BRG",
                        isOk = isBridgeActive,
                        icon = Icons.Filled.Cloud,
                        onClick = onReconnectClick
                    )

                    Spacer(modifier = Modifier.width(5.dp))

                    StatusBadge(
                        label = if (usbHwPresentDebounced) "USB" else "NO USB",
                        isOk = usbHwPresentDebounced,
                        icon = Icons.Filled.Usb
                    )

                    Spacer(modifier = Modifier.width(5.dp))

                    StatusBadge(
                        label = if (ampLinkAliveDebounced) "AMP" else "NO AMP",
                        isOk = ampLinkAliveDebounced,
                        icon = Icons.Filled.Memory
                    )

                    Spacer(modifier = Modifier.width(12.dp))

                    // Industrial Tab Selector
                    Row(
                        modifier = Modifier
                            .background(Color(0xFF0D0F12), RoundedCornerShape(6.dp))
                            .border(1.dp, Color(0xFF21262D), RoundedCornerShape(6.dp))
                            .padding(2.dp),
                        horizontalArrangement = Arrangement.spacedBy(4.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        IndustrialTabButton(
                            label = PanelStrings.tabAudioMedia(LanguageController.currentLanguage.value),
                            icon = Icons.Default.GraphicEq,
                            isSelected = selectedTab == 0,
                            onClick = { selectedTab = 0 }
                        )

                        IndustrialTabButton(
                            label = PanelStrings.tabSystemAmp(LanguageController.currentLanguage.value),
                            icon = Icons.Default.Memory,
                            isSelected = selectedTab == 1,
                            onClick = { selectedTab = 1 }
                        )
                    }

                    Spacer(modifier = Modifier.weight(1f))

                    // Power Button
                    Button(
                        onClick = { onCommand("power", !isPowerOn) },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (isPowerOn) Color(0xFF10B981) else Color(0xFFE53935)
                        ),
                        shape = RoundedCornerShape(4.dp),
                        contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp),
                        modifier = Modifier.requiredHeight(30.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.PowerSettingsNew,
                            contentDescription = "Power",
                            tint = Color.Black,
                            modifier = Modifier.size(17.dp)
                        )
                        Spacer(modifier = Modifier.width(5.dp))
                        Text(
                            text = "POWER",
                            color = Color.Black,
                            fontWeight = FontWeight.Bold,
                            fontSize = 12.sp,
                            fontFamily = JetBrainsMono,
                            lineHeight = 12.sp
                        )
                    }

                    if (!isPowerOn) {
                        Spacer(modifier = Modifier.width(6.dp))
                        OutlinedButton(
                            onClick = { onCommand("pwr_bypass", null) },
                            shape = RoundedCornerShape(4.dp),
                            contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                            modifier = Modifier.requiredHeight(30.dp)
                        ) {
                            Text(
                                text = "BYPASS",
                                color = IndustrialSecondary,
                                fontWeight = FontWeight.Bold,
                                fontSize = 11.5.sp,
                                fontFamily = JetBrainsMono,
                                lineHeight = 11.5.sp
                            )
                        }
                    }

                    Spacer(modifier = Modifier.width(6.dp))

                    // App Drawer Button
                    OutlinedButton(
                        onClick = onOpenAppDrawer,
                        shape = RoundedCornerShape(4.dp),
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                        modifier = Modifier.requiredHeight(30.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Apps,
                            contentDescription = "Apps",
                            tint = IndustrialPrimary,
                            modifier = Modifier.size(17.dp)
                        )
                        Spacer(modifier = Modifier.width(5.dp))
                        Text(
                            text = "APPS",
                            color = IndustrialPrimary,
                            fontWeight = FontWeight.Bold,
                            fontSize = 12.sp,
                            fontFamily = JetBrainsMono,
                            lineHeight = 12.sp
                        )
                    }

                    Spacer(modifier = Modifier.width(4.dp))

                    IconButton(
                        onClick = onOpenSettings,
                        modifier = Modifier.size(34.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Settings,
                            contentDescription = "Settings",
                            tint = IndustrialPrimary,
                            modifier = Modifier.size(22.dp)
                        )
                    }
                }
            }
        },
        containerColor = IndustrialBackground
    ) { padding ->
        // Conditional Rendering: ONLY compose and execute 30 FPS Canvas draws when on Tab 0 (AUDIO & MEDIA).
        // When on Tab 1 (SYSTEM & AMP), Canvas operations are completely skipped to save GPU/CPU cycles.
        when (selectedTab) {
            0 -> {
                // TAB 1: AUDIO & MEDIA
                Row(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(padding)
                        .padding(horizontal = 8.dp, vertical = 6.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    // Left Column: Visualizers (Spectrum Analyzer + Stereo VU Meter)
                    Column(
                        modifier = Modifier
                            .weight(1.15f)
                            .fillMaxHeight(),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        SpectrumAnalyzerView(
                            bands = spectrumBands,
                            mode = realtimeData.mode ?: "fft",
                            boxHeight = 150.dp,
                            modifier = Modifier.fillMaxWidth()
                        )

                        StereoVuMeterView(
                            vuL = realtimeData.vuL,
                            vuR = realtimeData.vuR,
                            clipping = realtimeData.clipping,
                            modifier = Modifier.fillMaxWidth()
                        )
                    }

                    // Right Column: Media Playback, Cover Art, Transport & DSP
                    MediaPlaybackCard(
                        trackInfo = trackInfo,
                        coverBitmap = coverBitmap,
                        btTelemetry = slowData.bt,
                        realtimeBtStatus = realtimeData.btStatus,
                        bassBoost = slowData.nvs?.bassBoost ?: false,
                        sound3d = slowData.nvs?.audio3d ?: false,
                        channelMode = slowData.nvs?.chMode ?: 0,
                        isPowerOn = isPowerOn,
                        onCommand = onCommand,
                        modifier = Modifier
                            .weight(0.85f)
                            .fillMaxHeight()
                    )
                }
            }
            1 -> {
                // TAB 2: SYSTEM & AMPLIFIER
                Row(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(padding)
                        .padding(horizontal = 8.dp, vertical = 6.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    // Left Column: Amplifier Hardware Telemetry Gauges
                    TelemetryGaugeView(
                        slowData = slowData,
                        realtimeData = realtimeData,
                        isAmpAlive = ampLinkAliveDebounced,
                        modifier = Modifier
                            .weight(1.05f)
                            .fillMaxHeight()
                    )

                    // Right Column: Panel Architecture & Network Information
                    SystemInfoCard(
                        serialBridgeStatus = serialBridgeStatus,
                        panelIp = panelIp,
                        connectionMode = connectionMode,
                        statusMsg = statusMsg,
                        isPowerOn = isPowerOn,
                        modifier = Modifier
                            .weight(0.95f)
                            .fillMaxHeight()
                    )
                }
            }
        }
    }
}
