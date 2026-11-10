#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/auth_client.py
Jacktor Audio - Secure API Authentication & Client Library

Handles machine-encrypted local credential persistence, auto-discovery,
and high-speed direct HTTP communication with jacktor-api (Port 80)
without requiring SSH (Port 22) or raw serial-bridge TCP (Port 7777).

Copyright (C) 2026 Ardo Zapp. All rights reserved.
Licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 (CC BY-NC-SA 4.0).
"""

import os
import sys
import json
import base64
import hashlib
import getpass
import urllib.request
import urllib.parse
import urllib.error
import socket
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
CONFIG_DIR = TOOLS_DIR / "config"
CRED_FILE = CONFIG_DIR / "credentials.enc"

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from discover import discover_jacktor_panel


def _get_machine_key() -> bytes:
    """Generate a machine-unique 32-byte key for local credential encryption."""
    identifiers = []
    # 1. Linux machine-id
    for mid_path in ("/etc/machine-id", "/var/lib/dbus/machine-id"):
        p = Path(mid_path)
        if p.is_file():
            try:
                identifiers.append(p.read_text().strip())
                break
            except Exception:
                pass

    # 2. Hostname and current user
    identifiers.append(socket.gethostname())
    identifiers.append(os.getenv("USER", "ardo"))
    identifiers.append("jacktor-audio-fw-v2-salt-2026")

    raw = ":".join(identifiers).encode("utf-8")
    return hashlib.sha256(raw).digest()


def _encrypt_data(plaintext: str) -> str:
    key = _get_machine_key()
    data = plaintext.encode("utf-8")
    # Stream XOR cipher with SHA-256 keystream expansion
    keystream = hashlib.sha256(key + b":keystream").digest()
    while len(keystream) < len(data):
        keystream += hashlib.sha256(key + keystream).digest()

    cipher_bytes = bytes([b ^ k for b, k in zip(data, keystream[:len(data)])])
    sig = hashlib.sha256(cipher_bytes + key).digest()[:8]
    return base64.b64encode(sig + cipher_bytes).decode("ascii")


def _decrypt_data(ciphertext: str) -> str:
    key = _get_machine_key()
    raw = base64.b64decode(ciphertext.encode("ascii"))
    if len(raw) < 8:
        raise ValueError("Invalid ciphertext length")
    sig, cipher_bytes = raw[:8], raw[8:]
    expected_sig = hashlib.sha256(cipher_bytes + key).digest()[:8]
    if sig != expected_sig:
        raise ValueError("Integrity check failed: credentials file modified or copied from another machine")

    keystream = hashlib.sha256(key + b":keystream").digest()
    while len(keystream) < len(cipher_bytes):
        keystream += hashlib.sha256(key + keystream).digest()

    data = bytes([b ^ k for b, k in zip(cipher_bytes, keystream[:len(cipher_bytes)])])
    return data.decode("utf-8")


class JacktorApiClient:
    """
    High-level API client for the Android Panel's native `jacktor-api` service.
    Replaces SSH and direct TCP serial-bridge connections for secure, high-speed operations.
    """

    def __init__(self, host: str = None, username: str = "root", password: str = None, token: str = None):
        self.host = host
        self.username = username
        self.password = password
        self.token = token
        self.port = 80
        self.base_url = f"http://{self.host}:{self.port}/cgi-bin/jacktor-api" if self.host else None

    @classmethod
    def load_saved_credentials(cls) -> dict:
        if not CRED_FILE.is_file():
            return {}
        try:
            content = CRED_FILE.read_text().strip()
            dec = _decrypt_data(content)
            return json.loads(dec)
        except Exception:
            return {}

    @classmethod
    def save_credentials(cls, data: dict):
        CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        txt = json.dumps(data)
        enc = _encrypt_data(txt)
        CRED_FILE.write_text(enc)
        CRED_FILE.chmod(0o600)

    @classmethod
    def authenticate(cls, host: str = None, interactive: bool = True) -> "JacktorApiClient":
        """
        Auto-discover panel if host not provided, retrieve saved encrypted credentials,
        verify token, or prompt user once for password and save encrypted token.
        """
        if not host:
            host = discover_jacktor_panel(interactive=interactive)
            if not host:
                raise ConnectionError("Panel Android Jacktor Audio tidak terdeteksi di jaringan.")

        creds = cls.load_saved_credentials()
        saved_host = creds.get("host")
        saved_user = creds.get("username", "root")
        saved_pass = creds.get("password")
        saved_tok = creds.get("token")

        client = cls(host=host, username=saved_user, password=saved_pass, token=saved_tok)

        # 1. Try saved token first
        if saved_tok:
            if client.check_auth():
                return client

        # 2. Try saved password if token expired
        if saved_pass:
            if client.login(saved_user, saved_pass):
                cls.save_credentials({
                    "host": host,
                    "username": saved_user,
                    "password": saved_pass,
                    "token": client.token
                })
                return client

        # 3. Prompt user for password if credentials missing or invalid
        if not interactive:
            raise PermissionError("Autentikasi Jacktor API gagal. Jalankan secara interaktif untuk memasukkan password.")

        print(f"\n[*] Memerlukan autentikasi ke Jacktor Audio Panel ({host})")
        while True:
            try:
                pwd = getpass.getpass(f"[*] Masukkan password [{saved_user}]: ")
            except (KeyboardInterrupt, EOFError):
                print("\n[INFO] Dibatalkan oleh pengguna.")
                sys.exit(0)

            if not pwd:
                print("[-] Password tidak boleh kosong.")
                continue

            if client.login(saved_user, pwd):
                print("[+] Autentikasi berhasil! Menyimpan kredensial terenkripsi di tools/config/credentials.enc...")
                cls.save_credentials({
                    "host": host,
                    "username": saved_user,
                    "password": pwd,
                    "token": client.token
                })
                return client
            else:
                print("[-] Password salah. Coba lagi.")

    def check_auth(self) -> bool:
        if not self.token:
            return False
        try:
            url = f"http://{self.host}:{self.port}/cgi-bin/jacktor-api?action=auth_check&token={urllib.parse.quote(self.token)}"
            req = urllib.request.Request(url, headers={"X-Jacktor-Token": self.token})
            with urllib.request.urlopen(req, timeout=3.0) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                return bool(data.get("authenticated", False))
        except Exception:
            return False

    def login(self, username: str, password: str) -> bool:
        try:
            url = f"http://{self.host}:{self.port}/cgi-bin/jacktor-api"
            payload = urllib.parse.urlencode({
                "action": "auth_login",
                "user": username,
                "pass": password
            }).encode("utf-8")
            req = urllib.request.Request(url, data=payload, headers={
                "Content-Type": "application/x-www-form-urlencoded"
            })
            with urllib.request.urlopen(req, timeout=4.0) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                if data.get("success") and data.get("token"):
                    self.username = username
                    self.password = password
                    self.token = data["token"]
                    self.base_url = f"http://{self.host}:{self.port}/cgi-bin/jacktor-api"
                    return True
        except Exception:
            pass
        return False

    def get_status(self) -> dict:
        tok = urllib.parse.quote(self.token or "")
        url = f"{self.base_url}?action=status&token={tok}"
        req = urllib.request.Request(url, headers={"X-Jacktor-Token": self.token or ""})
        with urllib.request.urlopen(req, timeout=4.0) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def send_amp_cmd(self, cmd_str: str) -> dict:
        tok = urllib.parse.quote(self.token or "")
        url = f"{self.base_url}?action=amp_cmd&cmd={urllib.parse.quote(cmd_str)}&token={tok}"
        req = urllib.request.Request(url, headers={"X-Jacktor-Token": self.token or ""})
        with urllib.request.urlopen(req, timeout=4.0) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def upload_firmware_file(self, filename: str, file_path: Path, progress_cb=None) -> dict:
        """Upload raw binary file to panel's /data/local/tmp/jacktor_upload/ directory."""
        size = file_path.stat().st_size
        url = f"{self.base_url}?action=upload_file&filename={urllib.parse.quote(filename)}&token={urllib.parse.quote(self.token)}"

        with open(file_path, "rb") as f:
            data = f.read()

        req = urllib.request.Request(url, data=data, headers={
            "Content-Type": "application/octet-stream",
            "Content-Length": str(size),
            "X-Jacktor-Token": self.token
        })
        with urllib.request.urlopen(req, timeout=30.0) as resp:
            res_data = json.loads(resp.read().decode("utf-8"))
            if progress_cb:
                progress_cb(filename, size, size)
            return res_data

    def flash_firmware(self, port: str = "/dev/ttyUSB0", baud: int = 921600, line_callback=None) -> bool:
        """Trigger flashing on panel and yield output line-by-line in real-time."""
        url = f"{self.base_url}?action=flash_firmware&port={urllib.parse.quote(port)}&baud={baud}&token={urllib.parse.quote(self.token)}"
        req = urllib.request.Request(url, headers={"X-Jacktor-Token": self.token})

        success = False
        with urllib.request.urlopen(req, timeout=120.0) as resp:
            for line_bytes in resp:
                line = line_bytes.decode("utf-8", errors="replace")
                if "[JACKTOR-FLASH-SUCCESS]" in line:
                    success = True
                if line_callback:
                    line_callback(line)
                else:
                    sys.stdout.write(line)
                    sys.stdout.flush()

        return success

    def stream_telemetry(self):
        """
        Connect to real-time Server-Sent Events stream from jacktor-api.
        Yields raw line / JSON dict packets as they arrive.
        """
        url = f"{self.base_url}?action=stream&token={urllib.parse.quote(self.token)}"
        req = urllib.request.Request(url, headers={"X-Jacktor-Token": self.token})

        with urllib.request.urlopen(req, timeout=None) as resp:
            for raw_line in resp:
                line = raw_line.decode("utf-8", errors="replace").strip()
                if line.startswith("data: "):
                    json_str = line[6:].strip()
                    if json_str:
                        yield json_str


if __name__ == "__main__":
    print("[*] Testing JacktorApiClient authentication...")
    client = JacktorApiClient.authenticate()
    print(f"[+] Client online on http://{client.host}:80")
    print(f"[*] Token: {client.token[:12]}...")
    status = client.get_status()
    print(f"[+] Device: {status.get('device_model')}, IP: {status.get('ip')}")
    print(f"[+] DAC Status: {status.get('dac', {}).get('status')}")
    print(f"[+] Track: {status.get('track', {}).get('title')} - {status.get('track', {}).get('artist')}")
    print(f"[+] Cover URL: {status.get('track', {}).get('cover')}")
