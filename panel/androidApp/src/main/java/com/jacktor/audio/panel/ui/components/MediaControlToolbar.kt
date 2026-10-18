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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.FastRewind
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material.icons.filled.VolumeDown
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
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
fun MediaControlToolbar(
    onCommand: (String, Any?) -> Unit,
    isPowerOn: Boolean,
    modifier: Modifier = Modifier
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
                text = "GAIA CONTROLS & POWER",
                style = MaterialTheme.typography.titleSmall,
                color = IndustrialPrimary,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                fontSize = 12.sp
            )
            Spacer(modifier = Modifier.weight(1f))

            // Main Power Button
            Button(
                onClick = { onCommand("power", !isPowerOn) },
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (isPowerOn) IndustrialTertiary else IndustrialError
                ),
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.height(32.dp)
            ) {
                Icon(
                    imageVector = Icons.Default.PowerSettingsNew,
                    contentDescription = "Power",
                    tint = Color.Black
                )
                Spacer(modifier = Modifier.width(4.dp))
                Text(
                    text = if (isPowerOn) "POWER ON" else "STANDBY",
                    color = Color.Black,
                    fontWeight = FontWeight.Bold,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace
                )
            }

            Spacer(modifier = Modifier.width(8.dp))

            // Temporary Bypass Button
            OutlinedButton(
                onClick = { onCommand("pwr_bypass", true) },
                shape = RoundedCornerShape(4.dp),
                modifier = Modifier.height(32.dp)
            ) {
                Text(
                    text = "BYPASS",
                    color = IndustrialSecondary,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace
                )
            }
        }

        Spacer(modifier = Modifier.height(12.dp))

        // GAIA Playback & Volume Control Toolbar
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF0D0F12), RoundedCornerShape(6.dp))
                .border(1.dp, Color(0xFF1F2630), RoundedCornerShape(6.dp))
                .padding(6.dp),
            horizontalArrangement = Arrangement.SpaceAround,
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = { onCommand("prev", null) }) {
                Icon(Icons.Default.FastRewind, contentDescription = "Prev", tint = IndustrialPrimary)
            }
            IconButton(onClick = { onCommand("play", null) }) {
                Icon(Icons.Default.PlayArrow, contentDescription = "Play", tint = IndustrialTertiary)
            }
            IconButton(onClick = { onCommand("pause", null) }) {
                Icon(Icons.Default.Pause, contentDescription = "Pause", tint = IndustrialSecondary)
            }
            IconButton(onClick = { onCommand("next", null) }) {
                Icon(Icons.Default.FastForward, contentDescription = "Next", tint = IndustrialPrimary)
            }

            Spacer(modifier = Modifier.width(8.dp))

            IconButton(onClick = { onCommand("voldown", null) }) {
                Icon(Icons.Default.VolumeDown, contentDescription = "Vol Down", tint = IndustrialPrimary)
            }
            IconButton(onClick = { onCommand("volup", null) }) {
                Icon(Icons.Default.VolumeUp, contentDescription = "Vol Up", tint = IndustrialPrimary)
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // GAIA Audio Enhancements
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedButton(
                onClick = { onCommand("setbass", 1) },
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(4.dp)
            ) {
                Text("BASS BOOST", color = IndustrialPrimary, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
            OutlinedButton(
                onClick = { onCommand("set3d", 1) },
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(4.dp)
            ) {
                Text("3D SOUND", color = IndustrialPrimary, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
            OutlinedButton(
                onClick = { onCommand("enterpairing", null) },
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(4.dp)
            ) {
                Text("PAIRING", color = IndustrialSecondary, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
        }
    }
}
