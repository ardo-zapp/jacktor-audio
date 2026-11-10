package com.jacktor.audiopanel.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.jacktor.audiopanel.model.SerialBridgeStatus
import com.jacktor.audiopanel.service.TelemetryConnectionManager.ConnectionMode
import com.jacktor.audiopanel.ui.theme.IndustrialPrimary
import com.jacktor.audiopanel.ui.theme.IndustrialSecondary
import com.jacktor.audiopanel.ui.theme.IndustrialSurface
import com.jacktor.audiopanel.ui.theme.IndustrialTertiary
import com.jacktor.audiopanel.ui.theme.JetBrainsMono

@Composable
fun SystemInfoCard(
    serialBridgeStatus: SerialBridgeStatus?,
    panelIp: String?,
    connectionMode: ConnectionMode?,
    statusMsg: String,
    isPowerOn: Boolean,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .background(IndustrialSurface, RoundedCornerShape(8.dp))
            .border(1.dp, Color(0xFF333D4B), RoundedCornerShape(8.dp))
            .padding(horizontal = 12.dp, vertical = 8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "JACKTOR PANEL SYSTEM ARCHITECTURE",
                style = MaterialTheme.typography.titleSmall,
                color = IndustrialPrimary,
                fontWeight = FontWeight.Bold,
                fontFamily = JetBrainsMono,
                fontSize = 12.5.sp
            )
            Spacer(modifier = Modifier.weight(1f))
            Text(
                text = if (isPowerOn) "RUNNING" else "STANDBY",
                style = MaterialTheme.typography.labelSmall,
                color = if (isPowerOn) IndustrialTertiary else IndustrialSecondary,
                fontFamily = JetBrainsMono,
                fontWeight = FontWeight.Bold,
                fontSize = 11.5.sp
            )
        }

        Spacer(modifier = Modifier.height(6.dp))

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF0D0F12), RoundedCornerShape(6.dp))
                .border(1.dp, Color(0xFF1F2630), RoundedCornerShape(6.dp))
                .padding(horizontal = 10.dp, vertical = 8.dp)
        ) {
            Column(verticalArrangement = Arrangement.spacedBy(4.5.dp)) {
                val isOnline = statusMsg.contains("ONLINE", ignoreCase = true)
                val isFlashing = statusMsg.contains("Firmware", ignoreCase = true) || statusMsg.contains("MAINTENANCE", ignoreCase = true)
                val isConnecting = statusMsg.contains("Connecting", ignoreCase = true)

                val linkText = when {
                    isFlashing -> "Flashing..."
                    isOnline -> "Online"
                    isConnecting -> "Connecting..."
                    else -> "Offline"
                }
                val linkColor = when {
                    isOnline -> IndustrialTertiary
                    isFlashing || isConnecting -> Color(0xFFF59E0B)
                    else -> Color(0xFFEF4444)
                }

                InfoLine(label = "LINK STATUS", value = linkText, valueColor = linkColor)
                InfoLine(
                    label = "LINK MODE",
                    value = connectionMode?.name ?: "AUTO / DIRECT",
                    valueColor = IndustrialPrimary
                )
                InfoLine(
                    label = "SERIAL BUS",
                    value = "UART0 @ 921,600 bps (${serialBridgeStatus?.hwPort ?: "/dev/ttyUSB0"})",
                    valueColor = IndustrialPrimary
                )
                InfoLine(
                    label = "JACKTOR API",
                    value = "Port 80 (Unified Dashboard & SSE)",
                    valueColor = IndustrialPrimary
                )
                InfoLine(
                    label = "PANEL IP",
                    value = panelIp ?: "Offline",
                    valueColor = IndustrialSecondary
                )
                InfoLine(
                    label = "TCP MULTIPLEXER",
                    value = "127.0.0.1:7777 (Clients: ${serialBridgeStatus?.clientsConnected ?: 0})",
                    valueColor = IndustrialPrimary
                )
                InfoLine(
                    label = "AUDIO POLICY",
                    value = "Bit-Perfect 384kHz / 32-bit",
                    valueColor = IndustrialTertiary
                )
                InfoLine(
                    label = "POWER MODE",
                    value = "24/7 Battery Bypass (Infinite Power)",
                    valueColor = IndustrialPrimary
                )
                InfoLine(
                    label = "PLATFORM & OS",
                    value = "Realme C15 Bengal / Android 14 Treble",
                    valueColor = IndustrialSecondary
                )
                InfoLine(
                    label = "KERNEL",
                    value = "Linux 4.19.152-perf+ (KPM Support)",
                    valueColor = Color(0xFF8090A0)
                )
            }
        }
    }
}

@Composable
private fun InfoLine(
    label: String,
    value: String,
    valueColor: Color
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            fontSize = 11.sp,
            fontFamily = JetBrainsMono,
            color = Color(0xFF8090A0),
            fontWeight = FontWeight.Medium
        )
        Text(
            text = value,
            fontSize = 11.sp,
            fontFamily = JetBrainsMono,
            fontWeight = FontWeight.Bold,
            color = valueColor
        )
    }
}
