#!/usr/bin/env python3
"""
Jacktor Audio - OTA Firmware Push Tool
--------------------------------------
Builds PlatformIO firmware and pushes the resulting binary (.bin) over HTTP POST
to the local Android App / OTA service.
"""

import argparse
import subprocess
import sys
import os
import urllib.request
import urllib.error
from pathlib import Path

MESSAGES = {
    "start_build": "[*] Starting PlatformIO build process in {target_dir}...",
    "build_success": "[+] Build completed successfully.",
    "pio_not_found": "[-] PlatformIO (pio) not found in system PATH. Ensure PlatformIO is installed and added to PATH.",
    "build_failed": "[-] Build failed. Check PlatformIO build log above.",
    "preparing_ota": "[*] Preparing OTA push to: {url}",
    "target_file": "[*] Target file: {bin_file}",
    "file_size": "[*] File size: {size_kb:.2f} KB",
    "sending_data": "[*] Sending binary payload...",
    "ota_success": "[+] OTA Push Successful!",
    "server_response": "    Android Server Response: {response}",
    "ota_failed": "[-] OTA Push Failed. HTTP Code: {code}",
    "connection_failed": "[-] Connection to Android App failed: {err}\n    Ensure Android phone is on, connected to the same Wi-Fi, and OTA service is active.",
    "unexpected_err": "[-] Unexpected error occurred: {err}",
    "invalid_target": "[-] Invalid target directory (platformio.ini missing): {target_path}",
    "bin_not_found": "[-] File firmware.bin not found after build completed."
}

def run_build(target_dir, msg):
    print(msg["start_build"].format(target_dir=target_dir))
    try:
        subprocess.run(["pio", "run"], cwd=target_dir, check=True)
        print(msg["build_success"])
    except FileNotFoundError:
        print(msg["pio_not_found"])
        sys.exit(1)
    except subprocess.CalledProcessError:
        print(msg["build_failed"])
        sys.exit(1)

def find_firmware_bin(target_dir):
    pio_dir = Path(target_dir) / ".pio" / "build"
    if not pio_dir.exists():
        return None

    for env_dir in pio_dir.iterdir():
        if env_dir.is_dir():
            bin_file = env_dir / "firmware.bin"
            if bin_file.exists():
                return bin_file
    return None

def push_ota(bin_file, ip, port, msg):
    url = f"http://{ip}:{port}/api/ota"
    print(msg["preparing_ota"].format(url=url))
    print(msg["target_file"].format(bin_file=bin_file))

    file_size = bin_file.stat().st_size
    print(msg["file_size"].format(size_kb=file_size / 1024.0))

    try:
        with open(bin_file, "rb") as f:
            data = f.read()

        req = urllib.request.Request(url, data=data)
        req.add_header('Content-Type', 'application/octet-stream')
        req.add_header('Content-Length', str(file_size))

        print(msg["sending_data"])
        response = urllib.request.urlopen(req, timeout=15)
        resp_data = response.read().decode('utf-8')

        if response.getcode() == 200:
            print(msg["ota_success"])
            print(msg["server_response"].format(response=resp_data))
        else:
            print(msg["ota_failed"].format(code=response.getcode()))
            print(f"    Response: {resp_data}")

    except urllib.error.URLError as e:
        print(msg["connection_failed"].format(err=e))
        sys.exit(1)
    except Exception as e:
        print(msg["unexpected_err"].format(err=e))
        sys.exit(1)

def main():
    msg = MESSAGES

    parser = argparse.ArgumentParser(description="Build & Push OTA firmware to Android App")
    parser.add_argument("--target", default="firmware/amplifier", help="PlatformIO project directory")
    parser.add_argument("--ip", required=True, help="Android device IP address in local network")
    parser.add_argument("--port", default="8080", help="Android HTTP service port (default: 8080)")

    args = parser.parse_args()

    target_path = Path(args.target).resolve()
    if not (target_path / "platformio.ini").exists():
        print(msg["invalid_target"].format(target_path=target_path))
        sys.exit(1)

    run_build(target_path, msg)

    bin_file = find_firmware_bin(target_path)
    if not bin_file:
        print(msg["bin_not_found"])
        sys.exit(1)

    push_ota(bin_file, args.ip, args.port, msg)

if __name__ == "__main__":
    main()
