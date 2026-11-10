package com.jacktor.audiopanel.ui.components

import android.graphics.drawable.Drawable
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Apps
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.ElectricBolt
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.TouchApp
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.core.graphics.drawable.toBitmap
import com.jacktor.audiopanel.ui.theme.IndustrialBackground
import com.jacktor.audiopanel.ui.theme.IndustrialError
import com.jacktor.audiopanel.ui.theme.IndustrialPrimary
import com.jacktor.audiopanel.ui.theme.IndustrialSecondary
import com.jacktor.audiopanel.ui.theme.IndustrialSurface
import com.jacktor.audiopanel.ui.theme.JetBrainsMono
import com.jacktor.audiopanel.utils.AppInfo
import com.jacktor.audiopanel.utils.AppLauncherManager
import com.jacktor.audiopanel.utils.AppShortcutInfo

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppDrawerDialog(
    apps: List<AppInfo>,
    onDismiss: () -> Unit,
    onAppClick: (String) -> Unit,
    onRefreshApps: () -> Unit = {}
) {
    var searchQuery by remember { mutableStateOf("") }
    var selectedAppForActions by remember { mutableStateOf<AppInfo?>(null) }

    val filteredApps = remember(apps, searchQuery) {
        if (searchQuery.isBlank()) {
            apps
        } else {
            apps.filter { it.label.contains(searchQuery, ignoreCase = true) }
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxSize()
                .padding(18.dp),
            shape = RoundedCornerShape(12.dp),
            color = IndustrialBackground,
            border = BorderStroke(1.dp, Color(0xFF333D4B))
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(16.dp)
            ) {
                // Header Bar
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        imageVector = Icons.Default.Apps,
                        contentDescription = "App Drawer",
                        tint = IndustrialPrimary,
                        modifier = Modifier.size(26.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Column {
                        Text(
                            text = "ANDROID APP DRAWER",
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold,
                            color = IndustrialPrimary,
                            fontSize = 15.sp
                        )
                        Text(
                            text = "Tekan & tahan ikon untuk pintasan & opsi",
                            fontFamily = JetBrainsMono,
                            color = Color.Gray,
                            fontSize = 10.5.sp
                        )
                    }

                    Spacer(modifier = Modifier.weight(1f))

                    // Search Box
                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = { searchQuery = it },
                        placeholder = {
                            Text("Cari aplikasi...", fontSize = 12.sp, color = Color.Gray, fontFamily = JetBrainsMono)
                        },
                        leadingIcon = {
                            Icon(Icons.Default.Search, contentDescription = "Search", tint = IndustrialPrimary, modifier = Modifier.size(20.dp))
                        },
                        singleLine = true,
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = IndustrialPrimary,
                            unfocusedBorderColor = Color(0xFF333D4B),
                            focusedContainerColor = IndustrialSurface,
                            unfocusedContainerColor = IndustrialSurface,
                            focusedTextColor = Color.White,
                            unfocusedTextColor = Color.White
                        ),
                        modifier = Modifier
                            .width(240.dp)
                            .height(46.dp)
                    )

                    Spacer(modifier = Modifier.width(12.dp))

                    IconButton(onClick = onDismiss) {
                        Icon(Icons.Default.Close, contentDescription = "Close", tint = IndustrialSecondary, modifier = Modifier.size(24.dp))
                    }
                }

                Spacer(modifier = Modifier.height(14.dp))

                // App Grid
                LazyVerticalGrid(
                    columns = GridCells.Adaptive(minSize = 96.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp),
                    contentPadding = PaddingValues(4.dp),
                    modifier = Modifier.fillMaxSize()
                ) {
                    items(filteredApps) { app ->
                        AppGridItem(
                            app = app,
                            onClick = { onAppClick(app.packageName) },
                            onLongClick = { selectedAppForActions = app }
                        )
                    }
                }
            }
        }
    }

    // Contextual Action & Shortcuts Dialog
    if (selectedAppForActions != null) {
        AppActionsDialog(
            app = selectedAppForActions!!,
            onDismiss = { selectedAppForActions = null },
            onAppLaunched = {
                selectedAppForActions = null
                onDismiss()
            },
            onAppChanged = {
                onRefreshApps()
            }
        )
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun AppGridItem(
    app: AppInfo,
    onClick: () -> Unit,
    onLongClick: () -> Unit
) {
    Column(
        modifier = Modifier
            .background(IndustrialSurface, RoundedCornerShape(8.dp))
            .border(1.dp, Color(0xFF2B333E), RoundedCornerShape(8.dp))
            .combinedClickable(
                onClick = onClick,
                onLongClick = onLongClick
            )
            .padding(10.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        val iconDrawable: Drawable? = app.icon
        if (iconDrawable != null) {
            val bitmap = remember(iconDrawable) {
                iconDrawable.toBitmap(width = 56, height = 56).asImageBitmap()
            }
            Image(
                bitmap = bitmap,
                contentDescription = app.label,
                modifier = Modifier.size(48.dp)
            )
        } else {
            Box(
                modifier = Modifier
                    .size(48.dp)
                    .background(Color(0xFF222831), RoundedCornerShape(6.dp)),
                contentAlignment = Alignment.Center
            ) {
                Icon(Icons.Default.Apps, contentDescription = null, tint = IndustrialPrimary, modifier = Modifier.size(28.dp))
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        Text(
            text = app.label,
            color = Color.White,
            fontSize = 11.sp,
            fontFamily = JetBrainsMono,
            fontWeight = FontWeight.Medium,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center
        )
    }
}

@Composable
fun AppActionsDialog(
    app: AppInfo,
    onDismiss: () -> Unit,
    onAppLaunched: () -> Unit,
    onAppChanged: () -> Unit
) {
    val context = LocalContext.current
    val shortcuts = remember(app.packageName) {
        AppLauncherManager.getAppShortcuts(context, app.packageName)
    }
    val hasShortcutHostPermission = remember {
        AppLauncherManager.hasShortcutHostPermission(context)
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .widthIn(min = 540.dp, max = 640.dp)
                .fillMaxHeight(0.90f)
                .padding(14.dp),
            shape = RoundedCornerShape(12.dp),
            color = IndustrialBackground,
            border = BorderStroke(1.dp, Color(0xFF333D4B))
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(16.dp)
            ) {
                // Header (App Icon, Label, Package Name, Version, Badges)
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    val iconDrawable = app.icon
                    if (iconDrawable != null) {
                        val bitmap = remember(iconDrawable) {
                            iconDrawable.toBitmap(width = 56, height = 56).asImageBitmap()
                        }
                        Image(
                            bitmap = bitmap,
                            contentDescription = app.label,
                            modifier = Modifier.size(48.dp)
                        )
                    } else {
                        Box(
                            modifier = Modifier
                                .size(48.dp)
                                .background(Color(0xFF222831), RoundedCornerShape(8.dp)),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(Icons.Default.Apps, contentDescription = null, tint = IndustrialPrimary, modifier = Modifier.size(28.dp))
                        }
                    }

                    Spacer(modifier = Modifier.width(12.dp))

                    Column(modifier = Modifier.weight(1f)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                text = app.label,
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold,
                                color = Color.White,
                                fontSize = 14.5.sp,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            if (app.isSystemApp) {
                                Box(
                                    modifier = Modifier
                                        .background(Color(0xFF2E3440), RoundedCornerShape(3.dp))
                                        .border(0.8.dp, IndustrialSecondary, RoundedCornerShape(3.dp))
                                        .padding(horizontal = 6.dp, vertical = 2.dp)
                                ) {
                                    Text(
                                        text = "SYSTEM",
                                        fontSize = 9.5.sp,
                                        fontFamily = JetBrainsMono,
                                        fontWeight = FontWeight.Bold,
                                        color = IndustrialSecondary
                                    )
                                }
                            } else {
                                Box(
                                    modifier = Modifier
                                        .background(Color(0xFF1E2A38), RoundedCornerShape(3.dp))
                                        .border(0.8.dp, IndustrialPrimary, RoundedCornerShape(3.dp))
                                        .padding(horizontal = 6.dp, vertical = 2.dp)
                                ) {
                                    Text(
                                        text = "USER APP",
                                        fontSize = 9.5.sp,
                                        fontFamily = JetBrainsMono,
                                        fontWeight = FontWeight.Bold,
                                        color = IndustrialPrimary
                                    )
                                }
                            }
                        }

                        Spacer(modifier = Modifier.height(3.dp))
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                text = app.packageName,
                                fontSize = 10.5.sp,
                                fontFamily = JetBrainsMono,
                                color = Color.Gray,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f, fill = false)
                            )
                            if (!app.versionName.isNullOrBlank()) {
                                Text(
                                    text = " • v${app.versionName}",
                                    fontSize = 10.5.sp,
                                    fontFamily = JetBrainsMono,
                                    color = Color.LightGray
                                )
                            }
                        }
                    }

                    IconButton(onClick = onDismiss) {
                        Icon(Icons.Default.Close, contentDescription = "Close", tint = IndustrialSecondary, modifier = Modifier.size(24.dp))
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(1.dp)
                        .background(Color(0xFF2B333E))
                )
                Spacer(modifier = Modifier.height(12.dp))

                // Landscape 2-Column Split: Actions on Left, Shortcuts on Right
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f),
                    horizontalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    // Left Column: Launcher Actions
                    Column(
                        modifier = Modifier
                            .weight(1f)
                            .fillMaxHeight(),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Text(
                            text = "ACTION LAUNCHER",
                            fontSize = 12.sp,
                            fontFamily = JetBrainsMono,
                            fontWeight = FontWeight.Bold,
                            color = IndustrialPrimary,
                            modifier = Modifier.padding(bottom = 2.dp)
                        )

                        // Button: Open
                        ActionButtonItem(
                            title = "Buka Aplikasi",
                            icon = Icons.Default.PlayArrow,
                            accentColor = IndustrialPrimary,
                            onClick = {
                                AppLauncherManager.launchApp(context, app.packageName)
                                onAppLaunched()
                            }
                        )

                        // Button: App Info
                        ActionButtonItem(
                            title = "Info Aplikasi",
                            icon = Icons.Default.Info,
                            accentColor = Color(0xFF8BE9FD),
                            onClick = {
                                AppLauncherManager.openAppInfo(context, app.packageName)
                                onDismiss()
                            }
                        )

                        // Button: Uninstall (Action Delete)
                        ActionButtonItem(
                            title = if (app.isSystemApp) "Copot Update Sistem" else "Uninstall Aplikasi",
                            icon = Icons.Default.Delete,
                            accentColor = if (app.isSystemApp) Color.Gray else IndustrialError,
                            onClick = {
                                AppLauncherManager.uninstallApp(context, app.packageName)
                                onAppChanged()
                                onDismiss()
                            }
                        )

                        // Button: Force Stop
                        ActionButtonItem(
                            title = "Paksa Berhenti (Force Stop)",
                            icon = Icons.Default.Stop,
                            accentColor = Color(0xFFFFB86C),
                            onClick = {
                                AppLauncherManager.forceStopApp(app.packageName)
                                onDismiss()
                            }
                        )
                    }

                    // Vertical Divider
                    Box(
                        modifier = Modifier
                            .width(1.dp)
                            .fillMaxHeight()
                            .background(Color(0xFF2B333E))
                    )

                    // Right Column: App Shortcuts
                    Column(
                        modifier = Modifier
                            .weight(1.2f)
                            .fillMaxHeight()
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(bottom = 8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Icon(
                                imageVector = Icons.Default.ElectricBolt,
                                contentDescription = null,
                                tint = IndustrialSecondary,
                                modifier = Modifier.size(18.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = "APP SHORTCUTS (${shortcuts.size})",
                                fontSize = 12.sp,
                                fontFamily = JetBrainsMono,
                                fontWeight = FontWeight.Bold,
                                color = IndustrialSecondary
                            )
                        }

                        if (shortcuts.isNotEmpty()) {
                            LazyColumn(
                                modifier = Modifier.fillMaxSize(),
                                verticalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                items(shortcuts) { shortcut ->
                                    ShortcutListItem(
                                        shortcut = shortcut,
                                        onClick = {
                                            AppLauncherManager.launchShortcut(context, shortcut)
                                            onAppLaunched()
                                        }
                                    )
                                }
                            }
                        } else {
                            Box(
                                modifier = Modifier
                                    .fillMaxSize()
                                    .background(Color(0xFF13171F), RoundedCornerShape(6.dp))
                                    .border(1.dp, Color(0xFF232A35), RoundedCornerShape(6.dp))
                                    .padding(12.dp),
                                contentAlignment = Alignment.Center
                            ) {
                                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                    Icon(
                                        imageVector = Icons.Default.TouchApp,
                                        contentDescription = null,
                                        tint = Color.DarkGray,
                                        modifier = Modifier.size(30.dp)
                                    )
                                    Spacer(modifier = Modifier.height(8.dp))
                                    Text(
                                        text = if (hasShortcutHostPermission) {
                                            "Aplikasi ini tidak memiliki pintasan (app shortcuts)."
                                        } else {
                                            "Pintasan tidak tersedia.\nJadikan aplikasi ini Launcher Default untuk mengaktifkan pintasan."
                                        },
                                        color = Color.Gray,
                                        fontSize = 11.sp,
                                        fontFamily = JetBrainsMono,
                                        textAlign = TextAlign.Center,
                                        lineHeight = 16.sp
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ActionButtonItem(
    title: String,
    icon: ImageVector,
    accentColor: Color,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        shape = RoundedCornerShape(6.dp),
        color = IndustrialSurface,
        border = BorderStroke(1.dp, Color(0xFF2B333E)),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = accentColor,
                modifier = Modifier.size(20.dp)
            )
            Spacer(modifier = Modifier.width(10.dp))
            Text(
                text = title,
                fontFamily = JetBrainsMono,
                fontWeight = FontWeight.Medium,
                fontSize = 12.sp,
                color = Color.White
            )
        }
    }
}

@Composable
private fun ShortcutListItem(
    shortcut: AppShortcutInfo,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        shape = RoundedCornerShape(6.dp),
        color = IndustrialSurface,
        border = BorderStroke(1.dp, Color(0xFF2B333E)),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 9.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            val iconDrawable = shortcut.icon
            if (iconDrawable != null) {
                val bitmap = remember(iconDrawable) {
                    iconDrawable.toBitmap(width = 40, height = 40).asImageBitmap()
                }
                Image(
                    bitmap = bitmap,
                    contentDescription = shortcut.label,
                    modifier = Modifier.size(26.dp)
                )
            } else {
                Icon(
                    imageVector = Icons.Default.ElectricBolt,
                    contentDescription = null,
                    tint = IndustrialSecondary,
                    modifier = Modifier.size(24.dp)
                )
            }

            Spacer(modifier = Modifier.width(10.dp))

            Text(
                text = shortcut.label,
                fontFamily = JetBrainsMono,
                fontSize = 12.sp,
                fontWeight = FontWeight.Normal,
                color = Color.White,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f)
            )

            Icon(
                imageVector = Icons.Default.ChevronRight,
                contentDescription = null,
                tint = Color.Gray,
                modifier = Modifier.size(18.dp)
            )
        }
    }
}
