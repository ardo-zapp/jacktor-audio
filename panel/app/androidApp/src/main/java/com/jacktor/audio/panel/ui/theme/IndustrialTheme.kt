package com.jacktor.audio.panel.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

val IndustrialBackground = Color(0xFF0F1216)
val IndustrialSurface = Color(0xFF181C22)
val IndustrialSurfaceVariant = Color(0xFF222831)
val IndustrialPrimary = Color(0xFF00E5FF) // Neon Cyan
val IndustrialSecondary = Color(0xFFFF9100) // LED Amber/Orange
val IndustrialTertiary = Color(0xFF00E676) // LED Green
val IndustrialError = Color(0xFFFF1744) // Alert Red
val IndustrialTextPrimary = Color(0xFFE0E6ED)
val IndustrialTextSecondary = Color(0xFF90A4AE)

private val DarkColorScheme = darkColorScheme(
    primary = IndustrialPrimary,
    secondary = IndustrialSecondary,
    tertiary = IndustrialTertiary,
    background = IndustrialBackground,
    surface = IndustrialSurface,
    surfaceVariant = IndustrialSurfaceVariant,
    error = IndustrialError,
    onPrimary = Color.Black,
    onSecondary = Color.Black,
    onBackground = IndustrialTextPrimary,
    onSurface = IndustrialTextPrimary,
    onSurfaceVariant = IndustrialTextSecondary
)

@Composable
fun IndustrialTheme(
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = DarkColorScheme,
        content = content
    )
}
