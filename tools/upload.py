#!/usr/bin/env python3
import os
import subprocess
import sys
import time
import json
import platform
import shutil
import serial
import serial.tools.list_ports

# Add common PlatformIO binary directories to PATH (Cross-platform)
path_additions = [
    os.path.expanduser("~/.local/bin"),
    os.path.expanduser("~/.platformio/penv/bin"),
    os.path.expanduser("~/.platformio/penv/Scripts"),  # Windows PlatformIO venv Scripts
]

for p in path_additions:
    if os.path.exists(p) and p not in os.environ.get("PATH", ""):
        os.environ["PATH"] = p + os.pathsep + os.environ.get("PATH", "")

MESSAGES = {
    "menu_title": "=== JACKTOR FIRMWARE UPLOADER ===",
    "menu_subtitle": "Target Firmware: Jacktor Audio Amplifier (ESP32)",
    "menu_opt1": "1. USB Langsung di Mesin Ini (Direct USB) [DEFAULT]",
    "menu_opt2": "2. Lewat ADB (Panel Android)",
    "menu_prompt": "Pilih target upload [1/2] (Default: 1): ",
    "invalid_opt": "[WARN] Pilihan tidak valid, menggunakan Direct USB (1).",
    "install_fail": "[ERROR] PlatformIO installation process failed.",
    "scanning_usb": "[INFO] Memindai port USB lokal untuk Jacktor ESP32 Amplifier...",
    "scanning_adb": "[INFO] Memeriksa koneksi ADB dan ketersediaan Android USB serial TTY...",
    "amp_not_found": "[SAFETY ERROR] ESP32 Amplifier tidak terdeteksi pada port USB lokal!\nPastikan Amplifier terhubung via USB dan pernah diflash manual minimal satu kali.",
    "adb_not_found": "[SAFETY ERROR] Perangkat Android tidak terdeteksi via ADB!\nPastikan Panel Android menyala, terhubung via USB/Wi-Fi ADB, dan statusnya authorized.",
    "adb_tty_not_found": "[SAFETY ERROR] Port USB Serial TTY (/dev/ttyUSB* / /dev/ttyACM*) tidak terdeteksi pada panel Android!\nPastikan kabel USB OTG terhubung dari ESP32 Amplifier ke Android panel.",
    "verified_amp": "[OK] Terverifikasi ESP32 Amplifier pada port lokal: {port}",
    "verified_adb": "[OK] Terverifikasi Panel Android via ADB ({device}) | Port TTY: {port}",
    "aborting": "[SAFETY ABORT] Membatalkan proses upload untuk mencegah kerusakan atau error.",
    "start_build": "\n[INFO] Memulai kompilasi firmware ESP32 Amplifier pada mesin ini...\n",
    "start_upload_usb": "\n[INFO] Memulai proses upload ke port USB lokal: {port} (Baud: 921600)...\n",
    "start_upload_adb": "\n[INFO] Memulai proses upload via ADB ke Android TTY: {port} (Baud: 921600)...\n",
    "pushing_files": "[INFO] Mengirim file binary firmware dan flasher tool ke Android panel...",
    "upload_success": "\n[OK] Firmware Amplifier berhasil diupload!",
    "cmd_not_found": "\n[ERROR] Command 'pio' tidak ditemukan. Pastikan PlatformIO terinstal dan ada dalam PATH.",
    "upload_fail": "\n[ERROR] Proses build/upload gagal dengan exit code"
}

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
                            print(f"[INFO] Freeing {port} held by monitor process PID {pid}...")
                            os.kill(pid, 15)
                            time.sleep(0.5)
                    except Exception:
                        pass
    except Exception:
        pass

def verify_and_get_amp_port(msg):
    for attempt in range(1, 3):
        ports = serial.tools.list_ports.comports()
        candidates = []

        for port in ports:
            desc_lower = (port.description or "").lower()
            hwid_lower = (port.hwid or "").lower()
            is_esp = False
            if any(x in hwid_lower for x in ["10c4:ea60", "1a86:7523", "0403:6001", "303a:"]):
                is_esp = True
            elif any(x in desc_lower for x in ["cp210", "ch340", "usb serial", "uart", "esp32"]):
                is_esp = True

            if is_esp:
                candidates.append(port)

        if candidates:
            # Probe each candidate for ESP32 Amplifier identity
            for port_info in candidates:
                port = port_info.device
                release_port_if_busy(port)

                try:
                    ser = serial.Serial(port, 921600, timeout=1.2)
                    try:
                        ser.dtr = False
                        ser.rts = False
                    except Exception:
                        pass
                    time.sleep(0.15)
                    ser.reset_input_buffer()

                    ser.write(b'\n{"type":"ping"}\n')
                    ser.flush()

                    start_time = time.time()
                    last_ping_time = start_time
                    is_amp = False
                    buffer = ""

                    while time.time() - start_time < 5.0:
                        if not is_amp and (time.time() - last_ping_time >= 0.35):
                            try:
                                ser.write(b'{"type":"ping"}\n')
                                ser.flush()
                            except Exception:
                                pass
                            last_ping_time = time.time()
                        if ser.in_waiting:
                            chunk = ser.read(ser.in_waiting).decode("utf-8", errors="ignore")
                            buffer += chunk
                            buffer_lower = buffer.lower()
                            if "jacktor audio" in buffer_lower or "vamp-" in buffer_lower or "amp-" in buffer_lower or "amplifier" in buffer_lower:
                                is_amp = True
                                break

                            while "\n" in buffer:
                                line, buffer = buffer.split("\n", 1)
                                line = line.strip()
                                if line.startswith("{"):
                                    try:
                                        data = json.loads(line)
                                        fw_ver = str(data.get("hz1", {}).get("fw_ver", "") or data.get("fw_ver", "")).lower()
                                        fw_name = str(data.get("fw_name", "")).lower()
                                        if fw_ver.startswith("amp-") or "amplifier" in fw_name or "jacktor audio" in fw_name:
                                            is_amp = True
                                            break
                                    except Exception:
                                        pass
                        time.sleep(0.05)

                    ser.close()

                    if is_amp or len(candidates) == 1:
                        print(msg["verified_amp"].format(port=port))
                        return port

                except Exception:
                    continue

        if attempt < 2:
            time.sleep(0.5)

    return None

def find_pio_command():
    pio_cmd = "pio.exe" if platform.system() == "Windows" else "pio"
    if shutil.which(pio_cmd):
        return pio_cmd

    possible_paths = [
        os.path.expanduser("~/.platformio/penv/bin/pio"),
        os.path.expanduser("~/.platformio/penv/Scripts/pio.exe"),
        os.path.expanduser("~/.local/bin/pio"),
    ]
    for p in possible_paths:
        if os.path.exists(p):
            return p

    return pio_cmd

def get_connected_adb_devices():
    try:
        res = subprocess.run(["adb", "devices"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        lines = res.stdout.strip().split("\n")[1:]
        devices = []
        for line in lines:
            parts = line.strip().split()
            if len(parts) >= 2 and parts[1] == "device":
                devices.append(parts[0])
        return devices
    except Exception:
        return []

def get_android_serial_ports(device_id=None):
    cmd_prefix = ["adb"]
    if device_id:
        cmd_prefix += ["-s", device_id]

    try:
        # 1. Try finding ttyUSB or ttyACM devices with root or standard shell
        check_cmd = cmd_prefix + ["shell", "su", "-c", "ls -1 /dev/ttyUSB* /dev/ttyACM* 2>/dev/null"]
        res = subprocess.run(check_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        ports = [p.strip() for p in res.stdout.strip().split("\n") if p.strip().startswith("/dev/tty")]
        if ports:
            return ports

        check_cmd_nosu = cmd_prefix + ["shell", "ls -1 /dev/ttyUSB* /dev/ttyACM* 2>/dev/null"]
        res = subprocess.run(check_cmd_nosu, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        ports = [p.strip() for p in res.stdout.strip().split("\n") if p.strip().startswith("/dev/tty")]
        if ports:
            return ports

        # 2. Check for connected USB Serial devices on OTG via sysfs
        # Recognized USB-Serial VID patterns (CP210x, CH340, FTDI, Espressif)
        known_vids = {
            "10c4", # Silicon Labs CP210x
            "1a86", # QinHeng CH340 / CH341
            "0403", # FTDI FT232 / FT2232
            "303a", # Espressif USB-JTAG-Serial
        }

        find_usb_cmd = cmd_prefix + ["shell", "su -c 'for d in /sys/bus/usb/devices/*; do [ -f $d/idVendor ] && echo $d $(cat $d/idVendor) $(cat $d/idProduct) $(cat $d/busnum) $(cat $d/devnum); done'"]
        res = subprocess.run(find_usb_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        for line in res.stdout.strip().split("\n"):
            parts = line.strip().split()
            if len(parts) >= 5:
                p, vid, pid, busnum, devnum = parts[0], parts[1].lower(), parts[2].lower(), parts[3], parts[4]
                if vid in known_vids:
                    dev_path = f"/dev/bus/usb/{int(busnum):03d}/{int(devnum):03d}"
                    return [dev_path]

        return []
    except Exception:
        return []

def run_upload():
    msg = MESSAGES

    install_script = os.path.join(os.path.dirname(__file__), "install_pio.py")
    if os.path.exists(install_script):
        try:
            subprocess.check_call([sys.executable, install_script])
        except subprocess.CalledProcessError:
            print(msg["install_fail"])
            sys.exit(1)

    target_choice = None
    if len(sys.argv) > 1:
        arg = sys.argv[1].lower().strip()
        if arg in ["1", "usb", "direct", "local", "amp", "amplifier"]:
            target_choice = "1"
        elif arg in ["2", "adb", "android", "panel"]:
            target_choice = "2"

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

        if not user_input or user_input == "1" or user_input.lower().startswith("u") or user_input.lower().startswith("d"):
            target_choice = "1"
        elif user_input == "2" or user_input.lower().startswith("a") or user_input.lower().startswith("p"):
            target_choice = "2"
        else:
            print(msg["invalid_opt"])
            target_choice = "1"

    pio_executable = find_pio_command()
    project_dir = os.path.join(os.path.dirname(__file__), "..", "firmware", "amplifier")

    # =========================================================================
    # OPTION 1: DIRECT USB ON LOCAL MACHINE
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
            subprocess.check_call([pio_executable, "run", "-d", project_dir, "-t", "upload", "--upload-port", target_port])
            print(msg["upload_success"])
        except FileNotFoundError:
            print(msg["cmd_not_found"])
            sys.exit(1)
        except subprocess.CalledProcessError as e:
            print(f"{msg['upload_fail']} {e.returncode}.")
            sys.exit(e.returncode)

    # =========================================================================
    # OPTION 2: VIA ADB TO ANDROID PANEL
    # =========================================================================
    else:
        print(msg["scanning_adb"])
        adb_devices = get_connected_adb_devices()
        if not adb_devices:
            print(msg["adb_not_found"])
            print(msg["aborting"])
            sys.exit(1)

        selected_device = adb_devices[0]
        android_ports = get_android_serial_ports(selected_device)
        if not android_ports:
            print(msg["adb_tty_not_found"])
            print(msg["aborting"])
            sys.exit(1)

        target_port = android_ports[0]
        print(msg["verified_adb"].format(device=selected_device, port=target_port))

        # 1. Build firmware on this machine
        print(msg["start_build"])
        try:
            subprocess.check_call([pio_executable, "run", "-d", project_dir])
        except FileNotFoundError:
            print(msg["cmd_not_found"])
            sys.exit(1)
        except subprocess.CalledProcessError as e:
            print(f"{msg['upload_fail']} {e.returncode}.")
            sys.exit(e.returncode)

        # 2. Locate built binaries
        build_dir = os.path.join(project_dir, ".pio", "build", "esp32dev")
        bootloader_bin = os.path.join(build_dir, "bootloader.bin")
        partitions_bin = os.path.join(build_dir, "partitions.bin")
        firmware_bin = os.path.join(build_dir, "firmware.bin")
        ota_data_bin = os.path.join(build_dir, "ota_data_initial.bin")
        if not os.path.exists(ota_data_bin):
            ota_data_bin = os.path.join(build_dir, "boot_app0.bin")

        required_files = [bootloader_bin, partitions_bin, firmware_bin, ota_data_bin]
        missing = [f for f in required_files if not os.path.exists(f)]
        if missing:
            print(f"[ERROR] File binary hasil build tidak lengkap: {missing}")
            print(msg["aborting"])
            sys.exit(1)

        # 3. Locate flasher tool (esputil for aarch64)
        local_flasher = os.path.join(os.path.dirname(__file__), "bin", "esputil_arm64")
        if not os.path.exists(local_flasher):
            # Fallback path in module
            local_flasher = os.path.join(os.path.dirname(__file__), "..", "panel", "realme-bengal", "modul", "jacktor-audio-framework", "system", "bin", "esputil")

        if not os.path.exists(local_flasher):
            print(f"[ERROR] Flasher tool esputil (ARM64) tidak ditemukan di: {local_flasher}")
            print(msg["aborting"])
            sys.exit(1)

        # 4. Push files to Android
        print(msg["pushing_files"])
        remote_dir = "/data/local/tmp/jacktor_upload"
        adb_cmd_base = ["adb", "-s", selected_device]

        subprocess.run(adb_cmd_base + ["shell", f"su -c 'mkdir -p {remote_dir} && chmod 777 {remote_dir}'"], check=True)
        subprocess.run(adb_cmd_base + ["push", local_flasher, f"{remote_dir}/esputil"], check=True)
        subprocess.run(adb_cmd_base + ["shell", f"su -c 'chmod 755 {remote_dir}/esputil'"], check=True)

        for bfile in required_files:
            fname = os.path.basename(bfile)
            subprocess.run(adb_cmd_base + ["push", bfile, f"{remote_dir}/{fname}"], check=True)

        # 5. Execute flash on Android via USB serial TTY
        ota_fname = os.path.basename(ota_data_bin)
        flash_cmd = (
            f"{remote_dir}/esputil -b 921600 -p {target_port} flash "
            f"0x1000 {remote_dir}/bootloader.bin "
            f"0x8000 {remote_dir}/partitions.bin "
            f"0xe000 {remote_dir}/{ota_fname} "
            f"0x10000 {remote_dir}/firmware.bin"
        )

        print(msg["start_upload_adb"].format(port=target_port))
        try:
            subprocess.run(adb_cmd_base + ["shell", f"su -c '{flash_cmd}'"], check=True)
            print(msg["upload_success"])
        except subprocess.CalledProcessError as e:
            print(f"{msg['upload_fail']} {e.returncode}.")
            sys.exit(e.returncode)

if __name__ == "__main__":
    run_upload()
