# Amplifier Electronic Control Unit (ESP32-WROOM-32)

This directory contains the firmware source code for the dedicated Electronic Control Unit (ECU) of the **Jacktor Audio** smart ecosystem. Built on **pure Espressif ESP-IDF v6+** using FreeRTOS (C++17), it enforces hard real-time deterministic timing for continuous dual-channel DMA ADC sampling, 32-band KissFFT digital signal processing, hardware safety automation, and full-duplex JSON telemetry streaming at **921,600 bps**.

---

## 1. Subsystems & Protection Mechanisms

- **Deterministic Dual-Core Processing**:
  - **Core 0 (Audio Signal Engine)**: Dedicated continuous DMA ADC sampling (44.1 kHz per channel across GPIO 34 and GPIO 39), Hann windowing, 1-pole IIR DC blocker ($R=0.996$), and 8/16/32/64-band KissFFT real-to-complex transformations.
  - **Core 1 (Safety & System Automation)**: Executes the safety finite state machine (FSM), multi-sensor polling, thermal PWM regulation, TM1637 display updates, and UART telemetry framing.
- **Multi-Sensor Thermal Monitoring (GPIO 27)**:
  - 1-Wire bus supporting up to 3 individual **DS18B20 digital temperature sensors** differentiated by unique 64-bit hardware ROM addresses stored in NVS (`temp_ab_rom`, `temp_d_rom`, `temp_smps_rom`):
    - **Stereo Amplifier Temperature** (`amp_stereo_temp` — physically verified: `0x1024020001892428`)
    - **Subwoofer Amplifier Temperature** (`amp_sub_temp`)
    - **SMPS Power Supply Temperature** (`amp_smps_temp`)
  - Supports dynamic address scanning (`temp-scan`), role assignment (`temp-assign`), and role swapping (`temp-swap`) without firmware reflashing.
- **Proactive Closed-Loop Thermal Regulation**:
  - Automatically modulates fan speed via 25 kHz PWM (GPIO 32) using a 3-point temperature curve.
  - Before reaching the critical Over-Temperature Protection (OTP 85°C) trip threshold, *Thermal Throttling* forces the fan to 100% at 75°C to arrest thermal runaway without interrupting audio.
  - In standby mode, residual heat dissipation runs dynamically until the heatsink cools below threshold, at which point the fan stops completely.
- **4-Channel High-Precision Voltage Monitoring (ADS1115 I2C `0x48`)**:
  - **Channel 0**: SMPS 65V Rail (under-voltage cutoff at 50.0V, recovery at 52.0V).
  - **Channel 1**: Standby Rail (continuous 4.3V – 12V; protected from triggering false UVLO alarms).
  - **Channel 2**: Logic 3.3V VDD Rail.
  - **Channel 3**: System 5.0V Rail.
- **Hardware SQW 1 Hz Timekeeping & Colon Pacing**:
  - Monotonic epoch counter advanced directly by the physical falling edge of the DS3231 RTC 1 Hz SQW pin (GPIO 36).
  - Drives the TM1637 4-digit 7-segment display clock colon (`:`) strictly on hardware pulses (500ms ON / 500ms OFF) with zero timer drift or microsecond jitter.
- **Speaker Protector Stability**:
  - Monitors the speaker protection board status (GPIO 35 optocoupler) with a 5-second stabilization grace period after speaker relay (GPIO 25) activation, avoiding false-positive DC trips during initial capacitor charging.
- **QCC5125 Bluetooth Co-Processor & Low-Noise RF**:
  - Real-time power detection on GPIO 5 (`QCC_PWR_DETECT_PIN`). If LOW, the GAIA SPP connection aborts immediately (`E-04`).
  - Dynamic TX power management: high RF power during pairing/handshake, automatically attenuated to low-noise power (-12 dBm to -6 dBm) after 3 seconds of connection stability to eliminate analog floor noise.

---

## 2. GPIO Mapping (Pinout & Wiring)

Configured centrally in [`include/config.h`](include/config.h):

| Component / Function | GPIO (ESP32) | Mode / Type | Specification & Details |
| :--- | :---: | :---: | :--- |
| **Main Power Button** | `13` | Input | Active LOW with 150ms software debouncing. |
| **Boot / Factory Reset Button** | `0` | Input | Held with power button for 5 seconds for factory reset. |
| **UART RX (Service Port)** | `16` | Input | High-speed telemetry & control line (**921,600 bps**). |
| **UART TX (Service Port)** | `17` | Output | High-speed telemetry & control line (**921,600 bps**). |
| **LED UART Activity** | `2` | Output | On-board activity LED pulsing during RX/TX communication. |
| **I2C SDA** | `21` | In/Out | Data Line: RTC DS3231 (`0x68`), ADS1115 (`0x48`), PCF8574. |
| **I2C SCL** | `22` | Output | Clock Line: RTC DS3231 (`0x68`), ADS1115 (`0x48`), PCF8574. |
| **Audio In Left (ADC)** | `34` | Analog | `ADC1_CH6` (VDET_1). Continuous DMA sampling (44.1 kHz). |
| **Audio In Right (ADC)** | `39` | Analog | `ADC1_CH3` (SENSOR_VN). Continuous DMA sampling (44.1 kHz). |
| **Speaker Protect Monitor** | `35` | Input | Optocoupler input reading speaker protector status. |
| **RTC SQW Input** | `36` | Input | 1 Hz square wave falling-edge interrupt from DS3231. |
| **1-Wire Temperature Bus** | `27` | In/Out | Bus for up to 3 multi-drop DS18B20 digital sensors. |
| **Cooling Fan (PWM)** | `32` | Output | 10-bit, 25 kHz PWM for closed-loop thermal regulation. |
| **Relay SMPS** | `14` | Output | Active HIGH. Controls the main SMPS 65V power relay. |
| **Relay Speaker** | `25` | Output | Controls the speaker protection disconnect relay. |
| **Power Button LED** | `26` | Output | Active HIGH indicator LED for the main power switch. |
| **Acoustic Buzzer** | `33` | Output | LEDC PWM output for tactile clicks and alarm tones. |
| **SMPS Fault Detect** | `15` | Input | Active LOW optocoupler reading SMPS hardware fault trip. |
| **Bluetooth Power Enable** | `4` | Output | Controls power gate to Qualcomm QCC5125 module. |
| **QCC Power Detect** | `5` | Input | Active HIGH confirmation of QCC module power. |
| **Bluetooth Audio Status** | `23` | Input | Real-time indication of Bluetooth streaming state. |
| **TM1637 Display CLK** | `18` | Output | Clock line for 4-digit 7-segment display. |
| **TM1637 Display DIO** | `19` | In/Out | Data line for 4-digit 7-segment display. |

---

## 3. Communication Protocol (UART0 @ 921,600 bps)

The ECU communicates asynchronously with the Android Control Panel strictly at **921,600 bps** using structured JSON telemetry:
1. **`rt` (Real-Time Audio Frame, ~30 FPS)**: Carries 8/16/32/64-band KissFFT magnitude arrays, stereo VU values, and active input mode.
2. **`hz1` (Diagnostic Frame, 1 FPS)**: Carries SMPS, Standby, Logic, and 5V rail voltages, heatsink temperatures (`heat_c`), relay states, sleep timers, and active error codes (`errors[]`).

Every valid command sent from the controller (e.g. `{"type":"cmd","cmd":{"power":false}}`) generates an immediate hardware acoustic confirmation click (`buzzerClick()`).

---

## 4. Compilation & Flashing

```bash
# Build firmware binary:
python3 tools/upload.py --build-only

# Flash to ESP32 over Android Panel via High-Speed Jacktor API @ 921,600 bps:
python3 tools/upload.py panel

# Or flash directly via host USB serial port @ 921,600 bps:
python3 tools/upload.py usb
```
