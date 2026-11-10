package com.jacktor.audiopanel.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredHeight
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeDown
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.FastRewind
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.jacktor.audiopanel.ui.theme.IndustrialPrimary
import com.jacktor.audiopanel.ui.theme.IndustrialSecondary
import com.jacktor.audiopanel.ui.theme.IndustrialSurface
import com.jacktor.audiopanel.ui.theme.IndustrialTertiary
import com.jacktor.audiopanel.ui.theme.JetBrainsMono

@Composable
fun MediaControlToolbar(
    onCommand: (String, Any?) -> Unit,
    @Suppress("UNUSED_PARAMETER") isPowerOn: Boolean = true,
    bassBoost: Boolean = false,
    sound3d: Boolean = false,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .background(IndustrialSurface, RoundedCornerShape(8.dp))
            .border(1.dp, Color(0xFF333D4B), RoundedCornerShape(8.dp))
            .padding(horizontal = 10.dp, vertical = 7.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "MEDIA PLAYBACK & DSP ENHANCEMENTS",
                style = MaterialTheme.typography.titleSmall,
                color = IndustrialPrimary,
                fontWeight = FontWeight.Bold,
                fontFamily = JetBrainsMono,
                fontSize = 13.sp
            )
        }

        Spacer(modifier = Modifier.height(5.dp))

        // GAIA Playback & Volume Control Toolbar
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF0D0F12), RoundedCornerShape(6.dp))
                .border(1.dp, Color(0xFF1F2630), RoundedCornerShape(6.dp))
                .padding(horizontal = 4.dp, vertical = 3.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = { onCommand("prev", null) }, modifier = Modifier.size(38.dp)) {
                    Icon(Icons.Default.FastRewind, contentDescription = "Prev", tint = IndustrialPrimary, modifier = Modifier.size(24.dp))
                }
                IconButton(onClick = { onCommand("play", null) }, modifier = Modifier.size(38.dp)) {
                    Icon(Icons.Default.PlayArrow, contentDescription = "Play", tint = IndustrialTertiary, modifier = Modifier.size(26.dp))
                }
                IconButton(onClick = { onCommand("pause", null) }, modifier = Modifier.size(38.dp)) {
                    Icon(Icons.Default.Pause, contentDescription = "Pause", tint = IndustrialSecondary, modifier = Modifier.size(24.dp))
                }
                IconButton(onClick = { onCommand("next", null) }, modifier = Modifier.size(38.dp)) {
                    Icon(Icons.Default.FastForward, contentDescription = "Next", tint = IndustrialPrimary, modifier = Modifier.size(24.dp))
                }
            }

            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = { onCommand("voldown", null) }, modifier = Modifier.size(38.dp)) {
                    Icon(Icons.AutoMirrored.Filled.VolumeDown, contentDescription = "Vol Down", tint = IndustrialPrimary, modifier = Modifier.size(24.dp))
                }
                IconButton(onClick = { onCommand("volup", null) }, modifier = Modifier.size(38.dp)) {
                    Icon(Icons.AutoMirrored.Filled.VolumeUp, contentDescription = "Vol Up", tint = IndustrialPrimary, modifier = Modifier.size(24.dp))
                }
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                OutlinedButton(
                    onClick = { onCommand("setbass", if (bassBoost) 0 else 1) },
                    shape = RoundedCornerShape(4.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                    border = androidx.compose.foundation.BorderStroke(
                        1.dp,
                        if (bassBoost) Color(0xFF60A5FA) else IndustrialPrimary.copy(alpha = 0.5f)
                    ),
                    colors = androidx.compose.material3.ButtonDefaults.outlinedButtonColors(
                        containerColor = if (bassBoost) Color(0xFF1D4ED8) else Color.Transparent
                    ),
                    modifier = Modifier.requiredHeight(32.dp)
                ) {
                    Text(
                        "BASS",
                        color = if (bassBoost) Color.White else IndustrialPrimary,
                        fontSize = 12.sp,
                        fontFamily = JetBrainsMono,
                        fontWeight = FontWeight.Bold
                    )
                }
                OutlinedButton(
                    onClick = { onCommand("set3d", if (sound3d) 0 else 1) },
                    shape = RoundedCornerShape(4.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                    border = androidx.compose.foundation.BorderStroke(
                        1.dp,
                        if (sound3d) Color(0xFF60A5FA) else IndustrialPrimary.copy(alpha = 0.5f)
                    ),
                    colors = androidx.compose.material3.ButtonDefaults.outlinedButtonColors(
                        containerColor = if (sound3d) Color(0xFF1D4ED8) else Color.Transparent
                    ),
                    modifier = Modifier.requiredHeight(32.dp)
                ) {
                    Text(
                        "3D",
                        color = if (sound3d) Color.White else IndustrialPrimary,
                        fontSize = 12.sp,
                        fontFamily = JetBrainsMono,
                        fontWeight = FontWeight.Bold
                    )
                }
                OutlinedButton(
                    onClick = { onCommand("enterpairing", null) },
                    shape = RoundedCornerShape(4.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                    modifier = Modifier.requiredHeight(32.dp)
                ) {
                    Text("PAIR", color = IndustrialSecondary, fontSize = 12.sp, fontFamily = JetBrainsMono, fontWeight = FontWeight.Bold)
                }
            }
        }
    }
}
