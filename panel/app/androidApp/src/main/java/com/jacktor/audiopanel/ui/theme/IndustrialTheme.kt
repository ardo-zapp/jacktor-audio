package com.jacktor.audiopanel.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import com.jacktor.audiopanel.R

val IndustrialBackground = Color(0xFF0F1216)
val IndustrialSurface = Color(0xFF181C22)
val IndustrialSurfaceVariant = Color(0xFF222831)
val IndustrialPrimary = Color(0xFF00E5FF) // Neon Cyan
val IndustrialSecondary = Color(0xFFFF9100) // LED Amber/Orange
val IndustrialTertiary = Color(0xFF00E676) // LED Green
val IndustrialError = Color(0xFFFF1744) // Alert Red
val IndustrialTextPrimary = Color(0xFFE0E6ED)
val IndustrialTextSecondary = Color(0xFF90A4AE)

// Matching Industrial JetBrains Mono Typography for Panel and Webroot
val JetBrainsMono = FontFamily(
    Font(R.font.jetbrains_mono_regular, FontWeight.Normal),
    Font(R.font.jetbrains_mono_bold, FontWeight.Bold)
)

val IndustrialTypography = Typography(
    displayLarge = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Normal),
    displayMedium = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Normal),
    displaySmall = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Normal),
    headlineLarge = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Normal),
    headlineMedium = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Normal),
    headlineSmall = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Normal),
    titleLarge = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Bold),
    titleMedium = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Bold),
    titleSmall = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Bold),
    bodyLarge = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Normal),
    bodyMedium = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Normal),
    bodySmall = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Normal),
    labelLarge = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Medium),
    labelMedium = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Medium),
    labelSmall = TextStyle(fontFamily = JetBrainsMono, fontWeight = FontWeight.Medium)
)

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
        typography = IndustrialTypography,
        content = content
    )
}
