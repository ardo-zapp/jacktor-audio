#!/usr/bin/env python3
import os
import subprocess
import sys
import time
import json
import platform
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
    "menu_opt1": "1. Amplifier (ESP32) [DEFAULT]",
    "menu_opt2": "2. Panel (ESP32-S3)",
    "menu_prompt": "Select target device [1/2] (Default: 1): ",
    "invalid_opt": "[WARN] Invalid selection, defaulting to Amplifier (1).",
    "install_fail": "[ERROR] PlatformIO installation process failed.",
    "scanning_amp": "[INFO] Scanning USB ports for Jacktor ESP32 Amplifier...",
    "scanning_panel": "[INFO] Scanning USB ports for Jacktor ESP32-S3 Main Panel...",
    "amp_not_found": "[SAFETY ERROR] ESP32 Amplifier device not detected! Ensure Amplifier was manually flashed at least once.",
    "panel_not_found": "[SAFETY ERROR] ESP32-S3 Main Panel device not detected! Ensure S3 Panel was manually flashed at least once.",
    "panel_detected": "[SAFETY WARNING] Port {port} is an ESP32-S3 Panel! Refusing to flash Amplifier firmware to S3 target.",
    "amp_detected": "[SAFETY WARNING] Port {port} is the Jacktor ESP32 Amplifier! Refusing to flash Panel firmware to Amplifier target.",
    "verified_amp": "[OK] Verified ESP32 Amplifier on port: {port}",
    "verified_panel": "[OK] Verified ESP32-S3 Panel on port: {port}",
    "aborting": "[SAFETY ABORT] Aborting flash to prevent hardware damage.",
    "start_upload_amp": "\n[INFO] Starting compilation and upload process for ESP32 Amplifier firmware...\n",
    "start_upload_panel": "\n[INFO] Starting compilation and upload process for ESP32-S3 Panel firmware...\n",
    "upload_success_amp": "\n[OK] Amplifier firmware upload completed successfully!",
    "upload_success_panel": "\n[OK] S3 Panel firmware upload completed successfully!",
    "cmd_not_found": "\n[ERROR] Command 'pio' not found. Ensure PlatformIO is installed and available in PATH.",
    "upload_fail": "\n[ERROR] Build/upload process failed with exit code"
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
                    is_panel = False
                    buffer = ""

                    while time.time() - start_time < 5.0:
                        if not is_amp and not is_panel and (time.time() - last_ping_time >= 0.35):
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
                            if "[panel]" in buffer_lower or "jacktor_panel" in buffer_lower or "panel-" in buffer_lower:
                                is_panel = True
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
                                        elif fw_ver.startswith("panel-") or fw_ver.startswith("s3-") or "panel" in fw_name:
                                            is_panel = True
                                            break
                                    except Exception:
                                        pass
                        time.sleep(0.05)

                    ser.close()

                    if is_panel:
                        print(msg["panel_detected"].format(port=port))
                        continue

                    if is_amp:
                        print(msg["verified_amp"].format(port=port))
                        return port

                except Exception:
                    continue

        if attempt < 2:
            time.sleep(0.5)

    return None

def verify_and_get_panel_port(msg):
    for attempt in range(1, 3):
        ports = serial.tools.list_ports.comports()
        filtered_ports = [p for p in ports if not (p.device and "/dev/ttys" in p.device.lower())]

        candidates = []
        for port in filtered_ports:
            desc_lower = (port.description or "").lower()
            hwid_lower = (port.hwid or "").lower()
            dev_lower = (port.device or "").lower()
            is_esp = False
            if any(x in hwid_lower for x in ["303a:", "10c4:ea60", "1a86:7523", "0403:6001"]):
                is_esp = True
            elif any(x in desc_lower for x in ["esp32s3", "esp32-s3", "cp210", "ch340", "usb serial", "uart", "jtag"]):
                is_esp = True
            elif "ttyacm" in dev_lower or "ttyusb" in dev_lower or "com" in dev_lower:
                is_esp = True
            if is_esp:
                candidates.append(port)

        if candidates:
            # Probe each candidate for ESP32-S3 Panel identity
            for port_info in candidates:
                port = port_info.device
                hwid_lower = (port_info.hwid or "").lower()
                desc_lower = (port_info.description or "").lower()

                # Direct hardware match for ESP32-S3 USB CDC (VID 303A)
                if "303a:" in hwid_lower or "esp32s3" in desc_lower or "esp32-s3" in desc_lower:
                    print(msg["verified_panel"].format(port=port))
                    return port

                try:
                    ser = serial.Serial(port, 921600, timeout=1.2)
                    try:
                        ser.dtr = True
                        ser.rts = False
                    except Exception:
                        pass
                    time.sleep(0.15)
                    ser.reset_input_buffer()

                    ser.write(b'\n{"type":"ping"}\n')
                    ser.flush()

                    start_time = time.time()
                    last_ping_time = start_time
                    is_s3_panel = False
                    is_amp = False
                    buffer = ""

                    while time.time() - start_time < 2.0:
                        if not is_s3_panel and not is_amp and (time.time() - last_ping_time >= 0.35):
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
                            if "[panel]" in buffer_lower or "jacktor_panel" in buffer_lower or "panel-" in buffer_lower:
                                is_s3_panel = True
                                break
                            if "jacktor audio" in buffer or "vamp-" in buffer_lower or "amp-" in buffer_lower:
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
                                        if fw_ver.startswith("panel-") or fw_ver.startswith("s3-") or "panel" in fw_name:
                                            is_s3_panel = True
                                            break
                                        elif fw_ver.startswith("amp-") or "amplifier" in fw_name or "jacktor audio" in fw_name:
                                            is_amp = True
                                            break
                                    except Exception:
                                        pass
                        time.sleep(0.05)

                    ser.close()

                    if is_amp:
                        print(msg["amp_detected"].format(port=port))
                        continue

                    if is_s3_panel or ("303a" in hwid_lower and not is_amp) or (len(candidates) == 1 and not is_amp):
                        print(msg["verified_panel"].format(port=port))
                        return port

                except Exception:
                    if "303a:" in hwid_lower or len(candidates) == 1:
                        print(msg["verified_panel"].format(port=port))
                        return port
                    continue

        if attempt < 2:
            time.sleep(0.5)

    return None

def find_pio_command():
    # Check if 'pio' command is directly in PATH or available via Windows extension
    pio_cmd = "pio.exe" if platform.system() == "Windows" else "pio"
    if shutil_which(pio_cmd):
        return pio_cmd

    # Check common PlatformIO locations
    possible_paths = [
        os.path.expanduser("~/.platformio/penv/bin/pio"),
        os.path.expanduser("~/.platformio/penv/Scripts/pio.exe"),
        os.path.expanduser("~/.local/bin/pio"),
    ]
    for p in possible_paths:
        if os.path.exists(p):
            return p

    return pio_cmd

def shutil_which(cmd):
    import shutil
    return shutil.which(cmd)

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
        if arg in ["amp", "amplifier", "1"]:
            target_choice = "1"
        elif arg in ["panel", "s3", "2"]:
            target_choice = "2"

    if not target_choice:
        print(msg["menu_title"])
        print(msg["menu_opt1"])
        print(msg["menu_opt2"])
        try:
            user_input = input(msg["menu_prompt"]).strip()
        except (KeyboardInterrupt, EOFError):
            print("\n[INFO] Upload cancelled by user.")
            sys.exit(0)

        if not user_input or user_input == "1" or user_input.lower().startswith("amp"):
            target_choice = "1"
        elif user_input == "2" or user_input.lower().startswith("pan") or user_input.lower().startswith("s3"):
            target_choice = "2"
        else:
            print(msg["invalid_opt"])
            target_choice = "1"

    pio_executable = find_pio_command()

    if target_choice == "1":
        print(msg["scanning_amp"])
        target_port = verify_and_get_amp_port(msg)
        if not target_port:
            print(msg["amp_not_found"])
            print(msg["aborting"])
            sys.exit(1)

        print(msg["start_upload_amp"])
        try:
            project_dir = os.path.join(os.path.dirname(__file__), "..", "firmware", "amplifier")
            subprocess.check_call([pio_executable, "run", "-d", project_dir, "-t", "upload", "--upload-port", target_port])
            print(msg["upload_success_amp"])
        except FileNotFoundError:
            print(msg["cmd_not_found"])
            sys.exit(1)
        except subprocess.CalledProcessError as e:
            print(f"{msg['upload_fail']} {e.returncode}.")
            sys.exit(e.returncode)

    else:
        print(msg["scanning_panel"])
        target_port = verify_and_get_panel_port(msg)
        if not target_port:
            print(msg["panel_not_found"])
            print(msg["aborting"])
            sys.exit(1)

        print(msg["start_upload_panel"])
        try:
            project_dir = os.path.join(os.path.dirname(__file__), "..", "firmware", "panel")
            subprocess.check_call([pio_executable, "run", "-d", project_dir, "-t", "upload", "--upload-port", target_port])
            print(msg["upload_success_panel"])
        except FileNotFoundError:
            print(msg["cmd_not_found"])
            sys.exit(1)
        except subprocess.CalledProcessError as e:
            print(f"{msg['upload_fail']} {e.returncode}.")
            sys.exit(e.returncode)

if __name__ == "__main__":
    run_upload()
