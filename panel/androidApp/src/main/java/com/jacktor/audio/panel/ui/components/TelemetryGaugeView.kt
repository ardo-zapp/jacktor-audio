package com.jacktor.audio.panel.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.jacktor.audio.panel.shared.model.SlowTelemetry
import com.jacktor.audio.panel.ui.theme.IndustrialError
import com.jacktor.audio.panel.ui.theme.IndustrialPrimary
import com.jacktor.audio.panel.ui.theme.IndustrialSecondary
import com.jacktor.audio.panel.ui.theme.IndustrialSurface
import com.jacktor.audio.panel.ui.theme.IndustrialTertiary

@Composable
fun TelemetryGaugeView(
    slowData: SlowTelemetry,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .background(IndustrialSurface, RoundedCornerShape(8.dp))
            .border(1.dp, Color(0xFF333D4B), RoundedCornerShape(8.dp))
            .padding(12.dp)
    ) {
        Text(
            text = "AMPLIFIER SYSTEM TELEMETRY",
            style = MaterialTheme.typography.titleSmall,
            color = IndustrialPrimary,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace,
            fontSize = 12.sp
        )

        Spacer(modifier = Modifier.height(10.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            val smpsV = slowData.smps?.v ?: 0f
            GaugeCard(
                label = "SMPS VOLTS",
                value = String.format("%.1f V", smpsV),
                status = slowData.smps?.stage?.uppercase() ?: "STBY",
                color = if (smpsV >= 20f) IndustrialTertiary else IndustrialSecondary,
                modifier = Modifier.weight(1f)
            )

            val heatC = slowData.heatC ?: 0f
            GaugeCard(
                label = "HEATSINK TEMP",
                value = String.format("%.1f °C", heatC),
                status = if (heatC > 70f) "HOT" else "NORMAL",
                color = if (heatC > 70f) IndustrialError else IndustrialPrimary,
                modifier = Modifier.weight(1f)
            )

            val stbyV = slowData.vStby
            GaugeCard(
                label = "STBY VOLTS",
                value = String.format("%.1f V", stbyV),
                status = "STBY",
                color = IndustrialTertiary,
                modifier = Modifier.weight(1f)
            )
        }

        if (slowData.errors.isNotEmpty() || slowData.warnings.isNotEmpty()) {
            Spacer(modifier = Modifier.height(8.dp))
            val alerts = (slowData.errors + slowData.warnings).joinToString(" | ")
            Text(
                text = "ALERTS: $alerts",
                color = IndustrialError,
                style = MaterialTheme.typography.labelSmall,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold
            )
        }
    }
}

@Composable
private fun GaugeCard(
    label: String,
    value: String,
    status: String,
    color: Color,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .background(Color(0xFF0D0F12), RoundedCornerShape(6.dp))
            .border(1.dp, Color(0xFF1F2630), RoundedCornerShape(6.dp))
            .padding(8.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = Color(0xFF8090A0),
            fontSize = 9.sp,
            fontFamily = FontFamily.Monospace
        )
        Spacer(modifier = Modifier.height(2.dp))
        Text(
            text = value,
            style = MaterialTheme.typography.titleMedium,
            color = color,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace
        )
        Spacer(modifier = Modifier.height(2.dp))
        Text(
            text = status,
            style = MaterialTheme.typography.labelSmall,
            color = color.copy(alpha = 0.8f),
            fontSize = 9.sp,
            fontFamily = FontFamily.Monospace
        )
    }
}
