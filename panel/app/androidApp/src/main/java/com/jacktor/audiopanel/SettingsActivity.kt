package com.jacktor.audiopanel

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.jacktor.audiopanel.model.NvsSettings
import com.jacktor.audiopanel.model.RealtimeTelemetry
import com.jacktor.audiopanel.model.SlowTelemetry
import com.jacktor.audiopanel.parser.TelemetryParser
import com.jacktor.audiopanel.ui.theme.IndustrialBackground
import com.jacktor.audiopanel.ui.theme.IndustrialError
import com.jacktor.audiopanel.ui.theme.IndustrialPrimary
import com.jacktor.audiopanel.ui.theme.IndustrialSecondary
import com.jacktor.audiopanel.ui.theme.IndustrialSurface
import com.jacktor.audiopanel.ui.theme.IndustrialTertiary
import com.jacktor.audiopanel.ui.theme.IndustrialTextSecondary
import com.jacktor.audiopanel.ui.theme.IndustrialTheme
import com.jacktor.audiopanel.ui.theme.JetBrainsMono
import com.jacktor.audiopanel.utils.AppLanguage
import com.jacktor.audiopanel.utils.LanguageController
import com.jacktor.audiopanel.utils.PanelStrings
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

class SettingsActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        LanguageController.init(this)
        val prefs = getSharedPreferences("JacktorPanelPrefs", Context.MODE_PRIVATE)
        val repo = MainActivity.repositoryInstance

        // Query latest analyzer & deviceinfo state on opening Settings
        MainActivity.connectionManager?.sendCommand(TelemetryParser.buildSimpleCommand("ana-get"))
        MainActivity.connectionManager?.sendCommand(TelemetryParser.buildSimpleCommand("deviceinfo"))

        setContent {
            IndustrialTheme {
                val slowData by (repo?.slowData?.collectAsState() ?: remember { mutableStateOf(SlowTelemetry()) })
                val realtimeData by (repo?.realtimeData?.collectAsState() ?: remember { mutableStateOf(com.jacktor.audiopanel.model.RealtimeTelemetry()) })
                val nvsData by (repo?.nvsSettings?.collectAsState() ?: remember { mutableStateOf(NvsSettings()) })
                val scannedSensors by (repo?.scannedTempSensors?.collectAsState() ?: remember { mutableStateOf(emptyList()) })

                SettingsScreen(
                    prefs = prefs,
                    slowData = slowData,
                    realtimeData = realtimeData,
                    nvsData = nvsData,
                    scannedSensors = scannedSensors,
                    onBack = { finish() },
                    onSendRawCommand = { cmdJson ->
                        MainActivity.connectionManager?.sendCommand(cmdJson)
                    },
                    onSaveNvs = { key, value ->
                        when (value) {
                            is Boolean -> prefs.edit().putBoolean(key, value).apply()
                            is Float -> prefs.edit().putFloat(key, value).apply()
                            is Int -> prefs.edit().putInt(key, value).apply()
                            is Long -> prefs.edit().putLong(key, value).apply()
                            is String -> prefs.edit().putString(key, value).apply()
                        }
                        val cmdStr = TelemetryParser.buildNvsConfigCommand(key, value)
                        MainActivity.connectionManager?.sendCommand(cmdStr)
                    }
                )
            }
        }
    }
}

/**
 * Universal Non-blocking Jacktor API HTTP Client for localhost Port 80
 */
object JacktorApiBridge {
    suspend fun execute(action: String, params: Map<String, String> = emptyMap()): JSONObject? = withContext(Dispatchers.IO) {
        try {
            val query = StringBuilder("action=").append(URLEncoder.encode(action, "UTF-8"))
            for ((k, v) in params) {
                query.append("&").append(URLEncoder.encode(k, "UTF-8"))
                    .append("=").append(URLEncoder.encode(v, "UTF-8"))
            }
            val url = URL("http://127.0.0.1/cgi-bin/jacktor-api?$query")
            val conn = (url.openConnection() as HttpURLConnection).apply {
                connectTimeout = 2500
                readTimeout = 2500
                requestMethod = "GET"
            }
            val code = conn.responseCode
            val body = if (code == 200) conn.inputStream.bufferedReader().use { it.readText() } else null
            conn.disconnect()
            if (!body.isNullOrBlank()) JSONObject(body) else null
        } catch (_: Exception) {
            null
        }
    }

    suspend fun runRoot(cmd: String): String = withContext(Dispatchers.IO) {
        try {
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", cmd))
            val reader = process.inputStream.bufferedReader()
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

    suspend fun verifyPassword(password: String): Boolean = withContext(Dispatchers.IO) {
        if (password.isBlank()) return@withContext false
        try {
            val res = execute("auth_login", mapOf("user" to "root", "pass" to password))
            if (res?.optBoolean("authenticated", false) == true) {
                return@withContext true
            }
        } catch (_: Exception) {}
        try {
            val conf = runRoot("cat /data/adb/modules/jacktor-audio-framework/config/auth.conf 2>/dev/null")
            val passLine = conf.lines().firstOrNull { it.trim().startsWith("password=") }
            if (passLine != null) {
                val expected = passLine.substringAfter("password=").trim()
                if (password == expected) return@withContext true
            } else {
                if (password == "furry2026") return@withContext true
            }
        } catch (_: Exception) {}
        false
    }
}

data class DangerActionPrompt(
    val title: String,
    val description: String,
    val confirmButtonText: String = "KONFIRMASI",
    val onConfirm: () -> Unit
)

@Composable
private fun PasswordConfirmationDialog(
    prompt: DangerActionPrompt,
    onDismiss: () -> Unit,
    onVerified: () -> Unit
) {
    var password by remember { mutableStateOf("") }
    var errorMessage by remember { mutableStateOf<String?>(null) }
    var successMessage by remember { mutableStateOf<String?>(null) }
    var isVerifying by remember { mutableStateOf(false) }
    var isResettingRoot by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()

    AlertDialog(
        onDismissRequest = { if (!isVerifying && !isResettingRoot) onDismiss() },
        title = {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Icon(
                    imageVector = Icons.Default.Warning,
                    contentDescription = null,
                    tint = IndustrialError
                )
                Text(
                    text = prompt.title,
                    color = Color.White,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = JetBrainsMono
                )
            }
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    text = prompt.description,
                    color = Color(0xFFD1D5DB),
                    fontSize = 11.5.sp,
                    fontFamily = JetBrainsMono,
                    lineHeight = 16.sp
                )

                Text(
                    text = "Masukkan Password Sistem (root):",
                    color = IndustrialPrimary,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.SemiBold,
                    fontFamily = JetBrainsMono
                )

                OutlinedTextField(
                    value = password,
                    onValueChange = {
                        password = it
                        errorMessage = null
                        successMessage = null
                    },
                    singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    isError = errorMessage != null,
                    placeholder = { Text("Password root...", color = Color.Gray, fontSize = 11.sp, fontFamily = JetBrainsMono) },
                    modifier = Modifier.fillMaxWidth()
                )

                if (errorMessage != null) {
                    Text(
                        text = errorMessage ?: "",
                        color = IndustrialError,
                        fontSize = 10.5.sp,
                        fontFamily = JetBrainsMono,
                        fontWeight = FontWeight.Bold
                    )
                }

                if (successMessage != null) {
                    Text(
                        text = successMessage ?: "",
                        color = IndustrialTertiary,
                        fontSize = 10.5.sp,
                        fontFamily = JetBrainsMono,
                        fontWeight = FontWeight.Bold
                    )
                }

                // Tombol Opsi Lupa Password via Akses Root Fisik
                OutlinedButton(
                    onClick = {
                        isResettingRoot = true
                        errorMessage = null
                        successMessage = null
                        scope.launch {
                            val cmd = "printf 'username=root\\npassword=furry2026\\n' > /data/adb/modules/jacktor-audio-framework/config/auth.conf; " +
                                "chmod 600 /data/adb/modules/jacktor-audio-framework/config/auth.conf 2>/dev/null; " +
                                "mkdir -p /data/adb/ssh 2>/dev/null; " +
                                "printf 'username=root\\npassword=furry2026\\n' > /data/adb/ssh/auth.conf; " +
                                "chmod 600 /data/adb/ssh/auth.conf 2>/dev/null; " +
                                "rm -f /data/adb/modules/jacktor-audio-framework/config/token.auth 2>/dev/null"
                            JacktorApiBridge.runRoot(cmd)
                            isResettingRoot = false
                            password = "furry2026"
                            successMessage = "Password di-reset ke default: furry2026"
                        }
                    },
                    enabled = !isResettingRoot && !isVerifying,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(30.dp),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 6.dp, vertical = 0.dp),
                    shape = RoundedCornerShape(4.dp)
                ) {
                    if (isResettingRoot) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(12.dp),
                            strokeWidth = 1.5.dp,
                            color = IndustrialSecondary
                        )
                    } else {
                        Text(
                            text = "LUPA PASSWORD? RESET FISIK KE DEFAULT",
                            color = IndustrialSecondary,
                            fontSize = 9.5.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = JetBrainsMono
                        )
                    }
                }
            }
        },
        confirmButton = {
            Button(
                onClick = {
                    if (password.isBlank()) {
                        errorMessage = "Password tidak boleh kosong!"
                        return@Button
                    }
                    isVerifying = true
                    errorMessage = null
                    successMessage = null
                    scope.launch {
                        val valid = JacktorApiBridge.verifyPassword(password)
                        isVerifying = false
                        if (valid) {
                            onVerified()
                        } else {
                            errorMessage = "Password salah! Tindakan dibatalkan."
                        }
                    }
                },
                enabled = !isVerifying && !isResettingRoot,
                colors = ButtonDefaults.buttonColors(containerColor = IndustrialError),
                shape = RoundedCornerShape(4.dp)
            ) {
                if (isVerifying) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(14.dp),
                        color = Color.White,
                        strokeWidth = 2.dp
                    )
                } else {
                    Text(
                        text = prompt.confirmButtonText,
                        color = Color.White,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = JetBrainsMono
                    )
                }
            }
        },
        dismissButton = {
            OutlinedButton(
                onClick = onDismiss,
                enabled = !isVerifying && !isResettingRoot,
                shape = RoundedCornerShape(4.dp)
            ) {
                Text("BATAL", color = Color.White, fontSize = 11.sp, fontFamily = JetBrainsMono)
            }
        },
        containerColor = Color(0xFF0F172A),
        shape = RoundedCornerShape(12.dp)
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    prefs: android.content.SharedPreferences,
    slowData: SlowTelemetry,
    realtimeData: com.jacktor.audiopanel.model.RealtimeTelemetry,
    nvsData: NvsSettings,
    scannedSensors: List<com.jacktor.audiopanel.model.TempScannedSensor> = emptyList(),
    onBack: () -> Unit,
    onSendRawCommand: (String) -> Unit,
    onSaveNvs: (String, Any) -> Unit
) {
    var selectedTab by remember { mutableIntStateOf(0) }
    val currentLang by LanguageController.currentLanguage
    val tabs = listOf(
        PanelStrings.settingsTabPanel(currentLang),
        PanelStrings.settingsTabAudio(currentLang),
        PanelStrings.settingsTabAmp(currentLang),
        PanelStrings.settingsTabAnalyzer(currentLang),
        PanelStrings.settingsTabThermal(currentLang)
    )

    val scope = rememberCoroutineScope()
    var systemStatus by remember { mutableStateOf<JSONObject?>(null) }
    var isRefreshing by remember { mutableStateOf(false) }
    var dangerPrompt by remember { mutableStateOf<DangerActionPrompt?>(null) }

    fun refreshStatus() {
        scope.launch {
            isRefreshing = true
            systemStatus = JacktorApiBridge.execute("status")
            isRefreshing = false
        }
    }

    LaunchedEffect(Unit) {
        while (isActive) {
            val s = JacktorApiBridge.execute("status")
            if (s != null) {
                systemStatus = s
            }
            delay(3000L)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            "JACKTOR PANEL & DSP SETTINGS",
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold,
                            color = IndustrialPrimary,
                            fontSize = 16.sp
                        )
                        Spacer(modifier = Modifier.weight(1f))
                        IconButton(onClick = { refreshStatus() }) {
                            Icon(
                                Icons.Default.Refresh,
                                contentDescription = "Refresh",
                                tint = if (isRefreshing) IndustrialSecondary else IndustrialPrimary,
                                modifier = Modifier.size(24.dp)
                            )
                        }
                    }
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back", tint = IndustrialPrimary, modifier = Modifier.size(24.dp))
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
        ) {
            // Category Tabs Bar (Consistent with Industrial Webroot)
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF0D0F12))
                    .horizontalScroll(rememberScrollState())
                    .padding(horizontal = 10.dp, vertical = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                tabs.forEachIndexed { idx, title ->
                    val active = selectedTab == idx
                    Button(
                        onClick = { selectedTab = idx },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (active) IndustrialPrimary else Color(0xFF1E242C)
                        ),
                        shape = RoundedCornerShape(4.dp),
                        modifier = Modifier.height(36.dp),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 12.dp, vertical = 0.dp)
                    ) {
                        Text(
                            text = title,
                            color = if (active) Color.Black else IndustrialPrimary,
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold,
                            fontSize = 12.sp
                        )
                    }
                }
            }

            // Tab Body with Scroll
            Column(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .padding(10.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                when (selectedTab) {
                    0 -> PanelAndSystemTab(
                        prefs = prefs,
                        status = systemStatus,
                        onRefresh = { refreshStatus() }
                    )
                    1 -> AudioAndDspTab(
                        status = systemStatus,
                        onRefresh = { refreshStatus() }
                    )
                    2 -> PowerRelaysAndBtTab(
                        prefs = prefs,
                        slowData = slowData,
                        realtimeData = realtimeData,
                        nvsData = nvsData,
                        onSendRawCommand = onSendRawCommand,
                        onSaveNvs = onSaveNvs,
                        onRequestDangerConfirm = { dangerPrompt = it }
                    )
                    3 -> AnalyzerAndAdcTab(
                        prefs = prefs,
                        slowData = slowData,
                        realtimeData = realtimeData,
                        onSendRawCommand = onSendRawCommand
                    )
                    4 -> ThermalAndSystemTab(
                        prefs = prefs,
                        slowData = slowData,
                        realtimeData = realtimeData,
                        nvsData = nvsData,
                        scannedSensors = scannedSensors,
                        onBack = onBack,
                        onSendRawCommand = onSendRawCommand,
                        onSaveNvs = onSaveNvs,
                        onRequestDangerConfirm = { dangerPrompt = it }
                    )
                }

                Spacer(modifier = Modifier.height(4.dp))

                Button(
                    onClick = onBack,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(40.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = IndustrialPrimary),
                    shape = RoundedCornerShape(4.dp)
                ) {
                    Icon(Icons.Default.Save, contentDescription = "Done", tint = Color.Black)
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        "DONE & RETURN TO DASHBOARD",
                        color = Color.Black,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace,
                        fontSize = 11.sp
                    )
                }
            }
        }

        dangerPrompt?.let { prompt ->
            PasswordConfirmationDialog(
                prompt = prompt,
                onDismiss = { dangerPrompt = null },
                onVerified = {
                    val p = dangerPrompt
                    dangerPrompt = null
                    p?.onConfirm?.invoke()
                }
            )
        }
    }
}

/* ==============================================================================
   TAB 0: PANEL & SISTEM (Layar, Resolusi, Stay-On, Battery Bypass, CPU, SSH)
============================================================================== */
@Composable
private fun PanelAndSystemTab(
    prefs: android.content.SharedPreferences,
    status: JSONObject?,
    onRefresh: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    val tweaks = status?.optJSONObject("tweaks")
    val kernel = status?.optJSONObject("kernel")
    val cpu = status?.optJSONObject("cpu")
    val battery = status?.optJSONObject("battery")
    val ssh = status?.optJSONObject("ssh")

    var rotation by remember(tweaks) { mutableIntStateOf(tweaks?.optInt("rotation", 1) ?: 1) }
    val rawStayMode = tweaks?.optString("stay_on_mode", "").orEmpty()
    val initialStayMode = when {
        rawStayMode.isNotEmpty() -> rawStayMode
        tweaks?.optInt("stayon", 1) == 1 -> "always"
        tweaks?.optInt("stayon", 1) == 2 -> "auto"
        else -> "normal"
    }
    var stayOnMode by remember(tweaks) { mutableStateOf(initialStayMode) }
    var lockscreenDisabled by remember(tweaks) { mutableIntStateOf(tweaks?.optInt("lockscreen_disabled", 1) ?: 1) }
    var autosuspend by remember(tweaks) { mutableIntStateOf(tweaks?.optInt("autosuspend", -1) ?: -1) }

    var isBypass by remember(battery) { mutableStateOf(battery?.optBoolean("is_bypass", true) ?: true) }
    var cpuProfile by remember(cpu) { mutableStateOf(cpu?.optString("profile", "balanced") ?: "balanced") }

    var sshRunning by remember(ssh) { mutableIntStateOf(ssh?.optInt("running", 1) ?: 1) }
    var sshAutostart by remember(ssh) { mutableIntStateOf(ssh?.optInt("autostart", 1) ?: 1) }

    // Launcher & Terminal Prefs
    var showBootTerminal by remember { mutableStateOf(prefs.getBoolean("show_boot_terminal", true)) }
    var terminalDelaySec by remember { mutableFloatStateOf(prefs.getInt("terminal_pass_delay_sec", 3).toFloat()) }

    // 0. Language Selector (Bahasa Indonesia & English)
    val currentLang by LanguageController.currentLanguage
    SectionCard(title = PanelStrings.appLanguageTitle(currentLang)) {
        Text(
            text = PanelStrings.selectLanguage(currentLang),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            AppLanguage.values().forEach { langOption ->
                val selected = currentLang == langOption
                OptionChipButton(
                    text = "${langOption.flag} ${langOption.displayName}",
                    selected = selected,
                    onClick = {
                        LanguageController.setLanguage(context, langOption)
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }
    }

    // 1. Hardware Architecture & Kernel
    SectionCard(title = PanelStrings.current(currentLang).panelKernelTitle) {
        InfoRow("Model Perangkat", status?.optString("device_model", "Realme C15 (Bengal SM4250)") ?: "Realme C15")
        InfoRow("Versi Kernel", kernel?.optString("display", "Linux 4.19.152-perf+") ?: "Linux 4.19.152-perf+")
        InfoRow("Uptime Sistem", status?.optString("uptime_id", "19 jam 13 menit") ?: "-")
        InfoRow("Suhu CPU / SoC", cpu?.optString("temp", "45 °C") ?: "-")
        InfoRow("Status Governor", cpu?.optString("governor", "schedutil") ?: "schedutil")
    }

    // 2. Layar & Orientasi
    SectionCard(title = PanelStrings.current(currentLang).orientationResolutionTitle) {
        Text(
            text = PanelStrings.current(currentLang).panelRotationLabel,
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(
                1 to "LANDSCAPE 90°",
                0 to "PORTRAIT 0°",
                3 to "REV LANDSCAPE 270°",
                2 to "REV PORTRAIT 180°"
            ).forEach { (rotVal, label) ->
                OptionChipButton(
                    text = label,
                    selected = rotation == rotVal,
                    onClick = {
                        rotation = rotVal
                        scope.launch {
                            JacktorApiBridge.execute("apply_tweak", mapOf("tweak" to "rotation", "val" to rotVal.toString()))
                            onRefresh()
                        }
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = PanelStrings.current(currentLang).resolutionDensityPresetLabel,
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(
                "720x1460" to "720x1460 (Default)",
                "720x1600" to "720x1600",
                "reset" to "Reset Resolusi"
            ).forEach { (resVal, label) ->
                OptionChipButton(
                    text = label,
                    selected = tweaks?.optString("resolution") == resVal,
                    onClick = {
                        scope.launch {
                            JacktorApiBridge.execute("apply_tweak", mapOf("tweak" to "resolution", "val" to resVal))
                            onRefresh()
                        }
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(
                "256" to "256 DPI",
                "280" to "280 DPI",
                "320" to "320 DPI",
                "reset" to "Reset DPI"
            ).forEach { (dpiVal, label) ->
                OptionChipButton(
                    text = label,
                    selected = tweaks?.optString("density") == dpiVal,
                    onClick = {
                        scope.launch {
                            JacktorApiBridge.execute("apply_tweak", mapOf("tweak" to "density", "val" to dpiVal))
                            onRefresh()
                        }
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }
    }

    // 3. Daya Layar & Auto-Standby
    SectionCard(title = PanelStrings.screenPowerTitle(currentLang)) {
        Text(
            text = PanelStrings.screenPowerModeLabel(currentLang),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(
                "always" to PanelStrings.screenModeAlways(currentLang),
                "normal" to PanelStrings.screenModeNormal(currentLang),
                "auto" to PanelStrings.screenModeAuto(currentLang)
            ).forEach { (modeVal, label) ->
                OptionChipButton(
                    text = label,
                    selected = stayOnMode == modeVal,
                    onClick = {
                        stayOnMode = modeVal
                        scope.launch {
                            JacktorApiBridge.execute("apply_tweak", mapOf("tweak" to "stayon", "val" to modeVal))
                            onRefresh()
                        }
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        Text(
            text = PanelStrings.screenModeAutoDesc(currentLang),
            color = Color(0xFF94A3B8),
            fontSize = 10.sp,
            fontFamily = FontFamily.Monospace,
            lineHeight = 14.sp
        )

        Spacer(modifier = Modifier.height(8.dp))

        SettingSwitchRow(
            label = PanelStrings.lockscreenDisabledLabel(currentLang),
            checked = lockscreenDisabled == 1,
            onCheckedChange = { isDis ->
                val v = if (isDis) 1 else 0
                lockscreenDisabled = v
                scope.launch {
                    JacktorApiBridge.execute("apply_tweak", mapOf("tweak" to "lockscreen", "val" to v.toString()))
                    onRefresh()
                }
            }
        )
    }

    // 4. Baterai & Performa CPU
    SectionCard(title = PanelStrings.current(currentLang).batteryCpuTitle) {
        SettingSwitchRow(
            label = PanelStrings.current(currentLang).batteryBypassLabel,
            checked = isBypass,
            onCheckedChange = { bypass ->
                isBypass = bypass
                scope.launch {
                    JacktorApiBridge.execute("apply_tweak", mapOf("tweak" to "battery_bypass", "val" to if (bypass) "1" else "0"))
                    onRefresh()
                }
            }
        )

        SettingSwitchRow(
            label = PanelStrings.current(currentLang).usbAutosuspendLabel,
            checked = autosuspend == -1,
            onCheckedChange = { anti ->
                val v = if (anti) "-1" else "2"
                scope.launch {
                    JacktorApiBridge.execute("apply_tweak", mapOf("tweak" to "autosuspend", "val" to v))
                    onRefresh()
                }
            }
        )

        Spacer(modifier = Modifier.height(6.dp))

        Text(
            text = PanelStrings.current(currentLang).cpuProfileLabel,
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(
                "balanced" to "SEIMBANG (schedutil)",
                "performance" to "PERFORMANCE (Hi-Fi)",
                "powersave" to "HEMAT DAYA"
            ).forEach { (prof, label) ->
                OptionChipButton(
                    text = label,
                    selected = cpuProfile.equals(prof, ignoreCase = true),
                    onClick = {
                        cpuProfile = prof
                        scope.launch {
                            JacktorApiBridge.execute("apply_tweak", mapOf("tweak" to "cpu_profile", "val" to prof))
                            onRefresh()
                        }
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }
    }

    // 5. Layanan OpenSSH Daemon
    // 5. OpenSSH Administration
    SectionCard(title = PanelStrings.current(currentLang).sshAdminTitle) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .background(if (sshRunning == 1) IndustrialTertiary else IndustrialError, RoundedCornerShape(4.dp))
                    .padding(horizontal = 8.dp, vertical = 2.dp)
            ) {
                Text(
                    text = if (sshRunning == 1) "RUNNING (PORT 22)" else "STOPPED",
                    color = Color.Black,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 10.sp
                )
            }

            SettingSwitchRow(
                label = "Autostart on Boot",
                checked = sshAutostart == 1,
                onCheckedChange = { auto ->
                    val v = if (auto) 1 else 0
                    sshAutostart = v
                    scope.launch {
                        JacktorApiBridge.execute("ssh_set_autostart", mapOf("enabled" to v.toString()))
                        onRefresh()
                    }
                }
            )
        }

        Spacer(modifier = Modifier.height(8.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Button(
                onClick = {
                    scope.launch {
                        JacktorApiBridge.execute("ssh_start")
                        onRefresh()
                    }
                },
                colors = ButtonDefaults.buttonColors(containerColor = IndustrialTertiary),
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("START SSH", color = Color.Black, fontWeight = FontWeight.Bold, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
            Button(
                onClick = {
                    scope.launch {
                        JacktorApiBridge.execute("ssh_restart")
                        onRefresh()
                    }
                },
                colors = ButtonDefaults.buttonColors(containerColor = IndustrialPrimary),
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("RESTART", color = Color.Black, fontWeight = FontWeight.Bold, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
            Button(
                onClick = {
                    scope.launch {
                        JacktorApiBridge.execute("ssh_stop")
                        onRefresh()
                    }
                },
                colors = ButtonDefaults.buttonColors(containerColor = IndustrialError),
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("STOP", color = Color.Black, fontWeight = FontWeight.Bold, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
        }
    }

    // 6. Launcher & Terminal Boot
    SectionCard(title = PanelStrings.current(currentLang).launcherBootTitle) {
        Button(
            onClick = {
                try {
                    context.startActivity(Intent(Settings.ACTION_HOME_SETTINGS))
                } catch (_: Exception) {
                }
            },
            colors = ButtonDefaults.buttonColors(containerColor = IndustrialSecondary),
            shape = RoundedCornerShape(4.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(
                "ATUR JACKTOR PANEL SEBAGAI DEFAULT HOME LAUNCHER",
                color = Color.Black,
                fontWeight = FontWeight.Bold,
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace
            )
        }

        Spacer(modifier = Modifier.height(8.dp))

        SettingSwitchRow(
            label = "Tampilkan Terminal Hitam saat MCU Power-On",
            checked = showBootTerminal,
            onCheckedChange = {
                showBootTerminal = it
                prefs.edit().putBoolean("show_boot_terminal", it).apply()
            }
        )

        Text(
            text = "Auto-Dismiss Terminal Delay: ${terminalDelaySec.toInt()} detik",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = terminalDelaySec,
            onValueChange = { terminalDelaySec = it },
            onValueChangeFinished = {
                prefs.edit().putInt("terminal_pass_delay_sec", terminalDelaySec.toInt()).apply()
            },
            valueRange = 0f..10f
        )
    }

    // 7. Keamanan & Manajemen Password (Root, Webroot & SSH)
    var currPass by remember { mutableStateOf("") }
    var newPass by remember { mutableStateOf("") }
    var confPass by remember { mutableStateOf("") }
    var pwdMsg by remember { mutableStateOf<String?>(null) }
    var isPwdSuccess by remember { mutableStateOf(false) }
    var isUpdatingPwd by remember { mutableStateOf(false) }
    var isResettingRootPwd by remember { mutableStateOf(false) }

    SectionCard(title = PanelStrings.current(currentLang).securityPasswordTitle) {
        Text(
            text = "Kelola kata sandi terintegrasi untuk Web Dashboard, SSH, dan otorisasi tindakan sistem (Default: furry2026).",
            color = Color(0xFF94A3B8),
            fontSize = 10.5.sp,
            fontFamily = JetBrainsMono,
            lineHeight = 15.sp
        )

        Spacer(modifier = Modifier.height(6.dp))

        OutlinedTextField(
            value = currPass,
            onValueChange = { currPass = it; pwdMsg = null },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            placeholder = { Text("Password Lama (kosongkan jika default)", color = Color.Gray, fontSize = 11.sp, fontFamily = JetBrainsMono) },
            label = { Text("Password Saat Ini", color = IndustrialPrimary, fontSize = 10.sp, fontFamily = JetBrainsMono) },
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(modifier = Modifier.height(4.dp))

        OutlinedTextField(
            value = newPass,
            onValueChange = { newPass = it; pwdMsg = null },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            placeholder = { Text("Password Baru (minimal 6 karakter)", color = Color.Gray, fontSize = 11.sp, fontFamily = JetBrainsMono) },
            label = { Text("Password Baru", color = IndustrialPrimary, fontSize = 10.sp, fontFamily = JetBrainsMono) },
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(modifier = Modifier.height(4.dp))

        OutlinedTextField(
            value = confPass,
            onValueChange = { confPass = it; pwdMsg = null },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            placeholder = { Text("Ulangi Password Baru", color = Color.Gray, fontSize = 11.sp, fontFamily = JetBrainsMono) },
            label = { Text("Konfirmasi Password Baru", color = IndustrialPrimary, fontSize = 10.sp, fontFamily = JetBrainsMono) },
            modifier = Modifier.fillMaxWidth()
        )

        if (pwdMsg != null) {
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = pwdMsg ?: "",
                color = if (isPwdSuccess) IndustrialTertiary else IndustrialError,
                fontSize = 10.5.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = JetBrainsMono
            )
        }

        Spacer(modifier = Modifier.height(8.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Button(
                onClick = {
                    if (newPass.length < 6) {
                        pwdMsg = "Password baru minimal 6 karakter!"
                        isPwdSuccess = false
                        return@Button
                    }
                    if (newPass != confPass) {
                        pwdMsg = "Konfirmasi password baru tidak cocok!"
                        isPwdSuccess = false
                        return@Button
                    }
                    isUpdatingPwd = true
                    pwdMsg = null
                    scope.launch {
                        val res = JacktorApiBridge.execute(
                            "auth_change_password",
                            mapOf("pass" to currPass, "new_pass" to newPass)
                        )
                        isUpdatingPwd = false
                        if (res?.optBoolean("success", false) == true) {
                            pwdMsg = "Password berhasil diperbarui & disinkronkan!"
                            isPwdSuccess = true
                            currPass = ""
                            newPass = ""
                            confPass = ""
                        } else {
                            pwdMsg = res?.optString("error", "Gagal memperbarui password!") ?: "Gagal memperbarui password!"
                            isPwdSuccess = false
                        }
                    }
                },
                enabled = !isUpdatingPwd && !isResettingRootPwd,
                colors = ButtonDefaults.buttonColors(containerColor = IndustrialPrimary),
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier
                    .weight(1.3f)
                    .height(34.dp)
            ) {
                if (isUpdatingPwd) {
                    CircularProgressIndicator(modifier = Modifier.size(14.dp), strokeWidth = 2.dp, color = Color.Black)
                } else {
                    Text("SIMPAN PASSWORD", color = Color.Black, fontWeight = FontWeight.Bold, fontSize = 10.sp, fontFamily = JetBrainsMono)
                }
            }

            OutlinedButton(
                onClick = {
                    isResettingRootPwd = true
                    pwdMsg = null
                    scope.launch {
                        val cmd = "printf 'username=root\\npassword=furry2026\\n' > /data/adb/modules/jacktor-audio-framework/config/auth.conf; " +
                            "chmod 600 /data/adb/modules/jacktor-audio-framework/config/auth.conf 2>/dev/null; " +
                            "mkdir -p /data/adb/ssh 2>/dev/null; " +
                            "printf 'username=root\\npassword=furry2026\\n' > /data/adb/ssh/auth.conf; " +
                            "chmod 600 /data/adb/ssh/auth.conf 2>/dev/null; " +
                            "rm -f /data/adb/modules/jacktor-audio-framework/config/token.auth 2>/dev/null"
                        JacktorApiBridge.runRoot(cmd)
                        isResettingRootPwd = false
                        isPwdSuccess = true
                        pwdMsg = "Password telah direset fisik ke default: furry2026"
                        currPass = ""
                        newPass = ""
                        confPass = ""
                    }
                },
                enabled = !isUpdatingPwd && !isResettingRootPwd,
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier
                    .weight(1.1f)
                    .height(34.dp)
            ) {
                if (isResettingRootPwd) {
                    CircularProgressIndicator(modifier = Modifier.size(14.dp), strokeWidth = 2.dp, color = IndustrialSecondary)
                } else {
                    Text("RESET DEFAULT", color = IndustrialSecondary, fontWeight = FontWeight.Bold, fontSize = 10.sp, fontFamily = JetBrainsMono)
                }
            }
        }
    }
}

/* ==============================================================================
   TAB 1: AUDIO & DSP (Target Perangkat, Resampler, Bit-Perfect, Qualcomm Ear PA)
============================================================================== */
@Composable
private fun AudioAndDspTab(
    status: JSONObject?,
    onRefresh: () -> Unit
) {
    val scope = rememberCoroutineScope()

    val audioDevices = status?.optJSONArray("audio_devices")
    val audioOutput = status?.optString("audio_output", "auto") ?: "auto"
    val audioGain = status?.optJSONObject("audio_gain")

    var selectedDevId by remember { mutableStateOf("internal_speaker") }
    var selectedResamplerMode by remember { mutableStateOf("fixed") }
    var selectedQuality by remember { mutableStateOf("192000_24") }

    // Gain state
    var earPaGain by remember(audioGain) { mutableStateOf(audioGain?.optString("ear_pa", "G_9_DB") ?: "G_9_DB") }
    var digGain by remember(audioGain) { mutableFloatStateOf((audioGain?.optInt("digital", 90) ?: 90).toFloat()) }
    var anaGain by remember(audioGain) { mutableFloatStateOf((audioGain?.optInt("analog", 21) ?: 21).toFloat()) }

    // Sync selected device details from API response
    LaunchedEffect(audioDevices, selectedDevId) {
        if (audioDevices != null) {
            for (i in 0 until audioDevices.length()) {
                val dev = audioDevices.optJSONObject(i) ?: continue
                if (dev.optString("id") == selectedDevId) {
                    val m = dev.optString("resampler_mode")
                    if (m.isNotEmpty()) selectedResamplerMode = m
                    val q = dev.optString("fixed_quality")
                    if (q.isNotEmpty()) selectedQuality = q
                    break
                }
            }
        }
    }

    // 1. Target Perangkat Audio
    SectionCard(title = "TARGET PERANGKAT AUDIO & ROUTING") {
        Text(
            text = "Pilih Perangkat untuk Dikonfigurasi:",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(
                "internal_speaker" to "Speaker Internal (SoC)",
                "internal_headset" to "Headset 3.5mm (SoC)",
                "usb_dac" to "USB DAC Prioritas"
            ).forEach { (devId, label) ->
                OptionChipButton(
                    text = label,
                    selected = selectedDevId == devId,
                    onClick = { selectedDevId = devId },
                    modifier = Modifier.weight(1f)
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "Active Hardware Output Routing:",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(
                "auto" to "AUTO (USB Prioritas)",
                "internal_speaker" to "Speaker Internal",
                "internal_headset" to "Headset 3.5mm"
            ).forEach { (outId, label) ->
                OptionChipButton(
                    text = label,
                    selected = audioOutput == outId,
                    onClick = {
                        scope.launch {
                            JacktorApiBridge.execute("audio_output", mapOf("mode" to outId))
                            onRefresh()
                        }
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }
    }

    // 1b. Audio Channel Routing (R/L Mode: Stereo, Left, Right, Mono)
    val curChMode = status?.optInt("channel_mode", 0) ?: 0
    var channelMode by remember(curChMode) { mutableIntStateOf(curChMode) }
    val currentLang by LanguageController.currentLanguage

    SectionCard(title = PanelStrings.channelModeTitle(currentLang)) {
        Text(
            text = PanelStrings.channelModeDesc(currentLang),
            color = Color(0xFFD1D5DB),
            fontSize = 11.sp,
            fontFamily = JetBrainsMono,
            lineHeight = 15.sp
        )
        Spacer(modifier = Modifier.height(6.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(
                0 to PanelStrings.channelStereo(currentLang),
                1 to PanelStrings.channelLeft(currentLang),
                2 to PanelStrings.channelRight(currentLang),
                3 to PanelStrings.channelMono(currentLang)
            ).forEach { (m, label) ->
                OptionChipButton(
                    text = label,
                    selected = channelMode == m,
                    onClick = {
                        channelMode = m
                        scope.launch {
                            JacktorApiBridge.execute("channel_mode", mapOf("mode" to m.toString()))
                            MainActivity.connectionManager?.sendCommand(TelemetryParser.buildMediaCommand("setchannel", m))
                            onRefresh()
                        }
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }
    }

    // 2. Resampler Engine & Resolusi Fixed
    SectionCard(title = "RESAMPLER ENGINE & RESOLUSI HI-RES (QUALCOMM BENGAL)") {
        Text(
            text = "Mode Resampling Audio ($selectedDevId):",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(
                "fixed" to "Fixed Resampling",
                "direct" to "Bit-Perfect Direct",
                "dynamic" to "Dynamic Mix"
            ).forEach { (rMode, label) ->
                OptionChipButton(
                    text = label,
                    selected = selectedResamplerMode == rMode,
                    onClick = {
                        selectedResamplerMode = rMode
                        scope.launch {
                            JacktorApiBridge.execute(
                                "set_device_audio_config",
                                mapOf(
                                    "target_device" to selectedDevId,
                                    "resampler_mode" to rMode,
                                    "fixed_quality" to selectedQuality
                                )
                            )
                            onRefresh()
                        }
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "Resolusi Sample Rate & Bit Depth Fixed (10 Format SoC Bengal):",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        val qualityList = listOf(
            "44100_16" to "44.1 kHz / 16-bit",
            "44100_24" to "44.1 kHz / 24-bit",
            "48000_16" to "48.0 kHz / 16-bit",
            "48000_24" to "48.0 kHz / 24-bit",
            "88200_24" to "88.2 kHz / 24-bit",
            "96000_24" to "96.0 kHz / 24-bit",
            "176400_24" to "176.4 kHz / 24-bit",
            "192000_24" to "192.0 kHz / 24-bit",
            "352800_32" to "352.8 kHz / 32-bit",
            "384000_32" to "384.0 kHz / 32-bit"
        )

        // Display in 2 rows of 5 buttons for clean industrial grid
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                qualityList.take(5).forEach { (qVal, label) ->
                    OptionChipButton(
                        text = label,
                        selected = selectedQuality == qVal,
                        onClick = {
                            selectedQuality = qVal
                            scope.launch {
                                JacktorApiBridge.execute(
                                    "set_device_audio_config",
                                    mapOf(
                                        "target_device" to selectedDevId,
                                        "resampler_mode" to selectedResamplerMode,
                                        "fixed_quality" to qVal
                                    )
                                )
                                onRefresh()
                            }
                        },
                        modifier = Modifier.weight(1f)
                    )
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                qualityList.drop(5).forEach { (qVal, label) ->
                    OptionChipButton(
                        text = label,
                        selected = selectedQuality == qVal,
                        onClick = {
                            selectedQuality = qVal
                            scope.launch {
                                JacktorApiBridge.execute(
                                    "set_device_audio_config",
                                    mapOf(
                                        "target_device" to selectedDevId,
                                        "resampler_mode" to selectedResamplerMode,
                                        "fixed_quality" to qVal
                                    )
                                )
                                onRefresh()
                            }
                        },
                        modifier = Modifier.weight(1f)
                    )
                }
            }
        }
    }

    // 3. Qualcomm WCD9370 Hardware Gain
    SectionCard(title = "QUALCOMM WCD9370 HARDWARE AUDIO GAIN") {
        Text(
            text = "Preset Hardware Gain Cepat:",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(
                "preset_stock" to "STOCK (84/20)",
                "preset_boost" to "BOOST (90/21)",
                "preset_loud" to "LOUD (96/22)",
                "preset_max" to "MAX (104/24)"
            ).forEach { (presetVal, label) ->
                OptionChipButton(
                    text = label,
                    selected = false,
                    onClick = {
                        scope.launch {
                            JacktorApiBridge.execute("set_gain", mapOf("mode" to presetVal))
                            onRefresh()
                        }
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "Qualcomm Ear PA Hardware Analog Gain:",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            listOf(
                "G_0_DB" to "0 dB",
                "G_3_DB" to "+3 dB",
                "G_6_DB" to "+6 dB",
                "G_9_DB" to "+9 dB",
                "G_12_DB" to "+12 dB",
                "G_15_DB" to "+15 dB",
                "G_18_DB" to "+18 dB"
            ).forEach { (earVal, label) ->
                OptionChipButton(
                    text = label,
                    selected = earPaGain == earVal,
                    onClick = {
                        earPaGain = earVal
                        scope.launch {
                            JacktorApiBridge.execute("set_gain", mapOf("mode" to "ear_pa", "val" to earVal))
                            onRefresh()
                        }
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "Digital Volume Gain: ${digGain.toInt()} (60..110)",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = digGain,
            onValueChange = { digGain = it },
            onValueChangeFinished = {
                scope.launch {
                    JacktorApiBridge.execute("set_gain", mapOf("mode" to "digital", "val" to digGain.toInt().toString()))
                }
            },
            valueRange = 60f..110f
        )

        Text(
            text = "Analog Volume Gain: ${anaGain.toInt()} dB (10..24 dB)",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = anaGain,
            onValueChange = { anaGain = it },
            onValueChangeFinished = {
                scope.launch {
                    JacktorApiBridge.execute("set_gain", mapOf("mode" to "analog", "val" to anaGain.toInt().toString()))
                }
            },
            valueRange = 10f..24f
        )
    }
}

/* ==============================================================================
   TAB 2: AMPLIFIER & BT (Relays, Sleep, Bluetooth QCC5125, SMPS Protection)
============================================================================== */
@Composable
private fun PowerRelaysAndBtTab(
    prefs: android.content.SharedPreferences,
    slowData: SlowTelemetry,
    realtimeData: RealtimeTelemetry?,
    nvsData: NvsSettings,
    onSendRawCommand: (String) -> Unit,
    onSaveNvs: (String, Any) -> Unit,
    onRequestDangerConfirm: (DangerActionPrompt) -> Unit
) {
    var spkPwr by remember(nvsData.spkPwr) { mutableStateOf(nvsData.spkPwr) }
    var sleepTimerMin by remember(slowData.sleepTimer) { mutableIntStateOf(slowData.sleepTimer) }

    var btEnabled by remember(nvsData.btEn, slowData.inputs?.bt) {
        mutableStateOf(slowData.inputs?.bt ?: nvsData.btEn)
    }
    var btAutoOffMin by remember(nvsData.btAutooff) {
        mutableIntStateOf((nvsData.btAutooff / 60000L).toInt().coerceAtLeast(0))
    }
    var eqPreset by remember(nvsData.eqPreset) { mutableIntStateOf(nvsData.eqPreset) }
    var bassBoost by remember(nvsData.bassBoost) { mutableStateOf(nvsData.bassBoost) }
    var sound3d by remember(nvsData.audio3d) { mutableStateOf(nvsData.audio3d) }

    var smpsBypass by remember(nvsData.smpsBypass) { mutableStateOf(nvsData.smpsBypass) }
    var smpsCutoff by remember(slowData.smps?.cutoff, nvsData.smpsCut) {
        mutableFloatStateOf((slowData.smps?.cutoff ?: nvsData.smpsCut).coerceIn(30f, 68f))
    }
    var smpsRecovery by remember(slowData.smps?.recover, nvsData.smpsRec) {
        mutableFloatStateOf((slowData.smps?.recover ?: nvsData.smpsRec).coerceIn(32f, 70f))
    }

    var autoSenseEn by remember(nvsData.autoSenseEn) { mutableStateOf(nvsData.autoSenseEn) }
    var autoSenseTimeout by remember(nvsData.autoSenseTimeout) {
        mutableFloatStateOf(nvsData.autoSenseTimeout.toFloat().coerceIn(1f, 60f))
    }
    var autoSenseThresh by remember(nvsData.autoSenseThresh) {
        mutableFloatStateOf(nvsData.autoSenseThresh.toFloat().coerceIn(1f, 50f))
    }

    // 1. Output Relays & Sleep Timer
    SectionCard(title = "OUTPUT RELAYS & SLEEP TIMER") {
        SettingSwitchRow(
            label = "Power Protektor Speaker (Cut-Off Relay)",
            checked = spkPwr,
            onCheckedChange = {
                spkPwr = it
                onSaveNvs("spk_pwr", it)
            }
        )
        Text(
            text = "Proteksi hardware DC selalu aktif permanen & tidak bisa di-bypass. Saklar ini mengontrol daya modul protektor untuk memutus speaker (cut-off) saat darurat / volume overload.",
            color = IndustrialTextSecondary,
            fontSize = 9.5.sp,
            fontFamily = JetBrainsMono
        )

        Spacer(modifier = Modifier.height(6.dp))

        Text(
            text = "Amplifier Sleep Timer:",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = JetBrainsMono,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(0 to "OFF", 15 to "15m", 30 to "30m", 60 to "60m", 90 to "90m", 120 to "120m").forEach { (minVal, label) ->
                OptionChipButton(
                    text = label,
                    selected = sleepTimerMin == minVal,
                    onClick = {
                        sleepTimerMin = minVal
                        onSaveNvs("sleep_timer", minVal)
                    }
                )
            }
        }
    }

    // 2. Bluetooth QCC5125 & GAIA DSP Hardware
    val btData = slowData.bt
    val qccPwr = realtimeData?.qccPwr ?: btData?.qccPwr ?: false
    val sppConnected = realtimeData?.sppConnected ?: btData?.sppConnected ?: false
    val isPowerOn = slowData.states?.on ?: false
    val isPowerInjected = (!isPowerOn && qccPwr)
    val btRssi = realtimeData?.btRssi ?: btData?.rssi ?: 0
    val sppState = btData?.sppState ?: if (sppConnected) "connected" else "idle"
    val activeCodec = if (isPowerOn && sppConnected) (btData?.codec?.takeIf { it.isNotBlank() && it != "None" && it != "--" } ?: "--") else "--"
    val espTxPower = if (isPowerOn) (btData?.espTxPower?.takeIf { it.isNotBlank() && it != "None" && it != "--" } ?: if (sppConnected) "Low-Noise (-12..-6 dBm)" else "--") else "--"

    val (statusText, statusColor) = when {
        !qccPwr -> "POWER OFF" to Color(0xFF8B949E)
        sppConnected -> "ACTIVE (SPP CONNECTED)" to IndustrialTertiary
        isPowerInjected -> "STANDBY (EXT POWER • NO SPP)" to IndustrialSecondary
        else -> "READY (NO SPP)" to IndustrialSecondary
    }

    SectionCard(title = "BLUETOOTH QCC5125 & GAIA DSP") {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "STATUS HARDWARE:",
                color = IndustrialPrimary,
                fontSize = 12.sp,
                fontFamily = JetBrainsMono,
                fontWeight = FontWeight.Bold
            )
            Box(
                modifier = Modifier
                    .background(statusColor.copy(alpha = 0.18f), RoundedCornerShape(3.dp))
                    .border(1.dp, statusColor, RoundedCornerShape(3.dp))
                    .padding(horizontal = 8.dp, vertical = 2.dp)
            ) {
                Text(
                    text = statusText,
                    color = statusColor,
                    fontSize = 11.5.sp,
                    fontFamily = JetBrainsMono,
                    fontWeight = FontWeight.Bold
                )
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        // Hardware Telemetry Details Box
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF0D0F12), RoundedCornerShape(4.dp))
                .border(1.dp, Color(0xFF1F2630), RoundedCornerShape(4.dp))
                .padding(horizontal = 8.dp, vertical = 6.dp)
        ) {
            Column(verticalArrangement = Arrangement.spacedBy(3.5.dp)) {
                InfoRow(
                    label = "QCC5125 Power State",
                    value = when {
                        !qccPwr -> "OFF (GPIO 5 LOW)"
                        isPowerInjected -> "EXT INJECTED (5V)"
                        else -> "POWER ON (5V)"
                    }
                )
                InfoRow(
                    label = "SPP Link Connection",
                    value = when {
                        sppConnected -> "CONNECTED"
                        qccPwr && isPowerOn -> sppState.uppercase()
                        else -> "DISCONNECTED"
                    }
                )
                InfoRow(
                    label = "Signal Strength (RSSI)",
                    value = if (isPowerOn && sppConnected && btRssi != 0) "$btRssi dBm" else "-- dBm"
                )
                InfoRow(
                    label = "ESP32 BT TX",
                    value = espTxPower
                )
                InfoRow(
                    label = "Active Audio Codec",
                    value = activeCodec
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        SettingSwitchRow(
            label = "Bluetooth Module Power Switch (bt-on / bt-off)",
            checked = btEnabled,
            onCheckedChange = {
                btEnabled = it
                onSaveNvs("bt_en", it)
            }
        )

        Spacer(modifier = Modifier.height(6.dp))

        Text(
            text = "Bluetooth Idle Auto-Off Timer:",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(0 to "NEVER", 5 to "5m", 10 to "10m", 15 to "15m", 30 to "30m", 60 to "60m").forEach { (minVal, label) ->
                OptionChipButton(
                    text = label,
                    selected = btAutoOffMin == minVal,
                    onClick = {
                        btAutoOffMin = minVal
                        onSaveNvs("bt_autooff", minVal * 60000L)
                    }
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "GAIA DSP Equalizer Preset:",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        val eqPresets = listOf(
            0 to "FLAT",
            1 to "ROCK",
            2 to "JAZZ",
            3 to "POP",
            4 to "CLASSIC",
            5 to "VOCAL",
            6 to "USER"
        )
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            eqPresets.forEach { (presetIdx, label) ->
                OptionChipButton(
                    text = label,
                    selected = eqPreset == presetIdx,
                    onClick = {
                        eqPreset = presetIdx
                        prefs.edit().putInt("bt_eq", presetIdx).apply()
                        onSendRawCommand(TelemetryParser.buildMediaCommand("setpreset", presetIdx))
                    }
                )
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        SettingSwitchRow(
            label = "GAIA Bass Boost Enhancement",
            checked = bassBoost,
            onCheckedChange = {
                bassBoost = it
                prefs.edit().putBoolean("bt_bass", it).apply()
                onSendRawCommand(TelemetryParser.buildMediaCommand("setbass", if (it) 1 else 0))
            }
        )

        SettingSwitchRow(
            label = "GAIA 3D Stereo Enhancement",
            checked = sound3d,
            onCheckedChange = {
                sound3d = it
                prefs.edit().putBoolean("bt_3d", it).apply()
                onSendRawCommand(TelemetryParser.buildMediaCommand("set3d", if (it) 1 else 0))
            }
        )

        Spacer(modifier = Modifier.height(6.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            OutlinedButton(
                onClick = { onSendRawCommand(TelemetryParser.buildMediaCommand("enterpairing", null)) },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("PAIRING", color = IndustrialPrimary, fontSize = 11.5.sp, fontFamily = FontFamily.Monospace)
            }
            OutlinedButton(
                onClick = {
                    onRequestDangerConfirm(
                        DangerActionPrompt(
                            title = "CLEAR BLUETOOTH PDL",
                            description = "Hapus seluruh riwayat perangkat yang pernah terpasang pada modul Bluetooth Qualcomm QCC5125?",
                            confirmButtonText = "HAPUS PDL",
                            onConfirm = { onSendRawCommand(TelemetryParser.buildMediaCommand("deletepdl", null)) }
                        )
                    )
                },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("CLEAR PDL", color = IndustrialSecondary, fontSize = 11.5.sp, fontFamily = FontFamily.Monospace)
            }
            OutlinedButton(
                onClick = {
                    onRequestDangerConfirm(
                        DangerActionPrompt(
                            title = "REBOOT QUALCOMM QCC5125",
                            description = "Reboot modul Bluetooth Qualcomm QCC5125? Sambungan audio Bluetooth akan terputus.",
                            confirmButtonText = "REBOOT QCC",
                            onConfirm = { onSendRawCommand(TelemetryParser.buildMediaCommand("qcc_reboot", null)) }
                        )
                    )
                },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("REBOOT QCC", color = IndustrialError, fontSize = 11.5.sp, fontFamily = FontFamily.Monospace)
            }
        }
    }

    // 3. SMPS & Auto Audio Sense
    SectionCard(title = "SMPS (65V) PROTECTION & AUTO AUDIO SENSE") {
        SettingSwitchRow(
            label = "SMPS (65V) Protection Bypass",
            checked = smpsBypass,
            onCheckedChange = {
                smpsBypass = it
                onSaveNvs("smps_bypass", it)
            }
        )

        Spacer(modifier = Modifier.height(4.dp))

        Text(
            text = String.format("SMPS (65V) Cutoff Voltage (30..68 V): %.1f V", smpsCutoff),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = smpsCutoff,
            onValueChange = { smpsCutoff = it },
            onValueChangeFinished = {
                if (smpsCutoff >= smpsRecovery) {
                    smpsRecovery = (smpsCutoff + 2.0f).coerceAtMost(70.0f)
                    onSaveNvs("smps_rec", smpsRecovery)
                }
                onSaveNvs("smps_cut", smpsCutoff)
            },
            valueRange = 30f..68f
        )

        Text(
            text = String.format("SMPS (65V) Recovery Voltage (32..70 V): %.1f V", smpsRecovery),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = smpsRecovery,
            onValueChange = { smpsRecovery = it },
            onValueChangeFinished = {
                if (smpsRecovery <= smpsCutoff) {
                    smpsCutoff = (smpsRecovery - 2.0f).coerceAtLeast(30.0f)
                    onSaveNvs("smps_cut", smpsCutoff)
                }
                onSaveNvs("smps_rec", smpsRecovery)
            },
            valueRange = 32f..70f
        )

        Spacer(modifier = Modifier.height(6.dp))

        SettingSwitchRow(
            label = "Auto Audio Sense Standby / Wake",
            checked = autoSenseEn,
            onCheckedChange = {
                autoSenseEn = it
                onSaveNvs("auto_sense_en", it)
            }
        )

        Text(
            text = "Auto Sense Idle Timeout: ${autoSenseTimeout.toInt()} min",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = autoSenseTimeout,
            onValueChange = { autoSenseTimeout = it },
            onValueChangeFinished = { onSaveNvs("auto_sense_timeout", autoSenseTimeout.toInt()) },
            valueRange = 1f..60f
        )

        Text(
            text = "Auto Sense Signal Threshold: ${autoSenseThresh.toInt()}",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = autoSenseThresh,
            onValueChange = { autoSenseThresh = it },
            onValueChangeFinished = { onSaveNvs("auto_sense_thresh", autoSenseThresh.toInt()) },
            valueRange = 1f..50f
        )
    }
}

/* ==============================================================================
   TAB 3: ANALYZER & ADC (KissFFT, Bands, Interval, Gain & Noise Gate)
============================================================================== */
@Composable
private fun AnalyzerAndAdcTab(
    prefs: android.content.SharedPreferences,
    slowData: SlowTelemetry,
    realtimeData: com.jacktor.audiopanel.model.RealtimeTelemetry,
    onSendRawCommand: (String) -> Unit
) {
    val cfg = slowData.analyzer
    var mode by remember(cfg?.mode) { mutableStateOf(cfg?.mode ?: prefs.getString("an_mode", "fft") ?: "fft") }
    var bands by remember(cfg?.bandsLen) { mutableIntStateOf(cfg?.bandsLen ?: prefs.getInt("an_bands", 32)) }
    var updateMs by remember(cfg?.updateMs) { mutableIntStateOf(cfg?.updateMs ?: prefs.getInt("an_update_ms", 33)) }
    var gainL by remember(cfg?.gainL) { mutableFloatStateOf(cfg?.gainL ?: prefs.getFloat("an_gain_l", 1.0f)) }
    var gainR by remember(cfg?.gainR) { mutableFloatStateOf(cfg?.gainR ?: prefs.getFloat("an_gain_r", 1.0f)) }
    var noiseL by remember(cfg?.noiseL) { mutableFloatStateOf(cfg?.noiseL ?: prefs.getFloat("an_noise_l", 12.0f)) }
    var noiseR by remember(cfg?.noiseR) { mutableFloatStateOf(cfg?.noiseR ?: prefs.getFloat("an_noise_r", 12.0f)) }

    fun pushAnalyzerMode() {
        prefs.edit()
            .putString("an_mode", mode)
            .putInt("an_bands", bands)
            .putInt("an_update_ms", updateMs)
            .apply()
        onSendRawCommand(TelemetryParser.buildAnalyzerModeCommand(mode, bands, updateMs))
    }

    fun pushAnalyzerParams() {
        prefs.edit()
            .putFloat("an_gain_l", gainL)
            .putFloat("an_gain_r", gainR)
            .putFloat("an_noise_l", noiseL)
            .putFloat("an_noise_r", noiseR)
            .apply()
        onSendRawCommand(TelemetryParser.buildAnalyzerParamsCommand(gainL, gainR, noiseL, noiseR))
    }

    // 1. Audio Analyzer Resistor Divider DC Bias (GPIO 34 Left / GPIO 39 Right)
    val dcL = realtimeData.dcL ?: 0
    val dcR = realtimeData.dcR ?: 0
    val biasL = (dcL * 3.3f) / 4095.0f
    val biasR = (dcR * 3.3f) / 4095.0f
    val p2pL = realtimeData.p2pL ?: 0
    val p2pR = realtimeData.p2pR ?: 0

    SectionCard(title = "AUDIO ANALYZER RESISTOR DIVIDER & DC BIAS (GPIO 34 L / GPIO 39 R)") {
        InfoRow(
            label = "Left Channel Bias Voltage (GPIO 34)",
            value = String.format("%.3f V (Raw: %d / 4095)", biasL, dcL)
        )
        InfoRow(
            label = "Right Channel Bias Voltage (GPIO 39)",
            value = String.format("%.3f V (Raw: %d / 4095)", biasR, dcR)
        )
        InfoRow(
            label = "Time-Domain Amplitude (Peak-to-Peak)",
            value = "L: $p2pL | R: $p2pR"
        )
        val isBalanced = (biasL in 1.2f..2.1f) && (biasR in 1.2f..2.1f)
        InfoRow(
            label = "Virtual Ground Bias State (~1.65V Ideal)",
            value = if (isBalanced) "BALANCED (Ideal ~1.65V)" else "ASYMMETRIC OFFSET"
        )

        Spacer(modifier = Modifier.height(6.dp))

        Button(
            onClick = { onSendRawCommand(TelemetryParser.buildSimpleCommand("calibrate")) },
            colors = ButtonDefaults.buttonColors(containerColor = IndustrialSecondary),
            shape = RoundedCornerShape(4.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(
                "AUTO-CALIBRATE ADC ZERO BIAS (calibrate)",
                color = Color.Black,
                fontWeight = FontWeight.Bold,
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace
            )
        }
    }

    SectionCard(title = "KISSFFT SPECTRUM & VU ENGINE") {
        Text(
            text = "Analyzer Engine Mode:",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            OptionChipButton(
                text = "FFT SPECTRUM + VU",
                selected = mode == "fft",
                onClick = {
                    mode = "fft"
                    pushAnalyzerMode()
                },
                modifier = Modifier.weight(1f)
            )
            OptionChipButton(
                text = "STEREO VU ONLY",
                selected = mode == "vu",
                onClick = {
                    mode = "vu"
                    pushAnalyzerMode()
                },
                modifier = Modifier.weight(1f)
            )
        }

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "KissFFT Band Resolution:",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(8, 16, 32, 64).forEach { b ->
                OptionChipButton(
                    text = "$b BANDS",
                    selected = bands == b,
                    onClick = {
                        bands = b
                        pushAnalyzerMode()
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "Telemetry Frame Interval (update_ms):",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf(30, 33, 40, 50, 80, 100).forEach { ms ->
                OptionChipButton(
                    text = "${ms}ms",
                    selected = updateMs == ms,
                    onClick = {
                        updateMs = ms
                        pushAnalyzerMode()
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }
    }

    SectionCard(title = "ADC GAIN MULTIPLIER & NOISE GATE (GPIO 34 L / GPIO 39 R)") {
        Text(
            text = String.format("Left Channel Gain (GPIO 34): %.2fx", gainL),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = gainL,
            onValueChange = { gainL = it },
            onValueChangeFinished = { pushAnalyzerParams() },
            valueRange = 0.2f..5.0f
        )

        Text(
            text = String.format("Right Channel Gain (GPIO 39): %.2fx", gainR),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = gainR,
            onValueChange = { gainR = it },
            onValueChangeFinished = { pushAnalyzerParams() },
            valueRange = 0.2f..5.0f
        )

        Text(
            text = String.format("Left Channel Noise Gate: %.0f", noiseL),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = noiseL,
            onValueChange = { noiseL = it },
            onValueChangeFinished = { pushAnalyzerParams() },
            valueRange = 0f..80f
        )

        Text(
            text = String.format("Right Channel Noise Gate: %.0f", noiseR),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = noiseR,
            onValueChange = { noiseR = it },
            onValueChangeFinished = { pushAnalyzerParams() },
            valueRange = 0f..80f
        )

        Spacer(modifier = Modifier.height(6.dp))

        Button(
            onClick = { onSendRawCommand(TelemetryParser.buildSimpleCommand("calibrate")) },
            colors = ButtonDefaults.buttonColors(containerColor = IndustrialSecondary),
            shape = RoundedCornerShape(4.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(
                "AUTO-CALIBRATE ADC ZERO BIAS (calibrate)",
                color = Color.Black,
                fontWeight = FontWeight.Bold,
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace
            )
        }
    }
}

/* ==============================================================================
   TAB 4: THERMAL & HW (Kipas PID, Buzzer, Button Lock, RTC Sync, ESP32 Maintenance)
============================================================================== */
@Composable
private fun ThermalAndSystemTab(
    prefs: android.content.SharedPreferences,
    slowData: SlowTelemetry,
    realtimeData: com.jacktor.audiopanel.model.RealtimeTelemetry,
    nvsData: NvsSettings,
    scannedSensors: List<com.jacktor.audiopanel.model.TempScannedSensor>,
    onBack: () -> Unit,
    onSendRawCommand: (String) -> Unit,
    onSaveNvs: (String, Any) -> Unit,
    onRequestDangerConfirm: (DangerActionPrompt) -> Unit
) {
    var fanMode by remember(nvsData.fanModeStr) {
        mutableStateOf(nvsData.fanModeStr.ifEmpty { prefs.getString("fan_mode", "auto") ?: "auto" })
    }
    var fanDutyPct by remember(nvsData.fanDuty) {
        val pct = ((nvsData.fanDuty.coerceIn(0, 1023) * 100f) / 1023f).coerceIn(20f, 100f)
        mutableFloatStateOf(pct)
    }
    var pidSp by remember(nvsData.pidSp) {
        val sp = if (nvsData.pidSp > 0f) nvsData.pidSp else prefs.getFloat("pid_sp", 45.0f)
        mutableFloatStateOf(sp.coerceIn(35f, 65f))
    }
    var otpThr by remember(nvsData.otpThr) {
        val thr = if (nvsData.otpThr > 0f) nvsData.otpThr else prefs.getFloat("otp_thr", 85.0f)
        mutableFloatStateOf(thr.coerceIn(65f, 95f))
    }

    var buzzEnabled by remember(slowData.buzzer?.enabled, nvsData.buzzEnabled) {
        mutableStateOf(slowData.buzzer?.enabled ?: nvsData.buzzEnabled)
    }
    var buzzVolume by remember(nvsData.buzzVolume) {
        mutableFloatStateOf(nvsData.buzzVolume.toFloat().coerceIn(0f, 100f))
    }
    var btnLock by remember(nvsData.btnLock) {
        mutableStateOf(nvsData.btnLock)
    }

    val stereoTemp = realtimeData.heatAb ?: slowData.thermal?.classAb?.temp ?: slowData.heatAb ?: realtimeData.heatC ?: slowData.heatC
    val subTemp = realtimeData.heatD ?: slowData.thermal?.classD?.temp ?: slowData.heatD
    val smpsTemp = realtimeData.heatSmps ?: slowData.thermal?.smps?.temp ?: slowData.heatSmps

    val romStereo = slowData.thermal?.classAb?.rom?.takeIf { it.isNotBlank() && it != "0000000000000000" }
        ?: nvsData.tempAbRom.takeIf { it.isNotBlank() && it != "0000000000000000" }
        ?: "Belum Diatur"
    val romSub = slowData.thermal?.classD?.rom?.takeIf { it.isNotBlank() && it != "0000000000000000" }
        ?: nvsData.tempDRom.takeIf { it.isNotBlank() && it != "0000000000000000" }
        ?: "Belum Diatur"
    val romSmps = slowData.thermal?.smps?.rom?.takeIf { it.isNotBlank() && it != "0000000000000000" }
        ?: nvsData.tempSmpsRom.takeIf { it.isNotBlank() && it != "0000000000000000" }
        ?: "Belum Diatur"

    // 0. Multi-Sensor Temperature Monitoring & Identification (GPIO 27)
    SectionCard(title = "SENSOR SUHU 1-WIRE DS18B20 (STEREO / SUBWOOFER / SMPS)") {
        InfoRow(
            label = "Suhu Amplifier Stereo (0x$romStereo)",
            value = if (stereoTemp != null) String.format("%.1f °C", stereoTemp) else "Belum Terpasang"
        )
        InfoRow(
            label = "Suhu Amplifier Subwoofer (0x$romSub)",
            value = if (subTemp != null) String.format("%.1f °C", subTemp) else "Belum Terpasang"
        )
        InfoRow(
            label = "Suhu Catu Daya SMPS (0x$romSmps)",
            value = if (smpsTemp != null) String.format("%.1f °C", smpsTemp) else "Belum Terpasang"
        )

        Spacer(modifier = Modifier.height(6.dp))

        Button(
            onClick = { onSendRawCommand("temp-scan") },
            colors = ButtonDefaults.buttonColors(containerColor = IndustrialPrimary),
            shape = RoundedCornerShape(4.dp),
            modifier = Modifier
                .fillMaxWidth()
                .height(34.dp)
        ) {
            Text(
                text = "CEK & SCAN ALAMAT SENSOR SUHU (GPIO 27)",
                color = Color.Black,
                fontWeight = FontWeight.Bold,
                fontSize = 10.5.sp,
                fontFamily = JetBrainsMono
            )
        }

        Spacer(modifier = Modifier.height(6.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            OutlinedButton(
                onClick = {
                    onSendRawCommand("temp-swap stereo subwoofer")
                    onSendRawCommand("temp-scan")
                },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f),
                contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 4.dp, vertical = 0.dp)
            ) {
                Text("TUKAR STEREO↔SUB", color = IndustrialPrimary, fontSize = 9.5.sp, fontFamily = JetBrainsMono)
            }
            OutlinedButton(
                onClick = {
                    onSendRawCommand("temp-swap stereo smps")
                    onSendRawCommand("temp-scan")
                },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f),
                contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 4.dp, vertical = 0.dp)
            ) {
                Text("TUKAR STEREO↔SMPS", color = IndustrialSecondary, fontSize = 9.5.sp, fontFamily = JetBrainsMono)
            }
            OutlinedButton(
                onClick = {
                    onSendRawCommand("temp-swap subwoofer smps")
                    onSendRawCommand("temp-scan")
                },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f),
                contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 4.dp, vertical = 0.dp)
            ) {
                Text("TUKAR SUB↔SMPS", color = IndustrialTertiary, fontSize = 9.5.sp, fontFamily = JetBrainsMono)
            }
        }

        if (scannedSensors.isNotEmpty()) {
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = "Daftar Sensor Terdeteksi di Bus 1-Wire (Klik untuk Atur Fungsi):",
                color = IndustrialSecondary,
                fontSize = 10.5.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = JetBrainsMono
            )
            Spacer(modifier = Modifier.height(4.dp))
            scannedSensors.forEach { s ->
                val roleLabel = when (s.role.lowercase()) {
                    "class_ab", "stereo" -> "STEREO"
                    "class_d", "subwoofer" -> "SUBWOOFER"
                    "smps" -> "SMPS"
                    else -> "BELUM DIATUR"
                }
                val tempTxt = if (s.temp != null) String.format("%.1f °C", s.temp) else "-- °C"
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 3.dp)
                        .background(Color(0xFF0D131D), RoundedCornerShape(4.dp))
                        .border(1.dp, Color(0xFF26354A), RoundedCornerShape(4.dp))
                        .padding(8.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "#${s.index + 1} ROM: 0x${s.rom} ($tempTxt)",
                            color = IndustrialPrimary,
                            fontSize = 10.5.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = JetBrainsMono
                        )
                        Text(
                            text = roleLabel,
                            color = IndustrialTertiary,
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = JetBrainsMono
                        )
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        OptionChipButton(
                            text = "SET STEREO",
                            selected = s.role == "class_ab" || s.role == "stereo",
                            onClick = {
                                onSendRawCommand("temp-assign stereo ${s.rom}")
                                onSendRawCommand("temp-scan")
                            },
                            modifier = Modifier.weight(1f)
                        )
                        OptionChipButton(
                            text = "SET SUBWOOFER",
                            selected = s.role == "class_d" || s.role == "subwoofer",
                            onClick = {
                                onSendRawCommand("temp-assign subwoofer ${s.rom}")
                                onSendRawCommand("temp-scan")
                            },
                            modifier = Modifier.weight(1f)
                        )
                        OptionChipButton(
                            text = "SET SMPS",
                            selected = s.role == "smps",
                            onClick = {
                                onSendRawCommand("temp-assign smps ${s.rom}")
                                onSendRawCommand("temp-scan")
                            },
                            modifier = Modifier.weight(1f)
                        )
                    }
                }
            }
        }
    }

    // 1. Cooling Fan & Thermal Protection
    SectionCard(title = "COOLING FAN & THERMAL PID PROTECTION") {
        Text(
            text = "Cooling Fan Control Mode:",
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(4.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf("auto" to "AUTO (PID)", "custom" to "MANUAL DUTY", "failsafe" to "MAX 100%").forEach { (mVal, label) ->
                OptionChipButton(
                    text = label,
                    selected = fanMode.equals(mVal, ignoreCase = true),
                    onClick = {
                        fanMode = mVal
                        onSaveNvs("fan_mode", mVal)
                    },
                    modifier = Modifier.weight(1f)
                )
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        Text(
            text = String.format("Manual Fan Duty Cycle: %d %%", fanDutyPct.toInt()),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = fanDutyPct,
            onValueChange = { fanDutyPct = it },
            onValueChangeFinished = {
                fanMode = "custom"
                onSaveNvs("fan_mode", "custom")
                val rawDuty = ((fanDutyPct / 100f) * 1023f).toInt().coerceIn(0, 1023)
                onSaveNvs("fan_duty", rawDuty)
            },
            valueRange = 20f..100f
        )

        Text(
            text = String.format("Thermal PID Target Setpoint: %.1f °C", pidSp),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = pidSp,
            onValueChange = { pidSp = it },
            onValueChangeFinished = {
                prefs.edit().putFloat("pid_sp", pidSp).apply()
                onSendRawCommand(TelemetryParser.buildTuneCommand(pidSetpoint = pidSp))
            },
            valueRange = 35f..65f
        )

        Text(
            text = String.format("Over-Temperature Protection (OTP) Cutoff: %.1f °C", otpThr),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = otpThr,
            onValueChange = { otpThr = it },
            onValueChangeFinished = {
                prefs.edit().putFloat("otp_thr", otpThr).apply()
                onSendRawCommand(TelemetryParser.buildTuneCommand(otpThresh = otpThr))
            },
            valueRange = 65f..95f
        )
    }

    // 2. Buzzer & Front Panel Lock
    SectionCard(title = "SYSTEM BUZZER & FRONT PANEL CONTROLS") {
        SettingSwitchRow(
            label = "Enable Piezo System Buzzer",
            checked = buzzEnabled,
            onCheckedChange = {
                buzzEnabled = it
                onSaveNvs("buzz_enabled", it)
            }
        )

        Spacer(modifier = Modifier.height(4.dp))

        Text(
            text = String.format("Buzzer Volume / PWM Duty: %d %%", buzzVolume.toInt()),
            color = IndustrialPrimary,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace
        )
        Slider(
            value = buzzVolume,
            onValueChange = { buzzVolume = it },
            onValueChangeFinished = { onSaveNvs("buzz_volume", buzzVolume.toInt()) },
            valueRange = 0f..100f
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            OutlinedButton(
                onClick = { onSendRawCommand(TelemetryParser.buildSimpleCommand("tone-click")) },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("CLICK TONE", color = IndustrialPrimary, fontSize = 11.5.sp, fontFamily = FontFamily.Monospace)
            }
            OutlinedButton(
                onClick = { onSendRawCommand(TelemetryParser.buildSimpleCommand("tone-test")) },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("TEST MELODY", color = IndustrialSecondary, fontSize = 11.5.sp, fontFamily = FontFamily.Monospace)
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        SettingSwitchRow(
            label = "Front Panel Physical Button Lock",
            checked = btnLock,
            onCheckedChange = {
                btnLock = it
                onSaveNvs("btn_lock", it)
            }
        )
    }

    // 3. ADS1115 4-Channel Precision Hardware Voltmeter
    val smpsV = slowData.smps?.v ?: 0f
    val stbyV = slowData.vStby
    val vddV = slowData.vVdd
    val espV = slowData.vEsp

    SectionCard(title = "ADS1115 4-CHANNEL PRECISION VOLTMETER (I2C 0x48)") {
        InfoRow(label = "Channel 0: Main SMPS Rail (65V)", value = String.format("%.2f V", smpsV))
        InfoRow(label = "Channel 1: Standby Aux Rail (12V)", value = String.format("%.2f V", stbyV))
        InfoRow(label = "Channel 2: Logic 3.3V Rail", value = String.format("%.2f V", vddV))
        InfoRow(label = "Channel 3: System 5.0V Rail", value = String.format("%.2f V", espV))
    }

    // 4. System Maintenance & RTC
    SectionCard(title = "SYSTEM MAINTENANCE, SELF-TEST & RTC CLOCK") {
        var liveClockStr by remember { mutableStateOf("--:--:--") }
        var anchorSecOfDay by remember { mutableIntStateOf(-1) }
        var anchorElapsedRealtime by remember { mutableLongStateOf(0L) }

        val rawRtcTime = slowData.currentTime ?: slowData.time
        LaunchedEffect(rawRtcTime) {
            if (!rawRtcTime.isNullOrBlank()) {
                val timePart = if (rawRtcTime.contains("T")) {
                    rawRtcTime.substringAfter("T").take(8)
                } else {
                    rawRtcTime.take(8)
                }
                val parts = timePart.split(":")
                if (parts.size == 3) {
                    val h = parts[0].toIntOrNull() ?: 0
                    val m = parts[1].toIntOrNull() ?: 0
                    val s = parts[2].toIntOrNull() ?: 0
                    anchorSecOfDay = h * 3600 + m * 60 + s
                    anchorElapsedRealtime = android.os.SystemClock.elapsedRealtime()
                }
            }
        }

        LaunchedEffect(anchorSecOfDay) {
            if (anchorSecOfDay >= 0) {
                while (isActive) {
                    val elapsed = (android.os.SystemClock.elapsedRealtime() - anchorElapsedRealtime) / 1000
                    val curSecOfDay = (anchorSecOfDay + elapsed) % 86400
                    val h = curSecOfDay / 3600
                    val m = (curSecOfDay % 3600) / 60
                    val s = curSecOfDay % 60
                    liveClockStr = String.format("%02d:%02d:%02d", h, m, s)
                    delay(250)
                }
            }
        }

        val clockStr = if (anchorSecOfDay >= 0) liveClockStr else (slowData.currentTime?.substringAfter("T")?.take(8) ?: slowData.time?.substringAfter("T")?.take(8) ?: "--:--:--")
        InfoRow(label = "Hardware Clock (DS3231)", value = clockStr)
        InfoRow(
            label = "DS3231 Internal Temp",
            value = if (slowData.rtcC != null) String.format("%.1f °C", slowData.rtcC) else "-- °C"
        )
        val sqwStr = if (slowData.sqw?.hwConnected == true) {
            String.format("GPIO 36 (Connected • %.2f Hz)", slowData.sqw.freq)
        } else {
            "GPIO 36 (Belum Terhubung / Fallback Timer)"
        }
        InfoRow(label = "SQW 1 Hz Pulse Status", value = sqwStr)

        Spacer(modifier = Modifier.height(6.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Button(
                onClick = {
                    val now = System.currentTimeMillis()
                    val localEpochSec = (now + java.util.TimeZone.getDefault().getOffset(now)) / 1000L
                    onSendRawCommand(TelemetryParser.buildRtcSyncCommand(localEpochSec))
                },
                colors = ButtonDefaults.buttonColors(containerColor = IndustrialTertiary),
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("SYNC DS3231 RTC", color = Color.Black, fontWeight = FontWeight.Bold, fontSize = 11.5.sp, fontFamily = JetBrainsMono)
            }

            Button(
                onClick = {
                    MainActivity.repositoryInstance?.startPowerOnTerminal("Manual Hardware Self-Test (setup) Requested...")
                    onSendRawCommand(TelemetryParser.buildSimpleCommand("setup"))
                    onBack()
                },
                colors = ButtonDefaults.buttonColors(containerColor = IndustrialPrimary),
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("RUN SELF-TEST", color = Color.Black, fontWeight = FontWeight.Bold, fontSize = 11.5.sp, fontFamily = JetBrainsMono)
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            OutlinedButton(
                onClick = { onSendRawCommand(TelemetryParser.buildSimpleCommand("scan")) },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("SCAN I2C BUS", color = IndustrialPrimary, fontSize = 11.5.sp, fontFamily = JetBrainsMono)
            }

            OutlinedButton(
                onClick = { onSendRawCommand("{\"type\":\"cmd\",\"cmd\":{\"alarm_stop\":true}}") },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("SILENCE ALARM", color = IndustrialSecondary, fontSize = 11.5.sp, fontFamily = JetBrainsMono)
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            OutlinedButton(
                onClick = {
                    onRequestDangerConfirm(
                        DangerActionPrompt(
                            title = "FACTORY RESET AMPLIFIER",
                            description = "PERINGATAN SANGAT BERBAHAYA: Seluruh data NVS ESP32 (gain, sensor suhu, batas tegangan SMPS, tuning PID kipas, preset DSP) akan dihapus kembali ke bawaan pabrik dan ESP32 akan reboot otomatis!",
                            confirmButtonText = "FACTORY RESET",
                            onConfirm = { onSendRawCommand(TelemetryParser.buildSimpleCommand("factory-reset")) }
                        )
                    )
                },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("FACTORY RESET", color = IndustrialError, fontSize = 11.5.sp, fontFamily = JetBrainsMono, fontWeight = FontWeight.Bold)
            }

            Button(
                onClick = { onSendRawCommand(TelemetryParser.buildSimpleCommand("reboot")) },
                colors = ButtonDefaults.buttonColors(containerColor = IndustrialSecondary),
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.weight(1f)
            ) {
                Text("REBOOT ESP32 MCU", color = Color.Black, fontWeight = FontWeight.Bold, fontSize = 11.5.sp, fontFamily = JetBrainsMono)
            }
        }
    }
}

/* ==============================================================================
   REUSABLE INDUSTRIAL UI COMPONENTS (Matching Webroot Design)
============================================================================== */
@Composable
private fun SectionCard(
    title: String,
    content: @Composable () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(IndustrialSurface, RoundedCornerShape(6.dp))
            .border(1.dp, Color(0xFF30363D), RoundedCornerShape(6.dp))
            .padding(10.dp)
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleSmall,
            color = IndustrialSecondary,
            fontWeight = FontWeight.Bold,
            fontFamily = JetBrainsMono,
            fontSize = 13.5.sp
        )
        Spacer(modifier = Modifier.height(8.dp))
        content()
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(
            text = label,
            color = Color(0xFF8B949E),
            fontSize = 12.sp,
            fontFamily = JetBrainsMono
        )
        Text(
            text = value,
            color = IndustrialPrimary,
            fontSize = 12.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = JetBrainsMono
        )
    }
}

@Composable
private fun OptionChipButton(
    text: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    Button(
        onClick = onClick,
        colors = ButtonDefaults.buttonColors(
            containerColor = if (selected) IndustrialPrimary else Color(0xFF1E242C)
        ),
        shape = RoundedCornerShape(4.dp),
        modifier = modifier.height(34.dp),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 10.dp, vertical = 0.dp)
    ) {
        Text(
            text = text,
            color = if (selected) Color.Black else IndustrialPrimary,
            fontSize = 12.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = JetBrainsMono,
            maxLines = 1,
            overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis
        )
    }
}

@Composable
private fun SettingSwitchRow(
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            color = Color.White,
            fontSize = 12.5.sp,
            fontFamily = JetBrainsMono,
            modifier = Modifier.weight(1f)
        )
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = SwitchDefaults.colors(
                checkedThumbColor = Color(0xFF60A5FA),
                checkedTrackColor = Color(0xFF1D4ED8),
                uncheckedThumbColor = Color(0xFF6B7280),
                uncheckedTrackColor = Color(0xFF1F2937)
            )
        )
    }
}
