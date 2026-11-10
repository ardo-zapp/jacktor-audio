package com.jacktor.audiopanel.utils

import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.LauncherApps
import android.content.pm.PackageManager
import android.content.pm.ShortcutInfo
import android.graphics.drawable.Drawable
import android.net.Uri
import android.os.Build
import android.os.Process
import android.provider.Settings

data class AppInfo(
    val label: String,
    val packageName: String,
    val icon: Drawable?,
    val versionName: String? = null,
    val isSystemApp: Boolean = false
)

data class AppShortcutInfo(
    val id: String,
    val packageName: String,
    val label: String,
    val icon: Drawable?,
    val isEnabled: Boolean = true,
    val disabledMessage: String? = null,
    val shortcutInfo: ShortcutInfo
)

object AppLauncherManager {

    fun getInstalledApps(context: Context): List<AppInfo> {
        val pm = context.packageManager
        val intent = Intent(Intent.ACTION_MAIN, null).apply {
            addCategory(Intent.CATEGORY_LAUNCHER)
        }

        val resolveInfos = pm.queryIntentActivities(intent, PackageManager.MATCH_ALL)
        val ownPackage = context.packageName

        val apps = mutableListOf<AppInfo>()
        for (info in resolveInfos) {
            val pkg = info.activityInfo.packageName
            if (pkg == ownPackage) continue // Skip launcher self

            val label = info.loadLabel(pm).toString()
            val icon = try {
                info.loadIcon(pm)
            } catch (e: Exception) {
                null
            }

            val appInfo = info.activityInfo.applicationInfo
            val isSystem = (appInfo.flags and ApplicationInfo.FLAG_SYSTEM) != 0

            val versionName = try {
                val pkgInfo = pm.getPackageInfo(pkg, 0)
                pkgInfo.versionName
            } catch (e: Exception) {
                null
            }

            apps.add(AppInfo(label, pkg, icon, versionName, isSystem))
        }

        return apps.sortedBy { it.label.lowercase() }
    }

    fun launchApp(context: Context, packageName: String): Boolean {
        return try {
            val launchIntent = context.packageManager.getLaunchIntentForPackage(packageName)
            if (launchIntent != null) {
                launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                context.startActivity(launchIntent)
                true
            } else {
                false
            }
        } catch (e: Exception) {
            false
        }
    }

    fun hasShortcutHostPermission(context: Context): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N_MR1) return false
        val launcherApps = context.getSystemService(Context.LAUNCHER_APPS_SERVICE) as? LauncherApps ?: return false
        return try {
            launcherApps.hasShortcutHostPermission()
        } catch (e: Throwable) {
            false
        }
    }

    fun getAppShortcuts(context: Context, packageName: String): List<AppShortcutInfo> {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N_MR1) return emptyList()
        val launcherApps = context.getSystemService(Context.LAUNCHER_APPS_SERVICE) as? LauncherApps ?: return emptyList()

        return try {
            val query = LauncherApps.ShortcutQuery().apply {
                setPackage(packageName)
                setQueryFlags(
                    LauncherApps.ShortcutQuery.FLAG_MATCH_DYNAMIC or
                    LauncherApps.ShortcutQuery.FLAG_MATCH_MANIFEST or
                    LauncherApps.ShortcutQuery.FLAG_MATCH_PINNED
                )
            }
            val user = Process.myUserHandle()
            val shortcuts = launcherApps.getShortcuts(query, user) ?: return emptyList()

            val density = context.resources.displayMetrics.densityDpi
            shortcuts.map { info ->
                val iconDrawable = try {
                    launcherApps.getShortcutBadgedIconDrawable(info, density)
                        ?: launcherApps.getShortcutIconDrawable(info, density)
                } catch (e: Throwable) {
                    null
                }

                val label = info.shortLabel?.toString()?.takeIf { it.isNotBlank() }
                    ?: info.longLabel?.toString()?.takeIf { it.isNotBlank() }
                    ?: info.id

                AppShortcutInfo(
                    id = info.id,
                    packageName = info.`package`,
                    label = label,
                    icon = iconDrawable,
                    isEnabled = info.isEnabled,
                    disabledMessage = info.disabledMessage?.toString(),
                    shortcutInfo = info
                )
            }
        } catch (e: Throwable) {
            emptyList()
        }
    }

    fun launchShortcut(context: Context, shortcut: AppShortcutInfo): Boolean {
        val launcherApps = context.getSystemService(Context.LAUNCHER_APPS_SERVICE) as? LauncherApps ?: return false
        return try {
            launcherApps.startShortcut(shortcut.shortcutInfo, null, null)
            true
        } catch (e: Throwable) {
            try {
                launcherApps.startShortcut(
                    shortcut.packageName,
                    shortcut.id,
                    null,
                    null,
                    Process.myUserHandle()
                )
                true
            } catch (e2: Throwable) {
                false
            }
        }
    }

    fun openAppInfo(context: Context, packageName: String): Boolean {
        return try {
            val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
                data = Uri.parse("package:$packageName")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
            true
        } catch (e: Exception) {
            try {
                val intent = Intent(Settings.ACTION_MANAGE_APPLICATIONS_SETTINGS).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                }
                context.startActivity(intent)
                true
            } catch (e2: Exception) {
                false
            }
        }
    }

    fun uninstallApp(context: Context, packageName: String): Boolean {
        return try {
            val intent = Intent(Intent.ACTION_DELETE).apply {
                data = Uri.parse("package:$packageName")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
            true
        } catch (e: Exception) {
            try {
                val intent = Intent(Intent.ACTION_UNINSTALL_PACKAGE).apply {
                    data = Uri.parse("package:$packageName")
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                }
                context.startActivity(intent)
                true
            } catch (e2: Exception) {
                false
            }
        }
    }

    fun forceStopApp(packageName: String) {
        Thread {
            try {
                val proc = Runtime.getRuntime().exec(arrayOf("su", "-c", "am force-stop $packageName"))
                proc.waitFor()
            } catch (e: Exception) {
                // Ignore background root command failures
            }
        }.start()
    }
}
