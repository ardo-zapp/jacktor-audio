package com.jacktor.audiopanel.ui.components

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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.jacktor.audiopanel.model.RealtimeTelemetry
import com.jacktor.audiopanel.model.SlowTelemetry
import com.jacktor.audiopanel.ui.theme.IndustrialError
import com.jacktor.audiopanel.ui.theme.IndustrialPrimary
import com.jacktor.audiopanel.ui.theme.IndustrialSecondary
import com.jacktor.audiopanel.ui.theme.IndustrialSurface
import com.jacktor.audiopanel.ui.theme.IndustrialTertiary
import com.jacktor.audiopanel.ui.theme.JetBrainsMono

@Composable
fun TelemetryGaugeView(
    slowData: SlowTelemetry,
    realtimeData: RealtimeTelemetry? = null,
    isAmpAlive: Boolean = true,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .background(IndustrialSurface, RoundedCornerShape(8.dp))
            .border(1.dp, Color(0xFF333D4B), RoundedCornerShape(8.dp))
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "AMPLIFIER ADS1115 & HARDWARE TELEMETRY",
                style = MaterialTheme.typography.titleSmall,
                color = IndustrialPrimary,
                fontWeight = FontWeight.Bold,
                fontFamily = JetBrainsMono,
                fontSize = 12.5.sp
            )
            val sqwText = if (slowData.sqw?.hwConnected == true) "SQW 1Hz OK" else "SQW OFF"
            Text(
                text = sqwText,
                color = if (slowData.sqw?.hwConnected == true) IndustrialTertiary else Color(0xFF8090A0),
                fontSize = 11.sp,
                fontFamily = JetBrainsMono,
                fontWeight = FontWeight.Bold
            )
        }

        // Section 1: ADS1115 4-Channel Voltage Rails
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            val smpsV = slowData.smps?.v ?: 0f
            GaugeCard(
                label = "SMPS 65V (CH0)",
                value = String.format("%.1f V", smpsV),
                status = slowData.smps?.stage?.uppercase() ?: "STBY",
                color = if (smpsV >= 20f) IndustrialTertiary else IndustrialSecondary,
                modifier = Modifier.weight(1f)
            )

            val stbyV = slowData.vStby
            GaugeCard(
                label = "STBY 12V (CH1)",
                value = String.format("%.1f V", stbyV),
                status = if (stbyV >= 10f) "HEALTHY" else "LOW",
                color = if (stbyV >= 10f) IndustrialTertiary else IndustrialSecondary,
                modifier = Modifier.weight(1f)
            )

            val vddV = slowData.vVdd
            GaugeCard(
                label = "3.3V (CH2)",
                value = String.format("%.2f V", vddV),
                status = "LOGIC",
                color = IndustrialPrimary,
                modifier = Modifier.weight(1f)
            )

            val espV = slowData.vEsp
            GaugeCard(
                label = "5.0V (CH3)",
                value = String.format("%.2f V", espV),
                status = "SYSTEM",
                color = IndustrialPrimary,
                modifier = Modifier.weight(1f)
            )
        }

        // Section 2: Multi-Sensor DS18B20 1-Wire Thermal Management (GPIO 27)
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            val stereoTemp = slowData.heatAb ?: slowData.heatC ?: 0f
            GaugeCard(
                label = "STEREO TEMP",
                value = if (stereoTemp > 0f) String.format("%.1f °C", stereoTemp) else "-- °C",
                status = if (stereoTemp > 70f) "HOT" else "NORMAL",
                color = if (stereoTemp > 70f) IndustrialError else IndustrialPrimary,
                modifier = Modifier.weight(1f)
            )

            val subTemp = slowData.heatD ?: 0f
            GaugeCard(
                label = "SUBWOOFER",
                value = if (subTemp > 0f) String.format("%.1f °C", subTemp) else "-- °C",
                status = if (subTemp > 70f) "HOT" else "NORMAL",
                color = if (subTemp > 70f) IndustrialError else IndustrialPrimary,
                modifier = Modifier.weight(1f)
            )

            val smpsTemp = slowData.heatSmps ?: 0f
            GaugeCard(
                label = "SMPS TEMP",
                value = if (smpsTemp > 0f) String.format("%.1f °C", smpsTemp) else "-- °C",
                status = if (smpsTemp > 70f) "HOT" else "NORMAL",
                color = if (smpsTemp > 70f) IndustrialError else IndustrialPrimary,
                modifier = Modifier.weight(1f)
            )

            val rtcTemp = slowData.rtcC ?: 0f
            GaugeCard(
                label = "RTC DS3231",
                value = if (rtcTemp > 0f) String.format("%.1f °C", rtcTemp) else "-- °C",
                status = "ON-BOARD",
                color = IndustrialSecondary,
                modifier = Modifier.weight(1f)
            )
        }

        // Section 3: Audio ADC DC Bias & Hardware Relays
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            val dcL = realtimeData?.dcL ?: 0
            val dcR = realtimeData?.dcR ?: 0
            val biasL = (dcL * 3.3f) / 4095.0f
            val biasR = (dcR * 3.3f) / 4095.0f
            val biasStr = if (dcL > 0 || dcR > 0) {
                String.format("%.2f|%.2f V", biasL, biasR)
            } else {
                "-- | --"
            }
            val isBalanced = (biasL in 1.2f..2.1f) && (biasR in 1.2f..2.1f)
            GaugeCard(
                label = "ADC BIAS L/R",
                value = biasStr,
                status = if (isBalanced) "BALANCED" else "OFFSET",
                color = if (isBalanced) IndustrialTertiary else IndustrialSecondary,
                modifier = Modifier.weight(1f)
            )

            val spkState = if (slowData.states?.on == true) "ENGAGED" else "MUTED"
            GaugeCard(
                label = "SPK RELAY",
                value = spkState,
                status = if (slowData.states?.on == true) "PROTECTED" else "STANDBY",
                color = if (slowData.states?.on == true) IndustrialTertiary else IndustrialSecondary,
                modifier = Modifier.weight(1f)
            )

            val fanMode = slowData.nvs?.fanModeStr?.uppercase() ?: "AUTO"
            val fanDutyPercent = ((slowData.nvs?.fanDuty ?: 0) * 100) / 1023
            GaugeCard(
                label = "FAN CONTROL",
                value = "$fanDutyPercent%",
                status = fanMode,
                color = IndustrialPrimary,
                modifier = Modifier.weight(1f)
            )
        }

        if (isAmpAlive && (slowData.errors.isNotEmpty() || slowData.warnings.isNotEmpty())) {
            val alerts = (slowData.errors + slowData.warnings).joinToString(" | ")
            Text(
                text = "ALERTS: $alerts",
                color = IndustrialError,
                style = MaterialTheme.typography.labelSmall,
                fontFamily = JetBrainsMono,
                fontWeight = FontWeight.Bold,
                fontSize = 11.5.sp
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
            .padding(horizontal = 4.dp, vertical = 5.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = Color(0xFF8090A0),
            fontSize = 10.5.sp,
            fontFamily = JetBrainsMono,
            fontWeight = FontWeight.Bold,
            maxLines = 1
        )
        Spacer(modifier = Modifier.height(2.dp))
        Text(
            text = value,
            style = MaterialTheme.typography.titleMedium,
            color = color,
            fontWeight = FontWeight.Bold,
            fontFamily = JetBrainsMono,
            fontSize = 14.5.sp,
            maxLines = 1
        )
        Spacer(modifier = Modifier.height(1.dp))
        Text(
            text = status,
            style = MaterialTheme.typography.labelSmall,
            color = color.copy(alpha = 0.85f),
            fontSize = 10.sp,
            fontFamily = JetBrainsMono,
            maxLines = 1
        )
    }
}
