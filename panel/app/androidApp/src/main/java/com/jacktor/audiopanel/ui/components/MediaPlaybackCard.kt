package com.jacktor.audiopanel.ui.components

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredHeight
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeDown
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.jacktor.audiopanel.model.BluetoothTelemetry
import com.jacktor.audiopanel.service.MediaNotificationListenerService
import com.jacktor.audiopanel.service.MediaTrackInfo
import com.jacktor.audiopanel.ui.theme.IndustrialPrimary
import com.jacktor.audiopanel.ui.theme.IndustrialSecondary
import com.jacktor.audiopanel.ui.theme.IndustrialSurface
import com.jacktor.audiopanel.ui.theme.IndustrialTertiary
import com.jacktor.audiopanel.ui.theme.JetBrainsMono
import com.jacktor.audiopanel.utils.PanelStrings

@Composable
fun MediaPlaybackCard(
    trackInfo: MediaTrackInfo,
    coverBitmap: Bitmap?,
    btTelemetry: BluetoothTelemetry?,
    realtimeBtStatus: String?,
    bassBoost: Boolean,
    sound3d: Boolean,
    channelMode: Int = 0,
    isPowerOn: Boolean,
    onCommand: (String, Any?) -> Unit,
    modifier: Modifier = Modifier
) {
    val isBtConnected = (btTelemetry?.sppConnected == true) ||
        (realtimeBtStatus?.contains("connect", ignoreCase = true) == true) ||
        (btTelemetry?.status?.contains("connect", ignoreCase = true) == true)
    val isBtPairing = btTelemetry?.sppState?.contains("pair", ignoreCase = true) == true

    val hasActiveTrack = trackInfo.title.isNotBlank()
    val displayTitle = if (hasActiveTrack) {
        trackInfo.title
    } else if (isBtConnected) {
        "Bluetooth Audio Stream"
    } else if (isPowerOn) {
        "Jacktor Hi-Res Master"
    } else {
        "Amplifier In Standby"
    }

    val displayArtist = if (hasActiveTrack) {
        trackInfo.artist.ifBlank {
            if (trackInfo.packageName.isNotBlank()) {
                trackInfo.packageName.substringAfterLast('.').replaceFirstChar { it.uppercase() }
            } else {
                "Unknown Artist"
            }
        }
    } else if (isBtConnected) {
        val codec = btTelemetry?.codec?.takeIf { it.isNotBlank() && it != "--" } ?: "LDAC / aptX"
        "Qualcomm QCC5125 ($codec)"
    } else if (isPowerOn) {
        "Qualcomm Bengal 384kHz / 32-bit"
    } else {
        "Catu Daya Siaga (Ready)"
    }

    Column(
        modifier = modifier
            .background(IndustrialSurface, RoundedCornerShape(8.dp))
            .border(1.dp, Color(0xFF333D4B), RoundedCornerShape(8.dp))
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        // 1. Header: Title & Bluetooth Status Badge
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    imageVector = Icons.Default.MusicNote,
                    contentDescription = null,
                    tint = IndustrialPrimary,
                    modifier = Modifier.size(16.dp)
                )
                Spacer(modifier = Modifier.width(6.dp))
                Text(
                    text = "MEDIA PLAYBACK & BLUETOOTH",
                    style = MaterialTheme.typography.titleSmall,
                    color = IndustrialPrimary,
                    fontWeight = FontWeight.Bold,
                    fontFamily = JetBrainsMono,
                    fontSize = 12.5.sp
                )
            }

            // Bluetooth Status Badge
            val btBadgeText = when {
                isBtConnected -> {
                    val codec = btTelemetry?.codec?.takeIf { it.isNotBlank() && it != "--" } ?: "QCC5125"
                    "BT ON ($codec)"
                }
                isBtPairing -> "PAIRING..."
                isPowerOn -> "BT STANDBY"
                else -> "BT OFF"
            }
            val btBadgeColor = when {
                isBtConnected -> IndustrialTertiary
                isBtPairing -> Color(0xFFF59E0B)
                isPowerOn -> Color(0xFF8090A0)
                else -> Color(0xFF556070)
            }

            Box(
                modifier = Modifier
                    .background(btBadgeColor.copy(alpha = 0.15f), RoundedCornerShape(4.dp))
                    .border(1.dp, btBadgeColor.copy(alpha = 0.6f), RoundedCornerShape(4.dp))
                    .padding(horizontal = 6.dp, vertical = 2.dp)
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        imageVector = if (isBtConnected) Icons.Default.BluetoothConnected else Icons.Default.Bluetooth,
                        contentDescription = null,
                        tint = btBadgeColor,
                        modifier = Modifier.size(13.dp)
                    )
                    Spacer(modifier = Modifier.width(4.dp))
                    Text(
                        text = btBadgeText,
                        color = btBadgeColor,
                        fontFamily = JetBrainsMono,
                        fontWeight = FontWeight.Bold,
                        fontSize = 11.sp
                    )
                }
            }
        }

        // 2. Now Playing Row: Album Artwork & Metadata
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .wrapContentHeight()
                .background(Color(0xFF0D0F12), RoundedCornerShape(6.dp))
                .border(1.dp, Color(0xFF1F2630), RoundedCornerShape(6.dp))
                .padding(8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Album Artwork (88x88 dp)
            Box(
                modifier = Modifier
                    .size(88.dp)
                    .clip(RoundedCornerShape(6.dp))
                    .background(Color(0xFF161B22))
                    .border(1.dp, Color(0xFF30363D), RoundedCornerShape(6.dp)),
                contentAlignment = Alignment.Center
            ) {
                if (coverBitmap != null) {
                    Image(
                        bitmap = coverBitmap.asImageBitmap(),
                        contentDescription = "Cover Art",
                        contentScale = ContentScale.Crop,
                        modifier = Modifier.fillMaxSize()
                    )
                } else {
                    // Industrial Vinyl / Disc Placeholder
                    Box(
                        modifier = Modifier
                            .size(72.dp)
                            .clip(CircleShape)
                            .background(
                                Brush.radialGradient(
                                    colors = listOf(
                                        Color(0xFF21262D),
                                        Color(0xFF161B22),
                                        Color(0xFF0D0F12)
                                    )
                                )
                            )
                            .border(1.5.dp, Color(0xFF38BDF8).copy(alpha = 0.4f), CircleShape),
                        contentAlignment = Alignment.Center
                    ) {
                        Box(
                            modifier = Modifier
                                .size(22.dp)
                                .clip(CircleShape)
                                .background(Color(0xFF0D0F12))
                                .border(1.dp, IndustrialPrimary, CircleShape),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.GraphicEq,
                                contentDescription = null,
                                tint = IndustrialPrimary,
                                modifier = Modifier.size(13.dp)
                            )
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.width(12.dp))

            // Metadata Column (Title, Artist, Format Badge)
            Column(
                modifier = Modifier
                    .weight(1f)
                    .wrapContentHeight(),
                verticalArrangement = Arrangement.Center
            ) {
                Text(
                    text = displayTitle,
                    style = MaterialTheme.typography.titleMedium,
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    fontFamily = JetBrainsMono,
                    fontSize = 14.sp,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    lineHeight = 17.sp
                )

                Spacer(modifier = Modifier.height(3.dp))

                Text(
                    text = displayArtist,
                    style = MaterialTheme.typography.bodyMedium,
                    color = IndustrialSecondary,
                    fontFamily = FontFamily.SansSerif,
                    fontSize = 12.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )

                Spacer(modifier = Modifier.height(5.dp))

                Row(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    // Audio Engine Badge
                    Box(
                        modifier = Modifier
                            .background(Color(0xFF1F2937), RoundedCornerShape(3.dp))
                            .padding(horizontal = 5.dp, vertical = 2.dp)
                    ) {
                        Text(
                            text = if (isPowerOn) "384kHz / 32-bit" else "STANDBY",
                            color = if (isPowerOn) IndustrialTertiary else Color(0xFF9CA3AF),
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold,
                            fontSize = 10.5.sp
                        )
                    }

                    if (btTelemetry != null && btTelemetry.rssi != 0) {
                        Box(
                            modifier = Modifier
                                .background(Color(0xFF1F2937), RoundedCornerShape(3.dp))
                                .padding(horizontal = 5.dp, vertical = 2.dp)
                        ) {
                            Text(
                                text = "RSSI: ${btTelemetry.rssi} dBm",
                                color = IndustrialPrimary,
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold,
                                fontSize = 10.5.sp
                            )
                        }
                    }
                }
            }
        }

        // 3. Playback Transport & Volume Controls Row
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF0D0F12), RoundedCornerShape(6.dp))
                .border(1.dp, Color(0xFF1F2630), RoundedCornerShape(6.dp))
                .padding(horizontal = 8.dp, vertical = 3.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Transport: Prev, Play/Pause, Next
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                IconButton(
                    onClick = {
                        MediaNotificationListenerService.sendTransportControl("prev")
                        onCommand("prev", null)
                    },
                    modifier = Modifier.size(34.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.SkipPrevious,
                        contentDescription = "Previous",
                        tint = IndustrialPrimary,
                        modifier = Modifier.size(24.dp)
                    )
                }

                // Play / Pause Toggle
                val isPlaying = trackInfo.isPlaying
                Box(
                    modifier = Modifier
                        .size(36.dp)
                        .background(
                            color = if (isPlaying) IndustrialSecondary.copy(alpha = 0.2f) else IndustrialTertiary.copy(alpha = 0.2f),
                            shape = CircleShape
                        )
                        .border(
                            width = 1.dp,
                            color = if (isPlaying) IndustrialSecondary else IndustrialTertiary,
                            shape = CircleShape
                        )
                        .clickable {
                            val action = if (isPlaying) "pause" else "play"
                            MediaNotificationListenerService.sendTransportControl(action)
                            onCommand(action, null)
                        },
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = if (isPlaying) Icons.Default.Pause else Icons.Default.PlayArrow,
                        contentDescription = if (isPlaying) "Pause" else "Play",
                        tint = if (isPlaying) IndustrialSecondary else IndustrialTertiary,
                        modifier = Modifier.size(22.dp)
                    )
                }

                IconButton(
                    onClick = {
                        MediaNotificationListenerService.sendTransportControl("next")
                        onCommand("next", null)
                    },
                    modifier = Modifier.size(34.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.SkipNext,
                        contentDescription = "Next",
                        tint = IndustrialPrimary,
                        modifier = Modifier.size(24.dp)
                    )
                }
            }

            // Volume Controls: Vol Down, Vol Up
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                IconButton(
                    onClick = { onCommand("voldown", null) },
                    modifier = Modifier.size(34.dp)
                ) {
                    Icon(
                        imageVector = Icons.AutoMirrored.Filled.VolumeDown,
                        contentDescription = "Vol Down",
                        tint = IndustrialPrimary,
                        modifier = Modifier.size(22.dp)
                    )
                }

                IconButton(
                    onClick = { onCommand("volup", null) },
                    modifier = Modifier.size(34.dp)
                ) {
                    Icon(
                        imageVector = Icons.AutoMirrored.Filled.VolumeUp,
                        contentDescription = "Vol Up",
                        tint = IndustrialPrimary,
                        modifier = Modifier.size(22.dp)
                    )
                }
            }
        }

        // 4. Quick DSP & Pairing Action Bar
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF0D0F12), RoundedCornerShape(6.dp))
                .border(1.dp, Color(0xFF1F2630), RoundedCornerShape(6.dp))
                .padding(horizontal = 8.dp, vertical = 3.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                // R/L Channel Routing Mode Button
                val isChActive = channelMode != 0
                OutlinedButton(
                    onClick = {
                        val next = (channelMode + 1) % 4
                        onCommand("setchannel", next)
                    },
                    shape = RoundedCornerShape(4.dp),
                    contentPadding = PaddingValues(horizontal = 6.dp, vertical = 0.dp),
                    border = androidx.compose.foundation.BorderStroke(
                        1.dp,
                        if (isChActive) Color(0xFF22D3EE) else IndustrialPrimary.copy(alpha = 0.5f)
                    ),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = if (isChActive) Color(0xFF0E7490).copy(alpha = 0.5f) else Color.Transparent
                    ),
                    modifier = Modifier.requiredHeight(28.dp)
                ) {
                    Text(
                        text = PanelStrings.channelButtonText(channelMode),
                        color = if (isChActive) Color.White else IndustrialPrimary,
                        fontSize = 10.sp,
                        fontFamily = JetBrainsMono,
                        fontWeight = FontWeight.Bold
                    )
                }

                OutlinedButton(
                    onClick = { onCommand("setbass", if (bassBoost) 0 else 1) },
                    shape = RoundedCornerShape(4.dp),
                    contentPadding = PaddingValues(horizontal = 7.dp, vertical = 0.dp),
                    border = androidx.compose.foundation.BorderStroke(
                        1.dp,
                        if (bassBoost) Color(0xFF60A5FA) else IndustrialPrimary.copy(alpha = 0.5f)
                    ),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = if (bassBoost) Color(0xFF1D4ED8) else Color.Transparent
                    ),
                    modifier = Modifier.requiredHeight(28.dp)
                ) {
                    Text(
                        text = "BASS BOOST",
                        color = if (bassBoost) Color.White else IndustrialPrimary,
                        fontSize = 10.sp,
                        fontFamily = JetBrainsMono,
                        fontWeight = FontWeight.Bold
                    )
                }

                OutlinedButton(
                    onClick = { onCommand("set3d", if (sound3d) 0 else 1) },
                    shape = RoundedCornerShape(4.dp),
                    contentPadding = PaddingValues(horizontal = 7.dp, vertical = 0.dp),
                    border = androidx.compose.foundation.BorderStroke(
                        1.dp,
                        if (sound3d) Color(0xFF60A5FA) else IndustrialPrimary.copy(alpha = 0.5f)
                    ),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = if (sound3d) Color(0xFF1D4ED8) else Color.Transparent
                    ),
                    modifier = Modifier.requiredHeight(28.dp)
                ) {
                    Text(
                        text = "3D AUDIO",
                        color = if (sound3d) Color.White else IndustrialPrimary,
                        fontSize = 10.sp,
                        fontFamily = JetBrainsMono,
                        fontWeight = FontWeight.Bold
                    )
                }
            }

            OutlinedButton(
                onClick = { onCommand("enterpairing", null) },
                shape = RoundedCornerShape(4.dp),
                contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp),
                border = androidx.compose.foundation.BorderStroke(
                    1.dp,
                    IndustrialSecondary.copy(alpha = 0.7f)
                ),
                modifier = Modifier.requiredHeight(28.dp)
            ) {
                Text(
                    text = "PAIR BT",
                    color = IndustrialSecondary,
                    fontSize = 10.5.sp,
                    fontFamily = JetBrainsMono,
                    fontWeight = FontWeight.Bold
                )
            }
        }
    }
}
