package com.jacktor.audio.panel

import android.content.Context
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Save
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
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
import com.jacktor.audio.panel.ui.theme.IndustrialBackground
import com.jacktor.audio.panel.ui.theme.IndustrialError
import com.jacktor.audio.panel.ui.theme.IndustrialPrimary
import com.jacktor.audio.panel.ui.theme.IndustrialSecondary
import com.jacktor.audio.panel.ui.theme.IndustrialSurface
import com.jacktor.audio.panel.ui.theme.IndustrialTertiary
import com.jacktor.audio.panel.ui.theme.IndustrialTheme

class SettingsActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val prefs = getSharedPreferences("JacktorPanelPrefs", Context.MODE_PRIVATE)

        setContent {
            IndustrialTheme {
                SettingsScreen(
                    prefs = prefs,
                    onBack = { finish() },
                    onSaveNvs = { key, value ->
                        // Save locally to SharedPref
                        when (value) {
                            is Boolean -> prefs.edit().putBoolean(key, value).apply()
                            is Float -> prefs.edit().putFloat(key, value).apply()
                            is Int -> prefs.edit().putInt(key, value).apply()
                            is String -> prefs.edit().putString(key, value).apply()
                        }
                        // Build JSON NVS command string to sync with Amplifier NVS
                        val cmdStr = TelemetryParser.buildNvsConfigCommand(key, value)
                        MainActivity.usbSerialManager?.sendCommand(cmdStr)
                    }
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    prefs: android.content.SharedPreferences,
    onBack: () -> Unit,
    onSaveNvs: (String, Any) -> Unit
) {
    var smpsCutoff by remember { mutableFloatStateOf(prefs.getFloat("smps_cut", 18.0f)) }
    var smpsRecovery by remember { mutableFloatStateOf(prefs.getFloat("smps_rec", 20.0f)) }
    var fanDuty by remember { mutableFloatStateOf(prefs.getInt("fan_duty", 50).toFloat()) }
    var buzzVolume by remember { mutableFloatStateOf(prefs.getInt("buzz_volume", 80).toFloat()) }

    var buzzEnabled by remember { mutableStateOf(prefs.getBoolean("buzz_enabled", true)) }
    var btnLock by remember { mutableStateOf(prefs.getBoolean("btn_lock", false)) }
    var autoSenseEn by remember { mutableStateOf(prefs.getBoolean("auto_sense_en", true)) }
    var smpsBypass by remember { mutableStateOf(prefs.getBoolean("smps_bypass", false)) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        "AMPLIFIER NVS & APP SETTINGS",
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        color = IndustrialPrimary,
                        fontSize = 14.sp
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back", tint = IndustrialPrimary)
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
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // SMPS Power Settings Section
            SectionCard(title = "SMPS & POWER PROTECTION") {
                SettingSwitchRow(
                    label = "SMPS Hardware Bypass",
                    checked = smpsBypass,
                    onCheckedChange = {
                        smpsBypass = it
                        onSaveNvs("smps_bypass", it)
                    }
                )

                Spacer(modifier = Modifier.height(8.dp))

                Text(
                    text = String.format("SMPS Cutoff Voltage: %.1f V", smpsCutoff),
                    color = IndustrialPrimary,
                    fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace
                )
                Slider(
                    value = smpsCutoff,
                    onValueChange = { smpsCutoff = it },
                    onValueChangeFinished = { onSaveNvs("smps_cut", smpsCutoff) },
                    valueRange = 12f..30f
                )

                Text(
                    text = String.format("SMPS Recovery Voltage: %.1f V", smpsRecovery),
                    color = IndustrialPrimary,
                    fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace
                )
                Slider(
                    value = smpsRecovery,
                    onValueChange = { smpsRecovery = it },
                    onValueChangeFinished = { onSaveNvs("smps_rec", smpsRecovery) },
                    valueRange = 15f..35f
                )
            }

            // Fan & Cooling Section
            SectionCard(title = "COOLING FAN CONTROL") {
                Text(
                    text = String.format("Fan Duty Cycle: %d %%", fanDuty.toInt()),
                    color = IndustrialPrimary,
                    fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace
                )
                Slider(
                    value = fanDuty,
                    onValueChange = { fanDuty = it },
                    onValueChangeFinished = { onSaveNvs("fan_duty", fanDuty.toInt()) },
                    valueRange = 20f..100f
                )
            }

            // Buzzer & Control Locks
            SectionCard(title = "SYSTEM BUZZER & CONTROLS") {
                SettingSwitchRow(
                    label = "Enable System Buzzer",
                    checked = buzzEnabled,
                    onCheckedChange = {
                        buzzEnabled = it
                        onSaveNvs("buzz_enabled", it)
                    }
                )

                Spacer(modifier = Modifier.height(8.dp))

                Text(
                    text = String.format("Buzzer Volume: %d %%", buzzVolume.toInt()),
                    color = IndustrialPrimary,
                    fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace
                )
                Slider(
                    value = buzzVolume,
                    onValueChange = { buzzVolume = it },
                    onValueChangeFinished = { onSaveNvs("buzz_volume", buzzVolume.toInt()) },
                    valueRange = 0f..100f
                )

                Spacer(modifier = Modifier.height(8.dp))

                SettingSwitchRow(
                    label = "Front Panel Button Lock",
                    checked = btnLock,
                    onCheckedChange = {
                        btnLock = it
                        onSaveNvs("btn_lock", it)
                    }
                )

                SettingSwitchRow(
                    label = "Auto Audio Sense Power On",
                    checked = autoSenseEn,
                    onCheckedChange = {
                        autoSenseEn = it
                        onSaveNvs("auto_sense_en", it)
                    }
                )
            }

            Button(
                onClick = onBack,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(44.dp),
                colors = ButtonDefaults.buttonColors(containerColor = IndustrialPrimary),
                shape = RoundedCornerShape(6.dp)
            ) {
                Icon(Icons.Default.Save, contentDescription = "Save", tint = Color.Black)
                Spacer(modifier = Modifier.width(8.dp))
                Text("DONE & RETURN", color = Color.Black, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
            }
        }
    }
}

@Composable
private fun SectionCard(
    title: String,
    content: @Composable () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(IndustrialSurface, RoundedCornerShape(8.dp))
            .border(1.dp, Color(0xFF333D4B), RoundedCornerShape(8.dp))
            .padding(14.dp)
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleSmall,
            color = IndustrialSecondary,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace,
            fontSize = 12.sp
        )
        Spacer(modifier = Modifier.height(10.dp))
        content()
    }
}

@Composable
private fun SettingSwitchRow(
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            color = Color.White,
            fontSize = 12.sp,
            fontFamily = FontFamily.Monospace
        )
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = SwitchDefaults.colors(
                checkedThumbColor = IndustrialTertiary,
                checkedTrackColor = Color(0xFF1E3A2B),
                uncheckedThumbColor = Color.Gray,
                uncheckedTrackColor = Color(0xFF20252D)
            )
        )
    }
}
