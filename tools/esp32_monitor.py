#!/usr/bin/env python3
import serial
import json
import sys
import threading
import time
from datetime import datetime

class ESP32Monitor:
    def __init__(self, port="/dev/ttyUSB0", baud=921600):
        self.port = port
        self.baud = baud
        self.ser = None
        self.running = False

    def connect(self):
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=1)
            time.sleep(0.5)
            print(f"[CONNECTED] {self.port} @ {self.baud} baud\n")
            return True
        except Exception as e:
            print(f"[ERROR] Connection failed: {e}")
            return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("\n[DISCONNECTED]")

    def send(self, json_str):
        """Send raw JSON"""
        try:
            self.ser.write((json_str + '\n').encode())
            self.ser.flush()
            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(f"[{ts}] TX -> {json_str}")
        except Exception as e:
            print(f"[ERROR] Send error: {e}")

    def send_command(self, cmd_dict):
        """Send command"""
        command = {"type": "cmd", "cmd": cmd_dict}
        self.send(json.dumps(command))

    def send_analyzer(self, cmd, **kwargs):
        """Send analyzer command"""
        data = {"type": "analyzer", "cmd": cmd}
        data.update(kwargs)
        self.send(json.dumps(data))

    def read_loop(self):
        """Read and parse messages"""
        while self.running:
            try:
                if self.ser and self.ser.in_waiting:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]

                        # Try parse as JSON
                        try:
                            data = json.loads(line)
                            msg_type = data.get('type', 'unknown')

                            if msg_type == 'log':
                                lvl = data.get('lvl', 'info').upper()
                                msg = data.get('msg', '')
                                reason = data.get('reason', '')
                                offset = data.get('offset_sec', None)

                                if offset is not None:
                                    print(f"[{ts}] LOG [{lvl}] {msg} (offset: {offset}s)")
                                elif reason:
                                    print(f"[{ts}] LOG [{lvl}] {msg} - {reason}")
                                else:
                                    print(f"[{ts}] LOG [{lvl}] {msg}")

                            elif msg_type == 'ack':
                                ok = data.get('ok', False)
                                changed = data.get('changed', '')
                                value = data.get('value', '')
                                error = data.get('error', '')

                                if ok:
                                    print(f"[{ts}] ACK [OK] {changed} = {value}")
                                else:
                                    print(f"[{ts}] ACK [ERR] {changed}: {error}")

                            elif msg_type == 'telemetry':
                                # Don't print full telemetry, just summary
                                if 'hz1' in data:
                                    hz1 = data['hz1']
                                    rtc_time = hz1.get('time', 'N/A')
                                    fw_ver = hz1.get('fw_ver', 'N/A')
                                    states = hz1.get('states', {})
                                    on = states.get('on', False)
                                    print(f"[{ts}] TELEM: FW={fw_ver} | Time={rtc_time} | Power={'ON' if on else 'OFF'}")
                                elif 'rt' in data:
                                    # Realtime telemetry - skip printing
                                    pass

                            else:
                                print(f"[{ts}] RX <- {line}")

                        except json.JSONDecodeError:
                            # Plain text (debug messages)
                            print(f"[{ts}] RX <- {line}")

            except Exception as e:
                if self.running:
                    print(f"[WARNING] Read error: {e}")
                break
            time.sleep(0.01)

    def parse_command(self, cmd):
        """Parse command string"""
        parts = cmd.split()
        if not parts:
            return

        action = parts[0].lower()
        args = parts[1:] if len(parts) > 1 else []

        # Power commands
        if action == "pwr-on":
            self.send_command({"power": True})
        elif action == "pwr-off":
            self.send_command({"power": False})

        # Speaker selection commands
        elif action == "spk-big":
            self.send_command({"spk_sel": "big"})
        elif action == "spk-small":
            self.send_command({"spk_sel": "small"})

        # Speaker power commands
        elif action == "spk-on":
            self.send_command({"spk_pwr": True})
        elif action == "spk-off":
            self.send_command({"spk_pwr": False})

        # Bluetooth commands
        elif action == "bt-on":
            self.send_command({"bt": True})
        elif action == "bt-off":
            self.send_command({"bt": False})

        # Fan commands
        elif action == "fan-auto":
            self.send_command({"fan_mode": "auto"})
        elif action == "fan-custom":
            self.send_command({"fan_mode": "custom"})
        elif action == "fan-duty":
            if not args:
                print("[ERROR] Usage: fan-duty <0-1023>")
                return
            try:
                duty = int(args[0])
                if duty < 0 or duty > 1023:
                    print("[ERROR] Duty must be 0-1023")
                    return
                # ✅ FIX: Auto set fan mode to CUSTOM when setting duty
                self.send_command({"fan_mode": "custom", "fan_duty": duty})
                print(f"[INFO] Set fan to CUSTOM mode with duty {duty}")
            except ValueError:
                print("[ERROR] Invalid duty value")
        elif action == "fan-min":
            if not args:
                print("[ERROR] Usage: fan-min <0-1023>")
                return
            try:
                duty = int(args[0])
                self.send_command({"fan_min_duty": duty})
            except ValueError:
                print("[ERROR] Invalid duty value")

        # SMPS commands
        elif action == "smps-on":
            self.send_command({"smps_bypass": False})
        elif action == "smps-off":
            self.send_command({"smps_bypass": True})
        elif action == "smps-cutoff":
            if not args:
                print("[ERROR] Usage: smps-cutoff <voltage>")
                return
            try:
                voltage = float(args[0])
                self.send_command({"smps_cutoff_v": voltage})
            except ValueError:
                print("[ERROR] Invalid voltage value")

        # Buzzer commands
        elif action == "buzz":
            if not args or len(args) < 2:
                print("[ERROR] Usage: buzz <freq_hz> <duration_ms> [duty]")
                print("        Example: buzz 1000 200")
                return
            try:
                freq = int(args[0])
                duration = int(args[1])
                duty = int(args[2]) if len(args) > 2 else 512
                buzz_obj = {"f": freq, "ms": duration, "d": duty}
                self.send_command({"buzz": buzz_obj})
            except ValueError:
                print("[ERROR] Invalid values")

        elif action == "buzz-click":
            self.send_command({"buzz": {"f": 1975, "ms": 60, "d": 512}})

        elif action == "buzz-beep":
            self.send_command({"buzz": {"f": 1000, "ms": 200, "d": 512}})

        elif action == "buzz-low":
            self.send_command({"buzz": {"f": 440, "ms": 300, "d": 512}})

        elif action == "buzz-mid":
            self.send_command({"buzz": {"f": 880, "ms": 300, "d": 512}})

        elif action == "buzz-high":
            self.send_command({"buzz": {"f": 1760, "ms": 300, "d": 512}})

        elif action == "buzz-error":
            print("[INFO] Playing error pattern...")
            for i in range(3):
                self.send_command({"buzz": {"f": 880, "ms": 150, "d": 512}})
                time.sleep(0.25)

        elif action == "buzz-warning":
            self.send_command({"buzz": {"f": 1760, "ms": 100, "d": 384}})

        elif action == "buzz-melody":
            print("[INFO] Playing melody...")
            notes = [
                (523, 200),  # C5
                (587, 200),  # D5
                (659, 200),  # E5
                (698, 300),  # F5
            ]
            for freq, dur in notes:
                self.send_command({"buzz": {"f": freq, "ms": dur, "d": 512}})
                time.sleep(dur / 1000.0 + 0.05)

        elif action == "buzz-stop":
            self.send_command({"buzz": {"f": 0, "ms": 0, "d": 0}})

        elif action == "buzz-test":
            print("[INFO] Testing buzzer tones...")
            tests = [
                ("Click", 1975, 60),
                ("Low (440Hz)", 440, 300),
                ("Mid (880Hz)", 880, 300),
                ("High (1760Hz)", 1760, 300),
                ("Very High (3520Hz)", 3520, 200),
            ]
            for name, freq, dur in tests:
                print(f"[INFO] Playing: {name}")
                self.send_command({"buzz": {"f": freq, "ms": dur, "d": 512}})
                time.sleep(dur / 1000.0 + 0.5)

        # Analyzer commands
        elif action == "ana-get":
            self.send_analyzer("get")
        elif action == "ana-mode":
            if not args:
                print("[ERROR] Usage: ana-mode <fft|vu>")
                return
            mode = args[0].lower()
            if mode not in ["fft", "vu"]:
                print("[ERROR] Mode must be 'fft' or 'vu'")
                return
            self.send_analyzer("set", mode=mode)
        elif action == "ana-bands":
            if not args:
                print("[ERROR] Usage: ana-bands <8|16|24|32|48|64>")
                return
            try:
                bands = int(args[0])
                if bands not in [8, 16, 24, 32, 48, 64]:
                    print("[ERROR] Bands must be 8, 16, 24, 32, 48, or 64")
                    return
                self.send_analyzer("set", bands=bands)
            except ValueError:
                print("[ERROR] Invalid bands value")
        elif action == "ana-update":
            if not args:
                print("[ERROR] Usage: ana-update <ms>")
                return
            try:
                update_ms = int(args[0])
                self.send_analyzer("set", update_ms=update_ms)
            except ValueError:
                print("[ERROR] Invalid update_ms value")
        elif action == "ana-gain":
            if not args:
                print("[ERROR] Usage: ana-gain <0.0-2.0>")
                return
            try:
                gain = float(args[0])
                self.send_analyzer("set", gain=gain)
            except ValueError:
                print("[ERROR] Invalid gain value")

        # RTC commands
        elif action == "rtc-sync":
            if not args:
                # Auto sync with system LOCAL time (WIB/Indonesia)
                local_time = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
                self.send_command({"rtc_set": local_time})
                print(f"[INFO] Syncing RTC with local time: {local_time}")
                print(f"[INFO] Note: May be rate-limited (24h interval)")
                print(f"[INFO] Use 'rtc-sync-force' to bypass rate limit")
            else:
                iso = ' '.join(args)
                self.send_command({"rtc_set": iso})
                print(f"[INFO] Syncing RTC with: {iso}")
                print(f"[INFO] Note: May be rate-limited (24h interval)")

        elif action == "rtc-sync-force":
            if not args:
                # Force sync - use epoch to bypass rate limit
                import time as time_module
                epoch = int(time_module.time())
                # Add timezone offset for WIB (UTC+7)
                epoch += 7 * 3600
                self.send_command({"rtc_set_epoch": epoch})
                local_time = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
                print(f"[INFO] Force syncing RTC (bypass rate limit): {local_time}")
            else:
                # Parse manual time and convert to epoch
                iso = ' '.join(args)
                try:
                    dt = datetime.strptime(iso, "%Y-%m-%dT%H:%M:%S")
                    # Add timezone offset for WIB (UTC+7)
                    epoch = int(dt.timestamp()) + (7 * 3600)
                    self.send_command({"rtc_set_epoch": epoch})
                    print(f"[INFO] Force syncing RTC with: {iso}")
                except ValueError:
                    print("[ERROR] Invalid time format. Use: YYYY-MM-DDTHH:MM:SS")

        elif action == "rtc-get":
            print("[INFO] RTC time available in telemetry 'hz1.time' field")
            print("[INFO] Wait for next telemetry message or force with 'status'")

        # State/Config commands
        elif action == "state-save":
            print("[INFO] State is auto-saved to NVS on each change")
        elif action == "state-load":
            print("[INFO] State is auto-loaded from NVS on boot")

        # NVS commands
        elif action == "nvs-reset":
            confirm = input("[WARNING] Reset NVS settings to defaults? (yes/no): ")
            if confirm.lower() == "yes":
                self.send_command({"nvs_reset": True})
                print("[INFO] NVS reset command sent")
            else:
                print("[INFO] NVS reset cancelled")

        # System commands
        elif action == "reset":
            confirm = input("[WARNING] Factory reset will erase all settings. Continue? (yes/no): ")
            if confirm.lower() == "yes":
                self.send_command({"factory_reset": True})
                print("[INFO] Factory reset command sent")
            else:
                print("[INFO] Factory reset cancelled")

        elif action == "reboot":
            print("[INFO] Reboot not implemented via serial")
            print("[INFO] Use hardware reset button or power cycle")

        # Status commands
        elif action == "status":
            print("[INFO] Check telemetry messages for status")

        elif action == "version":
            print("[INFO] Firmware version in telemetry 'hz1.fw_ver' field")

        # Raw JSON
        elif action == "json":
            if not args:
                print("[ERROR] Usage: json <json_string>")
                return
            json_str = ' '.join(args)
            try:
                # Validate JSON
                json.loads(json_str)
                self.send(json_str)
            except json.JSONDecodeError as e:
                print(f"[ERROR] Invalid JSON: {e}")

        # Help
        elif action == "help":
            if args:
                self.show_help_category(args[0])
            else:
                self.show_help()

        # Exit
        elif action in ["exit", "quit", "q"]:
            return "exit"

        # Clear screen
        elif action == "clear":
            print("\033[2J\033[H", end="")

        else:
            print(f"[ERROR] Unknown command: {action}")
            print("        Type 'help' for available commands")

    def show_help(self):
        """Show help"""
        print("\n=== ESP32 Monitor - Command Reference ===\n")

        print("Power Control:")
        print("  pwr-on                      Power ON")
        print("  pwr-off                     Power OFF (standby)")
        print()

        print("Speaker Control:")
        print("  spk-big                     Switch to BIG speaker")
        print("  spk-small                   Switch to SMALL speaker")
        print("  spk-on                      Speaker power ON")
        print("  spk-off                     Speaker power OFF")
        print()

        print("Bluetooth:")
        print("  bt-on                       Bluetooth ON")
        print("  bt-off                      Bluetooth OFF")
        print()

        print("Fan Control:")
        print("  fan-auto                    Fan auto mode")
        print("  fan-custom                  Fan custom mode")
        print("  fan-duty <0-1023>           Set fan duty (auto switches to custom)")
        print()

        print("SMPS Control:")
        print("  smps-on                     Enable SMPS")
        print("  smps-off                    Bypass SMPS")
        print("  smps-cutoff <voltage>       Set SMPS cutoff voltage")
        print()

        print("Buzzer Control:")
        print("  buzz <freq> <dur> [duty]    Custom buzzer tone")
        print("  buzz-click                  Quick click")
        print("  buzz-beep                   Standard beep")
        print("  buzz-low/mid/high           Tone presets")
        print("  buzz-error                  Error pattern")
        print("  buzz-melody                 Simple melody")
        print("  buzz-test                   Test all tones")
        print()

        print("Analyzer Control:")
        print("  ana-get                     Get analyzer config")
        print("  ana-mode <fft|vu>           Set analyzer mode")
        print("  ana-bands <8-64>            Set FFT bands")
        print("  ana-update <ms>             Set update interval")
        print()

        print("RTC Control:")
        print("  rtc-get                     Get RTC time (from telemetry)")
        print("  rtc-sync                    Sync RTC with system local time")
        print("  rtc-sync-force              Force sync (bypass 24h rate limit)")
        print("  rtc-sync-force <ISO8601>    Force sync with custom time")
        print()

        print("System:")
        print("  nvs-reset                   Reset NVS to defaults")
        print("  reset                       Factory reset")
        print("  status                      Request status")
        print("  version                     Show version")
        print()

        print("Utility:")
        print("  json <json_string>          Send raw JSON")
        print("  clear                       Clear screen")
        print("  help                        Show this help")
        print("  help <category>             Show category help")
        print("  exit / quit / q             Quit monitor")
        print()

        print("Examples:")
        print("  spk-big")
        print("  fan-duty 800               # Auto switches to custom mode")
        print("  buzz 1000 200")
        print("  buzz-melody")
        print("  ana-bands 24")
        print("  rtc-sync")
        print("  rtc-sync-force")
        print("  bt-on")
        print()

        print("Categories: power, speaker, fan, buzzer, analyzer, smps, rtc")
        print()

    def show_help_category(self, category):
        """Show category-specific help"""
        cat = category.lower()

        if cat == "power":
            print("\n=== Power Control ===")
            print("  pwr-on      - Turn amplifier ON")
            print("  pwr-off     - Turn amplifier OFF (standby mode)")
            print()

        elif cat == "speaker":
            print("\n=== Speaker Control ===")
            print("  spk-big     - Switch audio output to BIG speaker")
            print("  spk-small   - Switch audio output to SMALL speaker")
            print("  spk-on      - Enable speaker power relay")
            print("  spk-off     - Disable speaker power relay")
            print()
            print("Note: Speaker selection is independent from power relay")
            print()

        elif cat == "fan":
            print("\n=== Fan Control ===")
            print("  fan-auto           - Automatic fan control based on temperature")
            print("  fan-custom         - Manual fan control mode")
            print("  fan-duty <value>   - Set PWM duty cycle (0-1023)")
            print("                       Automatically switches to CUSTOM mode")
            print()
            print("Examples:")
            print("  fan-auto")
            print("  fan-duty 512       # 50% duty (auto switches to custom)")
            print("  fan-duty 800       # ~78% duty")
            print()
            print("Temperature curve (AUTO mode):")
            print("  < 40°C : duty 400  (39% - minimum)")
            print("  40-60°C: 400→650   (linear interpolation)")
            print("  60-80°C: 650→1023  (linear interpolation)")
            print("  > 80°C : duty 1023 (100% - maximum)")
            print()
            print("Note: Fan can be controlled even when power is OFF (standby)")
            print()

        elif cat == "buzzer":
            print("\n=== Buzzer Control ===")
            print("  buzz <freq> <dur> [duty]  - Play custom tone")
            print("    freq  : Frequency in Hz (100-5000)")
            print("    dur   : Duration in milliseconds")
            print("    duty  : PWM duty cycle 0-1023 (default: 512)")
            print()
            print("Presets:")
            print("  buzz-click      - Quick UI click (1975Hz, 60ms)")
            print("  buzz-beep       - Standard beep (1000Hz, 200ms)")
            print("  buzz-low        - Low tone (440Hz, 300ms)")
            print("  buzz-mid        - Mid tone (880Hz, 300ms)")
            print("  buzz-high       - High tone (1760Hz, 300ms)")
            print("  buzz-error      - Error pattern (3 short beeps)")
            print("  buzz-warning    - Warning beep (high pitch)")
            print("  buzz-melody     - Simple melody (C-D-E-F)")
            print("  buzz-stop       - Stop any playing sound")
            print("  buzz-test       - Test all tone presets")
            print()
            print("Examples:")
            print("  buzz 1000 200           # 1kHz beep for 200ms")
            print("  buzz 440 500 256        # A4 note, 500ms, lower volume")
            print("  buzz 2000 100           # High pitch short beep")
            print("  buzz-melody             # Play C-D-E-F melody")
            print()

        elif cat == "analyzer":
            print("\n=== Analyzer Control ===")
            print("  ana-get              - Get current analyzer configuration")
            print("  ana-mode <fft|vu>    - Set mode (FFT spectrum or VU meter)")
            print("  ana-bands <8-64>     - Set number of FFT bands")
            print("  ana-update <ms>      - Set update interval in milliseconds")
            print()
            print("Valid band counts: 8, 16, 24, 32, 48, 64")
            print("Recommended update: 33ms (30 Hz) or 50ms (20 Hz)")
            print()
            print("Examples:")
            print("  ana-mode fft")
            print("  ana-bands 24")
            print("  ana-update 33")
            print()

        elif cat == "smps":
            print("\n=== SMPS Control ===")
            print("  smps-on              - Enable SMPS (switching power supply)")
            print("  smps-off             - Bypass SMPS (use direct power)")
            print("  smps-cutoff <volts>  - Set low voltage cutoff threshold")
            print()
            print("Examples:")
            print("  smps-on")
            print("  smps-cutoff 24.5")
            print()

        elif cat == "rtc":
            print("\n=== RTC Control ===")
            print("  rtc-get                  - Get current RTC time (from telemetry)")
            print("  rtc-sync                 - Sync RTC with system local time")
            print("  rtc-sync-force           - Force sync (bypass 24h rate limit)")
            print("  rtc-sync-force <ISO8601> - Force sync with custom time")
            print()
            print("Time Format: YYYY-MM-DDTHH:MM:SS (local time WIB)")
            print()
            print("Examples:")
            print("  rtc-sync                          # Normal sync (rate-limited)")
            print("  rtc-sync-force                    # Force sync with PC time")
            print("  rtc-sync-force 2025-12-07T10:26:00 # Force sync custom time")
            print()
            print("Notes:")
            print("- rtc-sync: Rate-limited (24h interval, offset >2s)")
            print("- rtc-sync-force: Bypass rate limit using epoch method")
            print("- RTC stores LOCAL time (WIB/Indonesia)")
            print("- Auto sync uses PC's current local time")
            print("- Check telemetry 'hz1.time' field for current RTC time")
            print()

        else:
            print(f"\n[ERROR] Unknown category: {category}")
            print("Available categories: power, speaker, fan, buzzer, analyzer, smps, rtc")
            print()

    def run(self):
        """Run monitor"""
        if not self.connect():
            return

        self.running = True
        reader = threading.Thread(target=self.read_loop, daemon=True)
        reader.start()

        print("ESP32 Monitor + Commander")
        print("Type 'help' for available commands")
        print()

        try:
            while True:
                cmd = input("> ").strip()

                if not cmd:
                    continue

                result = self.parse_command(cmd)
                if result == "exit":
                    break

        except KeyboardInterrupt:
            print()
        finally:
            self.running = False
            time.sleep(0.2)
            self.disconnect()

if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
    monitor = ESP32Monitor(port)
    monitor.run()
