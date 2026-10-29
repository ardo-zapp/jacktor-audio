#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Jacktor Audio - Unified Project Build Automation Tool
Compiles:
  1. KernelPatch Module Driver: usb-serial.kpm (ARM64)
  2. Amplifier Firmware (ESP32): firmware/amplifier -> build/amplifier.bin
  3. Systemless Root Module (Magisk / APatch / KernelSU): build/jacktor-framework.zip
Outputs collected into the /build/ directory.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

# --- ANSI Color Formatting ---
RESET = "\033[0m"
BOLD = "\033[1m"
GREEN = "\033[32m"
CYAN = "\033[36m"
YELLOW = "\033[33m"
RED = "\033[31m"
MAGENTA = "\033[35m"


def print_banner():
    banner = f"""
{CYAN}{BOLD}==================================================================
           JACKTOR AUDIO - UNIFIED PROJECT BUILD ENGINE           
   KernelPatch Module + ESP32 Firmware + Systemless Android Module
=================================================================={RESET}
"""
    print(banner)


def get_project_root() -> Path:
    # tools/build.py -> parent of tools is project root
    return Path(__file__).resolve().parent.parent


def find_pio_executable() -> str:
    # 1. Check system PATH
    pio_path = shutil.which("pio")
    if pio_path:
        return pio_path

    # 2. Check PlatformIO penv paths
    home = Path.home()
    candidates = [
        home / ".platformio" / "penv" / "bin" / "pio",
        home / ".platformio" / "penv" / "Scripts" / "pio.exe",
        Path("/usr/local/bin/pio"),
        Path("/usr/bin/pio"),
    ]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return str(c)

    # 3. Check python3 -m platformio
    try:
        res = subprocess.run([sys.executable, "-m", "platformio", "--version"],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode == 0:
            return f"{sys.executable} -m platformio"
    except Exception:
        pass

    return ""


def format_size(size_bytes: int) -> str:
    if size_bytes < 1024:
        return f"{size_bytes} B"
    elif size_bytes < 1024 * 1024:
        return f"{size_bytes / 1024:.1f} KB"
    else:
        return f"{size_bytes / (1024 * 1024):.2f} MB"


def build_kpm(project_root: Path, build_dir: Path, clean: bool = False) -> Path:
    kpm_dir = project_root / "panel" / "realme-bengal" / "kpm" / "usb-serial"
    kpm_binary = kpm_dir / "usb-serial.kpm"

    print(f"\n{YELLOW}{BOLD}[1/3] Building KernelPatch Module Driver (usb-serial.kpm)...{RESET}")
    if not kpm_dir.is_dir():
        print(f"{RED}[ERROR] KPM source directory not found: {kpm_dir}{RESET}")
        sys.exit(1)

    t0 = time.time()

    # Clean previous build if requested
    if clean:
        print(f"  -> Cleaning KPM object files...")
        subprocess.run(["make", "clean"], cwd=kpm_dir, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Compile KPM
    print(f"  -> Executing Makefile with AArch64 Musl toolchain...")
    cmd = ["make", "clean", "all"] if clean else ["make", "all"]
    try:
        proc = subprocess.run(cmd, cwd=kpm_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if proc.returncode != 0:
            print(f"{RED}[ERROR] KPM compilation failed!{RESET}")
            print(proc.stderr or proc.stdout)
            sys.exit(proc.returncode)
    except FileNotFoundError:
        print(f"{RED}[ERROR] 'make' utility not found. Please install build-essential.{RESET}")
        sys.exit(1)

    if not kpm_binary.is_file():
        print(f"{RED}[ERROR] KPM binary was not generated: {kpm_binary}{RESET}")
        sys.exit(1)

    # Copy to external /build/ directory
    out_kpm = build_dir / "usb-serial.kpm"
    shutil.copy2(kpm_binary, out_kpm)

    # Also copy into the active framework module payload (kpm/ directory)
    mod_kpm_dir = project_root / "panel" / "realme-bengal" / "modul" / "jacktor-audio-framework" / "kpm"
    mod_kpm_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(kpm_binary, mod_kpm_dir / "usb-serial.kpm")

    elapsed = time.time() - t0
    size_str = format_size(out_kpm.stat().st_size)
    print(f"{GREEN}✓ KPM build successful:{RESET} {out_kpm.name} ({size_str}) in {elapsed:.2f}s")
    print(f"  -> Bundled into module: {mod_kpm_dir / 'usb-serial.kpm'}")
    return out_kpm


def build_firmware(project_root: Path, build_dir: Path, pio_cmd: str, clean: bool = False) -> Path:
    fw_dir = project_root / "firmware" / "amplifier"
    print(f"\n{YELLOW}{BOLD}[2/3] Building Amplifier Firmware (ESP32 / FreeRTOS)...{RESET}")
    if not fw_dir.is_dir():
        print(f"{RED}[ERROR] Firmware directory not found: {fw_dir}{RESET}")
        sys.exit(1)

    if not pio_cmd:
        print(f"{RED}[ERROR] PlatformIO Core (pio) was not found!{RESET}")
        print("  Please install PlatformIO or run: python3 tools/install_pio.py")
        sys.exit(1)

    t0 = time.time()
    pio_args = pio_cmd.split()

    if clean:
        print("  -> Cleaning PlatformIO build cache...")
        subprocess.run(pio_args + ["run", "-d", str(fw_dir), "-t", "clean"],
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    print(f"  -> Compiling firmware with PlatformIO ({pio_cmd})...")
    proc = subprocess.run(pio_args + ["run", "-d", str(fw_dir)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        print(f"{RED}[ERROR] Firmware compilation failed!{RESET}")
        print(proc.stdout)
        print(proc.stderr)
        sys.exit(proc.returncode)

    pio_out = fw_dir / ".pio" / "build" / "esp32dev"
    src_firmware = pio_out / "firmware.bin"
    if not src_firmware.is_file():
        print(f"{RED}[ERROR] Target firmware binary not found: {src_firmware}{RESET}")
        sys.exit(1)

    # Copy to build/amplifier.bin as requested
    out_amplifier = build_dir / "amplifier.bin"
    shutil.copy2(src_firmware, out_amplifier)

    # Also copy supporting binaries (bootloader, partitions, ota_data) for complete standalone flashing
    for extra in ["bootloader.bin", "partitions.bin", "ota_data_initial.bin"]:
        src_extra = pio_out / extra
        if src_extra.is_file():
            shutil.copy2(src_extra, build_dir / extra)

    elapsed = time.time() - t0
    size_str = format_size(out_amplifier.stat().st_size)
    print(f"{GREEN}✓ Firmware build successful:{RESET} {out_amplifier.name} ({size_str}) in {elapsed:.2f}s")
    return out_amplifier


def build_module_zip(project_root: Path, build_dir: Path) -> Path:
    mod_dir = project_root / "panel" / "realme-bengal" / "modul" / "jacktor-audio-framework"
    print(f"\n{YELLOW}{BOLD}[3/3] Packaging Android System Module (Magisk / APatch / KSU)...{RESET}")

    if not mod_dir.is_dir():
        print(f"{RED}[ERROR] Module directory not found: {mod_dir}{RESET}")
        sys.exit(1)

    # Verify that bundled KPM exists
    kpm_in_module = mod_dir / "kpm" / "usb-serial.kpm"
    if not kpm_in_module.is_file():
        print(f"{RED}[ERROR] Bundled KPM driver missing in module: {kpm_in_module}{RESET}")
        print("  Please build the KPM driver first.")
        sys.exit(1)

    t0 = time.time()
    zip_path = build_dir / "jacktor-framework.zip"
    zip_path_alt = build_dir / "jacktor-audio-framework.zip"

    # Remove existing zip files
    if zip_path.exists():
        zip_path.unlink()
    if zip_path_alt.exists():
        zip_path_alt.unlink()

    # Create ZIP archive
    file_count = 0
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for root, dirs, files in os.walk(mod_dir):
            for file in files:
                # Omit temporary files or READMEs inside module payload if any
                if file.endswith((".pyc", ".o", ".log", ".bak", "~")) or file == ".DS_Store":
                    continue
                full_path = Path(root) / file
                rel_path = full_path.relative_to(mod_dir)

                # Preserve Linux symlinks and executable permissions inside zip archive
                if os.path.islink(full_path):
                    link_target = os.readlink(full_path)
                    zinfo = zipfile.ZipInfo(str(rel_path))
                    zinfo.create_system = 3  # Unix
                    zinfo.external_attr = 0o120777 << 16  # S_IFLNK | 0777
                    zf.writestr(zinfo, link_target)
                else:
                    st = os.stat(full_path)
                    zinfo = zipfile.ZipInfo.from_file(full_path, arcname=str(rel_path))
                    zinfo.create_system = 3  # Unix
                    # Set external attributes to preserve 0755 permissions for scripts/binaries
                    if (st.st_mode & 0o111) or full_path.suffix in [".sh", ".init", ".prop"] or "bin" in full_path.parts:
                        zinfo.external_attr = (0o755 << 16) | 0o100000
                    else:
                        zinfo.external_attr = (0o644 << 16) | 0o100000

                    with open(full_path, "rb") as f:
                        zf.writestr(zinfo, f.read())
                file_count += 1

    # Create copy under both standard names
    shutil.copy2(zip_path, zip_path_alt)

    elapsed = time.time() - t0
    size_str = format_size(zip_path.stat().st_size)
    print(f"{GREEN}✓ Module packaged successfully:{RESET} {zip_path.name} ({size_str}, {file_count} files) in {elapsed:.2f}s")
    print(f"  -> Also available as: {zip_path_alt.name}")
    return zip_path


def print_summary(build_dir: Path, total_time: float):
    print(f"\n{CYAN}{BOLD}=================================================================={RESET}")
    print(f"{GREEN}{BOLD}                     BUILD COMPLETE (SUCCESS)                     {RESET}")
    print(f"{CYAN}{BOLD}=================================================================={RESET}")
    print(f"Total Elapsed Time: {total_time:.2f} seconds\n")
    print(f"{BOLD}Artifacts generated in {build_dir.resolve()}:{RESET}")

    artifacts = [
        ("jacktor-framework.zip", "Flashable Magisk/APatch/KSU Module (Includes KPM)"),
        ("jacktor-audio-framework.zip", "Alias for jacktor-framework.zip"),
        ("usb-serial.kpm", "KernelPatch Module Driver (/dev/ttyUSB0..7)"),
        ("amplifier.bin", "ESP32 Firmware binary (Flash address: 0x10000)"),
        ("bootloader.bin", "ESP32 Bootloader binary (Flash address: 0x1000)"),
        ("partitions.bin", "ESP32 Partitions table (Flash address: 0x8000)"),
        ("ota_data_initial.bin", "ESP32 OTA Data Initial binary (Flash address: 0xe000)"),
    ]

    for fname, desc in artifacts:
        fpath = build_dir / fname
        if fpath.is_file():
            sz = format_size(fpath.stat().st_size)
            print(f"  • {BOLD}{fname:<28}{RESET} [{sz:>8}] -> {desc}")

    print(f"\n{BOLD}Deployment Quick Commands:{RESET}")
    print(f"  1. Flash Android Module via ADB:")
    print(f"     adb push {build_dir / 'jacktor-framework.zip'} /data/local/tmp/")
    print(f"     adb shell su -c 'magisk --install-module /data/local/tmp/jacktor-framework.zip'")
    print(f"     # Or for APatch: adb shell su -c '/data/adb/ap/bin/apd --install /data/local/tmp/jacktor-framework.zip'")
    print(f"  2. Flash ESP32 Firmware over ADB OTG:")
    print(f"     python3 tools/upload.py adb")
    print(f"  3. Flash ESP32 Firmware via Direct USB:")
    print(f"     python3 tools/upload.py usb\n")


def main():
    parser = argparse.ArgumentParser(
        description="Jacktor Audio - Unified Build Automation Tool",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("--clean", action="store_true", help="Perform a clean build of all components")
    parser.add_argument("--kpm-only", action="store_true", help="Build only the KernelPatch Module driver")
    parser.add_argument("--fw-only", action="store_true", help="Build only the ESP32 Amplifier firmware")
    parser.add_argument("--module-only", action="store_true", help="Package only the Android Framework ZIP module")
    parser.add_argument("--out-dir", type=str, default="", help="Custom output directory (default: <root>/build)")

    args = parser.parse_args()

    project_root = get_project_root()
    build_dir = Path(args.out_dir) if args.out_dir else project_root / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    print_banner()
    t_start = time.time()
    pio_cmd = find_pio_executable()

    build_all = not (args.kpm_only or args.fw_only or args.module_only)

    if build_all or args.kpm_only:
        build_kpm(project_root, build_dir, clean=args.clean)

    if build_all or args.fw_only:
        build_firmware(project_root, build_dir, pio_cmd, clean=args.clean)

    if build_all or args.module_only:
        # If building module-only, make sure kpm was built or already exists
        kpm_in_module = project_root / "panel" / "realme-bengal" / "modul" / "jacktor-audio-framework" / "kpm" / "usb-serial.kpm"
        if not kpm_in_module.is_file():
            print(f"  -> Building prerequisite KPM driver for module bundle...")
            build_kpm(project_root, build_dir, clean=args.clean)
        build_module_zip(project_root, build_dir)

    total_time = time.time() - t_start
    print_summary(build_dir, total_time)


if __name__ == "__main__":
    main()
