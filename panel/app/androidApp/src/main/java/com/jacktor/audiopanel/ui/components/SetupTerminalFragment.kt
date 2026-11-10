package com.jacktor.audiopanel.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.jacktor.audiopanel.model.SetupLogLine
import com.jacktor.audiopanel.model.SetupTerminalState
import kotlinx.coroutines.delay

private val TermBlack = Color(0xFF000000)
private val TermHeaderBg = Color(0xFF070A08)
private val TermBorder = Color(0xFF1B2E22)
private val TermGreen = Color(0xFF00E676)
private val TermCyan = Color(0xFF00E5FF)
private val TermAmber = Color(0xFFFFD600)
private val TermRed = Color(0xFFFF1744)
private val TermDim = Color(0xFF6E8074)
private val TermText = Color(0xFFD8F3DC)

@Composable
fun SetupTerminalFragment(
    setupState: SetupTerminalState,
    countdownSeconds: Int,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier
) {
    val listState = rememberLazyListState()
    var cursorVisible by remember { mutableStateOf(true) }

    // Blinking terminal cursor
    LaunchedEffect(Unit) {
        while (true) {
            delay(450)
            cursorVisible = !cursorVisible
        }
    }

    // Instant scroll to latest log line (zero animation lag)
    LaunchedEffect(setupState.logs.size, setupState.currentStep, setupState.overallStatus) {
        if (setupState.logs.isNotEmpty()) {
            listState.scrollToItem(setupState.logs.size - 1)
        }
    }

    val statusColor = when (setupState.overallStatus) {
        "PASS" -> TermGreen
        "FAIL" -> TermRed
        else -> TermCyan
    }

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(TermBlack)
            .padding(10.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(TermBlack, RoundedCornerShape(6.dp))
                .border(1.dp, TermBorder, RoundedCornerShape(6.dp))
        ) {
            // Terminal Header Bar
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(TermHeaderBg, RoundedCornerShape(topStart = 6.dp, topEnd = 6.dp))
                    .padding(horizontal = 12.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "root@jacktor-dsp:~# ./amp-power-on --self-test",
                    color = TermGreen,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 12.sp
                )

                Spacer(modifier = Modifier.weight(1f))

                if (setupState.smpsV != null && setupState.smpsV > 0f) {
                    Text(
                        text = "SMPS: ${String.format("%.1f", setupState.smpsV)}V",
                        color = TermCyan,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 11.sp
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                }

                Text(
                    text = "[ STEP ${setupState.currentStep.coerceIn(0, 6)} / 6 ]",
                    color = TermText,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 11.sp
                )

                Spacer(modifier = Modifier.width(10.dp))

                Box(
                    modifier = Modifier
                        .background(statusColor, RoundedCornerShape(3.dp))
                        .padding(horizontal = 8.dp, vertical = 2.dp)
                ) {
                    Text(
                        text = setupState.overallStatus,
                        color = TermBlack,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 10.sp
                    )
                }
            }

            // ASCII Progress Bar Row
            val stepClamped = setupState.currentStep.coerceIn(0, 6)
            val filledBars = if (setupState.overallStatus == "PASS") 24 else (stepClamped * 4)
            val emptyBars = (24 - filledBars).coerceAtLeast(0)
            val asciiBar = "█".repeat(filledBars) + "░".repeat(emptyBars)

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF040605))
                    .padding(horizontal = 12.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "PROGRESS: [$asciiBar]",
                    color = statusColor,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 11.sp
                )
                Spacer(modifier = Modifier.weight(1f))
                Text(
                    text = "UART0 @ 921600 bps • TELEMETRY STREAM",
                    color = TermDim,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 10.sp
                )
            }

            // Divider
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(1.dp)
                    .background(TermBorder)
            )

            // Scrolling Black Terminal Log Body
            LazyColumn(
                state = listState,
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .background(TermBlack)
                    .padding(horizontal = 12.dp, vertical = 8.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                item {
                    Text(
                        text = "========================================================================",
                        color = TermDim,
                        fontFamily = FontFamily.Monospace,
                        fontSize = 11.sp
                    )
                    Text(
                        text = " JACKTOR AUDIO DSP AMPLIFIER — POWER-ON HARDWARE SELF-TEST CONSOLE",
                        color = TermGreen,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 11.sp
                    )
                    Text(
                        text = "========================================================================",
                        color = TermDim,
                        fontFamily = FontFamily.Monospace,
                        fontSize = 11.sp
                    )
                }

                items(setupState.logs, key = { it.id }) { line ->
                    TerminalLogRow(line)
                }

                item {
                    val promptSymbol = if (cursorVisible) "█" else " "
                    val promptText = when (setupState.overallStatus) {
                        "PASS" -> ">>> [ PASS ] SYSTEM READY — LAUNCHING MAIN DASHBOARD IN ${countdownSeconds}s... $promptSymbol"
                        "FAIL" -> ">>> [ FAIL ] HARDWARE SELF-TEST ABORTED — PROTECTION ENGAGED $promptSymbol"
                        else -> ">>> executing step ${setupState.currentStep}/6... $promptSymbol"
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = promptText,
                        color = statusColor,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 12.sp
                    )
                }
            }

            // Bottom Terminal Status / Action Footer
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(1.dp)
                    .background(TermBorder)
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(TermHeaderBg, RoundedCornerShape(bottomStart = 6.dp, bottomEnd = 6.dp))
                    .padding(horizontal = 12.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                val footerMsg = when (setupState.overallStatus) {
                    "PASS" -> "STATUS: Self-Test PASS. Automatically returning to main launcher screen (${countdownSeconds}s)..."
                    "FAIL" -> "STATUS: Self-Test FAIL. Inspect hardware fault logs above."
                    else -> "STATUS: Running power-on sequence & Bluetooth GAIA SPP initialization..."
                }
                Text(
                    text = footerMsg,
                    color = statusColor,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 11.sp,
                    modifier = Modifier.weight(1f)
                )

                OutlinedButton(
                    onClick = onDismiss,
                    shape = RoundedCornerShape(4.dp),
                    modifier = Modifier.height(28.dp)
                ) {
                    Text(
                        text = if (setupState.overallStatus == "PASS") "OPEN DASHBOARD NOW" else "HIDE TERMINAL",
                        color = TermGreen,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold,
                        fontSize = 10.sp
                    )
                }
            }
        }
    }
}

@Composable
private fun TerminalLogRow(line: SetupLogLine) {
    val badgeText: String
    val badgeColor: Color
    val msgColor: Color

    when (line.status.uppercase()) {
        "PASS" -> {
            badgeText = "[ PASS ]"
            badgeColor = TermGreen
            msgColor = TermGreen
        }
        "WARN", "WARN_BYPASSED" -> {
            badgeText = "[ WARN ]"
            badgeColor = TermAmber
            msgColor = TermAmber
        }
        "FAIL" -> {
            badgeText = "[ FAIL ]"
            badgeColor = TermRed
            msgColor = TermRed
        }
        "SKIP" -> {
            badgeText = "[ SKIP ]"
            badgeColor = TermDim
            msgColor = TermDim
        }
        "INFO" -> {
            badgeText = "[ INFO ]"
            badgeColor = TermCyan
            msgColor = TermText
        }
        else -> {
            badgeText = "[ RUN  ]"
            badgeColor = TermCyan
            msgColor = TermText
        }
    }

    val stepPrefix = if (line.step != null) "[Step ${line.step}] " else ""

    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.Top
    ) {
        Text(
            text = "[${line.timestamp}] ",
            color = TermDim,
            fontFamily = FontFamily.Monospace,
            fontSize = 11.sp
        )
        Text(
            text = "$badgeText ",
            color = badgeColor,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 11.sp
        )
        if (stepPrefix.isNotEmpty()) {
            Text(
                text = stepPrefix,
                color = TermCyan,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 11.sp
            )
        }
        Text(
            text = line.message,
            color = msgColor,
            fontFamily = FontFamily.Monospace,
            fontSize = 11.sp,
            modifier = Modifier.weight(1f)
        )
    }
}
