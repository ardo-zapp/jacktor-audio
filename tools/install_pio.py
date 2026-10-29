#!/usr/bin/env python3
import os
import subprocess
import sys
import shutil
import platform
import urllib.request

# Explicitly ensure PlatformIO paths are added to PATH across Linux, macOS, and Windows
penv_bin = os.path.expanduser("~/.platformio/penv/bin")
penv_scripts = os.path.expanduser("~/.platformio/penv/Scripts")
local_bin = os.path.expanduser("~/.local/bin")

for p in [penv_scripts, penv_bin, local_bin]:
    if os.path.exists(p) and p not in os.environ.get("PATH", ""):
        os.environ["PATH"] = p + os.pathsep + os.environ.get("PATH", "")

MESSAGES = {
    "check": "Checking PlatformIO (pio) installation...",
    "installed": "[OK] PlatformIO is already installed and available in PATH.",
    "found_penv": "[OK] PlatformIO found in penv directory: {path}",
    "downloading": "[INFO] Downloading official get-platformio.py installer...",
    "installing": "[INFO] Running official PlatformIO Core installer via {python_exec}...",
    "pio_success": "[OK] PlatformIO Core installed successfully via get-platformio.py!",
    "pio_fail": "[ERROR] Failed to install PlatformIO Core:"
}

def check_command(cmd):
    return shutil.which(cmd) is not None

def install_pio():
    msg = MESSAGES

    print(msg["check"])

    cmd_name = "pio.exe" if platform.system() == "Windows" else "pio"
    if check_command(cmd_name) or check_command("pio"):
        print(msg["installed"])
        return True

    # Check Windows & Linux penv paths
    possible_pio_paths = [
        os.path.expanduser("~/.platformio/penv/Scripts/pio.exe"),
        os.path.expanduser("~/.platformio/penv/bin/pio"),
        os.path.expanduser("~/.local/bin/pio"),
    ]

    for p in possible_pio_paths:
        if os.path.exists(p):
            print(msg["found_penv"].format(path=p))
            # Add containing directory to current process PATH
            p_dir = os.path.dirname(p)
            if p_dir not in os.environ.get("PATH", ""):
                os.environ["PATH"] = p_dir + os.pathsep + os.environ.get("PATH", "")
            return True

    print(msg["downloading"])
    installer_url = "https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py"
    installer_path = os.path.join(os.path.dirname(__file__), "get-platformio.py")

    try:
        try:
            req = urllib.request.Request(
                installer_url,
                headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'}
            )
            with urllib.request.urlopen(req) as response, open(installer_path, 'wb') as out_file:
                shutil.copyfileobj(response, out_file)
        except Exception:
            subprocess.check_call(["wget", "-O", installer_path, installer_url])

        py_exec = sys.executable or ("python.exe" if platform.system() == "Windows" else "python3")
        print(msg["installing"].format(python_exec=py_exec))

        subprocess.check_call([py_exec, installer_path])

        if os.path.exists(installer_path):
            os.remove(installer_path)

        # Re-update PATH after successful installation
        for p in [penv_scripts, penv_bin, local_bin]:
            if os.path.exists(p) and p not in os.environ.get("PATH", ""):
                os.environ["PATH"] = p + os.pathsep + os.environ.get("PATH", "")

        print(msg["pio_success"])
        return True
    except Exception as e:
        print(f"{msg['pio_fail']} {e}")
        if os.path.exists(installer_path):
            try:
                os.remove(installer_path)
            except Exception:
                pass
        return False

if __name__ == "__main__":
    if not install_pio():
        sys.exit(1)
