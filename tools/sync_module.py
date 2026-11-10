#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/sync_module.py
Jacktor Audio - Unified Framework Module & BusyBox Synchronizer over SSH
Integrates full module differential rsync and BusyBox NDK dynamic symlink generation.
Pushes changed files to /data/adb/modules/jacktor-audio-framework/ on the Android panel.
If rsync is not available on the remote Android device, seamlessly falls back to SCP.
If no files changed (e.g. busybox identical), nothing is transmitted and no re-symlinking occurs.
Restarts httpd only if webroot/ or httpd.conf changed.
"""

import os
import sys
import time
import hashlib
import subprocess
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from discover import discover_jacktor_panel, SshSession

PROJECT_ROOT = TOOLS_DIR.parent
MODULE_DIR = PROJECT_ROOT / "panel" / "realme-bengal" / "modul" / "jacktor-audio-framework"
LOCAL_MODULE_BIN = MODULE_DIR / "system" / "bin"
LOCAL_BUSYBOX = LOCAL_MODULE_BIN / "busybox"
REMOTE_MODDIR = "/data/adb/modules/jacktor-audio-framework"
REMOTE_MODULE_BIN = f"{REMOTE_MODDIR}/system/bin"
REMOTE_TMP_BUSYBOX = "/data/local/tmp/busybox_sync"

# Android App APK sync configuration (as system app in module)
LOCAL_APK_SOURCE_DIR = PROJECT_ROOT / "panel" / "app" / "androidApp" / "build" / "outputs" / "apk"  # Build output
LOCAL_APK_PATTERN = "*/*.apk"  # Pattern to find APK (debug/release subdirs)
MODULE_APK_DIR = MODULE_DIR / "system" / "priv-app" / "JacktorAudio"  # Module target dir
MODULE_APK_NAME = "JacktorAudio.apk"  # Standardized name in module
APP_PACKAGE_NAME = "com.jacktor.audiopanel"  # Package name for pm commands

# OpenSSH wrapper symlinks to fix on device
OPENSSH_SYMLINKS = [
    "rsync",
    "scp",
    "sftp",
    "sftp-server",
    "ssh",
    "ssh-keygen",
    "sshd",
    "sshd-auth",
    "sshd-session"
]
OPENSSH_WRAPPER_TARGET = "/system/usr/libexec/ssh-core/wrapper"


def get_file_md5(path: Path) -> str:
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def fix_busybox_symlinks(session: SshSession, force: bool = False) -> bool:
    """
    Fix/recreate BusyBox applet symlinks in module's system/bin/ on the Android device.
    Only creates symlinks for applets not already present in /system/bin.
    Does NOT recreate if already complete (> 150 applets) unless force=True.
    """
    # Check if busybox binary exists on device
    check_busybox = session.run_cmd(f"ls -l {REMOTE_MODULE_BIN}/busybox 2>/dev/null", check=False)
    if check_busybox.returncode != 0 or not (check_busybox.stdout or "").strip():
        print(f"[WARNING] BusyBox binary tidak ditemukan di {REMOTE_MODULE_BIN}/busybox.")
        print("[INFO] Melewati perbaikan symlink BusyBox.")
        return True

    if not force:
        # Check existing symlink count in one fast command
        res_count = session.run_cmd(
            f"find {REMOTE_MODULE_BIN} -maxdepth 1 -type l -lname '*busybox*' 2>/dev/null | wc -l",
            check=False
        )
        existing = 0
        try:
            existing = int((res_count.stdout or "0").strip())
        except ValueError:
            pass

        if existing >= 150:
            print(f"[INFO] Symlink applet BusyBox sudah lengkap ({existing} applets terpasang).")
            return True

    print("[*] Memeriksa dan memperbaiki symlink applet BusyBox...")

    # Build symlink commands - similar to sync_busybox_standalone but without upload
    symlink_script = (
        f"cd {REMOTE_MODULE_BIN} || exit 1; "
        f"find . -type l -lname '*busybox*' -delete; "
        f"sysbin=\"$(ls /system/bin 2>/dev/null)\"; "
        f"count=0; "
        f"for applet in $(./busybox --list 2>/dev/null); do "
        f"  if [ ! \"$(echo \"$sysbin\" | ./busybox grep -xF \"$applet\")\" ]; then "
        f"    ./busybox ln -sf busybox \"$applet\"; "
        f"    count=$((count + 1)); "
        f"  fi; "
        f"done; "
        f"echo \"SUCCESS:$count\""
    )
    res = session.run_cmd(symlink_script, check=False)
    stdout = (res.stdout or "").strip()

    count = "0"
    for line in stdout.splitlines():
        if line.startswith("SUCCESS:"):
            count = line.split(":", 1)[1]
            break

    if int(count) > 0:
        print(f"[+] Symlink applet BusyBox diperbaiki: {count} applets ditambahkan")
    else:
        print(f"[INFO] Symlink applet BusyBox sudah lengkap (tidak ada yang perlu ditambahkan)")

    return True


def sync_busybox_standalone(session: SshSession, force: bool = False) -> bool:
    """
    Dedicated BusyBox NDK dynamic symlink synchronization over SSH.
    Verifies remote MD5 first before transferring.
    """
    if not LOCAL_BUSYBOX.is_file():
        print(f"[ERROR] Local busybox binary tidak ditemukan di: {LOCAL_BUSYBOX}")
        return False

    local_md5 = get_file_md5(LOCAL_BUSYBOX)

    if not force:
        res = session.run_cmd(f"md5sum {REMOTE_MODULE_BIN}/busybox 2>/dev/null", check=False)
        remote_md5 = ""
        out = (res.stdout or "").strip()
        if out:
            remote_md5 = out.split()[0].lower()

        if remote_md5 and remote_md5 == local_md5.lower():
            print(f"[INFO] BusyBox di Android ({session.host}) sudah identik (MD5: {local_md5}).")
            print("[INFO] Tidak ada pembaruan BusyBox: upload dan re-symlink dilewati.")
            return True

    print(f"[*] Local BusyBox: {LOCAL_BUSYBOX} ({LOCAL_BUSYBOX.stat().st_size} bytes, MD5: {local_md5})")
    print("[*] Mengunggah binary BusyBox ke panel Android...")
    session.run_cmd("mkdir -p /data/local/tmp && chmod 777 /data/local/tmp", check=False)
    session.scp_push(str(LOCAL_BUSYBOX), REMOTE_TMP_BUSYBOX)

    deploy_cmd = (
        f"mkdir -p {REMOTE_MODULE_BIN} && "
        f"cp -f {REMOTE_TMP_BUSYBOX} {REMOTE_MODULE_BIN}/busybox && "
        f"chmod 0755 {REMOTE_MODULE_BIN}/busybox && "
        f"rm -f {REMOTE_TMP_BUSYBOX}"
    )
    session.run_cmd(deploy_cmd)

    print("[*] Membuat ulang symlink applet BusyBox terhadap ROM /system/bin...")
    symlink_script = (
        f"cd {REMOTE_MODULE_BIN} || exit 1; "
        f"find . -type l -lname '*busybox*' -delete; "
        f"sysbin=\"$(ls /system/bin 2>/dev/null)\"; "
        f"count=0; "
        f"for applet in $(./busybox --list 2>/dev/null); do "
        f"  if [ ! \"$(echo \"$sysbin\" | ./busybox grep -xF \"$applet\")\" ]; then "
        f"    ./busybox ln -sf busybox \"$applet\"; "
        f"    count=$((count + 1)); "
        f"  fi; "
        f"done; "
        f"echo \"SUCCESS:$count\""
    )
    res = session.run_cmd(symlink_script)
    stdout = (res.stdout or "").strip()

    count = "unknown"
    for line in stdout.splitlines():
        if line.startswith("SUCCESS:"):
            count = line.split(":", 1)[1]
            break

    print(f"[+] BusyBox tersinkronisasi! Re-symlinked {count} applets tanpa tabrakan ROM.")
    return True


def fix_openssh_symlinks(session: SshSession) -> bool:
    """
    Fix/recreate OpenSSH wrapper symlinks in module's system/bin/ on the Android device.
    These symlinks point to /system/usr/libexec/ssh-core/wrapper.
    Executes in a single remote SSH command to minimize network latency.
    """
    links_str = " ".join(OPENSSH_SYMLINKS)
    script = (
        f"[ -x {OPENSSH_WRAPPER_TARGET} ] || exit 0; "
        f"fixed=0; "
        f"for l in {links_str}; do "
        f"  if [ \"$(readlink {REMOTE_MODULE_BIN}/$l 2>/dev/null)\" != \"{OPENSSH_WRAPPER_TARGET}\" ]; then "
        f"    rm -f {REMOTE_MODULE_BIN}/$l && ln -sf {OPENSSH_WRAPPER_TARGET} {REMOTE_MODULE_BIN}/$l; "
        f"    fixed=$((fixed + 1)); "
        f"  fi; "
        f"done; "
        f"echo \"FIXED:$fixed\""
    )
    res = session.run_cmd(script, check=False)
    out = (res.stdout or "").strip()
    fixed = 0
    for line in out.splitlines():
        if line.startswith("FIXED:"):
            try:
                fixed = int(line.split(":", 1)[1])
            except ValueError:
                pass
            break

    if fixed > 0:
        print(f"[+] Symlink OpenSSH wrapper diperbaiki: {fixed} dari {len(OPENSSH_SYMLINKS)}")
    else:
        print(f"[INFO] Symlink OpenSSH wrapper valid ({len(OPENSSH_SYMLINKS)} tools).")

    return True


def fix_jacktor_api_symlink(session: SshSession) -> bool:
    """
    Fix/recreate Jacktor API CGI gateway symlink in module's webroot/cgi-bin/ on the Android device.
    Ensures webroot/cgi-bin/jacktor-api points to ../../system/bin/jacktor-api.
    """
    target = "../../system/bin/jacktor-api"
    cgi_path = f"{REMOTE_MODDIR}/webroot/cgi-bin/jacktor-api"

    check = session.run_cmd(f"readlink {cgi_path} 2>/dev/null", check=False)
    current = (check.stdout or "").strip()

    if current != target:
        fix_cmd = (
            f"mkdir -p {REMOTE_MODDIR}/webroot/cgi-bin && "
            f"rm -f {REMOTE_MODDIR}/webroot/cgi-bin/api.sh {REMOTE_MODDIR}/webroot/cgi-bin/api {cgi_path} 2>/dev/null && "
            f"ln -sf {target} {cgi_path} && "
            f"chmod 0755 {cgi_path} {REMOTE_MODDIR}/system/bin/jacktor-api 2>/dev/null"
        )
        res = session.run_cmd(fix_cmd, check=False)
        if res.returncode == 0:
            print(f"[+] Symlink Jacktor API CGI gateway diperbaiki: {cgi_path} -> {target}")
            return True
        else:
            print("[-] Gagal memperbaiki symlink Jacktor API CGI gateway")
            return False
    else:
        print("[INFO] Symlink Jacktor API CGI gateway valid (webroot/cgi-bin/jacktor-api).")
        return True


def fix_all_module_symlinks(session: SshSession, busybox_changed: bool = False, force: bool = False) -> bool:
    """
    Comprehensive verification and repair of all symlinks in the active Android module:
    1. Jacktor API CGI gateway symlink (webroot/cgi-bin/jacktor-api)
    2. OpenSSH & rsync wrapper symlinks (/system/usr/libexec/ssh-core/wrapper)
    3. BusyBox applet symlinks against ROM /system/bin
    """
    print("[*] Memeriksa dan memvalidasi integritas symlink modul di Panel Android...")
    ok1 = fix_jacktor_api_symlink(session)
    ok2 = fix_openssh_symlinks(session)
    ok3 = fix_busybox_symlinks(session, force=(busybox_changed or force))
    return ok1 and ok2 and ok3


def copy_apk_to_module() -> bool:
    """
    Copy built APK from source directory to module's system/priv-app/
    This makes the APK part of the module, synced via rsync.
    Returns True if APK was copied (changed), False if no APK or no change.
    """
    # Find local APK in build output
    apk_files = list(LOCAL_APK_SOURCE_DIR.rglob(LOCAL_APK_PATTERN))
    if not apk_files:
        print(f"[INFO] Tidak ada APK ditemukan di {LOCAL_APK_SOURCE_DIR} (pattern: {LOCAL_APK_PATTERN})")
        return False

    # Use the most recent APK (release/debug)
    local_apk = max(apk_files, key=lambda f: f.stat().st_mtime)
    local_md5 = get_file_md5(local_apk)

    # Target path in module
    MODULE_APK_DIR.mkdir(parents=True, exist_ok=True)
    module_apk_path = MODULE_APK_DIR / MODULE_APK_NAME

    # Check if module APK exists and compare MD5
    if module_apk_path.is_file():
        module_md5 = get_file_md5(module_apk_path)
        if module_md5 == local_md5:
            print(f"[INFO] APK di modul sudah identik (MD5: {local_md5}). Tidak perlu copy.")
            return False

    # Copy APK to module
    import shutil
    shutil.copy2(local_apk, module_apk_path)
    print(f"[+] APK dicopy ke modul: {module_apk_path} ({local_apk.stat().st_size} bytes, MD5: {local_md5})")
    return True


def install_system_app_on_device(session: SshSession) -> bool:
    """
    Install/update system app on Android device from module's priv-app directory.
    Called from module's service.sh or post-fs-data.sh on boot, or manually.
    Updates without reboot using pm install -r with system privileges.
    """
    remote_apk = f"{REMOTE_MODDIR}/system/priv-app/JacktorAudio/{MODULE_APK_NAME}"

    # Check if APK exists on device
    check = session.run_cmd(f"ls -l {remote_apk} 2>/dev/null", check=False)
    if check.returncode != 0 or not (check.stdout or "").strip():
        print(f"[INFO] System app APK tidak ditemukan di {remote_apk}")
        return False

    # Get MD5 of APK in module on device
    r_md5_res = session.run_cmd(f"md5sum {remote_apk} 2>/dev/null", check=False)
    remote_md5 = ((r_md5_res.stdout or "").strip().split() or [""])[0].lower()

    # Check currently installed version
    res = session.run_cmd(f"pm path {APP_PACKAGE_NAME} 2>/dev/null", check=False)
    out = (res.stdout or "").strip()

    needs_install = True
    if out.startswith("package:"):
        installed_path = out.split(":", 1)[1]
        i_md5_res = session.run_cmd(f"md5sum {installed_path} 2>/dev/null", check=False)
        installed_md5 = ((i_md5_res.stdout or "").strip().split() or [""])[0].lower()
        if installed_md5 and installed_md5 == remote_md5:
            needs_install = False
            print(f"[INFO] System app sudah versi terbaru (MD5: {remote_md5})")
            return True

    if not needs_install:
        return True

    print("[*] Memasang/memperbarui system app dari modul...")

    # Install as system app (requires root, uses -r for replace)
    # For system/priv-app, we can use pm install with proper SELinux context
    install_cmd = (
        f"pm install -r -g {remote_apk} 2>&1 || "
        f"pm install -r -g --user 0 {remote_apk} 2>&1"
    )
    res = session.run_cmd(install_cmd, check=False)

    stdout = (res.stdout or "").strip()
    stderr = (res.stderr or "").strip()
    output = f"{stdout} {stderr}".strip()

    if res.returncode == 0 or "Success" in output or "success" in output.lower():
        print(f"[+] System app berhasil diinstall/update: {APP_PACKAGE_NAME}")

        # Grant runtime permissions
        grant_runtime_permissions(session)

        # Restart app if running (hot-swap, no reboot)
        restart_app_if_needed(session)
        return True
    else:
        print(f"[-] Gagal install system app: {output}")
        return False


def grant_runtime_permissions(session: SshSession) -> bool:
    """Grant all runtime permissions to the system app (system-level via module)."""
    print("[*] Memberikan izin runtime & system ke system app...")

    # All permissions needed by Jacktor Audio Panel (system-level granted by module)
    common_perms = [
        # Network (always needed for socat/API)
        "android.permission.INTERNET",
        "android.permission.ACCESS_NETWORK_STATE",
        "android.permission.ACCESS_WIFI_STATE",
        "android.permission.QUERY_ALL_PACKAGES",

        # USB Serial (for direct USB access - module grants for socat fallback)
        "android.permission.USB_PERMISSION",

        # Foreground service for background telemetry
        "android.permission.FOREGROUND_SERVICE",
        "android.permission.FOREGROUND_SERVICE_DATA_SYNC",
        "android.permission.WAKE_LOCK",
        "android.permission.RECEIVE_BOOT_COMPLETED",

        # Storage & System (granted by module at system level)
        "android.permission.WRITE_SECURE_SETTINGS",
        "android.permission.WRITE_EXTERNAL_STORAGE",
        "android.permission.READ_EXTERNAL_STORAGE",
        "android.permission.MANAGE_EXTERNAL_STORAGE",
        "android.permission.SYSTEM_ALERT_WINDOW",
        "android.permission.REQUEST_INSTALL_PACKAGES",
    ]

    granted = 0
    for perm in common_perms:
        # Grant via pm (runtime permissions)
        res = session.run_cmd(f"pm grant {APP_PACKAGE_NAME} {perm} 2>/dev/null", check=False)
        if res.returncode == 0:
            granted += 1

    # Grant appops for special permissions (SYSTEM_ALERT_WINDOW, WRITE_SECURE_SETTINGS, etc.)
    print("[*] Memberikan appops system permissions...")
    special_appops = {
        "android:system_alert_window": "allow",
        "android:write_secure_settings": "allow",
        "android:manage_external_storage": "allow",
        "android:request_install_packages": "allow",
    }
    for op, mode in special_appops.items():
        session.run_cmd(f"appops set {APP_PACKAGE_NAME} {op} {mode} 2>/dev/null", check=False)

    # Also grant via permission controller for Android 10+
    session.run_cmd(f"cmd permission grant {APP_PACKAGE_NAME} android.permission.MANAGE_EXTERNAL_STORAGE 2>/dev/null", check=False)
    session.run_cmd(f"cmd permission grant {APP_PACKAGE_NAME} android.permission.SYSTEM_ALERT_WINDOW 2>/dev/null", check=False)

    if granted > 0:
        print(f"[+] Diberikan {granted} izin runtime + system permissions ke {APP_PACKAGE_NAME}")
    return True


def restart_app_if_needed(session: SshSession) -> bool:
    """Restart app gracefully if running (hot-swap, no reboot)."""
    res = session.run_cmd(f"pidof {APP_PACKAGE_NAME} 2>/dev/null", check=False)
    if not (res.stdout or "").strip():
        return False

    print("[*] Aplikasi berjalan, melakukan restart graceful (hot-swap)...")

    # Get launcher activity
    res = session.run_cmd(
        f"cmd package resolve-activity --brief {APP_PACKAGE_NAME} 2>/dev/null",
        check=False
    )
    out = (res.stdout or "").strip()

    if out and "/" in out:
        session.run_cmd(f"monkey -p {APP_PACKAGE_NAME} -c android.intent.category.LAUNCHER 1 2>/dev/null", check=False)
        print(f"[+] Aplikasi direstart: {out}")
    else:
        session.run_cmd(
            f"am start -n {APP_PACKAGE_NAME}/.MainActivity 2>/dev/null || "
            f"am start -n {APP_PACKAGE_NAME}/.{APP_PACKAGE_NAME}.MainActivity 2>/dev/null",
            check=False
        )
        print(f"[+] Aplikasi direstart via am start")

    return True


def sync_module_to_android(panel_ip: str = None, interactive: bool = True, force: bool = False, busybox_only: bool = False, skip_app: bool = False) -> bool:
    if not MODULE_DIR.is_dir():
        print(f"[ERROR] Direktori modul tidak ditemukan di: {MODULE_DIR}")
        return False

    if not panel_ip:
        panel_ip = discover_jacktor_panel(interactive=interactive)

    if not panel_ip:
        print("[-] Error: Panel Android Jacktor Audio tidak terdeteksi di jaringan.")
        return False

    session = SshSession(panel_ip)

    try:
        if busybox_only:
            return sync_busybox_standalone(session, force=force)

        # Step 1: Copy APK to module's system/priv-app/ if exists (before rsync)
        if not skip_app:
            print("[*] Memeriksa APK aplikasi untuk dimasukkan ke modul...")
            copy_apk_to_module()

        print(f"[*] Memeriksa perbedaan modul dengan Panel Android ({panel_ip})...")

        # Test if remote has rsync
        has_remote_rsync = False
        r_check = session.run_cmd("which rsync 2>/dev/null || [ -x /data/adb/ap/bin/rsync ] && echo YES", check=False)
        if "YES" in (r_check.stdout or ""):
            has_remote_rsync = True

        ssh_opts = " ".join([
            f"-p {session.port}",
            "-o StrictHostKeyChecking=no",
            "-o UserKnownHostsFile=/dev/null",
            "-o LogLevel=ERROR"
        ])

        if has_remote_rsync:
            rsync_path = "/data/adb/ap/bin/rsync" if "[ -x /data/adb/ap/bin/rsync ]" else "rsync"
            rsync_cmd = [
                "rsync",
                "-avzc",
                "-i",
                f"--rsync-path={rsync_path}",
                "-e", f"ssh {ssh_opts}",
                "--exclude=.git*",
                "--exclude=META-INF*",
                "--exclude=README.md",
                "--exclude=wadbd",
                "--exclude=*.pyc",
                "--exclude=__pycache__",
                "--exclude=*.bak",
                "--exclude=*~",
                # Protect live runtime settings on the panel from being overwritten
                "--exclude=config/session_token",
                "--exclude=config/auth.conf",
                "--exclude=config/audio_gain.conf",
                "--exclude=config/resampler_mode*",
                "--exclude=config/fixed_sample_rate*",
                "--exclude=config/fixed_quality*",
                "--exclude=config/notification_split",
                "--exclude=config/battery_bypass",
                "--exclude=config/stay_on_mode",
                "--exclude=config/cpu_profile",
                "--exclude=config/audio_output_target",
                "--exclude=config/current_track",
                "--exclude=config/current_cover_ts",
                "--exclude=config/swap_size_mb",
                "--exclude=config/usb_mode_state",
                "--exclude=webroot/cover.jpg",
                "--exclude=webroot/cgi-bin/api.sh",
                "--exclude=webroot/cgi-bin/api",
                f"{MODULE_DIR}/",
                f"{session.user}@{panel_ip}:{REMOTE_MODDIR}/"
            ]

            res = subprocess.run(
                rsync_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=session.env
            )

            if res.returncode != 0:
                print(f"[-] Warning: rsync keluar dengan returncode {res.returncode}, fallback ke transfer SCP...")
                has_remote_rsync = False

        if has_remote_rsync:
            changed_files = []
            busybox_changed = False
            webroot_changed = False

            for line in res.stdout.splitlines():
                line_str = line.strip()
                if not line_str or line_str.startswith("sending incremental file list") or line_str.startswith("sent ") or line_str.startswith("total size "):
                    continue

                parts = line_str.split(None, 1)
                if len(parts) == 2:
                    token, rel_path = parts[0], parts[1]
                    if token.startswith("<f") or token.startswith("<L"):
                        changed_files.append(rel_path)
                        if "system/bin/busybox" in rel_path:
                            busybox_changed = True
                        if rel_path.startswith("webroot/") or "config/httpd.conf" in rel_path:
                            webroot_changed = True

            if not changed_files:
                print(f"[INFO] Semua file modul di Android ({panel_ip}) sudah identik (checksum cocok).")
                print("[INFO] Tidak ada file yang perlu di-push. Memeriksa symlink...")
                fix_all_module_symlinks(session, busybox_changed=False, force=force)
                print(f"[OK] Modul sudah mutakhir dan identik. Sinkronisasi selesai tanpa transfer data.")
                return True

            print(f"[+] Berhasil sinkronisasi {len(changed_files)} file yang berubah ke Android ({panel_ip}):")
            for f in changed_files:
                print(f"    • {f}")
        else:
            # Fallback SCP: Push modified files
            print("[*] Menjalankan fallback push via SCP...")
            session.run_cmd(f"mkdir -p {REMOTE_MODDIR} {REMOTE_MODULE_BIN} {REMOTE_MODDIR}/webroot/cgi-bin {REMOTE_MODDIR}/config", check=False)
            
            # Check busybox md5
            busybox_changed = False
            if LOCAL_BUSYBOX.is_file():
                loc_md5 = get_file_md5(LOCAL_BUSYBOX)
                r_md5 = session.run_cmd(f"md5sum {REMOTE_MODULE_BIN}/busybox 2>/dev/null", check=False)
                rem_md5 = ((r_md5.stdout or "").strip().split() or [""])[0].lower()
                if rem_md5 != loc_md5.lower() or force:
                    sync_busybox_standalone(session, force=True)
                    busybox_changed = True

            # SCP key directory files
            for rel_root, _, files in os.walk(MODULE_DIR):
                for fn in files:
                    full_p = Path(rel_root) / fn
                    rel_p = full_p.relative_to(MODULE_DIR)
                    rel_str = str(rel_p)
                    if any(rel_str.startswith(ex) for ex in [".git", "META-INF", "config/", "webroot/cover.jpg", "webroot/cgi-bin/api.sh", "webroot/cgi-bin/api", "system/bin/busybox"]):
                        continue
                    rem_target = f"{REMOTE_MODDIR}/{rel_str}"
                    session.run_cmd(f"mkdir -p $(dirname {rem_target}) 2>/dev/null", check=False)
                    session.scp_push(str(full_p), rem_target)
            webroot_changed = True

        # Post-sync maintenance on Android
        cleanup_script = (
            f"rm -rf {REMOTE_MODDIR}/META-INF {REMOTE_MODDIR}/system/bin/wadbd {REMOTE_MODDIR}/README.md 2>/dev/null; "
            f"rm -f {REMOTE_MODDIR}/webroot/cgi-bin/api.sh {REMOTE_MODDIR}/webroot/cgi-bin/api 2>/dev/null; "
            f"chmod -R 0755 {REMOTE_MODDIR}/*.sh {REMOTE_MODDIR}/system/bin 2>/dev/null; "
            f"chmod 0755 {REMOTE_MODDIR}/system/bin/jacktor-api 2>/dev/null; "
            f"ln -sf ../../system/bin/jacktor-api {REMOTE_MODDIR}/webroot/cgi-bin/jacktor-api 2>/dev/null; "
            f"chmod 0644 {REMOTE_MODDIR}/module.prop {REMOTE_MODDIR}/system.prop 2>/dev/null; "
        )

        session.run_cmd(cleanup_script, check=False)

        # Verify and fix all module symlinks (jacktor-api, OpenSSH tools, BusyBox applets)
        fix_all_module_symlinks(session, busybox_changed=busybox_changed, force=force)

        # Install/update system app on device from module's priv-app (after rsync)
        apk_changed = any("JacktorAudio.apk" in f for f in changed_files)
        if not skip_app and (apk_changed or force):
            print("[*] Memasang/memperbarui system app di perangkat...")
            install_system_app_on_device(session)
        elif skip_app:
            print("[INFO] Install system app dilewati (--no-app).")
        else:
            print("[INFO] System app tidak berubah (install dilewati).")

        # Ensure WebUI daemon (busybox httpd on port 80) is running.
        # Note: busybox httpd serves static files & CGI directly from disk without caching,
        # so we only start/restart it if it is not currently listening on port 80.
        ensure_httpd_cmd = (
            f"if ! {REMOTE_MODDIR}/system/bin/busybox netstat -tlpn 2>/dev/null | grep -q ':80 '; then "
            f"  for p in $(pidof busybox 2>/dev/null); do "
            f"    tr '\\0' ' ' < \"/proc/$p/cmdline\" 2>/dev/null | grep -q 'httpd' && kill -9 \"$p\" 2>/dev/null; "
            f"  done; "
            f"  killall -9 jacktor-api 2>/dev/null || true; "
            f"  sleep 0.3; "
            f"  {REMOTE_MODDIR}/system/bin/busybox httpd -p 80 -h {REMOTE_MODDIR}/webroot -c {REMOTE_MODDIR}/config/httpd.conf >/dev/null 2>&1; "
            f"fi"
        )
        session.run_cmd(ensure_httpd_cmd, check=False)
        print("[+] Daemon WebUI httpd (Port 80) aktif.")

        return True

    finally:
        session.cleanup()


def main():
    target_ip = None
    force = False
    busybox_only = False
    skip_app = False

    for arg in sys.argv[1:]:
        a = arg.strip()
        if a in ("-h", "--help"):
            print("Penggunaan: python3 tools/sync_module.py [IP_PANEL] [--force] [--busybox] [--no-app]")
            print("  IP_PANEL (opsional) : IP perangkat Android.")
            print("  --busybox           : Hanya sinkronkan binary BusyBox dan buat ulang symlinks.")
            print("  --force             : Paksa sinkronisasi meskipun checksum cocok.")
            print("  --no-app            : Lewati sinkronisasi APK aplikasi Android.")
            sys.exit(0)
        elif a in ("-f", "--force"):
            force = True
        elif a in ("-b", "--busybox", "--busybox-only"):
            busybox_only = True
        elif a in ("--no-app", "--skip-app"):
            skip_app = True
        elif not a.startswith("-"):
            target_ip = a

    success = sync_module_to_android(target_ip, interactive=True, force=force, busybox_only=busybox_only, skip_app=skip_app)
    if not success:
        sys.exit(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[INFO] Dibatalkan oleh pengguna.")
        sys.exit(0)
