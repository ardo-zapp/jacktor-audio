#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/discover.py
Jacktor Audio - Automatic LAN Panel Discovery & SSH Authentication Engine
Discovers Jacktor Audio Android Panel via:
  1. Fast cache check (.jacktor_panel_ip)
  2. UDP broadcast beacon (Port 42424)
  3. Concurrent local subnet sweep (Port 80 HTTP API & Port 22 SSH)
Provides transparent SSH authentication handling (SSH Key / Password via SSH_ASKPASS).
"""

import os
import sys
import time
import socket
import tempfile
import getpass
import subprocess
import urllib.request
import urllib.error
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

CACHE_FILE = Path(__file__).resolve().parent / ".jacktor_panel_ip"
BEACON_PORT = 42424
DEFAULT_SSH_PORT = 22
DEFAULT_SSH_USER = "root"

def get_local_subnets():
    """Returns a list of local IPv4 subnet base strings, e.g. ['192.168.0.', '192.168.1.']."""
    subnets = set()
    try:
        # Route-based subnet detection on Linux
        res = subprocess.run(["ip", "-4", "route", "show"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        for line in res.stdout.strip().splitlines():
            parts = line.split()
            if "proto kernel" in line and len(parts) >= 1:
                cidr = parts[0]
                if "/" in cidr and not cidr.startswith("127."):
                    ip_part = cidr.split("/")[0]
                    dots = ip_part.split(".")
                    if len(dots) == 4:
                        subnets.add(f"{dots[0]}.{dots[1]}.{dots[2]}.")
    except Exception:
        pass

    # Fallback to connected UDP socket method
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
        dots = local_ip.split(".")
        if len(dots) == 4 and not local_ip.startswith("127."):
            subnets.add(f"{dots[0]}.{dots[1]}.{dots[2]}.")
    except Exception:
        pass

    # Always include adjacent common home network subnets (192.168.0. / 192.168.1.)
    # because dual-band routers, repeaters, or APs frequently route between 192.168.0.x and 192.168.1.x
    if any(s.startswith("192.168.") for s in subnets):
        subnets.add("192.168.0.")
        subnets.add("192.168.1.")

    # Standard fallback if nothing found
    if not subnets:
        subnets.add("192.168.1.")
        subnets.add("192.168.0.")
    return sorted(list(subnets))

def verify_jacktor_ip(ip: str, timeout: float = 0.5) -> bool:
    """Verifies that the target IP is indeed the Jacktor Audio Panel."""
    # Method 1: Check native Jacktor API
    url = f"http://{ip}/cgi-bin/jacktor-api?action=status"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "JacktorDiscovery/1.0"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            content = resp.read().decode("utf-8", errors="ignore").lower()
            if "jacktor" in content or "rmx2195" in content or "realme" in content or "authenticated" in content or "telemetry" in content:
                return True
    except Exception:
        pass

    # Method 2: Check WebUI login.html or index.html
    for page in ["/login.html", "/index.html"]:
        try:
            req = urllib.request.Request(f"http://{ip}{page}", headers={"User-Agent": "JacktorDiscovery/1.0"})
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                content = resp.read().decode("utf-8", errors="ignore").lower()
                if "jacktor" in content or "audio panel" in content:
                    return True
        except Exception:
            pass

    return False

def listen_udp_beacon(timeout: float = 0.5) -> str:
    """Listens for the UDP broadcast beacon sent by service.sh on the Android panel."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(timeout)
    try:
        sock.bind(("", BEACON_PORT))
        t_end = time.time() + timeout
        while time.time() < t_end:
            try:
                data, addr = sock.recvfrom(1024)
                text = data.decode("utf-8", errors="ignore").strip()
                if text.startswith("JACKTOR_AUDIO_PANEL"):
                    # Format: JACKTOR_AUDIO_PANEL:<ip>:<port>:<model>
                    parts = text.split(":")
                    if len(parts) >= 2 and parts[1]:
                        return parts[1]
                    return addr[0]
            except socket.timeout:
                break
    except Exception:
        pass
    finally:
        sock.close()
    return ""

def _probe_host(ip: str) -> str:
    # First fast TCP check on port 80
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.25)
        res = s.connect_ex((ip, 80))
        s.close()
        if res == 0 and verify_jacktor_ip(ip, timeout=0.4):
            return ip
    except Exception:
        pass
    return ""

def scan_subnets(max_workers: int = 64) -> str:
    """Fast concurrent subnet sweep scanning for Jacktor Audio WebUI signature."""
    subnets = get_local_subnets()
    candidates = []
    for subnet in subnets:
        for i in range(1, 255):
            candidates.append(f"{subnet}{i}")

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = {executor.submit(_probe_host, ip): ip for ip in candidates}
        for future in as_completed(futures):
            res = future.result()
            if res:
                # Cancel remaining
                for f in futures:
                    f.cancel()
                return res
    return ""

def get_cached_ip() -> str:
    if CACHE_FILE.exists():
        try:
            ip = CACHE_FILE.read_text().strip()
            if ip and len(ip.split(".")) == 4:
                return ip
        except Exception:
            pass
    return ""

def set_cached_ip(ip: str):
    try:
        CACHE_FILE.write_text(ip.strip())
    except Exception:
        pass

def discover_jacktor_panel(interactive: bool = True) -> str:
    """
    Main discovery entrypoint:
    1. Tests cached IP.
    2. Listens for UDP beacon.
    3. Scans local subnet(s).
    4. Prompts user if not found.
    """
    # 1. Fast Cache Check
    cached_ip = get_cached_ip()
    if cached_ip:
        if verify_jacktor_ip(cached_ip, timeout=0.5):
            return cached_ip

    print("[DISCOVERY] Mendeteksi IP Panel Android Jacktor Audio di jaringan...", end="", flush=True)

    # 2. UDP Beacon
    beacon_ip = listen_udp_beacon(timeout=0.6)
    if beacon_ip and verify_jacktor_ip(beacon_ip, timeout=0.5):
        print(f" [OK: Beacon ({beacon_ip})]")
        set_cached_ip(beacon_ip)
        return beacon_ip

    # 3. Subnet Sweep
    scanned_ip = scan_subnets(max_workers=64)
    if scanned_ip:
        print(f" [OK: Subnet Scan ({scanned_ip})]")
        set_cached_ip(scanned_ip)
        return scanned_ip

    print(" [NOT FOUND]")

    # 4. Interactive fallback
    if interactive:
        print("\n[PERINGATAN] Panel Jacktor Audio tidak ditemukan secara otomatis.")
        print("Pastikan Panel Android terhubung ke Wi-Fi yang sama dengan komputer ini.")
        try:
            manual_ip = input("Masukkan IP Panel Android secara manual (atau Enter untuk batal): ").strip()
            if manual_ip:
                if verify_jacktor_ip(manual_ip, timeout=1.0):
                    print(f"[OK] Terverifikasi Jacktor Audio di {manual_ip}")
                    set_cached_ip(manual_ip)
                    return manual_ip
                else:
                    print(f"[WARN] IP {manual_ip} dimasukkan (belum terverifikasi response API).")
                    set_cached_ip(manual_ip)
                    return manual_ip
        except (KeyboardInterrupt, EOFError):
            print("\nDibatalkan.")
            sys.exit(0)

    return ""

class SshSession:
    """Manages an authenticated SSH/SCP session with auto-password handling."""
    def __init__(self, host: str, user: str = DEFAULT_SSH_USER, port: int = DEFAULT_SSH_PORT):
        self.host = host
        self.user = user
        self.port = port
        self.password = None
        self._askpass_file = None
        self.env = os.environ.copy()
        self._init_auth()

    def _init_auth(self):
        # 1. Test passwordless SSH key authentication
        test_cmd = [
            "ssh",
            "-o", "BatchMode=yes",
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            "-o", "ConnectTimeout=5",
            "-p", str(self.port),
            f"{self.user}@{self.host}",
            "echo OK"
        ]
        res = subprocess.run(test_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode == 0 and "OK" in res.stdout:
            # Key-based authentication works out of the box
            return

        # 2. Key authentication failed, request password from user
        print(f"\n[SSH AUTH] Autentikasi kunci gagal untuk {self.user}@{self.host}:{self.port}")
        default_pwd = "furry2026"
        try:
            pwd = getpass.getpass(f"Masukkan password SSH {self.user}@{self.host} (Default: {default_pwd}): ").strip()
            if not pwd:
                pwd = default_pwd
            self.password = pwd
        except (KeyboardInterrupt, EOFError):
            print("\nDibatalkan oleh pengguna.")
            sys.exit(0)

        # Setup SSH_ASKPASS script
        askpass_script = tempfile.NamedTemporaryFile("w", delete=False, prefix="jacktor_askpass_", suffix=".sh")
        askpass_script.write(f"#!/bin/sh\ncat << 'EOF_PWD'\n{self.password}\nEOF_PWD\n")
        askpass_script.close()
        os.chmod(askpass_script.name, 0o700)
        self._askpass_file = askpass_script.name

        self.env["SSH_ASKPASS"] = self._askpass_file
        self.env["SSH_ASKPASS_REQUIRE"] = "force"
        # Dummy display if needed by some OpenSSH versions
        if "DISPLAY" not in self.env:
            self.env["DISPLAY"] = ":0"

        # Verify password
        verify_cmd = self.get_ssh_base_cmd() + ["echo OK"]
        vres = subprocess.run(verify_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=self.env)
        if vres.returncode != 0 or "OK" not in vres.stdout:
            print(f"[ERROR] Autentikasi SSH gagal dengan password yang dimasukkan!\n{vres.stderr}")
            self.cleanup()
            sys.exit(1)
        print(f"[OK] Terautentikasi ke Panel Jacktor Audio ({self.user}@{self.host})")

    def get_ssh_base_cmd(self, pty: bool = False):
        base = [
            "ssh",
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            "-o", "LogLevel=ERROR",
            "-o", "ConnectTimeout=5",
            "-o", "ServerAliveInterval=2",
            "-o", "ServerAliveCountMax=2",
        ]
        if pty:
            base.append("-tt")
        base.extend([
            "-p", str(self.port),
            f"{self.user}@{self.host}"
        ])
        return base

    def get_scp_base_cmd(self):
        return [
            "scp",
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            "-o", "LogLevel=ERROR",
            "-P", str(self.port)
        ]

    def run_cmd(self, remote_cmd: str, check: bool = True, capture_output: bool = True, pty: bool = False):
        cmd = self.get_ssh_base_cmd(pty=pty) + [remote_cmd]
        return subprocess.run(
            cmd,
            stdout=subprocess.PIPE if capture_output else None,
            stderr=subprocess.PIPE if capture_output else None,
            text=True,
            check=check,
            env=self.env
        )

    def scp_push(self, local_path: str, remote_path: str, check: bool = True):
        # Stream file over SSH stdin into remote 'cat > remote_path'.
        # This completely eliminates dependency on 'scp' or 'sftp-server' binaries on embedded Android.
        cmd = self.get_ssh_base_cmd() + [f"cat > '{remote_path}'"]
        with open(local_path, "rb") as f:
            return subprocess.run(cmd, stdin=f, check=check, env=self.env)

    def scp_pull(self, remote_path: str, local_path: str, check: bool = True):
        cmd = self.get_ssh_base_cmd() + [f"cat '{remote_path}'"]
        with open(local_path, "wb") as f:
            return subprocess.run(cmd, stdout=f, check=check, env=self.env)

    def cleanup(self):
        if self._askpass_file and os.path.exists(self._askpass_file):
            try:
                os.remove(self._askpass_file)
            except Exception:
                pass

    def __del__(self):
        self.cleanup()

if __name__ == "__main__":
    ip = discover_jacktor_panel()
    if ip:
        print(f"\n[SUCCESS] Jacktor Audio Panel ditemukan di: {ip}")
        session = SshSession(ip)
        res = session.run_cmd("uname -a")
        print("Panel Kernel:", res.stdout.strip())
        session.cleanup()
    else:
        print("\n[FAIL] Panel tidak ditemukan.")
