#!/usr/bin/env python3
"""
tools/sync_busybox.py
Synchronizes BusyBox NDK binary and generates dynamic applet symlinks on the
connected Android panel via ADB.

Ensures the installed module at /data/adb/modules/jacktor-audio-framework/system/bin
remains perfectly synchronized with the project codebase without colliding with
ROM-native /system/bin binaries.
"""

import sys
import subprocess
import os
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
LOCAL_MODULE_BIN = PROJECT_ROOT / "panel" / "realme-bengal" / "modul" / "jacktor-audio-framework" / "system" / "bin"
LOCAL_BUSYBOX = LOCAL_MODULE_BIN / "busybox"
REMOTE_MODULE_BIN = "/data/adb/modules/jacktor-audio-framework/system/bin"
REMOTE_TMP = "/data/local/tmp/busybox_sync"

def run_cmd(cmd, check=True):
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if check and result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(cmd)}\n{result.stderr}")
    return result

def get_adb_device():
    res = run_cmd(["adb", "devices"])
    lines = [line.strip() for line in res.stdout.strip().splitlines()[1:] if line.strip()]
    devices = [line.split()[0] for line in lines if line.split()[-1] == "device"]
    if not devices:
        raise RuntimeError("No active ADB devices found. Ensure device is connected and authorized.")
    # Prioritize network device if available (e.g. 192.168.1.11:5555)
    for dev in devices:
        if ":" in dev:
            return dev
    return devices[0]

def sync_busybox(device_id=None):
    if not LOCAL_BUSYBOX.exists():
        raise FileNotFoundError(f"Local busybox binary not found at: {LOCAL_BUSYBOX}")

    if not device_id:
        device_id = get_adb_device()

    adb_prefix = ["adb", "-s", device_id]
    print(f"[*] Target device: {device_id}")
    print(f"[*] Local BusyBox: {LOCAL_BUSYBOX} ({LOCAL_BUSYBOX.stat().st_size} bytes)")

    # 1. Push binary to /data/local/tmp
    print("[*] Pushing BusyBox binary to temporary storage...")
    run_cmd(adb_prefix + ["push", str(LOCAL_BUSYBOX), REMOTE_TMP])

    # 2. Deploy binary into module directory and set permissions
    print(f"[*] Deploying to {REMOTE_MODULE_BIN}/busybox...")
    deploy_cmd = (
        f"su -c 'mkdir -p {REMOTE_MODULE_BIN} && "
        f"cp -f {REMOTE_TMP} {REMOTE_MODULE_BIN}/busybox && "
        f"chmod 0755 {REMOTE_MODULE_BIN}/busybox && "
        f"rm -f {REMOTE_TMP}'"
    )
    run_cmd(adb_prefix + ["shell", deploy_cmd])

    # 3. Clean old symlinks and re-generate dynamic applet symlinks
    print("[*] Re-generating applet symlinks against ROM /system/bin...")
    symlink_script = (
        f"su -c '"
        f"cd {REMOTE_MODULE_BIN} || exit 1; "
        f"find . -type l -lname \"*busybox*\" -delete; "
        f"sysbin=\"$(ls /system/bin 2>/dev/null)\"; "
        f"count=0; "
        f"for applet in $(./busybox --list 2>/dev/null); do "
        f"  if [ ! \"$(echo \"$sysbin\" | ./busybox grep -xF \"$applet\")\" ]; then "
        f"    ./busybox ln -sf busybox \"$applet\"; "
        f"    count=$((count + 1)); "
        f"  fi; "
        f"done; "
        f"echo \"SUCCESS:$count\""
        f"'"
    )
    res = run_cmd(adb_prefix + ["shell", symlink_script])
    stdout = res.stdout.strip()
    
    count = "unknown"
    for line in stdout.splitlines():
        if line.startswith("SUCCESS:"):
            count = line.split(":", 1)[1]
            break

    print(f"[+] BusyBox synchronized! Re-symlinked {count} applets without system collisions.")

    # 4. Verify version
    ver_res = run_cmd(adb_prefix + ["shell", f"su -c '{REMOTE_MODULE_BIN}/busybox --help | head -n 2'"])
    print(f"[+] Device BusyBox Info:\n    {ver_res.stdout.strip()}")

if __name__ == "__main__":
    dev = sys.argv[1] if len(sys.argv) > 1 else None
    try:
        sync_busybox(dev)
    except Exception as e:
        print(f"[-] Error: {e}", file=sys.stderr)
        sys.exit(1)
