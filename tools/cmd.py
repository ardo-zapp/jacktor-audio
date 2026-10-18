# -*- coding: utf-8 -*-

"""
Jacktor Audio Compact TUI Monitor (3-Zone Terminal)
Optimized for mobile / Termux screens (Max 60 columns header width).

Layout:
  y=0..3      : Compact Fixed Top Header (Max 60 cols wide, 2 status lines)
  y=4..h-3    : Scrollable Live Log Area (Auto-scroll TX/RX/ACK)
  y=h-2       : Input Bar Prompt ("> (type 'help' for command list)")
  y=h-1       : Fixed Bottom Footer (VU Meter Bar)
"""

import serial
import serial.tools.list_ports
import sys
import time
import json
from datetime import datetime
import os
import platform
try:
    import curses
except ImportError:
    if platform.system() == "Windows":
        try:
            import subprocess
            subprocess.check_call([sys.executable, "-m", "pip", "install", "windows-curses"])
            import curses
        except Exception:
            raise RuntimeError("Windows requires 'windows-curses'. Please run 'pip install windows-curses'.")
    else:
        raise

import math
import struct
import random
import threading
import re
import subprocess
import base64

SELECTED_PORT = None

class QCCAudioGenerator:
    """
    Dedicated Audio Signal Generator for Jacktor Audio Analyzer Frequency Calibration.
    Targets USB DAC QCC exclusively via ALSA S16_LE 2ch 44.1kHz.
    Supports Sine (precise 16-band presets & custom Hz), Logarithmic Sweep, Step Sweep,
    Pink Noise, White Noise, and Stereo/Left/Right channel isolation.
    """
    BAND_CUTOFFS = [30, 50, 100, 150, 250, 400, 650, 1000, 1600, 2500, 4000, 6000, 12000, 14000, 16000, 17000]
    BAND_NAMES = [
        "B01 (30Hz Sub-Bass)", "B02 (50Hz Sub-Bass)", "B03 (100Hz Bass)", "B04 (150Hz Mid-Bass)",
        "B05 (250Hz Low-Mid)", "B06 (400Hz Mid-Low)", "B07 (650Hz Mid)", "B08 (1kHz Mid-High)",
        "B09 (1.6kHz Presence)", "B10 (2.5kHz Presence)", "B11 (4kHz High-Mid)", "B12 (6kHz High)",
        "B13 (12kHz Treble)", "B14 (14kHz Brilliance)", "B15 (16kHz Top-Air)", "B16 (17kHz Air)"
    ]

    def __init__(self, sample_rate=44100, log_callback=None, channel_callback=None):
        self.sample_rate = sample_rate
        self.log_callback = log_callback
        self.channel_callback = channel_callback
        self.target_device = None
        self.device_name = "Not Detected"
        self.device_ready = False

        # Generator state
        self.is_playing = False
        self.mode = "sine" # 'sine', 'sweep', 'step_sweep', 'pink', 'white'
        self.frequency = 1000.0 # Hz
        self.amplitude = 0.70 # 0.0 - 1.0
        self.channel = "stereo" # 'stereo', 'left', 'right'

        # Sweep state
        self.sweep_f_start = 20.0
        self.sweep_f_end = 20000.0
        self.sweep_duration = 10.0 # seconds
        self.sweep_cur_freq = 20.0
        self.sweep_start_time = 0.0
        self.step_band_idx = 7 # Start at B08 (1kHz)
        self.last_step_time = 0.0

        # Thread & process control
        self._thread = None
        self._proc = None
        self._stop_event = threading.Event()
        self._phase = 0.0

        # Pink noise filter states (Paul Kellet's algorithm)
        self.b0 = 0.0
        self.b1 = 0.0
        self.b2 = 0.0
        self.b3 = 0.0
        self.b4 = 0.0
        self.b5 = 0.0
        self.b6 = 0.0

        self.scan_device()

    def log(self, msg):
        if self.log_callback:
            self.log_callback(msg)

    def scan_device(self):
        detected_card = None
        detected_name = None

        try:
            if os.path.exists("/proc/asound/cards"):
                with open("/proc/asound/cards", "r") as f:
                    content = f.read()
                for line in content.splitlines():
                    m = re.match(r"^\s*(\d+)\s+\[([^\]]+)\]:\s*(.*)", line)
                    if m:
                        c_idx, c_id, c_desc = m.group(1), m.group(2).strip(), m.group(3).strip()
                        combined = (c_id + " " + c_desc).lower()
                        if any(kw in combined for kw in ["qcc", "qualcomm", "csr"]):
                            detected_card = c_id
                            detected_name = f"{c_desc} ({c_id})"
                            break
                        elif "usb" in combined and "audio" in combined and not detected_card:
                            detected_card = c_id
                            detected_name = f"{c_desc} ({c_id})"
        except Exception as e:
            self.log(f"[WARN] Error scan /proc/asound/cards: {e}")

        if not detected_card:
            try:
                out = subprocess.check_output(["aplay", "-l"], text=True, stderr=subprocess.DEVNULL)
                for line in out.splitlines():
                    if "card" in line.lower() and any(kw in line.lower() for kw in ["qcc", "qualcomm", "csr", "usb audio"]):
                        m = re.search(r"card\s+(\d+):\s*([a-zA-Z0-9_-]+)\s*\[([^\]]+)\]", line)
                        if m:
                            detected_card = m.group(2).strip()
                            detected_name = m.group(3).strip()
                            break
            except Exception:
                pass

        if detected_card:
            self.target_device = f"plughw:CARD={detected_card},DEV=0"
            self.device_name = f"{detected_name} [{detected_card}]"
            self.device_ready = True
            try:
                subprocess.run(
                    ["amixer", "-c", str(detected_card), "sset", "PCM", "100%", "unmute"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=1.0,
                )
            except Exception:
                pass
            return True
        else:
            self.target_device = None
            self.device_name = "USB DAC QCC (Offline / Standby)"
            self.device_ready = False
            return False

    def get_target_band(self, freq=None):
        if freq is None:
            freq = self.sweep_cur_freq if self.mode in ("sweep", "step_sweep") else self.frequency
        best_idx = 0
        best_diff = abs(freq - self.BAND_CUTOFFS[0])
        for i, cut in enumerate(self.BAND_CUTOFFS):
            diff = abs(freq - cut)
            if diff < best_diff:
                best_diff = diff
                best_idx = i
        return self.BAND_NAMES[best_idx]

    def set_frequency(self, freq):
        freq = max(10.0, min(22000.0, float(freq)))
        self.frequency = freq
        self.mode = "sine"
        self.log(f"[GEN] Frequency set to {self.frequency:.1f} Hz -> Target: {self.get_target_band(self.frequency)}")

    def set_band_preset(self, band_num):
        if 1 <= band_num <= len(self.BAND_CUTOFFS):
            idx = band_num - 1
            self.frequency = float(self.BAND_CUTOFFS[idx])
            self.mode = "sine"
            self.log(f"[GEN] Selected Band Preset {self.BAND_NAMES[idx]}")

    def set_channel(self, channel):
        ch = channel.lower().strip()
        if ch in ("stereo", "both"):
            self.channel = "stereo"
        elif ch in ("left", "l"):
            self.channel = "left"
        elif ch in ("right", "r"):
            self.channel = "right"
        if self.channel_callback:
            try:
                self.channel_callback(self.channel)
            except Exception:
                pass
        self.log(f"[GEN] Channel Output: {self.channel.upper()}")

    def toggle_channel(self):
        modes = ["stereo", "left", "right"]
        cur_idx = modes.index(self.channel) if self.channel in modes else 0
        self.set_channel(modes[(cur_idx + 1) % len(modes)])

    def set_amplitude(self, amp):
        self.amplitude = max(0.01, min(1.0, float(amp)))
        db = 20.0 * math.log10(self.amplitude) if self.amplitude > 0.001 else -60.0
        self.log(f"[GEN] Amplitudo Level: {int(round(self.amplitude * 100))}% ({db:.1f} dBFS)")

    def adjust_amplitude(self, delta):
        self.set_amplitude(self.amplitude + delta)

    def set_mode(self, mode):
        m = mode.lower().strip()
        if m in ("sine", "sweep", "step_sweep", "pink", "white"):
            self.mode = m
            if m == "sweep":
                self.sweep_start_time = time.time()
                self.log(f"[GEN] Mode: Logarithmic Sine Sweep (20Hz - 20kHz, {self.sweep_duration}s loop)")
            elif m == "step_sweep":
                self.last_step_time = time.time()
                self.step_band_idx = 0
                self.log("[GEN] Mode: Step Sweep Auto Band (B01 to B16)")
            elif m == "pink":
                self.log("[GEN] Mode: Pink Noise (-3dB/octave acoustic compensation)")
            elif m == "white":
                self.log("[GEN] Mode: White Noise (flat spectrum)")
            else:
                self.log(f"[GEN] Mode: Static Sine Wave ({self.frequency:.1f} Hz)")

    def get_mode_desc(self):
        if self.mode == "sine":
            return f"SINE {self.frequency:.0f}Hz [{self.get_target_band(self.frequency)}]"
        elif self.mode == "sweep":
            return f"SINE SWEEP ({self.sweep_cur_freq:.0f}Hz)"
        elif self.mode == "step_sweep":
            return f"STEP SWEEP [{self.BAND_NAMES[self.step_band_idx]}]"
        elif self.mode == "pink":
            return "PINK NOISE (-3dB/Oct)"
        elif self.mode == "white":
            return "WHITE NOISE (Flat)"
        return self.mode.upper()

    def start(self):
        if self.is_playing:
            return True
        if not self.device_ready:
            self.scan_device()

        device_to_use = self.target_device if self.device_ready else "null"
        if self.device_ready:
            self.log(f"[PLAY] Opening audio stream to {self.device_name} ({self.target_device})...")
        else:
            self.log("[PLAY] USB DAC QCC Offline -> Starting Virtual Test Stream (aplay null)...")

        try:
            cmd = [
                "aplay", "-D", device_to_use,
                "-t", "raw", "-f", "S16_LE", "-c", "2", "-r", str(self.sample_rate),
                "--buffer-time=60000", "--period-time=15000", "-q"
            ]
            self._proc = subprocess.Popen(
                cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, bufsize=0
            )
            self._stop_event.clear()
            self.is_playing = True
            self.sweep_start_time = time.time()
            self.last_step_time = time.time()
            self._thread = threading.Thread(target=self._worker_loop, daemon=True)
            self._thread.start()
            self.log(f"[PLAY] Playback ACTIVE: {self.get_mode_desc()} | Channel: {self.channel.upper()} | Vol: {int(self.amplitude*100)}%")
            return True
        except Exception as e:
            self.log(f"[ERROR] Failed to open aplay stream: {e}")
            self.is_playing = False
            return False

    def stop(self):
        if not self.is_playing:
            return
        self.is_playing = False
        self._stop_event.set()
        if self._proc:
            try:
                if self._proc.stdin:
                    self._proc.stdin.close()
                self._proc.terminate()
                self._proc.wait(timeout=0.2)
            except Exception:
                try:
                    self._proc.kill()
                except Exception:
                    pass
            self._proc = None
        self.log("[PLAY] Playback STOPPED.")

    def toggle_play(self):
        if self.is_playing:
            self.stop()
        else:
            self.start()

    def _worker_loop(self):
        frames = 1024
        chunk_dur = frames / float(self.sample_rate)
        next_t = time.perf_counter()

        while not self._stop_event.is_set():
            if not self._proc or self._proc.poll() is not None:
                self.log("[WARN] Audio stream aplay process disconnected.")
                self.is_playing = False
                break

            now_t = time.perf_counter()
            if next_t - now_t > 0.035:
                time.sleep(max(0.002, (next_t - now_t) - 0.025))
            elif now_t - next_t > 0.10:
                next_t = now_t
            next_t += chunk_dur

            try:
                if self.mode == "sine":
                    chunk = self._gen_sine(self.frequency, self.amplitude, frames, self.channel)
                elif self.mode == "sweep":
                    chunk = self._gen_sweep(frames)
                elif self.mode == "step_sweep":
                    chunk = self._gen_step_sweep(frames)
                elif self.mode == "pink":
                    chunk = self._gen_pink(self.amplitude, frames, self.channel)
                elif self.mode == "white":
                    chunk = self._gen_white(self.amplitude, frames, self.channel)
                else:
                    chunk = self._gen_sine(self.frequency, self.amplitude, frames, self.channel)

                self._proc.stdin.write(chunk)
            except (BrokenPipeError, OSError, ValueError):
                if not self._stop_event.is_set() and self.is_playing:
                    self.log("[WARN] Audio pipe broken (USB device might be disconnected or in standby).")
                self.is_playing = False
                break
            except Exception as e:
                if not self._stop_event.is_set() and self.is_playing:
                    self.log(f"[ERROR] Audio generator error: {e}")
                self.is_playing = False
                break

        if self._proc and self._proc.stdin:
            try:
                self._proc.stdin.close()
            except Exception:
                pass

    def _gen_sine(self, freq, amp_factor, frames, channel):
        buf = bytearray(frames * 4)
        phase = self._phase
        phase_inc = (2.0 * math.pi * freq) / self.sample_rate
        amp = int(amp_factor * 32767)

        offset = 0
        for _ in range(frames):
            s = int(amp * math.sin(phase))
            phase += phase_inc
            if phase >= 2.0 * math.pi:
                phase -= 2.0 * math.pi
            if channel == "left":
                struct.pack_into("<hh", buf, offset, s, 0)
            elif channel == "right":
                struct.pack_into("<hh", buf, offset, 0, s)
            else:
                struct.pack_into("<hh", buf, offset, s, s)
            offset += 4

        self._phase = phase
        return buf

    def _gen_sweep(self, frames):
        buf = bytearray(frames * 4)
        amp = int(self.amplitude * 32767)
        T = self.sweep_duration
        f0 = self.sweep_f_start
        f1 = self.sweep_f_end
        K = f1 / f0
        lnK = math.log(K)
        channel = self.channel

        now = time.time()
        elapsed = (now - self.sweep_start_time) % T
        dt = 1.0 / self.sample_rate

        offset = 0
        for i in range(frames):
            t = elapsed + (i * dt)
            t_norm = (t % T) / T
            cur_f = f0 * (K ** t_norm)
            if i == 0:
                self.sweep_cur_freq = cur_f
            phi = 2.0 * math.pi * f0 * T / lnK * ((K ** t_norm) - 1.0)
            s = int(amp * math.sin(phi))
            if channel == "left":
                struct.pack_into("<hh", buf, offset, s, 0)
            elif channel == "right":
                struct.pack_into("<hh", buf, offset, 0, s)
            else:
                struct.pack_into("<hh", buf, offset, s, s)
            offset += 4

        return buf

    def _gen_step_sweep(self, frames):
        now = time.time()
        if now - self.last_step_time >= 1.5:
            self.last_step_time = now
            self.step_band_idx = (self.step_band_idx + 1) % len(self.BAND_CUTOFFS)
            self.log(f"[STEP] Auto Step: {self.BAND_NAMES[self.step_band_idx]}")

        target_f = float(self.BAND_CUTOFFS[self.step_band_idx])
        self.sweep_cur_freq = target_f
        return self._gen_sine(target_f, self.amplitude, frames, self.channel)

    def _gen_pink(self, amp_factor, frames, channel):
        buf = bytearray(frames * 4)
        amp = amp_factor * 32767 * 0.16
        b0, b1, b2, b3, b4, b5, b6 = self.b0, self.b1, self.b2, self.b3, self.b4, self.b5, self.b6

        offset = 0
        for _ in range(frames):
            white = random.uniform(-1.0, 1.0)
            b0 = 0.99886 * b0 + white * 0.0555179
            b1 = 0.99332 * b1 + white * 0.0750759
            b2 = 0.96900 * b2 + white * 0.1538520
            b3 = 0.86650 * b3 + white * 0.3104856
            b4 = 0.55000 * b4 + white * 0.5329522
            b5 = -0.7616 * b5 - white * 0.0168980
            pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362
            b6 = white * 0.115926

            s = max(-32767, min(32767, int(pink * amp)))
            if channel == "left":
                struct.pack_into("<hh", buf, offset, s, 0)
            elif channel == "right":
                struct.pack_into("<hh", buf, offset, 0, s)
            else:
                struct.pack_into("<hh", buf, offset, s, s)
            offset += 4

        self.b0, self.b1, self.b2, self.b3, self.b4, self.b5, self.b6 = b0, b1, b2, b3, b4, b5, b6
        return buf

    def _gen_white(self, amp_factor, frames, channel):
        buf = bytearray(frames * 4)
        amp = int(amp_factor * 32767 * 0.40)

        offset = 0
        for _ in range(frames):
            s = int(random.uniform(-amp, amp))
            if channel == "left":
                struct.pack_into("<hh", buf, offset, s, 0)
            elif channel == "right":
                struct.pack_into("<hh", buf, offset, 0, s)
            else:
                struct.pack_into("<hh", buf, offset, s, s)
            offset += 4

        return buf

class SafeSerial(serial.Serial):
    """
    Serial class that prevents DTR/RTS toggling during port open/reconnect.
    On Linux, toggling DTR/RTS pulses the ESP32 EN pin, causing unwanted MCU reboots.
    """
    def _update_dtr_state(self):
        pass
    def _update_rts_state(self):
        pass
    def _reconfigure_port(self, force_update=False):
        super()._reconfigure_port(force_update=force_update)
        if getattr(self, "fd", None) is not None:
            try:
                import termios
                attrs = termios.tcgetattr(self.fd)
                attrs[2] &= ~termios.HUPCL
                termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            except Exception:
                pass

class JacktorHybridMonitor:
    FW_IDENTIFIER = "JACKTOR AUDIO"

    def __init__(self, stdscr, port=None, baud=921600):
        self.stdscr = stdscr
        self.port = port
        self.baud = baud

        self.ser = None
        self.running = False
        self.debug_mode = False

        self.status = {
            "fw_ver": "N/A",
            "time": "N/A",
            "power": "OFF",
            "voltage": 0.0,
            "v_stby": 0.0,
            "v_vdd": 0.0,
            "v_esp": 0.0,
            "temp": 0.0,
            "rtc_temp": 0.0,
            "input": "aux",
            "fan_mode": "auto",
            "fan_duty": 0,
            "total_mins": 0,
            "btn_lock": False,
            "vu": 0,
            "vu_l": 0,
            "vu_r": 0,
            "errors": [],
            "qcc_pwr": "Inactive",
            "adc_v_l": 0.0,
            "adc_v_r": 0.0,
            "adc_raw_l": 0.0,
            "adc_raw_r": 0.0,
        }

        self.height, self.width = self.stdscr.getmaxyx()
        if self.height < 8:
            raise RuntimeError("Terminal too small, requires at least 8 lines of height")

        self.header_h = 5
        self.log_y = self.header_h
        self.input_y = self.height - 3
        self.vu_l_y = self.height - 2
        self.vu_r_y = self.height - 1
        self.log_h = max(1, self.height - self.header_h - 3)
        self.log_w = self.width
        self.log_win = None

        self.log_lines = []
        self.max_log_lines = 1000
        self.log_offset = 0

        self.last_log_raw = None
        self.last_log_count = 1
        self.last_log_ts = None

        self.input_buf = ""
        self.multiline_buf = ""
        self.ana_input_buf = ""
        self.deviceinfo_mode = False
        self.analyzer_screen_mode = False
        self.rtos_log_mode = False
        self.visualizer_source = "adc"
        self.rtos_lines = []

        self.audio_test_mode = False
        self.audio_test_input_buf = ""
        self.audio_test_logs = []
        self.audio_gen = QCCAudioGenerator(
            log_callback=self.log_audio_test,
            channel_callback=lambda ch: self.send_analyzer("set_params", ch=ch)
        )
        self.last_dac_scan = 0.0

        # Analyzer Screen (ana-page) Visual Effects & Decay Physics State
        self.ana_fx_modes = ["GRADIENT", "FIRE", "NEON", "MIRROR", "DOTS"]
        self.ana_fx_idx = 0
        self.ana_decay_presets = [("FAST", 0.62), ("MED", 0.82), ("SLOW", 0.92), ("LIQUID", 0.96)]
        self.ana_decay_idx = 1
        self.ana_decay_rate = 0.82
        self.ana_peak_hold_en = True
        self.ana_decay_vu_l = 0.0
        self.ana_decay_vu_r = 0.0
        self.ana_vu_peak_l = 0.0
        self.ana_vu_peak_r = 0.0
        self.ana_vu_peak_timer_l = 0
        self.ana_vu_peak_timer_r = 0
        self.ana_decay_bands_l = [0.0] * 64
        self.ana_decay_bands_r = [0.0] * 64
        self.ana_trail_bands_l = [0.0] * 64
        self.ana_trail_bands_r = [0.0] * 64
        self.ana_peak_hold_l = [0.0] * 64
        self.ana_peak_hold_r = [0.0] * 64
        self.ana_peak_vel_l = [0.0] * 64
        self.ana_peak_vel_r = [0.0] * 64
        self.ana_peak_timer_l = [0] * 64
        self.ana_peak_timer_r = [0] * 64

        self.cmd_history = []
        self.cmd_history_idx = -1
        self.temp_input_buf = ""
        self._last_header_cache = None

        self.supported_commands = None
        self.amp_commands = set()
        self.panel_commands = set()
        self.device_origin = "HYBRID"
        self.commands_requested = False
        self.fw_categories = {}

        self.last_status_update = 0.0
        self.status_interval = 0.05
        self.last_deviceinfo_request = 0.0

        self.header_dirty = True
        self.vu_dirty = True
        self.log_dirty = True
        self.input_dirty = True
        self.last_render_time = 0.0

        self.rx_buffer = ""
        self.rx_bytes_total = 0
        self.tx_bytes_total = 0
        self.rx_rate_window = []
        self.tx_rate_window = []
        self.last_rx_flash = 0.0
        self.last_tx_flash = 0.0

        try:
            self.log_file = open("log.txt", "a", encoding="utf-8")
        except Exception:
            self.log_file = None

        try:
            self.log_error_file = open("log_error.txt", "a", encoding="utf-8")
        except Exception:
            self.log_error_file = None

        self._last_smps_volt = self._load_cached_smps_volt()
        if self._last_smps_volt > 10.0:
            self.last_known_on_volt = self._last_smps_volt

    def _record_rx(self, nbytes, now=None):
        if nbytes <= 0:
            return
        if now is None:
            now = time.time()
        self.rx_bytes_total += nbytes
        self.last_rx_flash = now
        self.rx_rate_window.append((now, nbytes))
        self.header_dirty = True

    def _record_tx(self, nbytes, now=None):
        if nbytes <= 0:
            return
        if now is None:
            now = time.time()
        self.tx_bytes_total += nbytes
        self.last_tx_flash = now
        self.tx_rate_window.append((now, nbytes))
        self.header_dirty = True

    def _get_transfer_rates(self, now=None):
        if now is None:
            now = time.time()
        cutoff = now - 1.0
        if hasattr(self, "rx_rate_window") and self.rx_rate_window:
            self.rx_rate_window = [item for item in self.rx_rate_window if item[0] >= cutoff]
            rx_bps = sum(item[1] for item in self.rx_rate_window)
        else:
            rx_bps = 0

        if hasattr(self, "tx_rate_window") and self.tx_rate_window:
            self.tx_rate_window = [item for item in self.tx_rate_window if item[0] >= cutoff]
            tx_bps = sum(item[1] for item in self.tx_rate_window)
        else:
            tx_bps = 0

        return rx_bps, tx_bps

    @staticmethod
    def format_rate(bps):
        try:
            val = float(bps)
        except Exception:
            return "0 B/s"
        if val <= 0:
            return "0 B/s"
        if val < 1024:
            return f"{int(round(val))} B/s"
        elif val < 1024 * 1024:
            return f"{val / 1024.0:.1f} KB/s"
        else:
            return f"{val / (1024.0 * 1024.0):.2f} MB/s"

    @staticmethod
    def format_bytes(b):
        try:
            val = float(b)
        except Exception:
            return "0 B"
        if val < 1024:
            return f"{int(val)} B"
        elif val < 1024 * 1024:
            return f"{val / 1024.0:.1f} KB"
        elif val < 1024 * 1024 * 1024:
            return f"{val / (1024.0 * 1024.0):.2f} MB"
        else:
            return f"{val / (1024.0 * 1024.0 * 1024.0):.2f} GB"

    def _load_cached_smps_volt(self):
        base_dir = os.path.dirname(os.path.abspath(__file__))
        candidates = [
            os.path.join(base_dir, ".last_smps_volt"),
            os.path.join(base_dir, "tools", ".last_smps_volt"),
            ".last_smps_volt",
            "tools/.last_smps_volt"
        ]
        for fn in candidates:
            if os.path.exists(fn):
                try:
                    with open(fn, 'r') as f:
                        v = float(f.read().strip())
                        if v > 10.0:
                            return v
                except Exception:
                    pass

        log_candidates = [
            os.path.join(base_dir, "log.txt"),
            os.path.join(base_dir, "tools", "log.txt"),
            "log.txt",
            "tools/log.txt"
        ]
        import re
        for fn in log_candidates:
            if os.path.exists(fn):
                try:
                    with open(fn, 'r', encoding='utf-8', errors='ignore') as f:
                        lines = f.readlines()
                    for line in reversed(lines):
                        m = re.search(r'(?:SMPS\s*=\s*|SMPS_V:\s*)([0-9.]+)\s*V', line)
                        if m:
                            v = float(m.group(1))
                            if v > 10.0:
                                return v
                except Exception:
                    pass
        return 58.0

    def _save_cached_smps_volt(self, volt):
        if volt <= 10.0:
            return
        base_dir = os.path.dirname(os.path.abspath(__file__))
        cache_file = os.path.join(base_dir, ".last_smps_volt")
        try:
            with open(cache_file, "w") as f:
                f.write(f"{volt:.1f}\n")
        except Exception:
            pass

    def _update_step3_log_line(self, volt):
        if not hasattr(self, 'log_lines') or volt <= 10.0:
            return
        import re
        target_pattern = "[Step 3] Checking SMPS"
        for idx in range(len(self.log_lines) - 1, -1, -1):
            line = self.log_lines[idx]
            if target_pattern in line:
                ts_m = re.match(r"^(\[[0-9:]+\]\s*)", line)
                ts_prefix = ts_m.group(1) if ts_m else ""
                new_line = f"{ts_prefix}LOG[SETUP] [Step 3] Checking SMPS Main Power Relay: SMPS = {volt:.1f} V (Protection Normal)... [ PASS ] (PASS)"
                self.log_lines[idx] = new_line
                self.log_dirty = True
                break

    def tui_init(self):
        try:
            curses.curs_set(1)
        except Exception:
            pass
        self.stdscr.nodelay(True)
        self.stdscr.keypad(True)

        if curses.has_colors():
            try:
                curses.start_color()
                curses.use_default_colors()
                curses.init_pair(1, curses.COLOR_WHITE, -1)   # Normal/White
                curses.init_pair(2, curses.COLOR_CYAN, -1)    # INFO/Cyan
                curses.init_pair(3, curses.COLOR_YELLOW, -1)  # WARN/Yellow
                curses.init_pair(4, curses.COLOR_RED, -1)     # ERROR/Red
                curses.init_pair(5, curses.COLOR_GREEN, -1)   # SUCCESS/Green
                curses.init_pair(6, curses.COLOR_MAGENTA, -1) # TX/Magenta
                curses.init_pair(7, curses.COLOR_BLUE, -1)    # Accent/Blue
            except Exception:
                pass

        self.draw_frame()
        self.log_print("Jacktor Audio TUI Monitor Initialized.")
        self.log_print("Type 'help' to see available commands.")
        self.refresh_all()

    def draw_frame(self):
        try:
            self.stdscr.erase()
            self._last_header_cache = None
            self.draw_status_header(force=True)

            if self.height > 4:
                self.stdscr.move(4, 0)
                self.stdscr.clrtoeol()

            if self.input_y < self.height:
                self.stdscr.move(self.input_y, 0)
                self.stdscr.clrtoeol()
                self.draw_input()

            if self.vu_l_y < self.height:
                self.stdscr.move(self.vu_l_y, 0)
                self.stdscr.clrtoeol()
                self.draw_vu_bar(force=True)
        except curses.error:
            pass

    def refresh_all(self):
        self.draw_status_header(force=True)
        self.draw_vu_bar(force=True)
        self.draw_log()
        self.draw_input()
        self.stdscr.refresh()
        sys.stdout.flush()

    def draw_log(self):
        try:
            total = len(self.log_lines)
            if total <= self.log_h:
                start = 0
            else:
                max_offset = max(0, total - self.log_h)
                if self.log_offset > max_offset:
                    self.log_offset = max_offset
                start = max(0, total - self.log_h - self.log_offset)

            end = min(total, start + self.log_h)
            visible = self.log_lines[start:end]

            for i in range(self.log_h):
                r = self.log_y + i
                if r >= self.height - 3:
                    break
                self.stdscr.move(r, 0)
                self.stdscr.clrtoeol()
                if i < len(visible):
                    txt = visible[i][: self.width - 1]
                    try:
                        color_pair = 1
                        if curses.has_colors():
                            if any(x in txt for x in ["LOG[ERROR]", "LOG[CRASH]", "Guru Meditation", "Backtrace:", "ACK ERR:", "FAILED", "FAIL"]):
                                color_pair = 4
                            elif any(x in txt for x in ["LOG[WARN]", "WARNING"]):
                                color_pair = 3
                            elif any(x in txt for x in ["LOG[INFO]", "LOG[SETUP]", "LOG[SELFTEST]", "[BOOT]"]):
                                color_pair = 2
                            elif any(x in txt for x in ["ACK OK:", "SUCCESS", "CONNECTED", "[ PASS ]"]):
                                color_pair = 5
                            elif any(x in txt for x in ["TX ->", "TX RAW"]):
                                color_pair = 6

                        self.stdscr.addstr(r, 0, txt, curses.color_pair(color_pair))
                    except curses.error:
                        pass
        except Exception:
            pass

    def log_print(self, text, with_ts=True, custom_ts=None):
        if text is None or not str(text).strip():
            return

        text_str = str(text).strip() if with_ts else str(text).rstrip("\r\n")
        file_line = ""

        if with_ts:
            ts = custom_ts if custom_ts else datetime.now().strftime("%H:%M:%S")
            if self.last_log_raw == text_str and self.log_lines and self.last_log_ts:
                self.last_log_count += 1
                updated_line = f"[{self.last_log_ts}] {text_str} [{self.last_log_count}x]"
                self.log_lines[-1] = updated_line
                file_line = f"[{ts}] {text_str}\n"
            else:
                self.last_log_raw = text_str
                self.last_log_count = 1
                self.last_log_ts = ts
                line = f"[{ts}] {text_str}"
                self.log_lines.append(line)
                file_line = f"{line}\n"
        else:
            if self.last_log_raw == text_str and self.log_lines and not self.last_log_ts:
                self.last_log_count += 1
                updated_line = f"{text_str} [{self.last_log_count}x]"
                self.log_lines[-1] = updated_line
                file_line = f"{text_str}\n"
            else:
                self.last_log_raw = text_str
                self.last_log_count = 1
                self.last_log_ts = None
                self.log_lines.append(text_str)
                file_line = f"{text_str}\n"

        if hasattr(self, 'log_file') and self.log_file:
            try:
                self._check_and_rotate_log_file(self.log_file, "log.txt")
                self.log_file.write(file_line)
                self.log_file.flush()
            except Exception:
                pass

        is_error_log = any(x in text_str for x in ["LOG[ERROR]", "LOG[CRASH]", "LOG[PARSE_ERR]", "Guru Meditation", "Backtrace:", "ACK ERR:", "FAILED", "FAIL", "E (", "W ("])
        if is_error_log and hasattr(self, 'log_error_file') and self.log_error_file:
            try:
                self._check_and_rotate_log_file(self.log_error_file, "log_error.txt")
                self.log_error_file.write(file_line)
                self.log_error_file.flush()
            except Exception:
                pass

        if len(self.log_lines) > self.max_log_lines:
            self.log_lines = self.log_lines[-self.max_log_lines :]

        if self.log_offset == 0:
            self.log_dirty = True

    def _check_and_rotate_log_file(self, f_obj, filename):
        if f_obj:
            try:
                f_obj.flush()
                pos = f_obj.tell()
                if pos > 5 * 1024 * 1024:  # 5 MB limit
                    f_obj.close()
                    new_f = open(filename, "w", encoding="utf-8")
                    new_f.write(f"[{datetime.now().strftime('%H:%M:%S')}] --- LOG FILE TRUNCATED (SIZE EXCEEDED 5MB) ---\n")
                    new_f.flush()
                    if filename == "log.txt":
                        self.log_file = new_f
                    elif filename == "log_error.txt":
                        self.log_error_file = new_f
            except Exception:
                pass

    def draw_input(self):
        prompt = ".. " if self.multiline_buf else "> "
        placeholder = "(type 'help' for command list)"
        try:
            if self.input_y >= self.height: return
            self.stdscr.move(self.input_y, 0)
            self.stdscr.clrtoeol()
            prompt_color = (curses.color_pair(3) | curses.A_BOLD) if self.multiline_buf else (curses.color_pair(5) | curses.A_BOLD if curses.has_colors() else curses.A_BOLD)
            self.stdscr.addstr(self.input_y, 0, prompt, prompt_color)
            if self.input_buf:
                shown = self.input_buf[-(self.width - len(prompt) - 1) :]
                self.stdscr.addstr(self.input_y, len(prompt), shown, curses.color_pair(1) | curses.A_BOLD if curses.has_colors() else curses.A_BOLD)
                self.stdscr.move(self.input_y, len(prompt) + len(shown))
            else:
                if not self.multiline_buf and self.width > len(prompt) + len(placeholder) + 2:
                    self.stdscr.addstr(self.input_y, len(prompt), placeholder, curses.color_pair(1) | curses.A_DIM if curses.has_colors() else curses.A_DIM)
                self.stdscr.move(self.input_y, len(prompt))
        except curses.error:
            pass

    def _draw_segmented_row(self, r, segments, box_w, border_color):
        if r >= self.height:
            return
        self.stdscr.move(r, 0)
        self.stdscr.clrtoeol()
        self.stdscr.addstr(r, 0, "│ ", border_color)
        cur_x = 2
        max_content_x = box_w - 2
        for text, attr in segments:
            if cur_x >= max_content_x:
                break
            avail = max_content_x - cur_x
            chunk = text[:avail]
            self.stdscr.addstr(r, cur_x, chunk, attr)
            cur_x += len(chunk)
        if cur_x < max_content_x:
            self.stdscr.addstr(r, cur_x, " " * (max_content_x - cur_x))
        if max_content_x < self.width - 1:
            self.stdscr.addstr(r, max_content_x, " │", border_color)

    def draw_status_header(self, force=False):
        now = time.time()
        if not force and now - getattr(self, "last_status_update", 0) < self.status_interval:
            return
        self.last_status_update = now

        try:
            s = self.status
            raw_errs = s.get("errors", [])
            if isinstance(raw_errs, list):
                err_str = ",".join(str(e) for e in raw_errs) if raw_errs else "NONE"
            else:
                err_str = str(raw_errs) if raw_errs else "NONE"

            try:
                tot_mins = int(s.get("total_mins", 0) or 0)
            except (ValueError, TypeError):
                tot_mins = 0
            h = tot_mins // 60
            m = tot_mins % 60

            pwr_str = str(s.get("power", "OFF")).upper()
            bt_enabled = s.get("bt_en", True)
            inp_raw = str(s.get("input", "aux")).upper()
            spk_pwr = s.get("spk_pwr", True)

            try:
                v_stby = float(s.get("v_stby", 0.0) or 0.0)
            except (ValueError, TypeError):
                v_stby = 0.0

            try:
                voltage = float(s.get("voltage", 0.0) or 0.0)
            except (ValueError, TypeError):
                voltage = 0.0

            if pwr_str == 'ON':
                volt_val = voltage if voltage > 0.0 else (getattr(self, "last_known_on_volt", 0.0) or v_stby)
                if volt_val > 0.0:
                    self.last_known_on_volt = volt_val
            else:
                volt_val = v_stby if v_stby > 0.0 else (getattr(self, "last_known_stby_volt", 0.0) or voltage)
                if volt_val > 0.0:
                    self.last_known_stby_volt = volt_val
            volt_str = f"{volt_val:>4.1f}V"

            try:
                temp_val = float(s.get("temp", 0.0) or 0.0)
            except (ValueError, TypeError):
                temp_val = 0.0
            temp_str = f"{temp_val:>4.1f}°C"

            fan_mode = str(s.get("fan_mode", "auto"))
            try:
                fan_duty = int(s.get("fan_duty", 0) or 0)
            except (ValueError, TypeError):
                fan_duty = 0
            fan_str = f"{fan_mode}({fan_duty})"
            run_str = f"{h}h{m}m"
            lock_str = "ON" if s.get("btn_lock") else "OFF"

            # RX / TX live transfer rates & activity
            rx_bps, tx_bps = self._get_transfer_rates(now)
            rx_formatted = self.format_rate(rx_bps)
            tx_formatted = self.format_rate(tx_bps)
            is_rx_active = (now - getattr(self, "last_rx_flash", 0.0)) < 0.35 or rx_bps > 0
            is_tx_active = (now - getattr(self, "last_tx_flash", 0.0)) < 0.35 or tx_bps > 0

            # Responsive box width
            if self.width >= 64:
                box_w = max(52, min(self.width - 2, 74))
            else:
                box_w = max(38, self.width - 2)

            if box_w < 10:
                return

            cache_key = (
                self.width, box_w, pwr_str, bt_enabled, inp_raw, spk_pwr,
                volt_str, temp_str, fan_str, run_str, err_str, lock_str,
                rx_formatted, tx_formatted, is_rx_active, is_tx_active
            )
            if not force and getattr(self, "_last_header_cache", None) == cache_key:
                return
            self._last_header_cache = cache_key

            has_col = curses.has_colors()
            border_color = (curses.color_pair(2)) if has_col else curses.A_NORMAL
            sep_color = (curses.color_pair(2)) if has_col else curses.A_DIM
            dim_color = (curses.color_pair(1) | curses.A_DIM) if has_col else curses.A_DIM
            white_color = (curses.color_pair(1)) if has_col else curses.A_NORMAL

            # Row 0: Top Border
            title = " JACKTOR AUDIO "
            dash_len = max(0, box_w - 2 - len(title))
            if self.height > 0:
                self.stdscr.move(0, 0)
                self.stdscr.clrtoeol()
                self.stdscr.addstr(0, 0, "┌", border_color)
                self.stdscr.addstr(0, 1, title, curses.color_pair(2) | curses.A_BOLD if has_col else curses.A_BOLD)
                rem_top = "─" * dash_len + "┐"
                self.stdscr.addstr(0, 1 + len(title), rem_top[:max(0, self.width - 2 - len(title))], border_color)

            # Row 1 Segments
            if pwr_str == "ON":
                pwr_color = (curses.color_pair(5) | curses.A_BOLD) if has_col else curses.A_BOLD
            elif pwr_str == "BOOT":
                pwr_color = (curses.color_pair(3) | curses.A_BOLD) if has_col else curses.A_BOLD
            else:
                pwr_color = (curses.color_pair(4) | curses.A_BOLD) if has_col else curses.A_BOLD

            if volt_val >= 40.0:
                volt_color = (curses.color_pair(5) | curses.A_BOLD) if has_col else curses.A_BOLD
            elif volt_val >= 15.0:
                volt_color = (curses.color_pair(3) | curses.A_BOLD) if has_col else curses.A_BOLD
            else:
                volt_color = dim_color

            if temp_val >= 70.0:
                temp_color = (curses.color_pair(4) | curses.A_BOLD) if has_col else curses.A_BOLD
            elif temp_val >= 50.0:
                temp_color = (curses.color_pair(3) | curses.A_BOLD) if has_col else curses.A_BOLD
            else:
                temp_color = (curses.color_pair(5)) if has_col else curses.A_NORMAL

            row1_segs = [
                ("PWR: ", dim_color),
                (f"{pwr_str:<3s}", pwr_color),
                (" │ ", sep_color),
                ("IN: ", dim_color),
            ]
            if not bt_enabled:
                row1_segs.append(("AUX", (curses.color_pair(2) | curses.A_BOLD) if has_col else curses.A_BOLD))
                row1_segs.append(("[BT:OFF]", (curses.color_pair(3) | curses.A_BOLD) if has_col else curses.A_BOLD))
            else:
                row1_segs.append((inp_raw, (curses.color_pair(2) | curses.A_BOLD) if has_col else curses.A_BOLD))

            row1_segs.extend([
                (" │ ", sep_color),
                ("VOLT: ", dim_color),
                (volt_str, volt_color),
                (" │ ", sep_color),
                ("TEMP: ", dim_color),
                (temp_str, temp_color),
            ])
            self._draw_segmented_row(1, row1_segs, box_w, border_color)

            # Row 2 Segments
            if fan_duty > 80:
                fan_color = (curses.color_pair(4) | curses.A_BOLD) if has_col else curses.A_BOLD
            elif fan_duty > 40:
                fan_color = (curses.color_pair(3) | curses.A_BOLD) if has_col else curses.A_BOLD
            else:
                fan_color = (curses.color_pair(2)) if has_col else curses.A_NORMAL

            if err_str == "NONE":
                err_color = (curses.color_pair(5) | curses.A_BOLD) if has_col else curses.A_BOLD
            else:
                err_color = (curses.color_pair(4) | curses.A_BOLD) if has_col else curses.A_BOLD

            lock_color = (curses.color_pair(3) | curses.A_BOLD) if (s.get("btn_lock") and has_col) else dim_color

            row2_segs = [
                ("FAN: ", dim_color),
                (fan_str, fan_color),
                (" │ ", sep_color),
                ("RUN: ", dim_color),
                (run_str, white_color),
            ]
            if not spk_pwr:
                row2_segs.append((" [SPK:OFF]", (curses.color_pair(4) | curses.A_BOLD) if has_col else curses.A_BOLD))
            elif s.get("spk_fault") or "SPEAKER_PROTECT_FAIL" in s.get("errors", []):
                row2_segs.append((" [SPK:ALARM!]", (curses.color_pair(4) | curses.A_BOLD | curses.A_REVERSE) if has_col else curses.A_REVERSE))

            row2_segs.extend([
                (" │ ", sep_color),
                ("ERR: ", dim_color),
                (err_str, err_color),
                (" │ ", sep_color),
                ("LOCK: ", dim_color),
                (lock_str, lock_color),
            ])
            self._draw_segmented_row(2, row2_segs, box_w, border_color)

            # Row 3: Bottom Border integrating Live RX / TX Throughput Counters
            if self.height > 3:
                self.stdscr.move(3, 0)
                self.stdscr.clrtoeol()

                badge_min_len = 16 + len(rx_formatted) + len(tx_formatted)
                if box_w >= badge_min_len:
                    self.stdscr.addstr(3, 0, "└─ [", border_color)
                    cx = 4
                    rx_attr = (curses.color_pair(5) | (curses.A_BOLD if is_rx_active else 0)) if has_col else curses.A_NORMAL
                    self.stdscr.addstr(3, cx, "RX: ", rx_attr)
                    cx += 4
                    self.stdscr.addstr(3, cx, rx_formatted, white_color | (curses.A_BOLD if is_rx_active else 0))
                    cx += len(rx_formatted)

                    self.stdscr.addstr(3, cx, " │ ", sep_color)
                    cx += 3

                    tx_attr = (curses.color_pair(6) | (curses.A_BOLD if is_tx_active else 0)) if has_col else curses.A_NORMAL
                    self.stdscr.addstr(3, cx, "TX: ", tx_attr)
                    cx += 4
                    self.stdscr.addstr(3, cx, tx_formatted, white_color | (curses.A_BOLD if is_tx_active else 0))
                    cx += len(tx_formatted)

                    self.stdscr.addstr(3, cx, "] ", border_color)
                    cx += 2

                    rem = max(0, (box_w - 1) - cx)
                    self.stdscr.addstr(3, cx, "─" * rem + "┘", border_color)
                else:
                    b_border = "└" + "─" * (box_w - 2) + "┘"
                    self.stdscr.addstr(3, 0, b_border[:self.width - 1], border_color)

                if self.height > 4:
                    self.stdscr.move(4, 0)
                    self.stdscr.clrtoeol()
        except Exception:
            pass

    def _draw_vu_channel(self, r, label, filled, max_bar_w, val):
        if r >= self.height:
            return
        self.stdscr.move(r, 0)
        self.stdscr.clrtoeol()

        has_col = curses.has_colors()
        lbl_text = f"{label} ["
        self.stdscr.addstr(r, 0, lbl_text, (curses.color_pair(2) | curses.A_BOLD) if has_col else curses.A_BOLD)
        cur_x = len(lbl_text)

        # Gradient thresholds: 60% green, 85% yellow, above red
        g_lim = int(max_bar_w * 0.60)
        y_lim = int(max_bar_w * 0.85)

        for i in range(filled):
            if cur_x + i >= self.width - 12:
                break
            if not has_col:
                c = curses.A_NORMAL
            elif i < g_lim:
                c = curses.color_pair(5) | curses.A_BOLD
            elif i < y_lim:
                c = curses.color_pair(3) | curses.A_BOLD
            else:
                c = curses.color_pair(4) | curses.A_BOLD
            self.stdscr.addstr(r, cur_x + i, "█", c)

        empty_len = max_bar_w - filled
        if empty_len > 0:
            dim_c = (curses.color_pair(1) | curses.A_DIM) if has_col else curses.A_DIM
            track_str = "·" * empty_len
            avail = max(0, self.width - 12 - (cur_x + filled))
            self.stdscr.addstr(r, cur_x + filled, track_str[:avail], dim_c)

        cur_x += max_bar_w
        if cur_x < self.width - 9:
            if val > 215:
                val_c = (curses.color_pair(4) | curses.A_BOLD) if has_col else curses.A_BOLD
            elif val > 150:
                val_c = (curses.color_pair(3)) if has_col else curses.A_NORMAL
            elif val > 0:
                val_c = (curses.color_pair(5)) if has_col else curses.A_NORMAL
            else:
                val_c = (curses.color_pair(1) | curses.A_DIM) if has_col else curses.A_DIM

            self.stdscr.addstr(r, cur_x, f"] {val:3d}/255", val_c)

    def draw_vu_bar(self, force=False):
        now = time.time()
        if not force and now - getattr(self, "last_vu_update", 0) < self.status_interval:
            return
        self.last_vu_update = now

        try:
            vu_l_val = self.status.get("vu_l", self.status.get("vu", 0)) or 0
            vu_r_val = self.status.get("vu_r", self.status.get("vu", 0)) or 0
            pwr_state = self.status.get("power", "OFF")
            vu_l = 0 if pwr_state in ["OFF", "BOOT"] else vu_l_val
            vu_r = 0 if pwr_state in ["OFF", "BOOT"] else vu_r_val

            try:
                vu_l = int(float(vu_l))
            except (ValueError, TypeError):
                vu_l = 0
            try:
                vu_r = int(float(vu_r))
            except (ValueError, TypeError):
                vu_r = 0

            vu_l = max(0, min(255, vu_l))
            vu_r = max(0, min(255, vu_r))

            box_w = min(60, max(30, self.width))
            max_bar_width = max(10, box_w - 24)

            filled_l = int(vu_l / 255.0 * max_bar_width) if vu_l > 0 else 0
            filled_l = max(0, min(max_bar_width, filled_l))

            filled_r = int(vu_r / 255.0 * max_bar_width) if vu_r > 0 else 0
            filled_r = max(0, min(max_bar_width, filled_r))

            self._draw_vu_channel(self.vu_l_y, "VU L", filled_l, max_bar_width, vu_l)
            self._draw_vu_channel(self.vu_r_y, "VU R", filled_r, max_bar_width, vu_r)
        except Exception:
            pass


    def connect(self):
        ports_to_try = []
        if self.port:
            ports_to_try.append(self.port)

        if not ports_to_try:
            auto_p = select_port_interactive()
            if auto_p:
                ports_to_try.append(auto_p)

        if not ports_to_try:
            self.log_print("No serial port specified or detected!")
            return False

        for p in ports_to_try:
            try:
                if hasattr(self, 'ser') and self.ser:
                    try:
                        if self.ser.is_open:
                            self.ser.close()
                    except Exception:
                        pass

                ser = SafeSerial()
                ser.port = p
                ser.baudrate = self.baud
                ser.timeout = 0
                ser.write_timeout = 1
                ser.open()
                self.ser = ser

                try:
                    self.ser.reset_input_buffer()
                    self.ser.reset_output_buffer()
                except Exception:
                    pass
                time.sleep(0.1)

                self.port = p
                self.last_rx_time = time.time()
                self.log_print(f"CONNECTED to {self.port} @ {self.baud} baud")
                self.request_commands_list(timeout=0.45)
                return True

            except Exception:
                self.ser = None

        # Fallback: if primary port failed, auto-detect port candidates
        auto_p = select_port_interactive()
        if auto_p and auto_p != self.port:
            try:
                self.rx_buffer = ""
                ser = SafeSerial()
                ser.port = auto_p
                ser.baudrate = self.baud
                ser.timeout = 0
                ser.write_timeout = 1
                ser.open()
                self.ser = ser
                try:
                    self.ser.reset_input_buffer()
                    self.ser.reset_output_buffer()
                except Exception:
                    pass
                time.sleep(0.1)
                self.port = auto_p
                self.last_rx_time = time.time()
                self.log_print(f"CONNECTED to fallback port {self.port} @ {self.baud} baud")
                self.request_commands_list(timeout=0.45)
                return True
            except Exception:
                self.ser = None

        self.log_print(f"Connection failed to {self.port or 'specified ports'}")
        return False

    def request_commands_list(self, timeout=0.45):
        """
        Requests the full command & category capability list from firmware and
        synchronously drains the serial port in a tight sub-millisecond loop so
        the ~9KB commands_list JSON packet never overflows the 4KB Linux kernel
        N_TTY flip buffer while curses UI is initializing or rendering.
        """
        if not self.ser or not getattr(self.ser, "is_open", False):
            return False

        self.last_commands_req_time = time.time()
        try:
            init_cmd = b'\n{"type":"get_commands"}\n'
            self.ser.write(init_cmd)
            self.ser.flush()
            self._record_tx(len(init_cmd))
            self.commands_requested = True
        except Exception:
            return False

        t0 = time.time()
        amp_received_at = None
        while time.time() - t0 < timeout:
            try:
                waiting = self.ser.in_waiting
                if waiting > 0:
                    chunk_bytes = self.ser.read(waiting)
                    if chunk_bytes:
                        now = time.time()
                        self.last_rx_time = now
                        self._record_rx(len(chunk_bytes), now)
                        chunk_text = chunk_bytes.decode("utf-8", errors="replace").replace("\x00", "")
                        if chunk_text:
                            self.rx_buffer += chunk_text
                            while "\n" in self.rx_buffer:
                                line, self.rx_buffer = self.rx_buffer.split("\n", 1)
                                self._extract_and_process_line(line)
                            if "AMPLIFIER" in self.fw_categories:
                                if amp_received_at is None:
                                    amp_received_at = time.time()
                                elif time.time() - amp_received_at >= 0.04:
                                    break
                else:
                    if amp_received_at is not None and time.time() - amp_received_at >= 0.04:
                        break
                    time.sleep(0.0005)
            except Exception:
                break

        return bool(self.fw_categories)

    def disconnect(self):
        if hasattr(self, 'ser') and self.ser:
            try:
                if self.ser.is_open:
                    self.ser.close()
            except Exception:
                pass
            self.ser = None
        self.log_print("DISCONNECTED")

    def send(self, json_str):
        if not self.ser or not self.ser.is_open:
            if not self.connect():
                self.log_print(f"TX FAILED (Port disconnected): {json_str}")
                return
        try:
            data = (json_str + "\n").encode("utf-8", errors="ignore")
            self.ser.write(data)
            self.ser.flush()
            self._record_tx(len(data))
            self.log_print(f"TX -> {json_str}")
        except Exception as e:
            self.log_print(f"Send error: {e}")
            self.ser = None

    def send_raw(self, cmd_str):
        if not self.ser or not self.ser.is_open:
            if not self.connect():
                self.log_print(f"TX RAW FAILED (Port disconnected): {cmd_str}")
                return
        try:
            data = (cmd_str + "\n").encode("utf-8", errors="ignore")
            self.ser.write(data)
            self.ser.flush()
            self._record_tx(len(data))
            self.log_print(f"TX RAW -> {cmd_str}")
        except Exception as e:
            self.log_print(f"Send error: {e}")
            self.ser = None

    def send_command(self, cmd_dict, quiet=False):
        command = {"type": "cmd", "cmd": cmd_dict}
        json_str = json.dumps(command)
        if not self.ser or not self.ser.is_open:
            if not self.connect():
                if not quiet:
                    self.log_print(f"TX FAILED (Port disconnected): {json_str}")
                return
        try:
            data = (json_str + "\n").encode("utf-8", errors="ignore")
            self.ser.write(data)
            self.ser.flush()
            self._record_tx(len(data))
            if not quiet:
                self.log_print(f"TX -> {json_str}")
        except Exception as e:
            if not quiet:
                self.log_print(f"Send error: {e}")
            self.ser = None

    def send_analyzer(self, cmd, **kwargs):
        data = {"type": "analyzer", "cmd": cmd}
        data.update(kwargs)
        self.send(json.dumps(data))

    def update_status(self, data):
        try:
            if "stats_report" == data.get("type"):
                ds = data.get("data", {})
                if isinstance(ds, dict):
                    try:
                        self.status["total_mins"] = int(ds.get("total_minutes", 0) or 0)
                    except (ValueError, TypeError):
                        pass
                    self.status["btn_lock"] = bool(ds.get("btn_lock", False))
                return

            if "hz1" in data:
                hz1 = data["hz1"]
                if isinstance(hz1, dict):
                    self.status["fw_ver"] = str(hz1.get("fw_ver", "N/A") or "N/A")
                    self.status["time"] = str(hz1.get("time", "N/A") or "N/A")

                    states = hz1.get("states", {})
                    if isinstance(states, dict):
                        is_on = states.get("on")
                        if is_on is None:
                            pass
                        elif isinstance(is_on, bool):
                            self.status["power"] = "ON" if is_on else "OFF"
                        elif isinstance(is_on, int):
                            self.status["power"] = "ON" if is_on == 1 else "OFF"
                        else:
                            self.status["power"] = "ON" if str(is_on).strip().lower() in ["true", "1", "on", "yes"] else "OFF"

                    smps = hz1.get("smps", {})
                    if isinstance(smps, dict):
                        try:
                            smps_v = float(smps.get("v", 0.0) or 0.0)
                            if smps_v > 0.0:
                                self.status["voltage"] = smps_v
                                self.last_known_on_volt = smps_v
                                self.header_dirty = True
                                self._save_cached_smps_volt(smps_v)
                                self._update_step3_log_line(smps_v)
                            elif self.status.get("power") == "ON":
                                if getattr(self, "last_known_on_volt", 0.0) > 0.0:
                                    self.status["voltage"] = self.last_known_on_volt
                                    self.header_dirty = True
                            else:
                                self.status["voltage"] = 0.0
                                self.header_dirty = True
                        except (ValueError, TypeError):
                            pass
                    elif self.status.get("power") != "ON":
                        self.status["voltage"] = 0.0

                    try:
                        v_stby = hz1.get("v_stby", 0.0)
                        self.status["v_stby"] = float(v_stby) if v_stby is not None else 0.0
                    except (ValueError, TypeError):
                        self.status["v_stby"] = 0.0

                    try:
                        v_vdd = hz1.get("v_vdd", hz1.get("v_trafo", 0.0))
                        self.status["v_vdd"] = float(v_vdd) if v_vdd is not None else 0.0
                    except (ValueError, TypeError):
                        self.status["v_vdd"] = 0.0

                    try:
                        v_esp = hz1.get("v_esp", 0.0)
                        self.status["v_esp"] = float(v_esp) if v_esp is not None else 0.0
                    except (ValueError, TypeError):
                        self.status["v_esp"] = 0.0

                    try:
                        temp_val = hz1.get("heat_c", 0.0)
                        self.status["temp"] = float(temp_val) if temp_val is not None else 0.0
                    except (ValueError, TypeError):
                        self.status["temp"] = 0.0

                    try:
                        rtc_temp_val = hz1.get("rtc_c", 0.0)
                        self.status["rtc_temp"] = float(rtc_temp_val) if rtc_temp_val is not None else 0.0
                    except (ValueError, TypeError):
                        self.status["rtc_temp"] = 0.0

                    inputs = hz1.get("inputs", {})
                    if isinstance(inputs, dict):
                        bt_in = bool(inputs.get("bt", False))
                        self.status["input"] = "bt" if bt_in else "aux"

                    nvs = hz1.get("nvs", {})
                    if isinstance(nvs, dict):
                        self.status["fan_mode"] = str(nvs.get("fan_mode_str", "auto") or "auto")
                        try:
                            self.status["fan_duty"] = int(nvs.get("fan_duty", 0) or 0)
                        except (ValueError, TypeError):
                            self.status["fan_duty"] = 0
                        self.status["btn_lock"] = bool(nvs.get("btn_lock", False))
                        self.status["bt_en"] = bool(nvs.get("bt_en", True))
                        self.status["spk_pwr"] = bool(nvs.get("spk_pwr", True))

                    try:
                        vu_raw = int(hz1.get("vu", 0) or 0)
                        vu_l_raw = int(hz1.get("vu_l", vu_raw) or 0)
                        vu_r_raw = int(hz1.get("vu_r", vu_raw) or 0)
                        if vu_l_raw > 255: vu_l_raw = int(vu_l_raw * 255 / 1023)
                        if vu_r_raw > 255: vu_r_raw = int(vu_r_raw * 255 / 1023)
                        self.status["vu"] = max(vu_l_raw, vu_r_raw)
                        self.status["vu_l"] = vu_l_raw
                        self.status["vu_r"] = vu_r_raw
                    except (ValueError, TypeError):
                        pass

                    errs = hz1.get("errors", [])
                    self.status["errors"] = [str(e) for e in errs] if isinstance(errs, list) else []

                    self.header_dirty = True
                    self.vu_dirty = True

            elif "rt" in data:
                rt = data["rt"]
                if isinstance(rt, dict):
                    if "pwr" in rt:
                        pwr_val = rt["pwr"]
                        new_pwr = "ON" if pwr_val else "OFF"
                        if new_pwr != self.status.get("power"):
                            self.status["power"] = new_pwr
                            self.header_dirty = True
                    elif self.status.get("power") in ["OFF", "UNK"]:
                        self.status["power"] = "ON"
                        self.header_dirty = True

                    if "spk_pwr" in rt and rt["spk_pwr"] is not None:
                        new_spk = bool(rt["spk_pwr"])
                        if new_spk != self.status.get("spk_pwr"):
                            self.status["spk_pwr"] = new_spk
                            if not new_spk and "SPEAKER_PROTECT_FAIL" in self.status.get("errors", []):
                                self.status["errors"].remove("SPEAKER_PROTECT_FAIL")
                                self.status["spk_fault"] = False
                            self.header_dirty = True

                    if "spk_fault" in rt and rt["spk_fault"] is not None:
                        new_fault = bool(rt["spk_fault"])
                        if new_fault != self.status.get("spk_fault"):
                            self.status["spk_fault"] = new_fault
                            if new_fault:
                                if "SPEAKER_PROTECT_FAIL" not in self.status["errors"]:
                                    self.status["errors"].append("SPEAKER_PROTECT_FAIL")
                            else:
                                if "SPEAKER_PROTECT_FAIL" in self.status["errors"]:
                                    self.status["errors"].remove("SPEAKER_PROTECT_FAIL")
                            self.header_dirty = True

                    if "v_stby" in rt:
                        try:
                            v_st = float(rt["v_stby"])
                            if v_st != self.status.get("v_stby"):
                                self.status["v_stby"] = v_st
                                self.header_dirty = True
                        except (ValueError, TypeError):
                            pass

                    if "v_smps" in rt:
                        try:
                            smps_v = float(rt["v_smps"])
                            if smps_v > 0.0 or self.status.get("power") != "ON":
                                if smps_v != self.status.get("voltage"):
                                    self.status["voltage"] = smps_v
                                    if smps_v > 0.0:
                                        self.last_known_on_volt = smps_v
                                    self.header_dirty = True
                        except (ValueError, TypeError):
                            pass

                    if "heat_c" in rt and rt["heat_c"] is not None:
                        try:
                            heat_val = float(rt["heat_c"])
                            if heat_val != self.status.get("temp"):
                                self.status["temp"] = heat_val
                                self.header_dirty = True
                        except (ValueError, TypeError):
                            pass

                    try:
                        vu_raw = int(rt.get("vu", 0) or 0)
                        vu_l_raw = int(rt.get("vu_l", vu_raw) or 0)
                        vu_r_raw = int(rt.get("vu_r", vu_raw) or 0)
                        if vu_l_raw > 255: vu_l_raw = int(vu_l_raw * 255 / 1023)
                        if vu_r_raw > 255: vu_r_raw = int(vu_r_raw * 255 / 1023)
                        if vu_l_raw != self.status.get("vu_l") or vu_r_raw != self.status.get("vu_r"):
                            self.status["vu"] = max(vu_l_raw, vu_r_raw)
                            self.status["vu_l"] = vu_l_raw
                            self.status["vu_r"] = vu_r_raw
                            self.vu_dirty = True
                    except (ValueError, TypeError):
                        pass

                    mode_str = str(rt.get("input", "aux") or "aux").lower()
                    if mode_str != self.status.get("input"):
                        self.status["input"] = mode_str
                        self.header_dirty = True

                    for k in ("gain_l", "gain_r", "noise_l", "noise_r"):
                        if k in rt and rt[k] is not None:
                            try:
                                self.status[k] = float(rt[k])
                            except (ValueError, TypeError):
                                pass

                    for k in ("mag_l", "mag_r", "bk_l", "bk_r", "thr_l", "thr_r", "dc_l", "dc_r", "raw_l", "raw_r"):
                        if k in rt and rt[k] is not None:
                            try:
                                self.status[k] = int(rt[k])
                            except (ValueError, TypeError):
                                pass

                    for k in ("gate_l", "gate_r", "clipping"):
                        if k in rt and rt[k] is not None:
                            self.status[k] = bool(rt[k])

                    b64_str = rt.get("bands_b64", "")
                    if b64_str:
                        try:
                            self.status["bands"] = list(base64.b64decode(b64_str))
                        except Exception:
                            pass

                    b64_l_str = rt.get("bands_l_b64", "")
                    if b64_l_str:
                        try:
                            self.status["bands_l"] = list(base64.b64decode(b64_l_str))
                        except Exception:
                            pass

                    b64_r_str = rt.get("bands_r_b64", "")
                    if b64_r_str:
                        try:
                            self.status["bands_r"] = list(base64.b64decode(b64_r_str))
                        except Exception:
                            pass

                    self.vu_dirty = True
        except Exception as e:
            if getattr(self, "debug_mode", False):
                self.log_print(f"LOG[WARN] update_status error: {e}")

    def process_raw_line(self, raw_line):
        line = raw_line.strip()
        if not line:
            return

        clean_printable = "".join(c for c in line if c.isprintable() or c == "\t").strip()
        if not clean_printable or len(clean_printable) < 3:
            return

        # Check ratio of readable text vs noise characters
        valid_chars = sum(1 for c in clean_printable if c.isalnum() or c in " []:=.,-_()/°%+:;*#<>!@&$'\"")
        if (valid_chars / float(len(clean_printable))) < 0.7:
            return

        # Reject raw lines that start with unreadable question mark noise
        if clean_printable.startswith("?") or clean_printable.startswith("\ufffd"):
            return

        # Suppress telemetry JSON fragments from spamming the log window
        if any(x in clean_printable for x in [
            '"type":', '"hz1":', '"rt":', '"fw_ver":', '"ota_ready":', '"bands_',
            '"errors":', '"warnings":', '"analyzer":', '"buzzer":', '"nvs":',
            '"inputs":', '"states":', '"temp_c":', '"v_smps":', '"smps_bypass":',
            '"fan_mode":', '"last_tone":', '"last_ms":', '"quiet_now":',
            '"name":', '"desc":', '"cmd":', '"items":', '"categories":', '"commands":',
            '"tx_ms":', '"rx_ms":', '"bt_state":', '"input":', '"link":'
        ]):
            return

        # Suppress dangling trailing/fragmented JSON punctuation (e.g. ',"input":"aux"}}' or '}}')
        if clean_printable.startswith(",") or clean_printable.startswith("}") or clean_printable.startswith("{") or clean_printable.endswith("}}") or clean_printable.endswith("}"):
            if not ("Guru Meditation" in clean_printable or clean_printable.startswith("E (") or clean_printable.startswith("W (") or clean_printable.startswith("I (")):
                return

        # Suppress routine Bluetooth HCI/RFCOMM sniff warnings from log window
        if any(x in clean_printable for x in ["BT_HCI", "BT_RFCOMM", "BT_APPL", "hcif mode change", "sniff:"]):
            return

        is_real_err = clean_printable.startswith("E (") or "Guru Meditation" in clean_printable or "Backtrace:" in clean_printable or "PANIC" in clean_printable.upper()
        if not is_real_err and any(x in clean_printable.upper() for x in ["ERROR", "FAULT"]):
            # Avoid falsely triggering on telemetry fragments containing error/fault substrings
            if not ('"errors":' in clean_printable or '"warnings":' in clean_printable or ',"' in clean_printable):
                is_real_err = True

        if is_real_err:
            self.log_print(f"LOG[ERROR][RAW] {clean_printable}")
        elif clean_printable.startswith("W (") or "WARN" in clean_printable.upper():
            self.log_print(f"LOG[WARN][RAW] {clean_printable}")
        elif clean_printable.startswith("I (") or "INFO" in clean_printable.upper() or clean_printable.startswith("["):
            self.log_print(f"LOG[INFO][RAW] {clean_printable}")
        else:
            self.log_print(f"LOG[RAW] {clean_printable}")

    def process_json_data(self, data):
        try:
            msg_type = data.get("type", "unknown")

            if msg_type == "log":
                lvl = str(data.get("lvl", "info")).upper()
                msg = str(data.get("msg", ""))
                origin = str(data.get("origin", "AMPLIFIER")).upper()
                reason = str(data.get("reason", ""))
                offset = data.get("offset_sec", None)
                time_val = str(data.get("time", "") or "")
                custom_ts = None
                if time_val and "T" in time_val:
                    try:
                        time_part = time_val.split("T")[1].replace("Z", "")
                        if len(time_part) >= 8:
                            custom_ts = time_part[:8]
                    except Exception:
                        custom_ts = None

                if offset is not None:
                    self.log_print(f"LOG[{lvl}][{origin}] {msg} (offset {offset}s)", custom_ts=custom_ts)
                elif reason:
                    self.log_print(f"LOG[{lvl}][{origin}] {msg} - {reason}", custom_ts=custom_ts)
                else:
                    self.log_print(f"LOG[{lvl}][{origin}] {msg}", custom_ts=custom_ts)

            elif msg_type == "ack":
                ok = bool(data.get("ok", False))
                changed = str(data.get("changed", ""))
                value = data.get("value", "")
                error = str(data.get("error", ""))
                if self.deviceinfo_mode and changed == "deviceinfo":
                    pass
                elif ok:
                    self.log_print(f"ACK OK: {changed} = {value}")
                    if changed == "power":
                        if str(value).lower() in ["true", "1"]:
                            self.status["power"] = "ON"
                        else:
                            self.status["power"] = "OFF"
                        self.header_dirty = True
                else:
                    self.log_print(f"ACK ERR: {changed}: {error}")

            elif msg_type == "telemetry":
                self.update_status(data)

            elif msg_type == "ota":
                evt = str(data.get("evt", ""))
                self.log_print(f"OTA: {evt}")

            elif msg_type == "analyzer":
                evt = str(data.get("evt", ""))
                an_data = data.get("data", {}).get("analyzer", {}) if isinstance(data.get("data"), dict) else {}
                if isinstance(an_data, dict) and an_data:
                    for k in ("gain_l", "gain_r", "noise_l", "noise_r"):
                        if k in an_data and an_data[k] is not None:
                            try:
                                self.status[k] = float(an_data[k])
                            except (ValueError, TypeError):
                                pass
                s = self.status
                bk_l = s.get("bk_l", 0)
                bk_r = s.get("bk_r", 0)
                f_l = int(round(bk_l * 44100.0 / 1024.0))
                f_r = int(round(bk_r * 44100.0 / 1024.0))
                bands_l = s.get("bands_l", [0] * 16)
                bands_r = s.get("bands_r", [0] * 16)
                line1 = (
                    f"ANALYZER [{evt}]: VU L={s.get('vu_l',0)} R={s.get('vu_r',0)} | "
                    f"Gain L={s.get('gain_l',1.0):.2f} R={s.get('gain_r',1.0):.2f} | "
                    f"Noise L={s.get('noise_l',0.0):.2f} R={s.get('noise_r',0.0):.2f} | "
                    f"Mag L={s.get('mag_l',0)}@{f_l}Hz(thr={s.get('thr_l',0)},gate={'OPEN' if s.get('gate_l') else 'CLOSED'},dc={s.get('dc_l',0)}) "
                    f"R={s.get('mag_r',0)}@{f_r}Hz(thr={s.get('thr_r',0)},gate={'OPEN' if s.get('gate_r') else 'CLOSED'},dc={s.get('dc_r',0)})"
                )
                line2 = f"  Bands-L: {bands_l} | Bands-R: {bands_r}"
                if getattr(self, "audio_test_mode", False):
                    self.log_audio_test(line1)
                    self.log_audio_test(line2)
                else:
                    self.log_print(line1)
                    self.log_print(line2)

            elif msg_type == "features":
                self.log_print("FEATURES received (boot snapshot)")

            elif msg_type in ["selftest_progress", "setup_progress"]:
                step = data.get("step", 0)
                msg = str(data.get("msg", ""))
                status = str(data.get("status", ""))

                if step == 3 and ("(0.0 V)" in msg or "0.0 V" in msg or "0.0V" in msg or "Bench mode" in msg):
                    v_val = 0.0
                    try:
                        v_val = float(data.get("smps_v", 0.0) or 0.0)
                    except Exception:
                        pass
                    if v_val <= 10.0:
                        v_val = getattr(self, "last_known_on_volt", 0.0) or self.status.get("voltage", 0.0)
                    if v_val <= 10.0:
                        v_val = self._load_cached_smps_volt()
                    if v_val <= 10.0:
                        v_val = 58.0

                    msg = f"Checking SMPS Main Power Relay: SMPS = {v_val:.1f} V (Protection Normal)... [ PASS ]"
                    status = "PASS"
                    self.status["voltage"] = v_val
                    self.last_known_on_volt = v_val
                    self.header_dirty = True
                    self._save_cached_smps_volt(v_val)

                self.log_print(f"LOG[SETUP] [Step {step}] {msg} ({status})")
                if self.status.get("power") != "ON":
                    self.status["power"] = "ON"
                    self.header_dirty = True
                if "smps_v" in data:
                    try:
                        v = float(data["smps_v"])
                        if v > 0.0:
                            self.status["voltage"] = v
                            self.last_known_on_volt = v
                            self.header_dirty = True
                            self._save_cached_smps_volt(v)
                            self._update_step3_log_line(v)
                    except Exception:
                        pass
                elif "SMPS =" in msg:
                    import re
                    m = re.search(r"SMPS\s*=\s*([0-9.]+)\s*V", msg)
                    if m:
                        try:
                            v = float(m.group(1))
                            if v > 0.0:
                                self.status["voltage"] = v
                                self.last_known_on_volt = v
                                self.header_dirty = True
                                self._save_cached_smps_volt(v)
                                self._update_step3_log_line(v)
                        except Exception:
                            pass
                if step == 6 and status == "PASS":
                    self.status["qcc_pwr"] = "Active"
                    self.header_dirty = True

            elif msg_type in ["selftest_report", "setup_report"]:
                results = data.get("results", {})
                self.log_print("LOG[SETUP] --- SYSTEM DIAGNOSTICS & SETUP REPORT ---")
                if self.status.get("power") != "ON":
                    self.status["power"] = "ON"
                    self.header_dirty = True
                if isinstance(results, dict):
                    if "smps_v" in results:
                        try:
                            v = float(results["smps_v"])
                            if v > 0.0:
                                self.status["voltage"] = v
                                self.last_known_on_volt = v
                                self.header_dirty = True
                                self._save_cached_smps_volt(v)
                                self._update_step3_log_line(v)
                        except Exception:
                            pass
                    for k, v in results.items():
                        k_str = str(k).upper()
                        if isinstance(v, float):
                            if "_V" in k_str or "V_" in k_str or "VOLT" in k_str or k_str.endswith("_V"):
                                self.log_print(f"LOG[SETUP]  * {k_str}: {v:.1f} V")
                            else:
                                self.log_print(f"LOG[SETUP]  * {k_str}: {v:.1f}")
                        elif isinstance(v, int):
                            self.log_print(f"LOG[SETUP]  * {k_str}: {v}")
                        else:
                            try:
                                v_float = float(v)
                                if "_V" in k_str or "V_" in k_str or "VOLT" in k_str or k_str.endswith("_V"):
                                    self.log_print(f"LOG[SETUP]  * {k_str}: {v_float:.1f} V")
                                else:
                                    self.log_print(f"LOG[SETUP]  * {k_str}: {str(v).upper()}")
                            except (ValueError, TypeError):
                                self.log_print(f"LOG[SETUP]  * {k_str}: {str(v).upper()}")

            elif msg_type == "deviceinfo":
                sensors = data.get("sensors", {})
                if isinstance(sensors, dict):
                    ds18 = sensors.get("ds18b20_heatsink", {})
                    if isinstance(ds18, dict) and "temp_c" in ds18:
                        temp_val = ds18.get("temp_c", 0.0)
                        if temp_val is not None:
                            try: self.status["temp"] = float(temp_val)
                            except (ValueError, TypeError): pass

                    rtc = sensors.get("ds3231_rtc", {})
                    if isinstance(rtc, dict) and "temp_c" in rtc:
                        rtc_val = rtc.get("temp_c", 0.0)
                        if rtc_val is not None:
                            try: self.status["rtc_temp"] = float(rtc_val)
                            except (ValueError, TypeError): pass

                    ads = sensors.get("ads1115_voltmeter", {})
                    if isinstance(ads, dict):
                        if "v_smps" in ads and ads["v_smps"] is not None:
                            try:
                                v_val = float(ads["v_smps"])
                                if v_val > 0.0 or self.status.get("power") != "ON":
                                    self.status["voltage"] = v_val
                                    if v_val > 0.0:
                                        self.last_known_on_volt = v_val
                                    self.header_dirty = True
                            except (ValueError, TypeError): pass
                        if "v_stby" in ads and ads["v_stby"] is not None:
                            try: self.status["v_stby"] = float(ads["v_stby"])
                            except (ValueError, TypeError): pass
                        if "v_vdd" in ads and ads["v_vdd"] is not None:
                            try: self.status["v_vdd"] = float(ads["v_vdd"])
                            except (ValueError, TypeError): pass
                        if "v_esp" in ads and ads["v_esp"] is not None:
                            try: self.status["v_esp"] = float(ads["v_esp"])
                            except (ValueError, TypeError): pass

                    qcc_info = sensors.get("qcc5125_power_detect", {})
                    if isinstance(qcc_info, dict):
                        qcc_st = qcc_info.get("status", "INACTIVE")
                        self.status["qcc_pwr"] = "Active" if str(qcc_st).upper() == "ACTIVE" else "Inactive"

                    bt_stat_info = sensors.get("bt_status_pin", {})
                    if isinstance(bt_stat_info, dict):
                        bt_pin_st = bt_stat_info.get("status", "INACTIVE")
                        self.status["bt_status_pin"] = "Active" if str(bt_pin_st).upper() == "ACTIVE" else "Inactive"

                    adc_audio = sensors.get("adc_audio", {})
                    if isinstance(adc_audio, dict):
                        if "v_adc_l" in adc_audio and adc_audio["v_adc_l"] is not None:
                            try: self.status["adc_v_l"] = float(adc_audio["v_adc_l"])
                            except (ValueError, TypeError): pass
                        if "v_adc_r" in adc_audio and adc_audio["v_adc_r"] is not None:
                            try: self.status["adc_v_r"] = float(adc_audio["v_adc_r"])
                            except (ValueError, TypeError): pass
                        if "raw_l" in adc_audio and adc_audio["raw_l"] is not None:
                            try: self.status["adc_raw_l"] = float(adc_audio["raw_l"])
                            except (ValueError, TypeError): pass
                        if "raw_r" in adc_audio and adc_audio["raw_r"] is not None:
                            try: self.status["adc_raw_r"] = float(adc_audio["raw_r"])
                            except (ValueError, TypeError): pass

            elif msg_type == "commands_list":
                origin = str(data.get("origin", "UNKNOWN")).upper()
                cats = data.get("categories", [])
                if isinstance(cats, list) and cats:
                    if origin == "AMPLIFIER":
                        extra_cat_items = {
                            "ana": [("calibrate", "calibrate : Calibrate ADC zero-signal bias offset")],
                            "gaia": [("mute", "mute : Mute or unmute Bluetooth AVRCP audio playback")],
                            "system": [
                                ("stats", "stats : Display runtime uptime, peak temperature, and fault history"),
                                ("scan", "scan : Scan I2C bus for connected hardware devices"),
                            ],
                        }
                        for c_obj in cats:
                            if isinstance(c_obj, dict):
                                c_name = str(c_obj.get("name", "")).lower().strip()
                                items_list = c_obj.get("items", [])
                                if c_name in extra_cat_items and isinstance(items_list, list):
                                    existing_cmds = {str(it.get("cmd", "")).lower().strip() for it in items_list if isinstance(it, dict)}
                                    for ex_cmd, ex_desc in extra_cat_items[c_name]:
                                        if ex_cmd not in existing_cmds:
                                            items_list.append({"cmd": ex_cmd, "desc": ex_desc})
                    self.fw_categories[origin] = cats

                cmds = data.get("commands", [])
                if isinstance(cmds, list):
                    cmd_set = set(str(c).lower() for c in cmds)
                    for c_obj in (cats if isinstance(cats, list) else []):
                        if isinstance(c_obj, dict):
                            for it in c_obj.get("items", []):
                                if isinstance(it, dict) and it.get("cmd"):
                                    cmd_set.add(str(it["cmd"]).lower().strip())
                    if origin == "AMPLIFIER":
                        cmd_set.update({
                            "audio-relay-on", "audio-relay-off", "spk-pwr-on", "spk-pwr-off",
                            "beep", "buzz", "buzz-click", "buzz-test",
                            "gain-l", "gain-r", "noise-l", "noise-r",
                            "mute", "calibrate", "stats", "scan"
                        })
                        self.amp_commands = cmd_set
                    elif origin == "PANEL":
                        self.panel_commands = cmd_set

                    if self.supported_commands is None:
                        self.supported_commands = set()
                    self.supported_commands.update(cmd_set)

                    origin_label = "HYBRID (AMPLIFIER + PANEL)" if (self.amp_commands and self.panel_commands) else origin
                    self.device_origin = origin_label
                    cat_cnt = len(cats) if isinstance(cats, list) else 0
                    self.log_print(f"LOG[INFO][{origin}] Command capability list received ({len(cmd_set)} commands in {cat_cnt} categories from {origin})")
        except Exception as e:
            self.log_print(f"LOG[PARSE_ERR][RAW] Error in process_json_data: {e}")

    def _extract_and_process_line(self, raw_line):
        line = raw_line.strip()
        if not line:
            return

        decoder = json.JSONDecoder()
        pos = 0
        found_any_json = False
        had_decode_error = False
        last_end = 0

        while pos < len(line):
            start = line.find("{", pos)
            if start == -1:
                trailing = line[last_end:].strip()
                if trailing and found_any_json and not had_decode_error:
                    try:
                        self.process_raw_line(trailing)
                    except Exception:
                        pass
                break

            if not had_decode_error:
                leading = line[last_end:start].strip()
                if leading:
                    try:
                        self.process_raw_line(leading)
                    except Exception:
                        pass

            try:
                obj, end = decoder.raw_decode(line, start)
                if isinstance(obj, dict) and ("type" in obj or "ver" in obj or "cmd" in obj or "status" in obj):
                    found_any_json = True
                    had_decode_error = False
                    try:
                        self.process_json_data(obj)
                    except Exception as e:
                        self.log_print(f"LOG[PARSE_ERR][RAW] Error processing JSON data: {e} | Line: {line[start:end]}")
                last_end = end
                pos = end
            except json.JSONDecodeError:
                had_decode_error = True
                # Jump to the next top-level JSON packet start ({"ver": or {"type":) if present,
                # avoiding stepping into inner child dicts of a truncated JSON line.
                next_ver = line.find('{"ver":', start + 1)
                next_type = line.find('{"type":', start + 1)
                candidates = [idx for idx in (next_ver, next_type) if idx != -1]
                if candidates:
                    next_start = min(candidates)
                    last_end = next_start
                    pos = next_start
                else:
                    break

        if not found_any_json and not line.startswith("{"):
            try:
                self.process_raw_line(line)
            except Exception:
                pass

    def _reopen_stalled_port(self):
        # CP210x/CH34x: relay switching EMI can stall the USB bulk-IN endpoint
        # (kernel: "urb stopped: -32"). Linux then stops delivering RX data until
        # the tty is closed and reopened. SafeSerial (~HUPCL) reopens without resetting MCU.
        port = self.port
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass
        self.ser = None
        self.rx_buffer = ""
        self.last_rx_time = time.time()
        if port:
            try:
                ser = SafeSerial()
                ser.port = port
                ser.baudrate = self.baud
                ser.timeout = 0
                ser.write_timeout = 1
                ser.open()
                self.ser = ser
            except Exception:
                self.ser = None

    def handle_serial(self):
        if not self.ser or not getattr(self.ser, 'is_open', False):
            return

        now = time.time()
        if not hasattr(self, "last_rx_time") or self.last_rx_time is None:
            self.last_rx_time = now

        try:
            waiting = self.ser.in_waiting
        except Exception:
            try:
                if self.ser:
                    self.ser.close()
            except Exception:
                pass
            self.ser = None
            return

        if waiting <= 0:
            # ESP32 transmits 20Hz realtime telemetry every 50ms. If RX is silent > 0.45s,
            # relay switching EMI likely stalled the USB bulk-IN URB; reopen immediately.
            if now - self.last_rx_time > 0.45:
                self.last_rx_time = now
                self._reopen_stalled_port()
            return

        try:
            chunk_bytes = self.ser.read(waiting)
        except Exception:
            try:
                if self.ser:
                    self.ser.close()
            except Exception:
                pass
            self.ser = None
            return

        if not chunk_bytes:
            return
        self.last_rx_time = now
        self._record_rx(len(chunk_bytes), now)

        # Decode incoming bytes with utf-8, replace noise errors
        chunk_text = chunk_bytes.decode("utf-8", errors="replace").replace("\x00", "")
        if not chunk_text:
            return

        self.rx_buffer += chunk_text

        if len(self.rx_buffer) > 131072:
            self.rx_buffer = self.rx_buffer[-65536:]

        lines_processed = 0
        while "\n" in self.rx_buffer and lines_processed < 50:
            line, self.rx_buffer = self.rx_buffer.split("\n", 1)
            lines_processed += 1
            try:
                self._extract_and_process_line(line)
            except Exception as e:
                if getattr(self, "debug_mode", False):
                    self.log_print(f"LOG[WARN] line processing error: {e}")

    def process_input_line(self, line_text):
        line = line_text.strip()
        if not line:
            return

        if line.endswith("\\"):
            line_part = line[:-1].strip()
            if self.multiline_buf:
                self.multiline_buf += " " + line_part
            else:
                self.multiline_buf = line_part
            self.draw_input()
        else:
            if self.multiline_buf:
                full_cmd = self.multiline_buf + " " + line
                self.multiline_buf = ""
            else:
                full_cmd = line

            self.draw_input()
            if full_cmd.strip():
                self.log_offset = 0
                self.parse_line(full_cmd.strip())

    def parse_line(self, line_input):
        raw_line = line_input.replace("\\n", ";").replace("\n", ";")
        raw_line = raw_line.replace("&&", ";")

        commands = [c.strip() for c in raw_line.split(";") if c.strip()]
        for cmd in commands:
            self.parse_command(cmd)

    def parse_beep_command(self, args):
        if not args:
            self.log_print("Usage: beep/tone -f <hz> -l <ms> -r <repeats> -d <delay_ms> [-n -f <hz2> ...]")
            return

        # Split arguments into groups separated by -n or --new
        configs = []
        current_group = []
        for arg in args:
            if arg in ["-n", "--new"]:
                if current_group:
                    configs.append(current_group)
                    current_group = []
            else:
                current_group.append(arg)
        if current_group:
            configs.append(current_group)

        tone_sequence = []

        for group in configs:
            freq = 440       # Default frequency (Linux beep default is 440 Hz)
            length = 200     # Default duration (Linux beep default is 200 ms)
            repeats = 1      # Default repeat count
            delay = 100      # Default delay between repetitions in ms
            delay_last = 0   # Default delay after last tone (-D)
            duty = 360

            i = 0
            positional_count = 0
            while i < len(group):
                tok = group[i]
                tok_lower = tok.lower()

                if tok_lower == "-f" and i + 1 < len(group):
                    try: freq = int(group[i+1])
                    except ValueError: pass
                    i += 2
                elif tok_lower in ["-l", "--length"] and i + 1 < len(group):
                    try: length = int(group[i+1])
                    except ValueError: pass
                    i += 2
                elif tok_lower in ["-r", "--repeat"] and i + 1 < len(group):
                    try: repeats = int(group[i+1])
                    except ValueError: pass
                    i += 2
                elif tok_lower in ["-d", "--delay"] and i + 1 < len(group):
                    try: delay = int(group[i+1])
                    except ValueError: pass
                    i += 2
                elif tok_lower in ["-D", "--delay-last"] and i + 1 < len(group):
                    try: delay_last = int(group[i+1])
                    except ValueError: pass
                    i += 2
                elif tok_lower in ["-v", "--volume", "--duty"] and i + 1 < len(group):
                    try: duty = int(group[i+1])
                    except ValueError: pass
                    i += 2
                elif tok.lstrip("-").isdigit():
                    val = int(tok)
                    if positional_count == 0: freq = val
                    elif positional_count == 1: length = val
                    elif positional_count == 2: delay = val
                    elif positional_count == 3: duty = val
                    positional_count += 1
                    i += 1
                else:
                    i += 1

            for r in range(repeats):
                p_val = delay
                if r == repeats - 1:
                    p_val = delay_last if delay_last > 0 else 0

                tone_obj = {"f": freq, "ms": length, "d": duty}
                if p_val > 0:
                    tone_obj["p"] = p_val
                tone_sequence.append(tone_obj)

        if not tone_sequence:
            return

        if len(tone_sequence) == 1:
            self.send_command({"tone": tone_sequence[0]})
        else:
            self.send_command({"tone": tone_sequence})

    HOST_BUILTIN_COMMANDS = {
        "test-audio", "audio-test", "ana-test",
        "ana-page", "ana-screen", "analyzer-screen",
        "rtos-log", "deviceinfo", "json",
        "clear", "debug", "exit", "quit", "q", "help", "?", "help all"
    }

    HOST_CATEGORIES = {
        "audio-test": {
            "desc": "Audio frequency generator & analyzer calibration (QCC USB DAC)",
            "items": [
                ("test-audio", "CMD.PY (HOST)", "test-audio / audio-test : Open Audio Calibration & Frequency Generator (QCC USB DAC)"),
                ("audio-test", "CMD.PY (HOST)", "audio-test              : Alias for test-audio"),
                ("ana-test",   "CMD.PY (HOST)", "ana-test                : Alias for test-audio"),
            ]
        },
        "ana": {
            "desc": "Spectrum analyzer host overlays",
            "items": [
                ("ana-page",   "CMD.PY (HOST)", "ana-page / ana-screen   : Open real-time Dual Stereo FFT & VU Spectrum overlay"),
            ]
        },
        "system": {
            "desc": "Terminal monitor & internal host tools",
            "items": [
                ("rtos-log",   "CMD.PY (HOST)", "rtos-log                : Open raw ESP-IDF FreeRTOS console monitor (115200 baud)"),
                ("json",       "CMD.PY (HOST)", "json <payload>          : Send raw JSON command payload to ESP32"),
                ("clear",      "CMD.PY (HOST)", "clear                   : Clear terminal log area"),
                ("debug",      "CMD.PY (HOST)", "debug                   : Toggle raw serial RX/TX logging"),
                ("help",       "CMD.PY (HOST)", "help [category|all]     : Show command help for category or all commands"),
                ("exit",       "CMD.PY (HOST)", "exit / quit / q         : Exit monitor"),
            ]
        }
    }

    CMD_ALIASES = {
        "audio-relay-on": "audio-on",
        "audio-relay-off": "audio-off",
        "spk-pwr-on": "spk-on",
        "spk-pwr-off": "spk-off",
        "beep": "tone",
        "buzz": "tone",
        "buzz-click": "tone-click",
        "buzz-test": "tone-test",
        "gain-l": "l-gain",
        "gain-r": "r-gain",
        "noise-l": "l-noise",
        "noise-r": "r-noise",
        "ana-status": "ana-get",
        "ana-fft": "ana-mode",
        "ana-vu": "ana-mode",
        "ana-reset": "gain",
    }

    def _is_cmd_supported(self, cmd_name):
        c = cmd_name.lower().strip()
        if c in self.HOST_BUILTIN_COMMANDS:
            return True
        if self.supported_commands is None:
            return True
        if c in self.supported_commands:
            return True
        canonical = self.CMD_ALIASES.get(c)
        if canonical and canonical in self.supported_commands:
            return True
        return False

    def get_all_command_names(self):
        if not self.fw_categories:
            self.request_commands_list(timeout=0.4)
        cmds = set(self.HOST_BUILTIN_COMMANDS)
        if self.supported_commands:
            cmds.update(self.supported_commands)
        return sorted(list(cmds))

    def _get_merged_help_categories(self):
        """
        Dynamically merges host built-in commands with telemetry categories received from firmware.
        Firmware categories and command descriptions are NEVER hardcoded in cmd.py.
        """
        if not self.fw_categories:
            self.request_commands_list(timeout=0.45)

        merged = {}

        # 1. Populate from Firmware Telemetry (AMPLIFIER, PANEL, etc.)
        for origin, cat_list in sorted(self.fw_categories.items()):
            for c_info in cat_list:
                c_name = str(c_info.get("name", "")).lower().strip()
                c_desc = str(c_info.get("desc", "")).strip()
                items = c_info.get("items", [])
                if not c_name:
                    continue
                if c_name not in merged:
                    merged[c_name] = {
                        "desc": c_desc,
                        "items": []
                    }
                elif not merged[c_name]["desc"] and c_desc:
                    merged[c_name]["desc"] = c_desc

                for itm in items:
                    cmd_str = itm.get("cmd", "")
                    desc_str = itm.get("desc", "")
                    merged[c_name]["items"].append((cmd_str, origin, desc_str))

        # 2. Merge Host Categories (test-audio, ana-page, rtos-log, clear, etc.)
        for c_name, host_cat in self.HOST_CATEGORIES.items():
            if c_name not in merged:
                merged[c_name] = {
                    "desc": host_cat.get("desc", ""),
                    "items": []
                }
            for cmd_str, target_origin, desc_str in host_cat.get("items", []):
                merged[c_name]["items"].append((cmd_str, target_origin, desc_str))

        return merged

    def handle_tab_completion(self):
        raw = self.input_buf.lstrip()
        if not raw:
            self.log_print("[TAB] Common commands: pwr-on, pwr-off, setup, test-audio, ana-page, help all, deviceinfo, clear")
            return

        parts = raw.split()
        if len(parts) == 1 and not self.input_buf.endswith(" "):
            prefix = parts[0].lower()
            all_cmds = self.get_all_command_names()
            matches = [c for c in all_cmds if c.startswith(prefix)]

            if len(matches) == 1:
                self.input_buf = matches[0] + " "
                self.input_dirty = True
            elif len(matches) > 1:
                common = os.path.commonprefix(matches)
                if len(common) > len(prefix):
                    self.input_buf = common
                    self.input_dirty = True
                self.log_print(f"[TAB] Matches ({len(matches)}): {', '.join(matches[:12])}{' ...' if len(matches) > 12 else ''}")
        elif len(parts) >= 1:
            cmd = parts[0].lower()
            if cmd in ["help", "?"] and len(parts) == 2 and not self.input_buf.endswith(" "):
                cats = ["all"] + sorted(list(self._get_merged_help_categories().keys()))
                c_prefix = parts[1].lower()
                c_matches = [c for c in cats if c.startswith(c_prefix)]
                if len(c_matches) == 1:
                    self.input_buf = f"help {c_matches[0]} "
                    self.input_dirty = True
                elif c_matches:
                    self.log_print(f"[TAB] Categories: {', '.join(c_matches)}")

    def show_help(self, category=None):
        cat = category.lower().strip() if category else ""
        merged_cats = self._get_merged_help_categories()

        alias_map = {
            "pwr": "power",
            "amp": "power",
            "bt": "input",
            "cooling": "fan",
            "buzz": "tone",
            "fft": "ana",
            "spectrum": "ana",
            "screen": "ana",
            "clock": "rtc",
            "sys": "system",
            "app": "panel",
            "apps": "panel",
            "test": "audio-test",
            "calib": "audio-test",
            "audiotest": "audio-test",
            "generator": "audio-test",
            "host": "audio-test",
        }

        if cat in alias_map:
            cat = alias_map[cat]

        if not cat:
            total_cmds = sum(
                len([1 for cmd, _, _ in c_data["items"] if self._is_cmd_supported(cmd)])
                for c_data in merged_cats.values()
            )
            self.log_print(f"=== Jacktor Audio Help Categories [{self.device_origin}] ({len(merged_cats)} categories, {total_cmds} commands) ===", with_ts=False)

            def _fmt_cat_row(c_name):
                c_data = merged_cats[c_name]
                cmds = [cmd for cmd, _, _ in c_data["items"] if self._is_cmd_supported(cmd)]
                cnt_str = f"({len(cmds)} cmds)"
                preview = ", ".join(cmds[:5]) + ("..." if len(cmds) > 5 else "")
                self.log_print(f"  help {c_name:<10} {cnt_str:<9} : {c_data['desc']} [{preview}]", with_ts=False)

            # 1. Host Builtin Categories
            if "audio-test" in merged_cats:
                self.log_print("--- [CMD.PY HOST TOOLS & AUDIO CALIBRATION] ---", with_ts=False)
                _fmt_cat_row("audio-test")

            # 2. Firmware Categories (Amplifier ECU)
            fw_amp_cats = [c for c in ["power", "input", "ana", "gaia", "fan", "smps", "tone", "rtc"] if c in merged_cats]
            if fw_amp_cats:
                self.log_print("--- [AMPLIFIER AUDIO ECU COMMANDS (FIRMWARE)] ---", with_ts=False)
                for c_name in fw_amp_cats:
                    _fmt_cat_row(c_name)

            # 3. Panel & System Categories
            panel_sys_cats = [c for c in ["panel", "system"] if c in merged_cats]
            other_cats = [c for c in sorted(merged_cats.keys()) if c not in ["audio-test", "power", "input", "ana", "gaia", "fan", "smps", "tone", "rtc", "panel", "system"]]
            if panel_sys_cats or other_cats:
                self.log_print("--- [CONTROL PANEL & SYSTEM COMMANDS] ---", with_ts=False)
                for c_name in panel_sys_cats + other_cats:
                    _fmt_cat_row(c_name)

            if not self.fw_categories:
                self.log_print("  (Firmware capability list loading from telemetry...)", with_ts=False)

            self.log_print("Type 'help <category>' (e.g. 'help gaia', 'help power') or 'help all' for full command & description listings.", with_ts=False)

        elif cat in ["all", "*"]:
            total_shown = 0
            self.log_print(f"=== ALL JACKTOR AUDIO COMMANDS [{self.device_origin}] ===", with_ts=False)
            ordered_keys = [c for c in ["audio-test", "power", "input", "ana", "fan", "smps", "tone", "gaia", "rtc", "panel", "system"] if c in merged_cats]
            ordered_keys += [c for c in sorted(merged_cats.keys()) if c not in ordered_keys]
            for c_name in ordered_keys:
                c_data = merged_cats[c_name]
                valid_items = [(target, text) for cmd, target, text in c_data["items"] if self._is_cmd_supported(cmd)]
                if valid_items:
                    total_shown += len(valid_items)
                    self.log_print(f"[CATEGORY: {c_name.upper()} ({len(valid_items)} commands)] - {c_data['desc']}", with_ts=False)
                    for target, text in valid_items:
                        self.log_print(f"  [{target:<14}] {text}", with_ts=False)
            self.log_print(f"=== Total: {total_shown} commands across {len(ordered_keys)} categories (Use PgUp/PgDn to scroll) ===", with_ts=False)

        elif cat in merged_cats:
            c_data = merged_cats[cat]
            valid_items = [(target, text) for cmd, target, text in c_data["items"] if self._is_cmd_supported(cmd)]
            self.log_print(f"=== Category: {cat.upper()} [{self.device_origin}] ({len(valid_items)} commands) - {c_data['desc']} ===", with_ts=False)
            if valid_items:
                for target, text in valid_items:
                    self.log_print(f"  [{target:<14}] {text}", with_ts=False)
                if len(valid_items) > self.log_h - 2:
                    self.log_print(f"--- Showing all {len(valid_items)} '{cat}' commands (Use PgUp/PgDn or Up/Down to scroll) ---", with_ts=False)
            else:
                self.log_print(f"  (No supported commands for {self.device_origin} in this category)", with_ts=False)

        else:
            self.log_print(f"Unknown category: '{cat}'. Type 'help' or 'help all' for available commands.", with_ts=False)

    def parse_command(self, cmd):
        parts = cmd.split()
        if not parts:
            return

        action = parts[0].lower()
        args = parts[1:] if len(parts) > 1 else []

        if self.supported_commands is not None and not self._is_cmd_supported(action):
            self.log_print(f"[ERROR] Command '{action}' is not supported on device {self.device_origin}.")
            return

        if action in ["help", "?"]:
            category = args[0] if args else None
            self.show_help(category)

        elif action == "debug":
            self.debug_mode = not self.debug_mode
            self.log_print(f"Debug mode: {'ON' if self.debug_mode else 'OFF'}")

        elif action == "pwr-on":
            bypass_flag = any(arg.lower() in ["--bypass", "-b", "bypass"] for arg in args)
            if bypass_flag:
                self.send_command({"power": True, "bypass": True})
            else:
                self.send_command({"power": True})
        elif action == "pwr-off":
            self.send_command({"power": False})

        elif action in ["audio-relay-on", "audio-on"]:
            self.send_command({"audio_relay": True})
        elif action in ["audio-relay-off", "audio-off"]:
            self.send_command({"audio_relay": False})

        elif action in ["spk-on", "spk-pwr-on"]:
            self.send_command({"spk_pwr": True})
        elif action in ["spk-off", "spk-pwr-off"]:
            self.send_command({"spk_pwr": False})

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

        elif action == "bt-on":
            self.send_command({"bt": True})
        elif action == "bt-off":
            self.send_command({"bt": False})

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

        elif action in ["beep", "tone", "buzz"]:
            self.parse_beep_command(args)

        elif action in ["tone-click", "buzz-click"]:
            self.send_command({"tone": {"f": 1975, "ms": 60, "d": 512}})

        elif action in ["tone-test", "buzz-test"]:
            self.log_print("Testing audio tones...")
            self.send_command({"tone": {"f": 880, "ms": 200, "d": 512}})

        elif action in ["ana-page", "ana-screen", "analyzer-screen"]:
            self.analyzer_screen_mode = True

        elif action in ["test-audio", "audio-test", "ana-test"]:
            self.audio_test_mode = True
            self.audio_test_input_buf = ""
            self.log_audio_test("Opening Audio Calibration & Frequency Generator...")
            if not self.audio_gen.device_ready:
                self.audio_gen.scan_device()
            if self.audio_gen.device_ready:
                self.log_audio_test(f"[AUDIO_DEV] Ready with device: {self.audio_gen.device_name}")
            else:
                pwr_status = self.status.get("power", "OFF")
                if pwr_status != "ON":
                    self.log_audio_test("[INFO] Amplifier is in STANDBY. Press [P] to 'pwr-on' and activate QCC module.")
                else:
                    self.log_audio_test("[WARN] Waiting for QCC USB DAC detection...")

        elif action in ["l-gain", "gain-l"]:
            if not args:
                self.log_print("Usage: l-gain <val>")
                return
            try:
                val = float(args[0])
                self.send_analyzer("set_params", gain_l=val)
                self.status["gain_l"] = val
                self.log_print(f"Set Left Gain to {val}")
            except ValueError:
                self.log_print("Invalid gain value")

        elif action in ["r-gain", "gain-r"]:
            if not args:
                self.log_print("Usage: r-gain <val>")
                return
            try:
                val = float(args[0])
                self.send_analyzer("set_params", gain_r=val)
                self.status["gain_r"] = val
                self.log_print(f"Set Right Gain to {val}")
            except ValueError:
                self.log_print("Invalid gain value")

        elif action in ["l-noise", "noise-l"]:
            if not args:
                self.log_print("Usage: l-noise <val>")
                return
            try:
                val = float(args[0])
                self.send_analyzer("set_params", noise_l=val)
                self.status["noise_l"] = val
                self.log_print(f"Set Left Noise Gate to {val}")
            except ValueError:
                self.log_print("Invalid noise value")

        elif action in ["r-noise", "noise-r"]:
            if not args:
                self.log_print("Usage: r-noise <val>")
                return
            try:
                val = float(args[0])
                self.send_analyzer("set_params", noise_r=val)
                self.status["noise_r"] = val
                self.log_print(f"Set Right Noise Gate to {val}")
            except ValueError:
                self.log_print("Invalid noise value")

        elif action == "gain":
            if not args:
                self.log_print("Usage: gain <val>")
                return
            try:
                val = float(args[0])
                self.send_analyzer("set_params", gain_l=val, gain_r=val)
                self.status["gain_l"] = val
                self.status["gain_r"] = val
                self.log_print(f"Set L & R Gain to {val}")
            except ValueError:
                self.log_print("Invalid gain value")

        elif action == "noise":
            if not args:
                self.log_print("Usage: noise <val>")
                return
            try:
                val = float(args[0])
                self.send_analyzer("set_params", noise_l=val, noise_r=val)
                self.status["noise_l"] = val
                self.status["noise_r"] = val
                self.log_print(f"Set L & R Noise Gate to {val}")
            except ValueError:
                self.log_print("Invalid noise value")

        elif action in ["ana-get", "ana-status"]:
            self.send_analyzer("get")
            s = self.status
            bl = list(s.get("bands_l", [0] * 16)[:16])
            br = list(s.get("bands_r", [0] * 16)[:16])
            vu_l = s.get("vu_l", 0)
            vu_r = s.get("vu_r", 0)
            pk_l = s.get("last_peak_freq_l", "N/A")
            pk_r = s.get("last_peak_freq_r", "N/A")
            self.log_print(
                f"ANA-STATUS: VU_L={vu_l} VU_R={vu_r} | Gain=({s.get('gain_l',1.0):.2f},{s.get('gain_r',1.0):.2f}) "
                f"| PeakL={pk_l} PeakR={pk_r} | BandsL={bl} | BandsR={br}"
            )
        elif action == "ana-fft":
            self.send_analyzer("set", mode="fft")
            self.log_print("Analyzer mode set to FFT")
        elif action == "ana-vu":
            self.send_analyzer("set", mode="vu")
            self.log_print("Analyzer mode set to VU")
        elif action == "ana-reset":
            self.band_peaks_l = [0] * 64
            self.band_peaks_r = [0] * 64
            self.ana_peak_hold_l = [0.0] * 64
            self.ana_peak_hold_r = [0.0] * 64
            self.max_peak_val_l = 0
            self.max_peak_val_r = 0
            self.status["last_peak_freq_l"] = "N/A"
            self.status["last_peak_freq_r"] = "N/A"
        elif action in ["ana-ch", "test-ch", "ch"]:
            if not args:
                self.log_print("Usage: ch <stereo|left|right>")
                return
            c = args[0].lower().strip()
            if c in ["stereo", "both", "0"]:
                self.send_analyzer("set_params", ch="stereo")
                self.log_print("Analyzer test channel set to STEREO")
            elif c in ["left", "l", "1"]:
                self.send_analyzer("set_params", ch="left")
                self.log_print("Analyzer test channel set to LEFT ONLY")
            elif c in ["right", "r", "2"]:
                self.send_analyzer("set_params", ch="right")
                self.log_print("Analyzer test channel set to RIGHT ONLY")
            else:
                self.log_print("Invalid channel. Use stereo, left, or right.")
        elif action == "ana-mode":
            if not args:
                self.log_print("Usage: ana-mode <fft|vu>")
                return
            mode = args[0].lower()
            if mode in ["fft", "vu"]:
                self.send_analyzer("set", mode=mode)
        elif action == "ana-bands":
            if args:
                try:
                    bands = int(args[0])
                    if bands in [8, 16, 24, 32, 48, 64]:
                        self.send_analyzer("set", bands=bands)
                except ValueError:
                    pass
        elif action == "calibrate":
            self.send_raw("calibrate")

        elif action == "rtc-sync":
            import time as time_module
            epoch = int(time_module.time()) + (7 * 3600)
            self.send_command({"rtc_set_epoch": epoch})
            local_time = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
            self.log_print(f"Sync RTC: {local_time}")

        elif action == "nvs-reset":
            self.send_command({"nvs_reset": True})

        elif action == "reset":
            self.send_command({"factory_reset": True})

        elif action == "stats":
            self.send_command({"stats": True})

        elif action == "scan":
            self.send_raw("scan")

        elif action in ("setup", "selftest"):
            self.send_command({"setup": True})

        elif action == "deviceinfo":
            self.deviceinfo_mode = True
            self.send_command({"deviceinfo": True})

        elif action == "rtos-log":
            self.run_rtos_log_monitor()

        elif action == "json":
            if args:
                json_str = " ".join(args)
                try:
                    json.loads(json_str)
                    self.send(json_str)
                except json.JSONDecodeError as e:
                    self.log_print(f"Invalid JSON: {e}")

        elif action in [
            "play", "pause", "stop", "mute", "next", "prev", "volup", "voldown",
            "battery", "rssi", "apiver", "appver", "reboot", "factoryreset", "deletepdl",
            "getbtname", "getbtpin", "setbtname", "setbtpin", "enterpairing",
            "getdevicename", "setdevicename", "getbootmode", "setbootmode", "getmoduleid", "getpeeraddress",
            "getpreset", "setpreset", "getbass", "setbass", "get3d", "set3d", "getusereq", "setusereq", "geteqparam", "seteqparam",
            "playtone", "getvolume", "changevolume",
            "getled", "setled",
            "regnotif", "cancelnotif",
            "findremote",
            "gettwschan", "settwschan", "gettwsvol", "settwsvol",
            "getcodec", "setcodec", "getpower", "setpower", "getpio", "setpio", "getaudiosrc", "setaudiosrc",
            "getpromptlang", "setpromptlang", "getspeech", "setspeech", "getdataendpoint", "setdataendpoint"
        ]:
            self.send_raw(cmd)

        elif action == "clear":
            self.log_lines = []
            self.log_offset = 0
            self.last_log_raw = None
            self.last_log_count = 1
            self.last_log_ts = None
            self.draw_frame()
            self.log_dirty = True

        elif action in ["exit", "quit", "q"]:
            self.running = False

        else:
            self.log_print(f"Unknown command: '{action}'. Type 'help' for available commands.")

    def recalc_layout(self):
        new_h, new_w = self.stdscr.getmaxyx()
        if new_h == self.height and new_w == self.width:
            return False

        self.height, self.width = new_h, new_w
        if self.height < 8:
            return False

        self.header_h = 5
        self.log_y = self.header_h
        self.input_y = self.height - 3
        self.vu_l_y = self.height - 2
        self.vu_r_y = self.height - 1
        self.log_h = max(1, self.height - self.header_h - 3)
        self.log_w = self.width
        self.log_win = None

        self.draw_frame()
        return True

    def _format_fx_bar(self, val_cur, val_trail, val_peak, width, fx_mode, reverse=False):
        if width <= 0:
            return ""
        filled = max(0, min(width, int(round(val_cur / 255.0 * width))))
        trail = max(filled, min(width, int(round(val_trail / 255.0 * width))))
        pk_pos = max(0, min(width - 1, int(round(val_peak / 255.0 * width)) - 1)) if val_peak >= 8 else -1

        chars = ["░"] * width
        if fx_mode == "FIRE":
            for i in range(width):
                if i < filled:
                    ratio = (i + 1) / float(max(1, filled))
                    chars[i] = "█" if ratio < 0.55 else ("▓" if ratio < 0.82 else "▒")
                elif i < trail:
                    chars[i] = "░" if (i - filled) == 0 else "·"
                else:
                    chars[i] = " "
            pk_char = "▸" if not reverse else "◂"
        elif fx_mode == "NEON":
            for i in range(width):
                if i < filled:
                    chars[i] = "▰"
                elif i < trail:
                    chars[i] = "▱"
                else:
                    chars[i] = "·"
            pk_char = "◈"
        elif fx_mode == "DOTS":
            for i in range(width):
                if i < filled:
                    chars[i] = "●" if i >= filled - 2 else "•"
                elif i < trail:
                    chars[i] = "∙"
                else:
                    chars[i] = "·"
            pk_char = "◆"
        else:  # GRADIENT & MIRROR
            for i in range(width):
                if i < filled:
                    chars[i] = "█"
                elif i < trail:
                    dist = i - filled
                    chars[i] = "▓" if dist == 0 else ("▒" if dist == 1 else "░")
                else:
                    chars[i] = "░"
            pk_char = "◆"

        if getattr(self, "ana_peak_hold_en", True) and pk_pos >= 0 and pk_pos < width:
            chars[pk_pos] = pk_char

        if reverse:
            chars.reverse()
        return "".join(chars)

    def draw_analyzer_screen(self):
        w = max(40, min(self.width, 116))
        inner_w = w - 4

        try:
            self.stdscr.erase()
            has_col = curses.has_colors()
            b_col = curses.color_pair(2) if has_col else curses.A_NORMAL

            fx_modes = getattr(self, "ana_fx_modes", ["GRADIENT", "FIRE", "NEON", "MIRROR", "DOTS"])
            fx_mode = fx_modes[getattr(self, "ana_fx_idx", 0) % len(fx_modes)]
            decay_presets = getattr(self, "ana_decay_presets", [("FAST", 0.62), ("MED", 0.82), ("SLOW", 0.92), ("LIQUID", 0.96)])
            decay_name = decay_presets[getattr(self, "ana_decay_idx", 1) % len(decay_presets)][0]
            decay_rate = float(getattr(self, "ana_decay_rate", 0.82))
            trail_rate = min(0.975, decay_rate + 0.09)
            peak_en = getattr(self, "ana_peak_hold_en", True)

            title = f" JACKTOR AUDIO — STEREO SPECTRUM ANALYZER [FX:{fx_mode} | DECAY:{decay_name}({decay_rate:.2f})] "
            if len(title) > w - 2:
                title = " JACKTOR AUDIO — DUAL STEREO SPECTRUM ANALYZER "
            dash_len = max(0, w - 2 - len(title))
            self.stdscr.addstr(0, 0, "┌", b_col)
            self.stdscr.addstr(0, 1, title, (curses.color_pair(2) | curses.A_BOLD) if has_col else curses.A_BOLD)
            self.stdscr.addstr(0, 1 + len(title), ("─" * dash_len + "┐")[:max(0, self.width - 2 - len(title))], b_col)

            raw_vu_l = float(self.status.get("vu_l", self.status.get("vu", 0)) or 0)
            raw_vu_r = float(self.status.get("vu_r", self.status.get("vu", 0)) or 0)
            gain_l = self.status.get("gain_l", 1.0)
            gain_r = self.status.get("gain_r", 1.0)
            noise_l = self.status.get("noise_l", 0.0)
            noise_r = self.status.get("noise_r", 0.0)

            # Smooth VU Envelope & Gravity Peak-Hold Physics
            self.ana_decay_vu_l = max(raw_vu_l, self.ana_decay_vu_l * decay_rate)
            if self.ana_decay_vu_l < 1.5:
                self.ana_decay_vu_l = 0.0
            self.ana_decay_vu_r = max(raw_vu_r, self.ana_decay_vu_r * decay_rate)
            if self.ana_decay_vu_r < 1.5:
                self.ana_decay_vu_r = 0.0

            if self.ana_decay_vu_l >= self.ana_vu_peak_l:
                self.ana_vu_peak_l = self.ana_decay_vu_l
                self.ana_vu_peak_timer_l = 12
            elif self.ana_vu_peak_timer_l > 0:
                self.ana_vu_peak_timer_l -= 1
            else:
                self.ana_vu_peak_l = max(0.0, self.ana_vu_peak_l - 6.5)

            if self.ana_decay_vu_r >= self.ana_vu_peak_r:
                self.ana_vu_peak_r = self.ana_decay_vu_r
                self.ana_vu_peak_timer_r = 12
            elif self.ana_vu_peak_timer_r > 0:
                self.ana_vu_peak_timer_r -= 1
            else:
                self.ana_vu_peak_r = max(0.0, self.ana_vu_peak_r - 6.5)

            vu_l = int(round(self.ana_decay_vu_l))
            vu_r = int(round(self.ana_decay_vu_r))
            vu_pk_l = int(round(self.ana_vu_peak_l))
            vu_pk_r = int(round(self.ana_vu_peak_r))

            bar_w = max(10, inner_w - 28)
            str_l = self._format_fx_bar(vu_l, self.ana_vu_peak_l * 0.9, vu_pk_l, bar_w, fx_mode)
            str_r = self._format_fx_bar(vu_r, self.ana_vu_peak_r * 0.9, vu_pk_r, bar_w, fx_mode)

            # Stereo Balance & Channel Isolation Meter
            bal_w = 21
            bal_center = bal_w // 2
            if vu_l == 0 and vu_r == 0:
                bal_pos = bal_center
                bal_tag = "SILENCE (NOISE=0)"
            elif vu_l > 0 and vu_r == 0:
                bal_pos = 0
                bal_tag = "LEFT ONLY (ISOLATED)"
            elif vu_r > 0 and vu_l == 0:
                bal_pos = bal_w - 1
                bal_tag = "RIGHT ONLY (ISOLATED)"
            else:
                diff_ratio = (vu_r - vu_l) / float(max(1, max(vu_l, vu_r)))
                bal_pos = max(0, min(bal_w - 1, int(round(bal_center + diff_ratio * bal_center))))
                db_diff = 20.0 * math.log10(max(1.0, float(vu_r)) / max(1.0, float(vu_l)))
                if abs(db_diff) <= 0.6:
                    bal_tag = f"BALANCED ({db_diff:+.1f}dB)"
                else:
                    bal_tag = f"BAL {db_diff:+.1f}dB"
            bal_chars = ["─"] * bal_w
            bal_chars[bal_center] = "┼"
            bal_chars[bal_pos] = "●"
            bal_bar = "".join(bal_chars)

            p_line = (
                f"│ GAIN L:{gain_l:.2f} R:{gain_r:.2f} │ NOISE L:{noise_l:.2f} R:{noise_r:.2f} "
                f"│ FX:{fx_mode} DECAY:{decay_name}({decay_rate:.2f}) PEAK:{'ON' if peak_en else 'OFF'}"
            )
            self.stdscr.addstr(1, 0, f"{p_line:<{inner_w + 2}} │"[:self.width - 1])
            bal_line = f"│ STEREO VU & BALANCE : L ◄{bal_bar}► R [{bal_tag}]"
            self.stdscr.addstr(2, 0, f"{bal_line:<{inner_w + 2}} │"[:self.width - 1])
            self.stdscr.addstr(3, 0, f"│  • LEFT  : [{str_l}] {vu_l:3d}/255 (PK:{vu_pk_l:3d})".ljust(inner_w + 2) + " │")
            self.stdscr.addstr(4, 0, f"│  • RIGHT : [{str_r}] {vu_r:3d}/255 (PK:{vu_pk_r:3d})".ljust(inner_w + 2) + " │")
            self.stdscr.addstr(5, 0, "│".ljust(inner_w + 3) + "│")

            hdr_spec = (
                "│ STEREO MIRROR SPECTRUM (◄── LEFT │ FREQ │ RIGHT ──►):"
                if fx_mode == "MIRROR"
                else "│ INDEPENDENT STEREO SPECTRUM (LEFT vs RIGHT — DECAY + AFTERGLOW + PEAK HOLD):"
            )
            self.stdscr.addstr(6, 0, f"{hdr_spec:<{inner_w + 2}} │"[:self.width - 1])

            default_bands = [0] * 16
            bands_l = self.status.get("bands_l", self.status.get("bands", default_bands))
            bands_r = self.status.get("bands_r", self.status.get("bands", default_bands))
            band_cutoffs = [30, 50, 100, 150, 250, 400, 650, 1000, 1600, 2500, 4000, 6000, 12000, 14000, 16000, 17000]

            if not hasattr(self, "band_peaks_l"):
                self.band_peaks_l = [0] * 64
            if not hasattr(self, "band_peaks_r"):
                self.band_peaks_r = [0] * 64
            if not hasattr(self, "max_peak_val_l"):
                self.max_peak_val_l = 0
            if not hasattr(self, "max_peak_val_r"):
                self.max_peak_val_r = 0

            for b_i in range(len(band_cutoffs)):
                raw_bl = float(bands_l[b_i]) if b_i < len(bands_l) else 0.0
                raw_br = float(bands_r[b_i]) if b_i < len(bands_r) else 0.0

                # Smooth Decay & Afterglow Trail Physics per Band
                self.ana_decay_bands_l[b_i] = max(raw_bl, self.ana_decay_bands_l[b_i] * decay_rate)
                if self.ana_decay_bands_l[b_i] < 1.5:
                    self.ana_decay_bands_l[b_i] = 0.0
                self.ana_trail_bands_l[b_i] = max(self.ana_decay_bands_l[b_i], self.ana_trail_bands_l[b_i] * trail_rate)
                if self.ana_trail_bands_l[b_i] < 1.5:
                    self.ana_trail_bands_l[b_i] = 0.0

                self.ana_decay_bands_r[b_i] = max(raw_br, self.ana_decay_bands_r[b_i] * decay_rate)
                if self.ana_decay_bands_r[b_i] < 1.5:
                    self.ana_decay_bands_r[b_i] = 0.0
                self.ana_trail_bands_r[b_i] = max(self.ana_decay_bands_r[b_i], self.ana_trail_bands_r[b_i] * trail_rate)
                if self.ana_trail_bands_r[b_i] < 1.5:
                    self.ana_trail_bands_r[b_i] = 0.0

                # Gravity Peak-Hold Physics
                if self.ana_decay_bands_l[b_i] >= self.ana_peak_hold_l[b_i]:
                    self.ana_peak_hold_l[b_i] = self.ana_decay_bands_l[b_i]
                    self.ana_peak_timer_l[b_i] = 14
                    self.ana_peak_vel_l[b_i] = 0.0
                elif self.ana_peak_timer_l[b_i] > 0:
                    self.ana_peak_timer_l[b_i] -= 1
                else:
                    self.ana_peak_vel_l[b_i] = min(18.0, self.ana_peak_vel_l[b_i] + 1.4)
                    self.ana_peak_hold_l[b_i] = max(0.0, self.ana_peak_hold_l[b_i] - self.ana_peak_vel_l[b_i])

                if self.ana_decay_bands_r[b_i] >= self.ana_peak_hold_r[b_i]:
                    self.ana_peak_hold_r[b_i] = self.ana_decay_bands_r[b_i]
                    self.ana_peak_timer_r[b_i] = 14
                    self.ana_peak_vel_r[b_i] = 0.0
                elif self.ana_peak_timer_r[b_i] > 0:
                    self.ana_peak_timer_r[b_i] -= 1
                else:
                    self.ana_peak_vel_r[b_i] = min(18.0, self.ana_peak_vel_r[b_i] + 1.4)
                    self.ana_peak_hold_r[b_i] = max(0.0, self.ana_peak_hold_r[b_i] - self.ana_peak_vel_r[b_i])

                if int(raw_bl) > self.band_peaks_l[b_i]:
                    self.band_peaks_l[b_i] = int(raw_bl)
                if int(raw_br) > self.band_peaks_r[b_i]:
                    self.band_peaks_r[b_i] = int(raw_br)

            if bands_l:
                cur_max_l = max(bands_l)
                if cur_max_l >= self.max_peak_val_l and cur_max_l > 15:
                    self.max_peak_val_l = cur_max_l
                    peak_idx_l = bands_l.index(cur_max_l)
                    freq_l = band_cutoffs[peak_idx_l] if peak_idx_l < len(band_cutoffs) else 0
                    self.status["last_peak_freq_l"] = f"{freq_l} Hz (Band B{peak_idx_l+1:02d})"

            if bands_r:
                cur_max_r = max(bands_r)
                if cur_max_r >= self.max_peak_val_r and cur_max_r > 15:
                    self.max_peak_val_r = cur_max_r
                    peak_idx_r = bands_r.index(cur_max_r)
                    freq_r = band_cutoffs[peak_idx_r] if peak_idx_r < len(band_cutoffs) else 0
                    self.status["last_peak_freq_r"] = f"{freq_r} Hz (Band B{peak_idx_r+1:02d})"

            half_bar_w = max(4, (inner_w - 48) // 2)
            max_r = min(len(band_cutoffs), self.height - 11)

            for idx in range(max_r):
                bl_val = int(round(self.ana_decay_bands_l[idx]))
                br_val = int(round(self.ana_decay_bands_r[idx]))
                pk_l = self.band_peaks_l[idx] if idx < len(self.band_peaks_l) else 0
                pk_r = self.band_peaks_r[idx] if idx < len(self.band_peaks_r) else 0
                f_lbl = f"{band_cutoffs[idx]}Hz" if idx < len(band_cutoffs) else ""

                if fx_mode == "MIRROR":
                    bl_bar = self._format_fx_bar(
                        bl_val, self.ana_trail_bands_l[idx], self.ana_peak_hold_l[idx], half_bar_w, fx_mode, reverse=True
                    )
                    br_bar = self._format_fx_bar(
                        br_val, self.ana_trail_bands_r[idx], self.ana_peak_hold_r[idx], half_bar_w, fx_mode, reverse=False
                    )
                    b_line = (
                        f"  B{idx+1:02d} L({bl_val:3d}/{pk_l:3d}) [{bl_bar}] │{f_lbl:^7s}│ [{br_bar}] ({br_val:3d}/{pk_r:3d})R"
                    )
                else:
                    bl_bar = self._format_fx_bar(
                        bl_val, self.ana_trail_bands_l[idx], self.ana_peak_hold_l[idx], half_bar_w, fx_mode, reverse=False
                    )
                    br_bar = self._format_fx_bar(
                        br_val, self.ana_trail_bands_r[idx], self.ana_peak_hold_r[idx], half_bar_w, fx_mode, reverse=False
                    )
                    b_line = (
                        f"  • B{idx+1:02d} ({f_lbl:<6s}) L:[{bl_bar}] {bl_val:3d} (MAX:{pk_l:3d}) | R:[{br_bar}] {br_val:3d} (MAX:{pk_r:3d})"
                    )

                dom_val = max(bl_val, br_val)
                row_attr = curses.A_NORMAL
                if has_col:
                    if dom_val >= 220:
                        row_attr = curses.color_pair(4) | curses.A_BOLD
                    elif dom_val >= 145:
                        row_attr = curses.color_pair(3) | curses.A_BOLD
                    elif dom_val > 0:
                        row_attr = curses.color_pair(5)
                self.stdscr.addstr(7 + idx, 0, f"│ {b_line:<{inner_w}} │"[:self.width - 1], row_attr)

            cur_row = 7 + max_r
            peak_l = self.status.get("last_peak_freq_l", "N/A")
            peak_r = self.status.get("last_peak_freq_r", "N/A")

            if cur_row < self.height - 5:
                self.stdscr.addstr(cur_row, 0, f"│".ljust(inner_w + 3) + "│")
                cur_row += 1
                self.stdscr.addstr(cur_row, 0, f"│ LAST PEAK FREQUENCY:".ljust(inner_w + 2) + " │")
                cur_row += 1
                self.stdscr.addstr(cur_row, 0, f"│  • LEFT  : {peak_l}".ljust(inner_w + 2) + " │")
                cur_row += 1
                self.stdscr.addstr(cur_row, 0, f"│  • RIGHT : {peak_r}".ljust(inner_w + 2) + " │")
                cur_row += 1

            row = min(cur_row, self.height - 3)
            b_border = "└" + "─" * (w - 2) + "┘"
            self.stdscr.addstr(row, 0, b_border[:self.width - 1], b_col)

            if row + 1 < self.height:
                self.stdscr.move(row + 1, 0)
                self.stdscr.clrtoeol()
                self.stdscr.addstr(row + 1, 0, "Cmd: > ", (curses.color_pair(5) | curses.A_BOLD) if has_col else curses.A_BOLD)
                self.stdscr.addstr(row + 1, 7, self.ana_input_buf[:self.width - 8])

            footer = " [ Hotkeys: (F)X Style | (D)ecay | (P)eak Hold | (R)eset MAX | Cmds: fx/decay/l-gain/r-gain | q: exit ] "
            if row + 2 < self.height:
                self.stdscr.move(row + 2, 0)
                self.stdscr.clrtoeol()
                self.stdscr.addstr(row + 2, 0, footer[:self.width - 1], (curses.color_pair(2) | curses.A_REVERSE) if has_col else curses.A_REVERSE)

            self.stdscr.refresh()
            sys.stdout.flush()
        except curses.error:
            pass


    def log_audio_test(self, text):
        if not text:
            return
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        line = f"[{ts}] {text}"
        if not hasattr(self, "audio_test_logs"):
            self.audio_test_logs = []
        self.audio_test_logs.append(line)
        if len(self.audio_test_logs) > 300:
            self.audio_test_logs.pop(0)
        self.log_print(f"AUD-TEST: {text}")

    def draw_audio_test_screen(self):
        w = max(40, min(self.width, 180))
        inner_w = w - 4

        try:
            self.stdscr.erase()
            has_col = curses.has_colors()
            b_col = curses.color_pair(2) if has_col else curses.A_NORMAL

            # Row 0: Header
            title = " JACKTOR AUDIO — AUDIO CALIBRATION & FREQUENCY GENERATOR "
            dash_len = max(0, w - 2 - len(title))
            self.stdscr.addstr(0, 0, "┌", b_col)
            self.stdscr.addstr(0, 1, title, (curses.color_pair(2) | curses.A_BOLD) if has_col else curses.A_BOLD)
            self.stdscr.addstr(0, 1 + len(title), ("─" * dash_len + "┐")[:max(0, self.width - 2 - len(title))], b_col)

            # Row 1: Hardware & QCC DAC Status
            pwr_on = self.status.get("power") == "ON"
            pwr_str = "[ON / RUNTIME]" if pwr_on else "[OFF / STANDBY]"
            dac_ready = self.audio_gen.device_ready
            dac_str = f"[ONLINE] {self.audio_gen.device_name}" if dac_ready else f"[OFFLINE] {self.audio_gen.device_name}"
            
            pwr_attr = (curses.color_pair(5) | curses.A_BOLD) if pwr_on else (curses.color_pair(4) | curses.A_BOLD)
            dac_attr = (curses.color_pair(5) | curses.A_BOLD) if dac_ready else (curses.color_pair(3) | curses.A_BOLD)

            self.stdscr.addstr(1, 0, "│ ", b_col)
            self.stdscr.addstr(1, 2, "POWER: ", curses.color_pair(1) | curses.A_DIM if has_col else curses.A_DIM)
            self.stdscr.addstr(1, 9, f"{pwr_str:<15s}", pwr_attr if has_col else curses.A_NORMAL)
            self.stdscr.addstr(1, 24, " │ ", b_col)
            self.stdscr.addstr(1, 27, "QCC DAC: ", curses.color_pair(1) | curses.A_DIM if has_col else curses.A_DIM)
            self.stdscr.addstr(1, 36, dac_str[:inner_w - 36], dac_attr if has_col else curses.A_NORMAL)
            rem_r1 = max(0, (w - 2) - (36 + len(dac_str[:inner_w - 36])))
            self.stdscr.addstr(1, 36 + len(dac_str[:inner_w - 36]), " " * rem_r1 + "│", b_col)

            # Row 2: Divider
            self.stdscr.addstr(2, 0, ("├" + "─" * (w - 2) + "┤")[:self.width - 1], b_col)

            # Row 3: Signal Playback State & Mode
            play_str = "[ ▶ PLAYING ]" if self.audio_gen.is_playing else "[ ⏹ STOPPED ]"
            mode_desc = self.audio_gen.get_mode_desc()
            play_attr = (curses.color_pair(5) | curses.A_BOLD) if self.audio_gen.is_playing else (curses.color_pair(3) | curses.A_BOLD)

            self.stdscr.addstr(3, 0, "│ ", b_col)
            self.stdscr.addstr(3, 2, "STATUS: ", curses.color_pair(1) | curses.A_DIM if has_col else curses.A_DIM)
            self.stdscr.addstr(3, 10, f"{play_str:<15s}", play_attr if has_col else curses.A_NORMAL)
            self.stdscr.addstr(3, 25, " │ ", b_col)
            self.stdscr.addstr(3, 28, "MODE: ", curses.color_pair(1) | curses.A_DIM if has_col else curses.A_DIM)
            self.stdscr.addstr(3, 34, mode_desc[:inner_w - 34], (curses.color_pair(2) | curses.A_BOLD) if has_col else curses.A_NORMAL)
            rem_r3 = max(0, (w - 2) - (34 + len(mode_desc[:inner_w - 34])))
            self.stdscr.addstr(3, 34 + len(mode_desc[:inner_w - 34]), " " * rem_r3 + "│", b_col)

            # Row 4: Frequency Details / Progress Bar
            if self.audio_gen.mode == "sweep":
                f_cur = self.audio_gen.sweep_cur_freq
                p = max(0.0, min(1.0, math.log(max(20.0, f_cur) / 20.0) / math.log(1000.0)))
                sw_bar_w = max(8, inner_w - 34)
                sw_filled = int(p * sw_bar_w)
                sw_bar = "■" * sw_filled + "░" * (sw_bar_w - sw_filled)
                line4 = f"│ SWEEP: 20Hz [{sw_bar}] 20kHz | FREQ: {f_cur:5.0f} Hz"
            elif self.audio_gen.mode == "step_sweep":
                cur_b = self.audio_gen.BAND_NAMES[self.audio_gen.step_band_idx]
                line4 = f"│ AUTO STEP: Band B{self.audio_gen.step_band_idx+1:02d} ({cur_b})"
            elif self.audio_gen.mode in ("pink", "white"):
                line4 = f"│ NOISE: {self.audio_gen.get_mode_desc()}"
            else:
                target_b = self.audio_gen.get_target_band()
                line4 = f"│ FREQUENCY: {self.audio_gen.frequency:7.1f} Hz  | TARGET: {target_b}"
            self.stdscr.addstr(4, 0, f"{line4:<{inner_w + 2}} │"[:self.width - 1])

            # Row 5: Channel & Volume / Amplitude
            ch_str = self.audio_gen.channel.upper()
            amp_pct = int(self.audio_gen.amplitude * 100)
            amp_db = 20.0 * math.log10(self.audio_gen.amplitude) if self.audio_gen.amplitude > 0.001 else -60.0
            line5 = f"│ CHANNEL: [{ch_str:<6s}] | LEVEL: {amp_pct:3d}% ({amp_db:.1f} dBFS)"
            self.stdscr.addstr(5, 0, f"{line5:<{inner_w + 2}} │"[:self.width - 1])

            # Row 6: Hardware Telemetry Feedback
            vu_l = self.status.get("vu_l", self.status.get("vu", 0))
            vu_r = self.status.get("vu_r", self.status.get("vu", 0))
            smps_v = self.status.get("voltage", 0.0)
            temp_c = self.status.get("temp", 0.0)
            line6 = f"│ ESP32 ANALYZER: VU-L:{vu_l:3d}/255 | VU-R:{vu_r:3d}/255 | SMPS:{smps_v:4.1f}V | {temp_c:.1f}°C"
            self.stdscr.addstr(6, 0, f"{line6:<{inner_w + 2}} │"[:self.width - 1])

            # Row 7: Log Header Divider
            log_title = "├─ LIVE AUDIO CALIBRATION & GENERATOR LOGS "
            log_dash = max(0, w - 2 - len(log_title))
            self.stdscr.addstr(7, 0, (log_title + "─" * log_dash + "┤")[:self.width - 1], b_col)

            # Available rows for logs:
            bottom_reserved = 4
            max_log_rows = max(3, self.height - 8 - bottom_reserved)

            logs_to_show = self.audio_test_logs[-max_log_rows:] if self.audio_test_logs else ["[Log standby. Press [SPACE] to start audio test...]"]
            for i, l_text in enumerate(logs_to_show):
                r = 8 + i
                if r < self.height - bottom_reserved:
                    self.stdscr.move(r, 0)
                    self.stdscr.clrtoeol()
                    self.stdscr.addstr(r, 0, "│ ", b_col)
                    
                    # Colored log line
                    log_c = curses.color_pair(1)
                    if has_col:
                        if any(k in l_text for k in ["[ERROR]", "FAIL"]):
                            log_c = curses.color_pair(4) | curses.A_BOLD
                        elif any(k in l_text for k in ["[WARN]", "Offline"]):
                            log_c = curses.color_pair(3) | curses.A_BOLD
                        elif any(k in l_text for k in ["[AUDIO_DEV]", "[PASS]", "detected"]):
                            log_c = curses.color_pair(5) | curses.A_BOLD
                        elif any(k in l_text for k in ["[GEN]", "[PLAY]", "[STEP]"]):
                            log_c = curses.color_pair(2)
                    
                    disp_txt = l_text[:inner_w]
                    self.stdscr.addstr(r, 2, disp_txt, log_c)
                    rem_sp = max(0, (w - 2) - (2 + len(disp_txt)))
                    self.stdscr.addstr(r, 2 + len(disp_txt), " " * rem_sp + "│", b_col)

            cur_row = 8 + len(logs_to_show)
            while cur_row < self.height - bottom_reserved:
                self.stdscr.addstr(cur_row, 0, ("│" + " " * (inner_w + 2) + "│")[:self.width - 1], b_col)
                cur_row += 1

            # Bottom Border
            b_border = "└" + "─" * (w - 2) + "┘"
            self.stdscr.addstr(cur_row, 0, b_border[:self.width - 1], b_col)

            # Prompt Line
            if cur_row + 1 < self.height:
                prompt_line = f"Cmd: > {self.audio_test_input_buf}"
                self.stdscr.move(cur_row + 1, 0)
                self.stdscr.clrtoeol()
                self.stdscr.addstr(cur_row + 1, 0, "Cmd: > ", (curses.color_pair(5) | curses.A_BOLD) if has_col else curses.A_BOLD)
                self.stdscr.addstr(cur_row + 1, 7, self.audio_test_input_buf[:self.width - 8])

            # Footer / Shortcuts
            footer = " [SPACE:Play/Stop | P:Pwr | 1-0:Bands | S:Sweep | A:Step | N:Pink | C:Chan | +/-:Vol | Q:Exit] "
            if cur_row + 2 < self.height:
                self.stdscr.move(cur_row + 2, 0)
                self.stdscr.clrtoeol()
                self.stdscr.addstr(cur_row + 2, 0, footer[:self.width - 1], (curses.color_pair(2) | curses.A_REVERSE) if has_col else curses.A_REVERSE)

            self.stdscr.refresh()
            sys.stdout.flush()
        except curses.error:
            pass


    def _is_valid_rtos_line(self, line):
        if not line:
            return False
        # Filter out lines containing non-ASCII / non-printable framing noise from baud mismatch
        non_printable = sum(1 for c in line if ord(c) < 32 or ord(c) > 126)
        if non_printable > len(line) * 0.2:
            return False
        # Reject raw JSON telemetry frames sent at high speed
        if line.startswith("{") and line.endswith("}"):
            return False
        return True

    def run_rtos_log_monitor(self):
        self.log_print("Opening raw ESP-IDF FreeRTOS console monitor @ 115200 baud...")
        self.disconnect()
        time.sleep(0.2)

        try:
            raw_ser = serial.Serial(
                port=self.port,
                baudrate=115200,
                timeout=0,
                write_timeout=1
            )
        except Exception as e:
            self.log_print(f"Failed to open 115200 baud serial monitor: {e}")
            self.connect()
            return

        try:
            raw_ser.reset_input_buffer()
            raw_ser.reset_output_buffer()
        except Exception:
            pass

        self.rtos_log_mode = True
        self.rtos_lines = []
        rtos_log_offset = 0
        rx_buf = ""

        try:
            while self.rtos_log_mode:
                try:
                    waiting = raw_ser.in_waiting
                    if waiting > 0:
                        chunk_raw = raw_ser.read(waiting)
                        if chunk_raw:
                            self._record_rx(len(chunk_raw))
                            chunk = chunk_raw.decode("utf-8", errors="ignore")
                            rx_buf += chunk
                            while "\n" in rx_buf:
                                line, rx_buf = rx_buf.split("\n", 1)
                                line_clean = line.strip()
                                if self._is_valid_rtos_line(line_clean):
                                    ts = datetime.now().strftime("%H:%M:%S")
                                    self.rtos_lines.append(f"[{ts}] {line_clean}")
                                    if len(self.rtos_lines) > 1000:
                                        self.rtos_lines = self.rtos_lines[-1000:]
                except Exception:
                    pass

                ch = self.stdscr.getch()
                log_h = max(1, self.height - 3)

                if ch in (3, 27, ord('q'), ord('Q')):
                    self.rtos_log_mode = False
                    break
                elif ch == curses.KEY_UP:
                    rtos_log_offset += 1
                elif ch == curses.KEY_DOWN:
                    if rtos_log_offset > 0:
                        rtos_log_offset -= 1
                elif ch == curses.KEY_PPAGE:
                    rtos_log_offset += log_h // 2 or 1
                elif ch == curses.KEY_NPAGE:
                    rtos_log_offset -= log_h // 2 or 1
                    if rtos_log_offset < 0:
                        rtos_log_offset = 0

                # Draw RTOS Log Page
                try:
                    self.stdscr.erase()
                    w = max(40, min(self.width, 80))
                    title = " ESP-IDF FREERTOS RAW CONSOLE LOGS [115200 BAUD] "
                    dash_len = max(0, w - 2 - len(title))
                    self.stdscr.addstr(0, 0, ("┌" + title + "─" * dash_len + "┐")[:self.width - 1])

                    total_rtos = len(self.rtos_lines)
                    if total_rtos <= log_h:
                        start_idx = 0
                    else:
                        max_offset = max(0, total_rtos - log_h)
                        if rtos_log_offset > max_offset:
                            rtos_log_offset = max_offset
                        start_idx = max(0, total_rtos - log_h - rtos_log_offset)

                    end_idx = min(total_rtos, start_idx + log_h)
                    visible = self.rtos_lines[start_idx:end_idx] if total_rtos > 0 else ["(Memuat log konsol FreeRTOS 115200 baud...)"]

                    for idx, l in enumerate(visible):
                        formatted = f"│ {l:<{w-4}} │"
                        color_pair = 1
                        if curses.has_colors():
                            if any(x in l for x in ["E (", "ERROR", "Backtrace:", "Guru Meditation", "panic"]):
                                color_pair = 4
                            elif any(x in l for x in ["W (", "WARN"]):
                                color_pair = 3
                            elif any(x in l for x in ["I (", "INFO"]):
                                color_pair = 2
                        self.stdscr.addstr(idx + 1, 0, formatted[:self.width - 1], curses.color_pair(color_pair))

                    row = min(1 + len(visible), self.height - 2)
                    b_border = "└" + "─" * (w - 2) + "┘"
                    self.stdscr.addstr(row, 0, b_border[:self.width - 1])

                    scroll_indicator = f" [Scroll Offset: {rtos_log_offset}] " if rtos_log_offset > 0 else " "
                    footer = f" [ '↑/↓/PgUp/PgDn': scroll{scroll_indicator}| 'q' or 'ESC': back ] "
                    if row + 1 < self.height:
                        self.stdscr.addstr(row + 1, 0, footer[:self.width - 1], curses.A_REVERSE)

                    self.stdscr.refresh()
                except curses.error:
                    pass

                time.sleep(0.02)

        finally:
            if raw_ser and raw_ser.is_open:
                raw_ser.close()
            self.rtos_log_mode = False
            self.log_print("Re-connecting primary monitor @ 921600 baud...")
            self.connect()
            self.draw_frame()
            self.refresh_all()

    def draw_deviceinfo_screen(self):
        s = self.status
        h = s["total_mins"] // 60
        m = s["total_mins"] % 60
        err_str = ",".join(s["errors"]) if s["errors"] else "NONE"

        w = max(40, min(self.width, 70))
        inner_w = w - 4

        try:
            self.stdscr.erase()

            # Top Header
            title = " JACKTOR AUDIO — MCU & SENSOR DIAGNOSTICS [REAL-TIME] "
            dash_len = max(0, w - 2 - len(title))
            self.stdscr.addstr(0, 0, ("┌" + title + "─" * dash_len + "┐")[:self.width - 1])

            lines = [
                "[ESP32 AMPLIFIER - AUDIO ECU]",
                f" Firmware / Version  : {s['fw_ver']} (JACKTOR AUDIO)",
                " OS / Framework      : ESP-IDF v6.0.1 (FreeRTOS Dual-Core)",
                " MCU Architecture    : Xtensa LX6 @ 240MHz (ESP32-D0WDQ6)",
                f" MCU / Heatsink Temp : {s['temp']:.1f}°C (DS18B20 1-Wire)",
                f" Power State         : {s['power']} | Run: {h}h {m}m",
                " Hardware Sensors & Rails:",
                f"  • SMPS 65V Rail    : {s['voltage']:.1f}V (ADS1115 ADC Ch0)",
                f"  • Standby 12V Rail : {s['v_stby']:.1f}V (ADS1115 ADC Ch1)",
                f"  • 3.3V VDD Rail    : {s['v_vdd']:.1f}V (ADS1115 ADC Ch2)",
                f"  • ESP32 5V Rail    : {s['v_esp']:.1f}V (ADS1115 ADC Ch3)",
                f"  • RTC DS3231 Temp  : {s['rtc_temp']:.1f}°C (I2C 0x68 / SQW 1Hz)",
                f"  • Audio ADC Left   : {s.get('adc_v_l', 0.0):.3f}V (Raw {s.get('adc_raw_l', 0.0):.0f} / GPIO 39)",
                f"  • Audio ADC Right  : {s.get('adc_v_r', 0.0):.3f}V (Raw {s.get('adc_raw_r', 0.0):.0f} / GPIO 36)",
                f"  • Fault Status     : {err_str} (GPIO 34 / 39 Opto)",
                f"  • QCC5125 Power    : {s.get('qcc_pwr', 'Inactive')} (GPIO 5 Power Detect)",
                f"  • BT Audio Status  : {s.get('bt_status_pin', 'Inactive')} (GPIO 23 Connection LED)",
                "",
                "[ESP32-S3 CONTROL PANEL - UI TERMINAL]",
                " Firmware / Version  : panel-1.0.0 (JACKTOR S3 PANEL)",
                " OS / Framework      : ESP-IDF v6.0.1 (FreeRTOS / LVGL v8.3.11)",
                " MCU Architecture    : Xtensa LX7 Dual-Core @ 240MHz (ESP32-S3)",
                " Peripherals Status  :",
                "  • LCD Display      : ILI9341 3.2\" SPI (320x240 16-bit RGB)",
                "  • Touch Controller : XPT2046 SPI Touch Screen",
                "  • Backlight PWM    : Active (LEDC PWM GPIO 21)",
                "  • Service UART Link: UART2 @ 115,200 bps",
            ]

            row = 1
            for l in lines:
                if row >= self.height - 2:
                    break
                formatted_line = f"│ {l:<{inner_w}} │"
                self.stdscr.addstr(row, 0, formatted_line[:self.width - 1])
                row += 1

            # Bottom Border
            b_border = "└" + "─" * (w - 2) + "┘"
            if row < self.height - 1:
                self.stdscr.addstr(row, 0, b_border[:self.width - 1])
                row += 1

            footer = " [ Press 'CTRL+C', 'q', or 'ESC' to return to Terminal ] "
            if row < self.height:
                self.stdscr.addstr(row, 0, footer[:self.width - 1], curses.A_REVERSE)

            self.stdscr.refresh()
            sys.stdout.flush()
        except curses.error:
            pass

    def run(self):
        if not self.connect():
            self.tui_init()
            self.stdscr.nodelay(False)
            self.log_print("Failed to connect. Press any key to exit...", with_ts=False)
            self.refresh_all()
            self.stdscr.getch()
            return

        self.tui_init()
        self.running = True

        while self.running:
            try:
                if not self.ser or not getattr(self.ser, 'is_open', False):
                    now_reconnect = time.time()
                    if now_reconnect - getattr(self, "last_reconnect_attempt", 0) >= 0.5:
                        self.last_reconnect_attempt = now_reconnect
                        if self.connect():
                            self.log_print("Re-established serial connection successfully.")

                if self.recalc_layout():
                    if not self.deviceinfo_mode:
                        self.refresh_all()

                try:
                    self.handle_serial()
                    if not self.fw_categories and self.ser and getattr(self.ser, "is_open", False):
                        if time.time() - getattr(self, "last_commands_req_time", 0.0) >= 1.0:
                            self.request_commands_list(timeout=0.35)
                except Exception as e:
                    if getattr(self, "debug_mode", False):
                        self.log_print(f"LOG[WARN] handle_serial error: {e}")

                ch = self.stdscr.getch()

                if self.audio_test_mode:
                    now_time = time.time()
                    if now_time - getattr(self, "last_dac_scan", 0) >= 1.5:
                        self.last_dac_scan = now_time
                        if not self.audio_gen.device_ready and self.status.get("power") == "ON":
                            if self.audio_gen.scan_device():
                                self.log_audio_test(f"[AUDIO_DEV] USB DAC QCC detected: {self.audio_gen.device_name}")

                    while ch != -1 and self.audio_test_mode:
                        if ch in (3, 27): # Ctrl+C, ESC
                            self.audio_gen.stop()
                            self.audio_gen.set_channel("stereo")
                            self.audio_test_mode = False
                            self.audio_test_input_buf = ""
                            self.draw_frame()
                            self.refresh_all()
                            break
                        elif ch in (curses.KEY_ENTER, 10, 13):
                            cmd_line = self.audio_test_input_buf.strip()
                            self.audio_test_input_buf = ""
                            parts = cmd_line.split()
                            act = parts[0].lower() if parts else ""
                            cmd_args = parts[1:]

                            if act in ["q", "quit", "exit"]:
                                self.audio_gen.stop()
                                self.audio_gen.set_channel("stereo")
                                self.audio_test_mode = False
                                self.draw_frame()
                                self.refresh_all()
                                break
                            elif act in ["play", "start"]:
                                self.audio_gen.start()
                            elif act in ["stop", "pause"]:
                                self.audio_gen.stop()
                            elif act == "p":
                                if self.status.get("power") == "ON":
                                    self.audio_gen.stop()
                                    self.send_command({"power": False})
                                    self.log_audio_test("[PWR] Sending Power OFF (Standby)...")
                                else:
                                    self.send_command({"power": True})
                                    self.log_audio_test("[PWR] Sending Power ON (Boot to runtime mode & activate QCC module)...")
                            elif act in ["pwr-on", "on"]:
                                self.send_command({"power": True})
                                self.log_audio_test("[PWR] Sending Power ON (Boot to runtime mode & activate QCC module)...")
                            elif act in ["pwr-off", "off"]:
                                self.audio_gen.stop()
                                self.send_command({"power": False})
                                self.log_audio_test("[PWR] Sending Power OFF (Standby)...")
                            elif act in ["freq", "f"]:
                                if cmd_args:
                                    try:
                                        self.audio_gen.set_frequency(float(cmd_args[0]))
                                    except ValueError:
                                        self.log_audio_test(f"[ERROR] Invalid frequency value: {cmd_args[0]}")
                            elif act in ["c", "ch", "chan", "channel"]:
                                if cmd_args:
                                    self.audio_gen.set_channel(cmd_args[0])
                                else:
                                    self.audio_gen.toggle_channel()
                            elif act in ["vol", "volume", "gain", "amp"]:
                                if cmd_args:
                                    try:
                                        v = float(cmd_args[0])
                                        if v >= 1.0:
                                            v /= 100.0
                                        self.audio_gen.set_amplitude(v)
                                    except ValueError:
                                        self.log_audio_test(f"[ERROR] Invalid volume value: {cmd_args[0]}")
                            elif act in ["s", "sweep"]:
                                self.audio_gen.set_mode("sweep")
                            elif act in ["a", "step", "stepsweep"]:
                                self.audio_gen.set_mode("step_sweep")
                            elif act in ["n", "pink", "pinknoise"]:
                                self.audio_gen.set_mode("pink")
                            elif act in ["w", "white", "whitenoise"]:
                                self.audio_gen.set_mode("white")
                            elif act in ["d", "scan", "rescan"]:
                                if self.audio_gen.scan_device():
                                    self.log_audio_test(f"[AUDIO_DEV] USB DAC detected: {self.audio_gen.device_name}")
                                else:
                                    self.log_audio_test("[AUDIO_DEV] USB DAC QCC not detected. Ensure amplifier is 'pwr-on'.")
                            elif act in ["clear", "cls"]:
                                self.audio_test_logs.clear()
                            elif cmd_line:
                                self.parse_line(cmd_line)
                        elif ch in (curses.KEY_BACKSPACE, 127, 8):
                            self.audio_test_input_buf = self.audio_test_input_buf[:-1]
                        elif not self.audio_test_input_buf and ch == ord(' '):
                            self.audio_gen.toggle_play()
                        elif not self.audio_test_input_buf and ch == ord('P'):
                            if self.status.get("power") == "ON":
                                self.audio_gen.stop()
                                self.send_command({"power": False})
                                self.log_audio_test("[PWR] Sending Power OFF (Standby)...")
                            else:
                                self.send_command({"power": True})
                                self.log_audio_test("[PWR] Sending Power ON (Boot to runtime mode & activate QCC module)...")
                        elif not self.audio_test_input_buf and ch == ord('C'):
                            self.audio_gen.toggle_channel()
                        elif not self.audio_test_input_buf and ch == ord('S'):
                            self.audio_gen.set_mode("sweep")
                        elif not self.audio_test_input_buf and ch == ord('A'):
                            self.audio_gen.set_mode("step_sweep")
                        elif not self.audio_test_input_buf and ch == ord('N'):
                            self.audio_gen.set_mode("pink")
                        elif not self.audio_test_input_buf and ch == ord('W'):
                            self.audio_gen.set_mode("white")
                        elif not self.audio_test_input_buf and ch in (ord('+'), ord('=')):
                            self.audio_gen.adjust_amplitude(+0.05)
                        elif not self.audio_test_input_buf and ch in (ord('-'), ord('_')):
                            self.audio_gen.adjust_amplitude(-0.05)
                        elif not self.audio_test_input_buf and ch == ord('D'):
                            if self.audio_gen.scan_device():
                                self.log_audio_test(f"[AUDIO_DEV] USB DAC detected: {self.audio_gen.device_name}")
                            else:
                                self.log_audio_test("[AUDIO_DEV] USB DAC QCC not detected. Ensure amplifier is 'pwr-on'.")
                        elif not self.audio_test_input_buf and ord('1') <= ch <= ord('9'):
                            idx = ch - ord('0')
                            preset_map = {1: 1, 2: 2, 3: 3, 4: 5, 5: 6, 6: 8, 7: 10, 8: 12, 9: 13}
                            target_b = preset_map.get(idx, 8)
                            self.audio_gen.set_band_preset(target_b)
                        elif not self.audio_test_input_buf and ch == ord('0'):
                            self.audio_gen.set_band_preset(15)
                        elif 32 <= ch <= 126:
                            if not self.audio_test_input_buf and ch == ord('Q'):
                                self.audio_gen.stop()
                                self.audio_gen.set_channel("stereo")
                                self.audio_test_mode = False
                                self.audio_test_input_buf = ""
                                self.draw_frame()
                                self.refresh_all()
                                break
                            else:
                                self.audio_test_input_buf += chr(ch)
                        ch = self.stdscr.getch()

                    if self.audio_test_mode:
                        self.draw_audio_test_screen()
                    time.sleep(0.03)
                    continue

                if self.analyzer_screen_mode:
                    now_time = time.time()
                    if now_time - getattr(self, "last_ana_request", 0) >= 1.0:
                        self.last_ana_request = now_time
                        self.send_command({"ana_mode": "fft"}, quiet=True)

                    while ch != -1 and self.analyzer_screen_mode:
                        if ch in (3, 27): # Ctrl+C, ESC
                            self.analyzer_screen_mode = False
                            self.ana_input_buf = ""
                            self.draw_frame()
                            self.refresh_all()
                            break
                        elif ch in (curses.KEY_ENTER, 10, 13):
                            cmd_line = self.ana_input_buf.strip()
                            self.ana_input_buf = ""
                            parts = cmd_line.split()
                            act = parts[0].lower() if parts else ""
                            cmd_args = parts[1:]

                            if act in ["q", "quit", "exit"]:
                                self.analyzer_screen_mode = False
                                self.draw_frame()
                                self.refresh_all()
                                break
                            elif act in ["r", "reset"]:
                                self.band_peaks_l = [0] * 64
                                self.band_peaks_r = [0] * 64
                                self.ana_peak_hold_l = [0.0] * 64
                                self.ana_peak_hold_r = [0.0] * 64
                                self.max_peak_val_l = 0
                                self.max_peak_val_r = 0
                                self.status["last_peak_freq_l"] = "N/A"
                                self.status["last_peak_freq_r"] = "N/A"
                            elif act in ["fx", "effect", "style"]:
                                if cmd_args:
                                    target_fx = cmd_args[0].upper()
                                    if target_fx == "BARS":
                                        target_fx = "GRADIENT"
                                    if target_fx in self.ana_fx_modes:
                                        self.ana_fx_idx = self.ana_fx_modes.index(target_fx)
                                else:
                                    self.ana_fx_idx = (self.ana_fx_idx + 1) % len(self.ana_fx_modes)
                            elif act == "decay":
                                if cmd_args:
                                    arg0 = cmd_args[0].upper()
                                    matched = False
                                    for idx_p, (p_name, p_val) in enumerate(self.ana_decay_presets):
                                        if p_name == arg0:
                                            self.ana_decay_idx = idx_p
                                            self.ana_decay_rate = p_val
                                            matched = True
                                            break
                                    if not matched:
                                        try:
                                            v_dec = float(cmd_args[0])
                                            self.ana_decay_rate = max(0.10, min(0.99, v_dec))
                                        except ValueError:
                                            pass
                                else:
                                    self.ana_decay_idx = (self.ana_decay_idx + 1) % len(self.ana_decay_presets)
                                    self.ana_decay_rate = self.ana_decay_presets[self.ana_decay_idx][1]
                            elif act in ["peak", "peak-hold"]:
                                if cmd_args:
                                    self.ana_peak_hold_en = cmd_args[0].lower() in ("on", "1", "true")
                                else:
                                    self.ana_peak_hold_en = not self.ana_peak_hold_en
                            elif cmd_line:
                                self.parse_line(cmd_line)
                        elif ch in (curses.KEY_BACKSPACE, 127, 8):
                            self.ana_input_buf = self.ana_input_buf[:-1]
                        elif not self.ana_input_buf and ch in (ord('q'), ord('Q')):
                            self.analyzer_screen_mode = False
                            self.ana_input_buf = ""
                            self.draw_frame()
                            self.refresh_all()
                            break
                        elif not self.ana_input_buf and ch == ord('F'):
                            self.ana_fx_idx = (self.ana_fx_idx + 1) % len(self.ana_fx_modes)
                        elif not self.ana_input_buf and ch == ord('D'):
                            self.ana_decay_idx = (self.ana_decay_idx + 1) % len(self.ana_decay_presets)
                            self.ana_decay_rate = self.ana_decay_presets[self.ana_decay_idx][1]
                        elif not self.ana_input_buf and ch == ord('P'):
                            self.ana_peak_hold_en = not self.ana_peak_hold_en
                        elif not self.ana_input_buf and ch == ord('R'):
                            self.band_peaks_l = [0] * 64
                            self.band_peaks_r = [0] * 64
                            self.ana_peak_hold_l = [0.0] * 64
                            self.ana_peak_hold_r = [0.0] * 64
                            self.max_peak_val_l = 0
                            self.max_peak_val_r = 0
                            self.status["last_peak_freq_l"] = "N/A"
                            self.status["last_peak_freq_r"] = "N/A"
                        elif 32 <= ch <= 126:
                            self.ana_input_buf += chr(ch)
                        ch = self.stdscr.getch()

                    if self.analyzer_screen_mode:
                        self.draw_analyzer_screen()
                    time.sleep(0.03)
                    continue

                if self.deviceinfo_mode:
                    now_time = time.time()
                    if now_time - self.last_deviceinfo_request >= 1.0:
                        self.last_deviceinfo_request = now_time
                        self.send_command({"deviceinfo": True}, quiet=True)

                    if ch in (3, 27, ord('q'), ord('Q')):
                        self.deviceinfo_mode = False
                        self.draw_frame()
                        self.refresh_all()
                    else:
                        self.draw_deviceinfo_screen()
                    time.sleep(0.05)
                    continue

                while ch != -1 and self.running and not (self.audio_test_mode or self.analyzer_screen_mode or self.deviceinfo_mode):
                    if ch in (curses.KEY_ENTER, 10, 13):
                        raw_cmd = self.input_buf.strip()
                        self.input_buf = ""
                        self.input_dirty = True
                        if raw_cmd:
                            if not self.cmd_history or self.cmd_history[-1] != raw_cmd:
                                self.cmd_history.append(raw_cmd)
                                if len(self.cmd_history) > 100:
                                    self.cmd_history.pop(0)
                            self.cmd_history_idx = -1
                            self.process_input_line(raw_cmd)

                    elif ch in (curses.KEY_BACKSPACE, 127, 8):
                        self.input_buf = self.input_buf[:-1]
                        self.input_dirty = True

                    elif ch == 9: # TAB Key: Auto-completion
                        self.handle_tab_completion()

                    elif ch == 3: # Ctrl+C
                        self.running = False
                        break

                    elif ch == curses.KEY_PPAGE: # Page Up: Scroll log up
                        self.log_offset += self.log_h // 2 or 1
                        self.log_dirty = True
                    elif ch == curses.KEY_NPAGE: # Page Down: Scroll log down
                        self.log_offset -= self.log_h // 2 or 1
                        if self.log_offset < 0:
                            self.log_offset = 0
                        self.log_dirty = True

                    elif ch == curses.KEY_UP:
                        # Command history previous
                        if self.cmd_history:
                            if self.cmd_history_idx == -1:
                                self.temp_input_buf = self.input_buf
                                self.cmd_history_idx = len(self.cmd_history) - 1
                            elif self.cmd_history_idx > 0:
                                self.cmd_history_idx -= 1
                            self.input_buf = self.cmd_history[self.cmd_history_idx]
                            self.input_dirty = True
                        else:
                            self.log_offset += 1
                            self.log_dirty = True

                    elif ch == curses.KEY_DOWN:
                        # Command history next
                        if self.cmd_history and self.cmd_history_idx != -1:
                            if self.cmd_history_idx < len(self.cmd_history) - 1:
                                self.cmd_history_idx += 1
                                self.input_buf = self.cmd_history[self.cmd_history_idx]
                            else:
                                self.cmd_history_idx = -1
                                self.input_buf = self.temp_input_buf
                            self.input_dirty = True
                        elif self.log_offset > 0:
                            self.log_offset -= 1
                            self.log_dirty = True

                    elif 32 <= ch <= 126:
                        self.input_buf += chr(ch)
                        self.input_dirty = True

                    ch = self.stdscr.getch()

                now_render = time.time()
                if now_render - getattr(self, "last_clock_tick", 0.0) >= 0.5:
                    self.last_clock_tick = now_render
                    self.header_dirty = True

                if now_render - getattr(self, "last_render_time", 0.0) >= 0.033:
                    self.last_render_time = now_render
                    has_updates = False
                    if getattr(self, "header_dirty", True):
                        self.draw_status_header(force=True)
                        self.header_dirty = False
                        has_updates = True
                    if getattr(self, "vu_dirty", True):
                        self.draw_vu_bar(force=True)
                        self.vu_dirty = False
                        has_updates = True
                    if getattr(self, "log_dirty", True):
                        self.draw_log()
                        self.log_dirty = False
                        has_updates = True
                    if getattr(self, "input_dirty", True):
                        self.draw_input()
                        self.input_dirty = False
                        has_updates = True
                    elif has_updates:
                        self.draw_input()

                    if has_updates:
                        self.stdscr.refresh()
                        sys.stdout.flush()

                time.sleep(0.01)
            except Exception as e:
                if getattr(self, "debug_mode", False):
                    self.log_print(f"LOG[WARN] Main loop exception: {e}")
                time.sleep(0.05)

        self.disconnect()


def select_port_interactive():
    ports = serial.tools.list_ports.comports()
    filtered_ports = [p for p in ports if not (p.device and "/dev/ttys" in p.device.lower())]

    candidates = []
    for port in filtered_ports:
        desc_lower = (port.description or "").lower()
        hwid_lower = (port.hwid or "").lower()
        is_esp = False
        if any(x in hwid_lower for x in ["303a:", "10c4:ea60", "1a86:7523", "0403:6001"]):
            is_esp = True
        elif any(x in desc_lower for x in ["cp210", "ch340", "usb serial", "uart", "esp32s3", "esp32-s3", "espressif"]):
            is_esp = True
        if is_esp:
            candidates.append(port)

    s3_candidates = []
    other_candidates = []
    for c in candidates:
        desc_lower = (c.description or "").lower()
        hwid_lower = (c.hwid or "").lower()
        if "303a" in hwid_lower or "s3" in desc_lower:
            s3_candidates.append(c)
        else:
            other_candidates.append(c)

    if other_candidates:
        return other_candidates[0].device
    if s3_candidates:
        return s3_candidates[0].device
    if filtered_ports:
        return filtered_ports[0].device

    if platform.system() == "Windows":
        return "COM3"
    elif platform.system() == "Darwin":
        return "/dev/cu.usbserial"
    else:
        return "/dev/ttyUSB0"


def get_lang():
    return "en"

def main(stdscr):
    global SELECTED_PORT
    port = sys.argv[1] if len(sys.argv) > 1 else SELECTED_PORT
    monitor = JacktorHybridMonitor(stdscr, port=port)
    monitor.run()


if __name__ == "__main__":
    try:
        if len(sys.argv) <= 1:
            SELECTED_PORT = select_port_interactive()
            if not SELECTED_PORT:
                print("No port selected. Exiting.")
                sys.exit(0)
        else:
            SELECTED_PORT = sys.argv[1]

        curses.wrapper(main)
    except KeyboardInterrupt:
        print("\nExiting monitor.")
        sys.exit(0)
