# -*- coding: utf-8 -*-

"""
ESP32 Monitor - Jacktor Audio Amplifier (curses TUI)

Layout:
  y=0         : status bar (header, realtime)
  y=1..h-4    : log window (scrollable dengan PgUp/PgDn / ↑/↓)
  y=h-3       : input bar ("> " + command)
  y=h-2       : baris kosong pemisah
  y=h-1       : VU bar (footer, realtime)
"""

import serial
import serial.tools.list_ports
import json
import sys
import time
from datetime import datetime
import platform
import curses


class ESP32Monitor:
    FW_IDENTIFIER = "JACKTOR AUDIO"

    def __init__(self, stdscr, port=None, baud=921600):
        self.stdscr = stdscr
        self.port = port
        self.baud = baud

        # Serial
        self.ser = None
        self.running = False
        self.debug_mode = False

        # Status cache (header + VU)
        self.status = {
            "fw_ver": "N/A",
            "time": "N/A",
            "power": "OFF",
            "voltage": 0.0,
            "v12": 0.0,
            "temp": 0.0,
            "input": "aux",
            "speaker": "small",
            "fan_mode": "auto",
            "fan_duty": 0,
            "vu": 0,
            "errors": [],
        }

        # Layout
        self.height, self.width = self.stdscr.getmaxyx()
        # Minimal safety
        if self.height < 8:
            raise RuntimeError("Terminal terlalu kecil, butuh tinggi minimal 8 baris")

        self.status_y = 0
        self.log_y = 1
        self.input_y = self.height - 3   # PROMPT di baris ketiga dari bawah
        self.sep_y = self.height - 2     # baris kosong pemisah
        self.vu_y = self.height - 1      # VU di baris paling bawah
        self.log_h = self.height - 4     # 1 header + 3 baris bawah

        self.log_w = self.width
        self.log_win = self.stdscr.derwin(self.log_h, self.log_w, self.log_y, 0)

        # Log buffer + scroll
        self.log_lines = []
        self.max_log_lines = 1000
        self.log_offset = 0  # 0 = paling bawah (auto-follow), >0 = scrolled up

        # Input buffer
        self.input_buf = ""

        # Throttle status/VU redraw
        self.last_status_update = 0.0
        self.status_interval = 0.05  # ~20 FPS

        # Serial RX buffer
        self.rx_buffer = ""

    # -------------------------------------------------------------------------
    # UI helpers
    # -------------------------------------------------------------------------

    def ui_init(self):
        try:
            curses.curs_set(1)
        except Exception:
            pass
        self.stdscr.nodelay(True)
        self.stdscr.keypad(True)
        self.draw_frame()
        self.log_print("Jacktor Audio Monitor (curses UI)")
        self.log_print(f"OS: {platform.system()}  Python: {sys.version.split()[0]}")
        self.log_print("Type 'help' for commands.")
        self.log_print("")
        self.refresh_all()

    def draw_frame(self):
        self.stdscr.erase()

        # Status bar (header)
        self.stdscr.move(self.status_y, 0)
        self.stdscr.addstr(" " * (self.width - 1), curses.A_REVERSE)

        # Input bar
        self.stdscr.move(self.input_y, 0)
        self.stdscr.clrtoeol()
        self.stdscr.addstr(self.input_y, 0, "> ")

        # Separator (kosong / bisa diganti garis)
        self.stdscr.move(self.sep_y, 0)
        self.stdscr.clrtoeol()
        # contoh kalau mau garis: self.stdscr.hline(self.sep_y, 0, ord('-'), self.width - 1)

        # VU bar
        self.stdscr.move(self.vu_y, 0)
        self.stdscr.clrtoeol()

    def refresh_all(self):
        self.draw_status_bar(force=True)
        self.draw_vu_bar(force=True)
        self.draw_log()
        self.draw_input()
        self.stdscr.refresh()

    def draw_log(self):
        self.log_win.erase()

        total = len(self.log_lines)
        # Hitung index start berdasarkan offset scroll
        if total <= self.log_h:
            start = 0
        else:
            max_offset = max(0, total - self.log_h)
            if self.log_offset > max_offset:
                self.log_offset = max_offset
            start = max(0, total - self.log_h - self.log_offset)

        end = min(total, start + self.log_h)
        visible = self.log_lines[start:end]

        for i, line in enumerate(visible):
            txt = line[: self.log_w - 1]
            try:
                self.log_win.addstr(i, 0, txt)
            except curses.error:
                pass

        self.log_win.noutrefresh()

    def log_print(self, text, with_ts=True):
        if with_ts:
            ts = datetime.now().strftime("%H:%M:%S")
            line = f"[{ts}] {text}"
        else:
            line = text
        self.log_lines.append(line)
        if len(self.log_lines) > self.max_log_lines:
            self.log_lines = self.log_lines[-self.max_log_lines :]

        # Auto-follow hanya kalau lagi di paling bawah
        if self.log_offset == 0:
            self.draw_log()

    def draw_input(self):
        prompt = "> "
        self.stdscr.move(self.input_y, 0)
        self.stdscr.clrtoeol()
        self.stdscr.addstr(self.input_y, 0, prompt)
        shown = self.input_buf[-(self.width - len(prompt) - 1) :]
        try:
            self.stdscr.addstr(self.input_y, len(prompt), shown)
        except curses.error:
            pass
        self.stdscr.move(self.input_y, len(prompt) + len(shown))

    def draw_status_bar(self, force=False):
        now = time.time()
        if not force and now - self.last_status_update < self.status_interval:
            return
        self.last_status_update = now

        fw = self.status["fw_ver"]
        tm = self.status["time"]
        pwr = self.status["power"]
        inp = self.status["input"].upper()
        spk = self.status["speaker"].upper()
        volt = self.status["voltage"]
        v12 = self.status["v12"]
        temp = self.status["temp"]
        fanm = self.status["fan_mode"]
        fand = self.status["fan_duty"]
        errs = self.status["errors"]
        err_str = ",".join(errs) if errs else "-"

        line = (
            f"FW={fw} | Time={tm} | PWR={pwr} | IN={inp} | SPK={spk} | "
            f"V={volt:.1f}V | 12V={v12:.2f}V | T={temp:.1f}C | "
            f"Fan={fanm}({fand}) | ERR={err_str}"
        )

        self.stdscr.move(self.status_y, 0)
        try:
            self.stdscr.addstr(" " * (self.width - 1), curses.A_REVERSE)
            self.stdscr.move(self.status_y, 1)
            self.stdscr.addstr(line[: self.width - 2], curses.A_REVERSE)
        except curses.error:
            pass

    def draw_vu_bar(self, force=False):
        now = time.time()
        if not force and now - self.last_status_update < self.status_interval:
            return

        vu = self.status["vu"]
        max_bar_width = max(10, self.width - 15)
        filled = int(vu / 255.0 * max_bar_width)
        filled = max(0, min(max_bar_width, filled))
        bar = "█" * filled + "░" * (max_bar_width - filled)
        text = f"VU [{bar}] {vu:3d}/255"

        self.stdscr.move(self.vu_y, 0)
        try:
            self.stdscr.clrtoeol()
            self.stdscr.addstr(self.vu_y, 0, text[: self.width - 1])
        except curses.error:
            pass

    # -------------------------------------------------------------------------
    # Koneksi & serial
    # -------------------------------------------------------------------------

    def find_device_port(self):
        self.log_print(f"Searching for device: {self.FW_IDENTIFIER}...")
        ports = serial.tools.list_ports.comports()
        candidates = []

        for port in ports:
            is_esp = False
            desc_lower = (port.description or "").lower()
            hwid_lower = (port.hwid or "").lower()

            if any(x in hwid_lower for x in ["10c4:ea60", "1a86:7523", "0403:6001", "303a:"]):
                is_esp = True
            elif any(x in desc_lower for x in ["cp210", "ch340", "usb serial", "uart", "esp32"]):
                is_esp = True

            if is_esp:
                candidates.append(port)
                self.log_print(f"Found candidate: {port.device} - {port.description}")

        if not candidates:
            self.log_print("No ESP32-compatible serial devices found!")
            self.log_print("Available ports:")
            for port in ports:
                self.log_print(f" - {port.device}: {port.description}")
            return None

        for port in candidates:
            try:
                self.log_print(f"Probing {port.device}...")
                test_ser = serial.Serial(port.device, self.baud, timeout=2)
                test_ser.reset_input_buffer()

                start = time.time()
                found = False
                buffer = ""

                while time.time() - start < 3.0:
                    if test_ser.in_waiting:
                        chunk = test_ser.read(test_ser.in_waiting).decode("utf-8", errors="ignore")
                        buffer += chunk
                        while "\n" in buffer:
                            line, buffer = buffer.split("\n", 1)
                            line = line.strip()
                            if not line:
                                continue
                            try:
                                data = json.loads(line)
                                if data.get("type") == "telemetry":
                                    fw_ver = data.get("hz1", {}).get("fw_ver", "")
                                    if fw_ver.startswith("amp-"):
                                        self.log_print(f"Detected FW: {fw_ver}")
                                        found = True
                                        break
                            except Exception:
                                pass
                    if found:
                        break
                    time.sleep(0.1)

                test_ser.close()
                if found:
                    return port.device
            except Exception as e:
                self.log_print(f"Probe {port.device} failed: {e}")
                continue

        if candidates:
            fallback = candidates[0].device
            self.log_print(f"Using fallback port: {fallback}")
            return fallback

        return None

    def connect(self):
        try:
            if not self.port:
                self.port = self.find_device_port()
            if not self.port:
                return False

            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                timeout=0,
                write_timeout=1,
            )

            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            time.sleep(0.3)

            self.log_print(f"CONNECTED to {self.port} @ {self.baud} baud")
            return True

        except Exception as e:
            self.log_print(f"Connection failed: {e}")
            return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.log_print("DISCONNECTED")

    def send(self, json_str):
        try:
            self.ser.write((json_str + "\n").encode())
            self.ser.flush()
            self.log_print(f"TX -> {json_str}")
        except Exception as e:
            self.log_print(f"Send error: {e}")

    def send_command(self, cmd_dict):
        command = {"type": "cmd", "cmd": cmd_dict}
        self.send(json.dumps(command))

    def send_analyzer(self, cmd, **kwargs):
        data = {"type": "analyzer", "cmd": cmd}
        data.update(kwargs)
        self.send(json.dumps(data))

    # -------------------------------------------------------------------------
    # Telemetry & status
    # -------------------------------------------------------------------------

    def update_status(self, data):
        if "hz1" in data:
            hz1 = data["hz1"]
            self.status["fw_ver"] = hz1.get("fw_ver", "N/A")
            self.status["time"] = hz1.get("time", "N/A")
            self.status["voltage"] = hz1.get("smps", {}).get("v", 0.0)
            self.status["v12"] = hz1.get("v12", 0.0)
            self.status["temp"] = hz1.get("heat_c", 0.0) or 0.0

            states = hz1.get("states", {})
            self.status["power"] = "ON" if states.get("on", False) else "OFF"

            inputs = hz1.get("inputs", {})
            self.status["input"] = "bt" if inputs.get("bt", False) else "aux"
            self.status["speaker"] = inputs.get("speaker", "small")

            nvs = hz1.get("nvs", {})
            self.status["fan_mode"] = nvs.get("fan_mode_str", "auto")
            self.status["fan_duty"] = nvs.get("fan_duty", 0)

            self.status["vu"] = hz1.get("vu", 0)
            self.status["errors"] = hz1.get("errors", [])

            self.draw_status_bar(force=True)
            self.draw_vu_bar(force=True)

        elif "rt" in data:
            rt = data["rt"]
            self.status["vu"] = rt.get("vu", 0)
            self.status["input"] = rt.get("input", "aux")
            self.draw_vu_bar(force=True)

    # -------------------------------------------------------------------------
    # Serial handler
    # -------------------------------------------------------------------------

    def handle_serial(self):
        if not self.ser:
            return

        try:
            waiting = self.ser.in_waiting
        except Exception:
            return

        if waiting <= 0:
            return

        try:
            chunk = self.ser.read(waiting).decode("utf-8", errors="ignore")
        except Exception:
            return

        if not chunk:
            return

        self.rx_buffer += chunk

        while "\n" in self.rx_buffer:
            line, self.rx_buffer = self.rx_buffer.split("\n", 1)
            line = line.strip()
            if not line:
                continue

            try:
                data = json.loads(line)
                msg_type = data.get("type", "unknown")

                if msg_type == "log":
                    lvl = data.get("lvl", "info").upper()
                    msg = data.get("msg", "")
                    reason = data.get("reason", "")
                    offset = data.get("offset_sec", None)
                    if offset is not None:
                        self.log_print(f"LOG[{lvl}] {msg} (offset {offset}s)")
                    elif reason:
                        self.log_print(f"LOG[{lvl}] {msg} - {reason}")
                    else:
                        self.log_print(f"LOG[{lvl}] {msg}")

                elif msg_type == "ack":
                    ok = data.get("ok", False)
                    changed = data.get("changed", "")
                    value = data.get("value", "")
                    error = data.get("error", "")
                    if ok:
                        self.log_print(f"ACK OK: {changed} = {value}")
                    else:
                        self.log_print(f"ACK ERR: {changed}: {error}")

                elif msg_type == "telemetry":
                    self.update_status(data)

                elif msg_type == "ota":
                    evt = data.get("evt", "")
                    self.log_print(f"OTA: {evt}")

                elif msg_type == "analyzer":
                    evt = data.get("evt", "")
                    self.log_print(f"ANALYZER: {evt}")

                elif msg_type == "features":
                    self.log_print("FEATURES received (boot snapshot)")

                else:
                    if self.debug_mode:
                        self.log_print(f"RX: {line}")

            except json.JSONDecodeError:
                if self.debug_mode or line.startswith("["):
                    self.log_print(f"RX RAW: {line}")

    # -------------------------------------------------------------------------
    # Command parser
    # -------------------------------------------------------------------------

    def parse_command(self, cmd):
        parts = cmd.split()
        if not parts:
            return

        action = parts[0].lower()
        args = parts[1:] if len(parts) > 1 else []

        # Debug toggle
        if action == "debug":
            self.debug_mode = not self.debug_mode
            self.log_print(f"Debug mode: {'ON' if self.debug_mode else 'OFF'}")

        # Power
        elif action == "pwr-on":
            self.send_command({"power": True})
        elif action == "pwr-off":
            self.send_command({"power": False})

        # Sleep Timer
        elif action == "sleep":
            if not args:
                self.log_print("Usage: sleep <minutes>")
                return
            try:
                minutes = int(args[0])
                self.send_command({"sleep_timer": minutes})
                self.log_print(f"Set sleep timer to {minutes} minutes")
            except ValueError:
                self.log_print("Invalid sleep minutes")

        # Speaker
        elif action == "spk-big":
            self.send_command({"spk_sel": "big"})
        elif action == "spk-small":
            self.send_command({"spk_sel": "small"})
        elif action == "spk-on":
            self.send_command({"spk_pwr": True})
        elif action == "spk-off":
            self.send_command({"spk_pwr": False})

        # Bluetooth
        elif action == "bt-on":
            self.send_command({"bt": True})
        elif action == "bt-off":
            self.send_command({"bt": False})

        # Fan
        elif action == "fan-auto":
            self.send_command({"fan_mode": "auto"})
        elif action == "fan-custom":
            self.send_command({"fan_mode": "custom"})
        elif action == "fan-duty":
            if not args:
                self.log_print("Usage: fan-duty <0-1023>")
                return
            try:
                duty = int(args[0])
                if duty < 0 or duty > 1023:
                    self.log_print("Duty must be 0-1023")
                    return
                self.send_command({"fan_mode": "custom", "fan_duty": duty})
                self.log_print(f"Set fan duty {duty}")
            except ValueError:
                self.log_print("Invalid duty value")

        # SMPS
        elif action == "smps-on":
            self.send_command({"smps_bypass": False})
        elif action == "smps-off":
            self.send_command({"smps_bypass": True})
        elif action == "smps-cutoff":
            if not args:
                self.log_print("Usage: smps-cutoff <voltage>")
                return
            try:
                voltage = float(args[0])
                self.send_command({"smps_cut": voltage})
            except ValueError:
                self.log_print("Invalid voltage value")

        # Buzzer (multi sequence)
        elif action == "buzz":
            if not args:
                self.log_print("Usage: buzz freq ms [duty] [, freq ms [duty] ...]")
                self.log_print("Example: buzz 1000 200")
                self.log_print("         buzz 349 125 512 , 262 125 , 220 125 512")
                return

            seq_str = " ".join(args)
            segments = [s.strip() for s in seq_str.split(",") if s.strip()]

            for seg in segments:
                tokens = seg.split()
                if len(tokens) < 2:
                    self.log_print(f"Invalid segment: '{seg}'")
                    continue
                try:
                    freq = int(tokens[0])
                    duration = int(tokens[1])
                    duty = int(tokens[2]) if len(tokens) > 2 else 512
                except ValueError:
                    self.log_print(f"Invalid numbers in segment: '{seg}'")
                    continue

                buzz_obj = {"f": freq, "ms": duration, "d": duty}
                self.send_command({"buzz": buzz_obj})
                gap = duration * 1.3 / 1000.0
                time.sleep(gap)

        elif action == "buzz-click":
            self.send_command({"buzz": {"f": 1975, "ms": 60, "d": 512}})

        elif action == "buzz-test":
            self.log_print("Testing buzzer tones...")
            tests = [
                ("Click", 1975, 60),
                ("Low (440Hz)", 440, 300),
                ("Mid (880Hz)", 880, 300),
                ("High (1760Hz)", 1760, 300),
            ]
            for name, freq, dur in tests:
                self.log_print(f"Playing: {name}")
                self.send_command({"buzz": {"f": freq, "ms": dur, "d": 512}})
                time.sleep(dur / 1000.0 + 0.5)

        # Analyzer
        elif action == "ana-get":
            self.send_analyzer("get")
        elif action == "ana-mode":
            if not args:
                self.log_print("Usage: ana-mode <fft|vu>")
                return
            mode = args[0].lower()
            if mode not in ["fft", "vu"]:
                self.log_print("Mode must be 'fft' or 'vu'")
                return
            self.send_analyzer("set", mode=mode)
        elif action == "ana-bands":
            if not args:
                self.log_print("Usage: ana-bands <8|16|24|32|48|64>")
            else:
                try:
                    bands = int(args[0])
                    if bands not in [8, 16, 24, 32, 48, 64]:
                        self.log_print("Bands must be 8,16,24,32,48,64")
                        return
                    self.send_analyzer("set", bands=bands)
                except ValueError:
                    self.log_print("Invalid bands value")

        # RTC
        elif action == "rtc-sync":
            local_time = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
            self.send_command({"rtc_set": local_time})
            self.log_print(f"Sync RTC: {local_time}")
        elif action == "rtc-sync-force":
            import time as time_module

            epoch = int(time_module.time()) + (7 * 3600)
            self.send_command({"rtc_set_epoch": epoch})
            local_time = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
            self.log_print(f"Force sync RTC: {local_time}")

        # NVS
        elif action == "nvs-reset":
            self.log_print("NVS reset requested (device will handle confirmation)")
            self.send_command({"nvs_reset": True})

        # System
        elif action == "reset":
            self.log_print("Factory reset requested")
            self.send_command({"factory_reset": True})

        # Raw JSON
        elif action == "json":
            if not args:
                self.log_print("Usage: json <jsonstring>")
                return
            json_str = " ".join(args)
            try:
                json.loads(json_str)
                self.send(json_str)
            except json.JSONDecodeError as e:
                self.log_print(f"Invalid JSON: {e}")

        # Clear log
        elif action == "clear":
            self.log_lines = []
            self.log_offset = 0
            self.log_win.erase()
            self.draw_log()

        # Help (tanpa timestamp)
        elif action == "help":
            self.log_print("=== ESP32 Monitor - Command Reference ===", with_ts=False)
            self.log_print("General:", with_ts=False)
            self.log_print("  help                Tampilkan daftar command ini", with_ts=False)
            self.log_print("  clear               Bersihkan area log", with_ts=False)
            self.log_print("  debug               Toggle debug RX mentah", with_ts=False)
            self.log_print("  exit | quit | q     Keluar dari monitor", with_ts=False)
            self.log_print("Power:", with_ts=False)
            self.log_print("  pwr-on              Nyalakan amplifier (main power ON)", with_ts=False)
            self.log_print("  pwr-off             Matikan amplifier (main power OFF)", with_ts=False)
            self.log_print("  sleep <menit>       Set sleep timer auto power-off (mis. sleep 15)", with_ts=False)
            self.log_print("Speaker:", with_ts=False)
            self.log_print("  spk-big             Pilih speaker output BIG", with_ts=False)
            self.log_print("  spk-small           Pilih speaker output SMALL", with_ts=False)
            self.log_print("  spk-on              Aktifkan power ke speaker", with_ts=False)
            self.log_print("  spk-off             Matikan power ke speaker", with_ts=False)
            self.log_print("Bluetooth / Input:", with_ts=False)
            self.log_print("  bt-on               Aktifkan Bluetooth input", with_ts=False)
            self.log_print("  bt-off              Matikan Bluetooth (kembali ke AUX)", with_ts=False)
            self.log_print("Fan / Cooling:", with_ts=False)
            self.log_print("  fan-auto            Mode kipas otomatis", with_ts=False)
            self.log_print("  fan-custom          Mode kipas manual", with_ts=False)
            self.log_print("  fan-duty <0-1023>   Set duty PWM kipas", with_ts=False)
            self.log_print("SMPS / Power Supply:", with_ts=False)
            self.log_print("  smps-on             SMPS aktif normal", with_ts=False)
            self.log_print("  smps-off            Bypass SMPS", with_ts=False)
            self.log_print("  smps-cutoff <V>     Set tegangan cut-off SMPS", with_ts=False)
            self.log_print("Buzzer / Tone:", with_ts=False)
            self.log_print("  buzz freq ms [duty] [, freq ms [duty] ...]", with_ts=False)
            self.log_print("                     Kirim satu atau lebih nada berurutan", with_ts=False)
            self.log_print("                     Host delay ~1.3x durasi (tempo)", with_ts=False)
            self.log_print("  buzz-click          Bunyi klik pendek", with_ts=False)
            self.log_print("  buzz-test           Nada uji low/mid/high", with_ts=False)
            self.log_print("Analyzer / FFT:", with_ts=False)
            self.log_print("  ana-get             Snapshot konfigurasi analyzer", with_ts=False)
            self.log_print("  ana-mode <fft|vu>   Mode analyzer", with_ts=False)
            self.log_print("  ana-bands <8..64>   Jumlah band FFT", with_ts=False)
            self.log_print("RTC / Jam:", with_ts=False)
            self.log_print("  rtc-sync            Sync RTC pakai waktu lokal", with_ts=False)
            self.log_print("  rtc-sync-force      Sync RTC pakai epoch + WIB offset", with_ts=False)
            self.log_print("NVS / Config:", with_ts=False)
            self.log_print("  nvs-reset           Reset NVS ke default", with_ts=False)
            self.log_print("System:", with_ts=False)
            self.log_print("  reset               Factory reset firmware", with_ts=False)
            self.log_print("Utility:", with_ts=False)
            self.log_print("  json <string>       Kirim JSON mentah ke device", with_ts=False)

        # Exit
        elif action in ["exit", "quit", "q"]:
            self.running = False

        else:
            self.log_print(f"Unknown command: {action}")

    # -------------------------------------------------------------------------
    # Main loop
    # -------------------------------------------------------------------------

    def run(self):
        if not self.connect():
            self.ui_init()
            self.log_print("Failed to connect. Press any key to exit.", with_ts=False)
            self.refresh_all()
            self.stdscr.getch()
            return

        self.ui_init()
        self.running = True

        while self.running:
            # 1) Serial
            self.handle_serial()

            # 2) Keyboard
            ch = self.stdscr.getch()
            if ch != -1:
                if ch in (curses.KEY_ENTER, 10, 13):
                    cmd = self.input_buf.strip()
                    self.input_buf = ""
                    self.draw_input()
                    if cmd:
                        # keluar dari scroll mode saat kirim command
                        self.log_offset = 0
                        self.parse_command(cmd)

                elif ch in (curses.KEY_BACKSPACE, 127, 8):
                    self.input_buf = self.input_buf[:-1]
                    self.draw_input()

                # Ctrl+C (tidak perlu raise KeyboardInterrupt)
                elif ch == 3:  # ASCII ETX
                    self.running = False

                # Scroll log
                elif ch == curses.KEY_PPAGE:  # PageUp
                    self.log_offset += self.log_h // 2 or 1
                    self.draw_log()
                elif ch == curses.KEY_NPAGE:  # PageDown
                    self.log_offset -= self.log_h // 2 or 1
                    if self.log_offset < 0:
                        self.log_offset = 0
                    self.draw_log()
                elif ch == curses.KEY_UP:
                    self.log_offset += 1
                    self.draw_log()
                elif ch == curses.KEY_DOWN:
                    if self.log_offset > 0:
                        self.log_offset -= 1
                        self.draw_log()

                # Text input
                elif 32 <= ch <= 126:
                    self.input_buf += chr(ch)
                    self.draw_input()

            # 3) Update header + VU + log + input
            self.draw_status_bar()
            self.draw_vu_bar()
            self.draw_log()
            self.draw_input()
            self.stdscr.noutrefresh()
            curses.doupdate()

            time.sleep(0.01)

        self.disconnect()


def main(stdscr):
    port = sys.argv[1] if len(sys.argv) > 1 else None
    monitor = ESP32Monitor(stdscr, port=port)
    monitor.run()


if __name__ == "__main__":
    # Tangkap KeyboardInterrupt supaya Ctrl+C tidak menampilkan traceback
    try:
        curses.wrapper(main)
    except KeyboardInterrupt:
        pass
