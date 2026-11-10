package com.jacktor.audiopanel.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
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
import com.jacktor.audiopanel.ui.theme.IndustrialError
import com.jacktor.audiopanel.ui.theme.IndustrialPrimary
import com.jacktor.audiopanel.ui.theme.IndustrialSecondary
import com.jacktor.audiopanel.ui.theme.IndustrialSurface
import com.jacktor.audiopanel.ui.theme.IndustrialTertiary
import com.jacktor.audiopanel.ui.theme.JetBrainsMono

@Composable
fun StereoVuMeterView(
    vuL: Int,
    vuR: Int,
    clipping: Boolean,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .background(IndustrialSurface, RoundedCornerShape(8.dp))
            .border(1.dp, Color(0xFF333D4B), RoundedCornerShape(8.dp))
            .padding(horizontal = 10.dp, vertical = 6.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "STEREO VU METER",
                style = MaterialTheme.typography.titleSmall,
                color = IndustrialPrimary,
                fontWeight = FontWeight.Bold,
                fontFamily = JetBrainsMono,
                fontSize = 12.5.sp
            )
            Spacer(modifier = Modifier.weight(1f))
            if (clipping) {
                Text(
                    text = "[ CLIP ]",
                    style = MaterialTheme.typography.labelSmall,
                    color = IndustrialError,
                    fontWeight = FontWeight.Bold,
                    fontFamily = JetBrainsMono,
                    fontSize = 11.sp
                )
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        VuBarRow(label = "L", value = vuL)
        Spacer(modifier = Modifier.height(3.dp))
        VuBarRow(label = "R", value = vuR)
    }
}

@Composable
private fun VuBarRow(label: String, value: Int) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = IndustrialSecondary,
            fontWeight = FontWeight.Bold,
            fontFamily = JetBrainsMono,
            fontSize = 12.sp,
            modifier = Modifier.width(18.dp)
        )

        Row(
            modifier = Modifier
                .weight(1f)
                .height(16.dp)
                .background(Color(0xFF0D0F12), RoundedCornerShape(3.dp))
                .border(1.dp, Color(0xFF1F2630), RoundedCornerShape(3.dp))
                .padding(horizontal = 3.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            val totalSegments = 24
            val activeSegments = ((value.coerceIn(0, 255) / 255f) * totalSegments).toInt()

            for (i in 0 until totalSegments) {
                val segColor = when {
                    i >= 20 -> IndustrialError       // Overload / Clip
                    i >= 15 -> IndustrialSecondary   // Amber high
                    i >= 8  -> IndustrialPrimary     // Cyan mid
                    else    -> IndustrialTertiary    // Green low
                }

                val isLit = i < activeSegments

                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .background(
                            if (isLit) segColor else Color(0xFF1B2028),
                            RoundedCornerShape(1.dp)
                        )
                )
            }
        }

        Spacer(modifier = Modifier.width(8.dp))

        Text(
            text = "$value",
            style = MaterialTheme.typography.labelSmall,
            color = IndustrialPrimary,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.width(32.dp)
        )
    }
}
