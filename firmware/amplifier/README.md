# Amplifier Unit (ESP32)

This firmware serves as the central Electronic Control Unit (ECU) of the Jacktor Audio smart ecosystem using a *Single-Node* architecture. It is built on the **ESP-IDF** framework using FreeRTOS to guarantee high-precision timing for ESP-IDF ADC Continuous DMA conversion, 32-Band FFT calculations, asynchronous UART communications, and thermal data acquisition to ensure hardware stability.

## Protection Mechanisms & Professional Features

- **Proactive Thermal Throttling**: Before reaching the critical OTP threshold (85°C), the system triggers *Thermal Throttling* (forcing the fan to 100%) as soon as the temperature reaches 75°C. This provides pre-emptive safety without shutting down the system immediately.
- **Speaker Protector Stability**: Monitors the speaker protection line (GPIO 34) with a 5-second stabilization grace period after speaker relay (GPIO 25) activation, mitigating *false-positive* triggers during capacitor charge cycles at power-on.
- **Hardware Setup & Diagnostics Sequence**: Performs a safe step-by-step diagnostic on boot:
  1. **Cooling Fan Diagnostic**: 10-second test (3s ramp up, 4s hold at 100% max speed, 3s ramp down).
  2. **I2C Bus Scan**: Active bus scan for RTC (DS3231) and Voltmeter (ADS1115). On failure, logs a full bus scan across addresses 1–127 and powers off to standby.
  3. **Power Relays & Voltage Check**: SMPS and Audio relays activation and rail voltage validation.
  4. **SMPS Hardware Fault Check**: Real-time optocoupler fault pin reading (GPIO 15).
  5. **Speaker Power & Protector Monitor**: 5-second stabilization and optocoupler status check (GPIO 34).
- **QCC Power Detection (GPIO 5)**: Real-time monitoring of `QCC_PWR_DETECT_PIN` (active HIGH) with a 2-second timeout before establishing GAIA SPP Bluetooth connection.
- **TM1637 4-Digit 7-Segment Display**: Bit-banging driver on GPIO 18 (CLK) and GPIO 19 (DIO) providing visual status feedback:
  - `"  on"`: Booting, setup phase, and QCC GAIA connection (held for 500ms prior to setup diagnostics).
  - `"pass"`: Setup passed (displayed for 2s).
  - `" err"` -> `" off"`: Setup/system failure (displayed for 2s each before returning to standby clock).
  - `"  bt"` / `" aux"`: Runtime active input source.
  - `" usb"`: USB DAC mode active on the control panel.
  - `"HH:MM"`: Standby mode clock display with a 1 Hz SQW-synchronized colon (`:`) blinking (500ms ON / 500ms OFF).
- **Hardware SQW 1 Hz Timekeeping**: Monotonic epoch advancement strictly synchronized to the physical falling edge of the DS3231 1 Hz SQW pin (GPIO 35), eliminating internal timer drift and providing sub-millisecond precision timestamps.
- **Standby Fan & State Management**: In standby mode, all runtime states are completely reset. The cooling fan operates dynamically based on heatsink temperature—spinning to dissipate residual heat and fully stopping once cooled below threshold—unlike runtime mode which maintains a low idle speed.
- **Advanced Diagnostics (Hour Meter)**: Tracks total operational runtime in minutes and persists it to NVS along with peak heatsink temperature and fault history.
- **KissFFT Spectrum Analyzer**: High-performance audio analyzer algorithm with *Dynamic DC Removal*, *1-Pole IIR DC Blocker ($R=0.996$)*, and *Hardware Analog Resistor Compensation ($kAnalogScaleR = 9.80$)* on GPIO 36/39.
- **SMPS Hardware Fault (Latching)**: Detects hardware-level SMPS faults via an optocoupler, triggering a repeating acoustic alarm until resolved or manually silenced.
- **Smart Power Logic**: Features software debouncing (50ms) and automatic state synchronization to prevent *relay chattering* and power looping under strong electromagnetic interference (EMI). Persistent NVS tracks disabled modules (e.g. QCC, speaker protector) to prevent false error triggers across reboots.

## GPIO Mapping (Pinout & Wiring)

GPIO mappings are configured in [`include/config.h`](include/config.h). Understanding this pin layout is critical for proper physical wiring and code development.

| Component / Function | GPIO (ESP32) | Specification & Details |
| :--- | :---: | :--- |
| **Main Power Button** | `13` | Input (*Active LOW*) with 50ms software debouncing. |
| **Boot / Factory Reset Button** | `0` | Default ESP32 board button; held with the power button for 5 seconds for factory reset. |
| **UART RX (Service Port)** | `16` | Receive line for debugging & system tuning (921,600 bps). |
| **UART TX (Service Port)** | `17` | Transmit line for debugging & system tuning (921,600 bps). |
| **LED UART Indicator** | `2` | On-board activity LED pulsing during RX/TX communications. |
| **I2C SDA** | `21` | Data Line (RTC DS3231, ADS1115 Voltmeter, PCF8574). |
| **I2C SCL** | `22` | Clock Line (RTC DS3231, ADS1115 Voltmeter, PCF8574). |
| **ADC Audio In R** | `36` | `ADC1_CH0` (Internal). Reads the Right audio channel signal for FFT/VU (`adc_continuous`). |
| **ADC Audio In L** | `39` | `ADC1_CH3` (Internal). Reads the Left audio channel signal for FFT/VU (`adc_continuous`). |
| **Heatsink Temp Sensor** | `27` | 1-Wire protocol for the `DS18B20` temperature sensor. |
| **Heatsink Fan (PWM)** | `32` | PWM control signal (10-bit, 25kHz) for dynamic fan speed control. |
| **Relay SMPS** | `14` | Output (*Active HIGH*). Main SMPS power relay. Default *OFF*. |
| **Speaker Power Switch** | `25` | Output controlling the speaker protection relay. |
| **Relay Audio** | `26` | Output (*Active HIGH*). Active ON during audio program execution (NES, USB DAC, DAP, etc.). |
| **Buzzer** | `33` | PWM output for acoustic feedback and warning/error alarms. |
| **SMPS Fault Detect** | `15` | Optocoupler input reading the SMPS fault indicator (*Active LOW*). |
| **Speaker Protect LED Monitor** | `34` | Optocoupler input monitoring the speaker protection board status. |
| **RTC SQW Input** | `35` | 1 Hz square wave input from the RTC module. |
| **Bluetooth Power Enable** | `4` | Output enabling/disabling power to the QCC Bluetooth module. |
| **QCC Power Detect** | `5` | Input (*Active HIGH*) detecting power status of the QCC Bluetooth module (`QCC_PWR_DETECT_PIN`). |
| **TM1637 CLK** | `18` | Clock signal for TM1637 4-digit 7-segment display (`TM1637_CLK_PIN`). |
| **TM1637 DIO** | `19` | Data signal for TM1637 4-digit 7-segment display (`TM1637_DIO_PIN`). |
| **Bluetooth Connection Indicator** | `23` | Input detecting audio connection status from the Bluetooth module (`BT_STATUS_PIN`). |

## JSON Communication Protocol

The amplifier utilizes a full-duplex asynchronous protocol over USB Serial (to the Android Panel) running at **921,600 bps**, broadcasting the following telemetry frames:
1. **`rt` (Realtime) ~ 30 Hz**: Carries 16-Band FFT spectrum arrays, VU status, and active input mode (Bluetooth/AUX).
2. **`hz1` (Diagnostic) ~ 1 Hz**: Carries supply voltage levels (SMPS & 12V), remaining sleep timer minutes, heatsink temperature (`heat_c`), relay statuses, and active `errors[]` (triggered on hardware failures like Speaker Protection or OTP).

Every valid instruction sent from the controller (e.g. `{"type":"cmd","cmd":{"power":false}}`) is acknowledged with a short acoustic click (`buzzerClick()`), confirming execution at the hardware level.

> [!IMPORTANT]
> **Initial Manual Flash Requirement for Upload Scripts**
> To use `tools/upload.py`, the ESP32 Amplifier MUST be manually flashed via USB cable AT LEAST ONCE using `pio run -t upload`. Manual initial flashing places the Jacktor Audio firmware signature onto the microcontroller, enabling `tools/upload.py` to automatically verify device identity via serial when uploading directly over USB or via ADB to the Android Panel.
