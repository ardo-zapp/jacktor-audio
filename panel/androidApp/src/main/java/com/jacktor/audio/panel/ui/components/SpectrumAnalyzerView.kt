package com.jacktor.audio.panel.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.jacktor.audio.panel.ui.theme.IndustrialError
import com.jacktor.audio.panel.ui.theme.IndustrialPrimary
import com.jacktor.audio.panel.ui.theme.IndustrialSecondary
import com.jacktor.audio.panel.ui.theme.IndustrialSurface
import com.jacktor.audio.panel.ui.theme.IndustrialTertiary

@Composable
fun SpectrumAnalyzerView(
    bands: IntArray,
    modifier: Modifier = Modifier,
    mode: String = "fft"
) {
    Column(
        modifier = modifier
            .background(IndustrialSurface, RoundedCornerShape(8.dp))
            .border(1.dp, Color(0xFF333D4B), RoundedCornerShape(8.dp))
            .padding(12.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "AUDIO SPECTRUM ANALYZER (32 BANDS)",
                style = MaterialTheme.typography.titleSmall,
                color = IndustrialPrimary,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                fontSize = 12.sp
            )
            Spacer(modifier = Modifier.weight(1f))
            Text(
                text = "MODE: ${mode.uppercase()}",
                style = MaterialTheme.typography.labelSmall,
                color = IndustrialSecondary,
                fontFamily = FontFamily.Monospace
            )
        }

        Spacer(modifier = Modifier.height(8.dp))

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(140.dp)
                .background(Color(0xFF0D0F12), RoundedCornerShape(4.dp))
                .border(1.dp, Color(0xFF1F2630), RoundedCornerShape(4.dp))
                .padding(4.dp)
        ) {
            Canvas(modifier = Modifier.fillMaxSize()) {
                val numBands = if (bands.isNotEmpty()) bands.size else 32
                val spacing = 2f
                val totalSpacing = spacing * (numBands - 1)
                val barWidth = (size.width - totalSpacing) / numBands
                val maxHeight = size.height

                val barGradient = Brush.verticalGradient(
                    colors = listOf(
                        IndustrialError,      // High freq / peak red
                        IndustrialSecondary,  // Mid amber
                        IndustrialPrimary,    // Low cyan
                        IndustrialTertiary    // Bottom green
                    )
                )

                for (i in 0 until numBands) {
                    val rawVal = if (i < bands.size) bands[i] else 0
                    val normalizedVal = (rawVal.coerceIn(0, 255) / 255f)
                    val barHeight = (normalizedVal * maxHeight).coerceAtLeast(2f)

                    val x = i * (barWidth + spacing)
                    val y = maxHeight - barHeight

                    drawRect(
                        brush = barGradient,
                        topLeft = Offset(x, y),
                        size = Size(barWidth, barHeight)
                    )

                    // Draw subtle grid lines
                    drawRect(
                        color = Color(0x22000000),
                        topLeft = Offset(x, y),
                        size = Size(barWidth, 1f)
                    )
                }
            }
        }
    }
}
