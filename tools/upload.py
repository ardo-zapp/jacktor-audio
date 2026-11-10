#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/upload.py
Jacktor Firmware Builder & Flasher
Compiles and flashes ESP32 Amplifier firmware strictly at 921,600 bps either:
  1. Direct USB serial on local machine (PlatformIO CLI @ 921600) [DEFAULT]
  2. Android Panel via socat bridge & esputil (@ 921600 bps)

Supports CLI flags:
  --build-only (-b): Build firmware only (skip upload)
  --upload-only (-u): Upload pre-built binary only (skip compilation)
"""

import os
import subprocess
import sys
import time
import json
import platform
import shutil
import argparse
import serial
import serial.tools.list_ports
from pathlib import Path

# Add tools directory to sys.path for discover module
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from discover import discover_jacktor_panel, SshSession, verify_jacktor_ip
from auth_client import JacktorApiClient

# Add common PlatformIO binary directories to PATH (Cross-platform)
path_additions = [
    os.path.expanduser("~/.local/bin"),
    os.path.expanduser("~/.platformio/penv/bin"),
    os.path.expanduser("~/.platformio/penv/Scripts"),
]

for p in path_additions:
    if os.path.exists(p) and p not in os.environ.get("PATH", ""):
        os.environ["PATH"] = p + os.pathsep + os.environ.get("PATH", "")

MESSAGES = {
    "menu_title": "=== JACKTOR FIRMWARE BUILDER & FLASHER ===",
    "menu_subtitle": "Target: Jacktor Audio Amplifier (ESP32 @ 921,600 bps)",
    "menu_opt1": "1. USB (Direct USB serial port) [DEFAULT]",
    "menu_opt2": "2. Panel (Android Panel via High-Speed Jacktor API: Upload & Real-Time Flash @ 921600)",
    "menu_prompt": "Pilih target upload [1/2] (Default: 1): ",
    "invalid_opt": "[WARN] Pilihan tidak valid, menggunakan USB (1).",
    "install_fail": "[ERROR] PlatformIO installation process failed.",
    "scanning_usb": "[INFO] Memindai port USB lokal untuk Jacktor ESP32 Amplifier...",
    "scanning_panel": "[INFO] Mendeteksi IP Panel Android Jacktor Audio dan ketersediaan TTY...",
    "amp_not_found": "[SAFETY ERROR] ESP32 Amplifier tidak terdeteksi pada port USB lokal!\nPastikan Amplifier terhubung via USB dan pernah diflash manual minimal satu kali.",
    "panel_not_found": "[SAFETY ERROR] Panel Android Jacktor Audio tidak ditemukan di jaringan!",
    "panel_tty_not_found": "[SAFETY ERROR] Port USB Serial TTY (/dev/ttyUSB* / /dev/ttyACM*) tidak terdeteksi pada panel Android!\nPastikan kabel USB OTG terhubung dari ESP32 Amplifier ke Android panel.",
    "verified_amp": "[OK] Terverifikasi ESP32 Amplifier pada port lokal: {port}",
    "verified_panel": "[OK] Terverifikasi Panel Android ({host}) | Port TTY: {port}",
    "aborting": "[SAFETY ABORT] Membatalkan proses upload untuk mencegah kerusakan atau error.",
    "start_build": "\n[INFO] Memulai kompilasi firmware ESP32 Amplifier pada mesin ini...\n",
    "start_upload_usb": "\n[INFO] Memulai proses upload ke port USB lokal: {port} (Baud: 921600)...\n",
    "start_upload_panel": "\n[INFO] Memulai proses flash di Panel via esputil: {port} (Baud: 921600, PTY realtime)...\n",
    "pushing_files": "[INFO] Mengirim file binary firmware dan flasher tool ke Android panel via SCP...",
    "upload_success": "\n[OK] Firmware Amplifier berhasil diupload ke ESP32 @ 921600 baud!",
    "build_success": "\n[OK] Build firmware ESP32 Amplifier selesai dengan sukses!",
    "cmd_not_found": "\n[ERROR] Command 'pio' tidak ditemukan. Pastikan PlatformIO terinstal dan ada dalam PATH.",
    "upload_fail": "\n[ERROR] Proses build/upload gagal dengan exit code"
}

def release_local_monitor():
    # Non-destructive: cmd.py handles auto-reconnect gracefully.
    # Stopping the serial bridge on the panel frees the port without exiting user TUI.
    pass

def panel_maintenance_mode(session, enable=True):
    """
    Toggle panel maintenance mode.
    When enabled: stops service.sh, serial-bridge, and any process holding /dev/ttyUSB*
    When disabled: restarts service.sh and serial-bridge
    """
    if enable:
        print("[INFO] Panel → Maintenance Mode (menghentikan service.sh, serial-bridge, layanan serial)...")
        session.run_cmd(
            "touch /data/local/tmp/jacktor_flashing.lock 2>/dev/null; "
            "chmod 666 /data/local/tmp/jacktor_flashing.lock 2>/dev/null; "
            "/data/adb/modules/jacktor-audio-framework/system/bin/serial-bridge stop >/dev/null 2>&1 || true; "
            "pkill -f 'serial-bridge' 2>/dev/null || true; "
            "pkill -f 'socat.*7777' 2>/dev/null || true; "
            "pkill -f 'cat /dev/ttyUSB' 2>/dev/null || true; "
            "/data/adb/modules/jacktor-audio-framework/service.sh stop >/dev/null 2>&1 || true; "
            "pkill -f 'service.sh' 2>/dev/null || true; "
            "/data/adb/modules/jacktor-audio-framework/system/bin/busybox fuser -k /dev/ttyUSB* 2>/dev/null || true; "
            "/data/adb/modules/jacktor-audio-framework/system/bin/busybox fuser -k /dev/ttyACM* 2>/dev/null || true",
            check=False
        )
        time.sleep(1.0)
    else:
        print("[INFO] Panel → Normal Mode (memulai ulang service.sh & serial-bridge)...")
        session.run_cmd(
            "rm -f /data/local/tmp/jacktor_flashing.lock 2>/dev/null || true; "
            "/data/adb/modules/jacktor-audio-framework/service.sh start >/dev/null 2>&1 || true; "
            "/data/adb/modules/jacktor-audio-framework/system/bin/serial-bridge restart >/dev/null 2>&1 || true",
            check=False
        )
        time.sleep(1.0)

def release_port_if_busy(port):
    if not port or not os.path.exists(port):
        return
    try:
        res = subprocess.run(["fuser", port], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        out = res.stdout.strip()
        if out:
            pids = [int(p) for p in out.split() if p.isdigit()]
            for pid in pids:
                if pid != os.getpid():
                    try:
                        with open(f"/proc/{pid}/cmdline", "r") as f:
                            cmdline = f.read()
                        if "cmd.py" in cmdline or "python" in cmdline:
                            print(f"[WARN] Port {port} sedang digunakan oleh PID {pid} ({cmdline.strip()}). Menghentikan proses...")
                            os.kill(pid, 15)  # SIGTERM
                            time.sleep(0.5)
                    except Exception:
                        pass
    except Exception:
        pass

def probe_and_validate_amp(port, msg):
    release_port_if_busy(port)
    try:
        ser = serial.Serial(
            port=port,
            baudrate=921600,
            timeout=0.6,
            write_timeout=0.6,
            dsrdtr=False,
            rtscts=False
        )
    except Exception:
        return False

    try:
        if platform.system() == "Linux":
            try:
                import termios
                attrs = termios.tcgetattr(ser.fd)
                attrs[2] &= ~termios.HUPCL
                termios.tcsetattr(ser.fd, termios.TCSANOW, attrs)
            except Exception:
                pass

        ser.reset_input_buffer()
        ser.reset_output_buffer()

        payload = {"cmd": "ping", "client": "uploader"}
        msg_str = json.dumps(payload, separators=(',', ':')) + "\n"
        ser.write(msg_str.encode("utf-8"))
        ser.flush()

        t_end = time.time() + 0.8
        rx_buf = ""
        while time.time() < t_end:
            if ser.in_waiting:
                chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
                rx_buf += chunk
                for line in rx_buf.split("\n"):
                    line = line.strip()
                    if line.startswith("{") and line.endswith("}"):
                        try:
                            data = json.loads(line)
                            if "amp" in data or "t" in data or "temp" in data or "p" in data or "res" in data or "rt" in data:
                                ser.close()
                                return True
                        except Exception:
                            pass
            time.sleep(0.02)
        ser.close()
        return False
    except Exception:
        try:
            ser.close()
        except Exception:
            pass
        return False

def verify_and_get_amp_port(msg):
    ports = list(serial.tools.list_ports.comports())
    candidates = []
    for p in ports:
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        if "cp210" in desc or "ch340" in desc or "ch341" in desc or "ft232" in desc or "usb" in desc:
            candidates.append(p.device)
        elif "10c4" in hwid or "1a86" in hwid or "0403" in hwid:
            candidates.append(p.device)

    for dev in ["/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0"]:
        if os.path.exists(dev) and dev not in candidates:
            candidates.append(dev)

    for c in candidates:
        if probe_and_validate_amp(c, msg):
            return c

    for c in candidates:
        if os.path.exists(c):
            return c

    return None

def find_pio_command():
    pio_cmd = shutil.which("pio")
    if not pio_cmd:
        penv_pio = os.path.expanduser("~/.platformio/penv/bin/pio")
        if os.path.exists(penv_pio):
            pio_cmd = penv_pio
        elif platform.system() == "Windows":
            penv_pio_win = os.path.expanduser("~/.platformio/penv/Scripts/pio.exe")
            if os.path.exists(penv_pio_win):
                pio_cmd = penv_pio_win
    if not pio_cmd:
        pio_cmd = "pio"
    return pio_cmd

def compile_firmware(pio_executable, project_dir, msg):
    print(msg["start_build"])
    try:
        subprocess.check_call([pio_executable, "run", "-d", project_dir])
        print(msg["build_success"])
        return True
    except FileNotFoundError:
        print(msg["cmd_not_found"])
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"{msg['upload_fail']} {e.returncode}.")
        sys.exit(e.returncode)

def run_upload():
    msg = MESSAGES

    install_script = os.path.join(os.path.dirname(__file__), "install_pio.py")
    if os.path.exists(install_script):
        try:
            subprocess.check_call([sys.executable, install_script])
        except subprocess.CalledProcessError:
            print(msg["install_fail"])
            sys.exit(1)

    parser = argparse.ArgumentParser(
        description="Jacktor Audio ESP32 Firmware Builder & Flasher (Strict 921,600 bps)",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "target",
        nargs="?",
        default=None,
        help="Target: '1'/'usb' (Direct USB) or '2'/'panel' (Android Panel via socat bridge) or IP address"
    )
    parser.add_argument(
        "host",
        nargs="?",
        default=None,
        help="Android Panel IP address (optional, e.g. <panel_ip>)"
    )
    parser.add_argument(
        "-b", "--build-only", "--build",
        action="store_true",
        dest="build_only",
        help="Compile/build firmware only (do not upload)"
    )
    parser.add_argument(
        "-u", "--upload-only", "--upload",
        action="store_true",
        dest="upload_only",
        help="Upload pre-built binary only (skip compilation)"
    )

    args = parser.parse_args()

    pio_executable = find_pio_command()
    project_dir = os.path.join(os.path.dirname(__file__), "..", "firmware", "amplifier")

    # If --build-only requested, simply compile and exit
    if args.build_only:
        compile_firmware(pio_executable, project_dir, msg)
        print("[OK] Selesai. Mode --build-only aktif, firmware tidak diupload.")
        return

    # Determine target choice and host
    target_choice = None
    cli_host = None

    if args.target:
        arg_lower = args.target.lower().strip()
        if arg_lower in ["1", "usb", "direct", "local", "amp", "amplifier"]:
            target_choice = "1"
        elif arg_lower in ["2", "panel", "socat", "ssh", "android", "remote", "wifi", "adb"]:
            target_choice = "2"
            if args.host:
                cli_host = args.host.strip()
        elif verify_jacktor_ip(args.target):
            target_choice = "2"
            cli_host = args.target.strip()
        elif args.host and verify_jacktor_ip(args.host):
            target_choice = "2"
            cli_host = args.host.strip()

    if not target_choice:
        print(msg["menu_title"])
        print(msg["menu_subtitle"])
        print(msg["menu_opt1"])
        print(msg["menu_opt2"])
        try:
            user_input = input(msg["menu_prompt"]).strip()
        except (KeyboardInterrupt, EOFError):
            print("\n[INFO] Upload dibatalkan oleh pengguna.")
            sys.exit(0)

        # Default is 1 (USB)
        if not user_input or user_input == "1" or user_input.lower().startswith(("u", "d", "l")):
            target_choice = "1"
        elif user_input == "2" or user_input.lower().startswith(("p", "s", "r", "a")):
            target_choice = "2"
        else:
            print(msg["invalid_opt"])
            target_choice = "1"

    # Default action: compile firmware first (unless --upload-only is passed)
    if not args.upload_only:
        compile_firmware(pio_executable, project_dir, msg)

    # =========================================================================
    # OPTION 1: DIRECT USB SERIAL ON LOCAL MACHINE (STRICT 921600)
    # =========================================================================
    if target_choice == "1":
        print(msg["scanning_usb"])
        target_port = verify_and_get_amp_port(msg)
        if not target_port:
            print(msg["amp_not_found"])
            print(msg["aborting"])
            sys.exit(1)

        print(msg["start_upload_usb"].format(port=target_port))
        try:
            # PlatformIO handles flashing at upload_speed=921600 defined in platformio.ini
            pio_upload_cmd = [pio_executable, "run", "-d", project_dir, "-t", "upload", "--upload-port", target_port]
            if args.upload_only:
                pio_upload_cmd.insert(4, "-t")
                pio_upload_cmd.insert(5, "nobuild")
            subprocess.check_call(pio_upload_cmd)
            print(msg["upload_success"])
        except FileNotFoundError:
            print(msg["cmd_not_found"])
            sys.exit(1)
        except subprocess.CalledProcessError as e:
            print(f"{msg['upload_fail']} {e.returncode}.")
            sys.exit(e.returncode)

    # =========================================================================
    # OPTION 2: ANDROID PANEL VIA HIGH-SPEED JACKTOR API (STRICT 921600)
    # =========================================================================
    else:
        print("[INFO] Mendeteksi IP Panel Android Jacktor Audio di jaringan...")
        panel_ip = cli_host or discover_jacktor_panel(interactive=True)
        if not panel_ip:
            print(msg["panel_not_found"])
            print(msg["aborting"])
            sys.exit(1)

        print(f"[INFO] Menghubungkan ke Jacktor API pada Panel Android ({panel_ip}:80)...")
        client = JacktorApiClient.authenticate(host=panel_ip, interactive=True)

        # 1. Locate built binaries
        build_dir = os.path.join(project_dir, ".pio", "build", "esp32dev")
        bootloader_bin = os.path.join(build_dir, "bootloader.bin")
        partitions_bin = os.path.join(build_dir, "partitions.bin")
        firmware_bin = os.path.join(build_dir, "firmware.bin")

        required_files = [bootloader_bin, partitions_bin, firmware_bin]
        missing = [f for f in required_files if not os.path.exists(f)]
        if missing:
            print(f"[ERROR] File binary hasil build tidak lengkap: {missing}")
            print(msg["aborting"])
            sys.exit(1)

        # Copy firmware.bin to amplifier.bin locally for upload
        amp_bin_local = os.path.join(build_dir, "amplifier.bin")
        shutil.copy2(firmware_bin, amp_bin_local)
        root_build_amp = os.path.join(os.path.dirname(__file__), "..", "build", "amplifier.bin")
        if os.path.exists(os.path.dirname(root_build_amp)):
            shutil.copy2(firmware_bin, root_build_amp)

        # 2. Upload files via Jacktor API HTTP POST
        print("\n[INFO] Mengunggah file binary firmware ke Android Panel via Jacktor API (Port 80)...")
        upload_targets = [
            ("bootloader.bin", Path(bootloader_bin)),
            ("partitions.bin", Path(partitions_bin)),
            ("amplifier.bin", Path(amp_bin_local)),
        ]

        for fname, fpath in upload_targets:
            fsize = fpath.stat().st_size
            print(f"  • Mengunggah {fname} ({fsize:,} bytes)...", end="", flush=True)
            res = client.upload_firmware_file(fname, fpath)
            if res.get("success"):
                print(" [OK]")
            else:
                print(f" [GAGAL: {res.get('error', 'Unknown error')}]")
                print(msg["aborting"])
                sys.exit(1)

        # 3. Trigger Real-Time Flash via Jacktor API Streaming
        print("\n[INFO] Menjalankan proses flash firmware di ESP32 @ 921,600 bps secara real-time...")
        flash_ok = client.flash_firmware(port="/dev/ttyUSB0", baud=921600)

        if flash_ok:
            print(msg["upload_success"])
        else:
            print("\n[ERROR] Proses flash via API gagal!")
            sys.exit(1)

if __name__ == "__main__":
    try:
        run_upload()
    except KeyboardInterrupt:
        print("\n[INFO] Upload dibatalkan.")
        sys.exit(0)
