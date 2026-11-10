#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Jacktor Audio - Unified Project Build Automation Tool
Compiles:
  1. KernelPatch Module Driver: usb-serial.kpm (ARM64)
  2. Native ARM64 System Binaries: jacktor-api, serial-bridge, esputil, socat, etc.
  3. Amplifier Firmware (ESP32): firmware/amplifier -> build/amplifier.bin
  4. Systemless Root Module (Magisk / APatch / KernelSU): build/jacktor-framework.zip
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

NATIVE_BIN_TARGETS = [
    ("jacktor-api", "High-speed unified REST/CGI/SSE API and control engine"),
    ("serial-bridge", "Multi-client telemetry bridge (socat multiplexer)"),
    ("esputil", "Standalone ESP32 utility and flasher"),
    ("amp-status", "Amplifier hardware and audio diagnostics"),
    ("aplay", "ALSA compatibility player wrapper"),
    ("audio-test", "Hardware tone and frequency sweep generator"),
    ("clear", "Terminal ANSI screen and scrollback clear"),
    ("jacktor-motd", "SSH banner and system telemetry generator"),
    ("otg-trigger", "USB OTG state switcher and PMIC detector"),
    ("socat", "Multipurpose byte-stream relay (ARM64)"),
]


def print_banner():
    banner = f"""
{CYAN}{BOLD}==================================================================
           JACKTOR AUDIO - UNIFIED PROJECT BUILD ENGINE           
   KernelPatch Module + Native ARM64 Binaries + ESP32 Firmware    
                + Systemless Android Root Module                  
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

    print(f"\n{YELLOW}{BOLD}[1/4] Building KernelPatch Module Driver (usb-serial.kpm)...{RESET}")
    if not kpm_dir.is_dir():
        print(f"{RED}[ERROR] KPM source directory not found: {kpm_dir}{RESET}")
        sys.exit(1)

    t0 = time.time()

    # Verify or fetch KernelPatch tree if missing in /tmp
    kp_kernel_dir = Path("/tmp/KernelPatch/kernel")
    if not kp_kernel_dir.is_dir():
        print(f"  -> Fetching KernelPatch kernel headers to /tmp/KernelPatch...")
        subprocess.run(
            ["git", "clone", "--depth", "1", "https://github.com/bmax121/KernelPatch", "/tmp/KernelPatch"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False
        )

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

    # Clean up build artifact from source directory (keep source clean)
    if kpm_binary.is_file():
        kpm_binary.unlink()
    subprocess.run(["make", "clean"], cwd=kpm_dir, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    elapsed = time.time() - t0
    size_str = format_size(out_kpm.stat().st_size)
    print(f"{GREEN}✓ KPM build successful:{RESET} {out_kpm.name} ({size_str}) in {elapsed:.2f}s")
    print(f"  -> Bundled into module: {mod_kpm_dir / 'usb-serial.kpm'}")
    return out_kpm


def build_native_binaries(project_root: Path, build_dir: Path, clean: bool = False, target_name: str = None) -> list:
    bin_base_dir = project_root / "panel" / "realme-bengal" / "bin"
    mod_bin_dir = project_root / "panel" / "realme-bengal" / "modul" / "jacktor-audio-framework" / "system" / "bin"
    cgi_dir = project_root / "panel" / "realme-bengal" / "modul" / "jacktor-audio-framework" / "webroot" / "cgi-bin"

    mod_bin_dir.mkdir(parents=True, exist_ok=True)
    cgi_dir.mkdir(parents=True, exist_ok=True)

    targets = NATIVE_BIN_TARGETS
    if target_name:
        targets = [t for t in NATIVE_BIN_TARGETS if t[0] == target_name]
        if not targets:
            print(f"{RED}[ERROR] Unknown native binary target: {target_name}{RESET}")
            sys.exit(1)

    step_title = f"Target: {target_name}" if target_name else "All Tools (jacktor-api, serial-bridge, esputil, socat, etc.)"
    print(f"\n{YELLOW}{BOLD}[2/4] Building Native ARM64 System Binaries [{step_title}]...{RESET}")

    built_paths = []
    t_start = time.time()

    for bin_name, desc in targets:
        src_dir = bin_base_dir / bin_name
        if not src_dir.is_dir():
            print(f"  {YELLOW}⚠ Source directory for {bin_name} not found, skipping.{RESET}")
            continue

        print(f"  -> Compiling {BOLD}{bin_name:<16}{RESET} ({desc})...", end="", flush=True)
        t0 = time.time()

        cmd = ["make", "clean", "all", "install", "clean"] if clean else ["make", "all", "install", "clean"]
        proc = subprocess.run(cmd, cwd=src_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if proc.returncode != 0:
            print(f" {RED}[FAILED]{RESET}")
            print(proc.stderr or proc.stdout)
            sys.exit(proc.returncode)

        # The Makefile 'install' target installs into ../../modul/.../system/bin/<bin_name>
        installed_mod_bin = mod_bin_dir / bin_name
        if not installed_mod_bin.is_file():
            print(f" {RED}[MISSING: {installed_mod_bin}]{RESET}")
            sys.exit(1)

        # Set executable permissions
        installed_mod_bin.chmod(0o755)

        # Copy to /build/ directory
        out_build_bin = build_dir / bin_name
        shutil.copy2(installed_mod_bin, out_build_bin)
        out_build_bin.chmod(0o755)

        built_paths.append(out_build_bin)
        elapsed = time.time() - t0
        sz_str = format_size(out_build_bin.stat().st_size)
        print(f" {GREEN}[OK]{RESET} {sz_str} in {elapsed:.2f}s")

    # Ensure cgi-bin symlink for jacktor-api is properly configured
    cgi_api_symlink = cgi_dir / "jacktor-api"
    legacy_api_sh = cgi_dir / "api.sh"
    legacy_api = cgi_dir / "api"
    if legacy_api_sh.exists() or legacy_api_sh.is_symlink():
        legacy_api_sh.unlink()
    if legacy_api.exists() or legacy_api.is_symlink():
        legacy_api.unlink()

    if not cgi_api_symlink.exists():
        try:
            cgi_api_symlink.symlink_to("../../system/bin/jacktor-api")
        except Exception:
            pass

    total_el = time.time() - t_start
    print(f"{GREEN}✓ Native ARM64 binaries compiled & installed ({len(built_paths)} binaries) in {total_el:.2f}s{RESET}")
    return built_paths


def build_firmware(project_root: Path, build_dir: Path, pio_cmd: str, clean: bool = False) -> Path:
    fw_dir = project_root / "firmware" / "amplifier"
    print(f"\n{YELLOW}{BOLD}[3/4] Building Amplifier Firmware (ESP32 / FreeRTOS)...{RESET}")
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

    # Copy to build/amplifier.bin
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
    print(f"\n{YELLOW}{BOLD}[4/4] Packaging Android System Module (Magisk / APatch / KSU)...{RESET}")

    if not mod_dir.is_dir():
        print(f"{RED}[ERROR] Module directory not found: {mod_dir}{RESET}")
        sys.exit(1)

    # 1. Verify that bundled KPM exists
    kpm_in_module = mod_dir / "kpm" / "usb-serial.kpm"
    if not kpm_in_module.is_file():
        print(f"{RED}[ERROR] Bundled KPM driver missing in module: {kpm_in_module}{RESET}")
        print("  Please build the KPM driver first.")
        sys.exit(1)

    # 2. Verify that critical system binaries exist
    critical_bins = ["jacktor-api", "serial-bridge", "esputil"]
    for b in critical_bins:
        b_path = mod_dir / "system" / "bin" / b
        if not b_path.is_file():
            print(f"{RED}[ERROR] Critical binary missing in module: {b_path}{RESET}")
            print(f"  Please build native binaries first (e.g. python3 tools/build.py --bin-only).")
            sys.exit(1)

    # 3. Verify cgi-bin symlink for jacktor-api
    cgi_dir = mod_dir / "webroot" / "cgi-bin"
    cgi_api = cgi_dir / "jacktor-api"
    legacy_api_sh = cgi_dir / "api.sh"
    legacy_api = cgi_dir / "api"
    if legacy_api_sh.exists() or legacy_api_sh.is_symlink():
        legacy_api_sh.unlink()
    if legacy_api.exists() or legacy_api.is_symlink():
        legacy_api.unlink()

    if not cgi_api.exists() and not cgi_api.is_symlink():
        cgi_api.symlink_to("../../system/bin/jacktor-api")

    t0 = time.time()
    zip_path = build_dir / "jacktor-framework.zip"
    zip_path_alt = build_dir / "jacktor-audio-framework.zip"

    # Remove existing zip files
    if zip_path.exists():
        zip_path.unlink()
    if zip_path_alt.exists():
        zip_path_alt.unlink()

    # Create ZIP archive preserving Unix symlinks and permissions
    file_count = 0
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for root, dirs, files in os.walk(mod_dir):
            for file in files:
                # Omit temporary files, editor backups, or object files
                if file.endswith((".pyc", ".o", ".log", ".bak", "~")) or file == ".DS_Store":
                    continue
                full_path = Path(root) / file
                rel_path = full_path.relative_to(mod_dir)

                # Omit symlinks from ZIP archive as customize.sh creates them dynamically on device
                if os.path.islink(full_path) or full_path.is_symlink():
                    continue

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

    # Create copy under alternate standard name
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
        ("jacktor-framework.zip", "Flashable Magisk/APatch/KSU Module (Includes KPM, WebUI, and all binaries)"),
        ("jacktor-audio-framework.zip", "Alias for jacktor-framework.zip"),
        ("usb-serial.kpm", "KernelPatch Module Driver (/dev/ttyUSB0..7)"),
        ("jacktor-api", "Unified High-Speed REST/CGI/SSE API and control engine (Port 80)"),
        ("serial-bridge", "Multiplexed Serial Telemetry Bridge daemon (Port 7777)"),
        ("esputil", "Standalone ESP32 Utility and Flasher binary (ARM64 Musl)"),
        ("socat", "Multipurpose byte-stream relay binary (ARM64 Musl)"),
        ("amp-status", "Amplifier hardware and audio diagnostics binary"),
        ("aplay", "ALSA compatibility player wrapper binary"),
        ("audio-test", "Safe audio tone and frequency sweep generator binary"),
        ("clear", "Terminal ANSI screen and scrollback clear binary"),
        ("jacktor-motd", "SSH banner and system telemetry generator binary"),
        ("otg-trigger", "USB OTG state switcher binary"),
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

    print(f"\n{BOLD}Deployment & Operations Quick Commands:{RESET}")
    print(f"  1. Flash Android Module via ADB:")
    print(f"     adb push {build_dir / 'jacktor-framework.zip'} /data/local/tmp/")
    print(f"     adb shell su -c 'magisk --install-module /data/local/tmp/jacktor-framework.zip'")
    print(f"     # Or for APatch: adb shell su -c '/data/adb/ap/bin/apd --install /data/local/tmp/jacktor-framework.zip'")
    print(f"  2. Flash ESP32 Firmware over High-Speed Jacktor API (Port 80 @ 921,600 bps):")
    print(f"     python3 tools/upload.py panel")
    print(f"  3. Stream Telemetry & Monitor Audio Spectrum via Jacktor API:")
    print(f"     python3 tools/cmd.py panel")
    print(f"  4. Sync Module & WebUI Live to Android Panel:")
    print(f"     python3 tools/sync_module.py\n")


def main():
    parser = argparse.ArgumentParser(
        description="Jacktor Audio - Unified Build Automation Tool",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("--clean", action="store_true", help="Perform a clean build of all components")
    parser.add_argument("--kpm-only", action="store_true", help="Build only the KernelPatch Module driver")
    parser.add_argument("--bin-only", action="store_true", help="Build all native ARM64 system binaries")
    parser.add_argument("--api-only", action="store_true", help="Build only the jacktor-api binary")
    parser.add_argument("--esputil-only", action="store_true", help="Build only the esputil binary")
    parser.add_argument("--bridge-only", action="store_true", help="Build only the serial-bridge binary")
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

    specific_target = (
        args.kpm_only or args.bin_only or args.api_only or 
        args.esputil_only or args.bridge_only or args.fw_only or args.module_only
    )
    build_all = not specific_target

    if build_all or args.kpm_only:
        build_kpm(project_root, build_dir, clean=args.clean)

    if build_all or args.bin_only:
        build_native_binaries(project_root, build_dir, clean=args.clean)
    elif args.api_only:
        build_native_binaries(project_root, build_dir, clean=args.clean, target_name="jacktor-api")
    elif args.esputil_only:
        build_native_binaries(project_root, build_dir, clean=args.clean, target_name="esputil")
    elif args.bridge_only:
        build_native_binaries(project_root, build_dir, clean=args.clean, target_name="serial-bridge")

    if build_all or args.fw_only:
        build_firmware(project_root, build_dir, pio_cmd, clean=args.clean)

    if build_all or args.module_only:
        # If building module-only, make sure kpm exists
        kpm_in_module = project_root / "panel" / "realme-bengal" / "modul" / "jacktor-audio-framework" / "kpm" / "usb-serial.kpm"
        if not kpm_in_module.is_file():
            print(f"  -> Building prerequisite KPM driver for module bundle...")
            build_kpm(project_root, build_dir, clean=args.clean)

        # Make sure critical binaries exist
        for req_bin in ["jacktor-api", "serial-bridge", "esputil"]:
            b_path = project_root / "panel" / "realme-bengal" / "modul" / "jacktor-audio-framework" / "system" / "bin" / req_bin
            if not b_path.is_file():
                print(f"  -> Building prerequisite binary '{req_bin}' for module bundle...")
                build_native_binaries(project_root, build_dir, clean=args.clean, target_name=req_bin)

        build_module_zip(project_root, build_dir)

    total_time = time.time() - t_start
    print_summary(build_dir, total_time)


if __name__ == "__main__":
    main()
