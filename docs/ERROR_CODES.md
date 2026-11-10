# Jacktor Audio - System Error Codes & Telemetry Faults

This document outlines the hardware error codes displayed on the front panel's TM1637 7-segment display (`tm1637ShowText`) and reported in JSON telemetry (`errors[]` array in `hz1` slow frame) when the system encounters a fault during boot self-test or runtime operation.

---

## 1. Error Codes & Fault Table

| Display Code | Telemetry String (`errors[]`) | Classification | Component / Subsystem | Behavior & Recovery |
| :---: | :--- | :--- | :--- | :--- |
| **`E-01`** | `"ADS1115_MISSING"` | **Hardware Critical** | ADS1115 16-bit Voltmeter ADC (I2C `0x48`) | System aborts boot / enters standby to prevent unmonitored SMPS or logic voltage operations. Check I2C SDA/SCL lines. |
| **`E-02`** | `"SMPS_FAULT"` | **Hardware Critical** | SMPS 65V Power Supply (Optocoupler GPIO 15) | Triggered when SMPS error LED illuminates (short circuit, overcurrent, or internal thermal shutdown). Main relay shuts down immediately. |
| **`E-03`** | `"SPEAKER_PROTECT"` | **Hardware Critical** | Speaker Protection Board (Optocoupler GPIO 35) | DC offset or speaker relay failure detected. Audio output relay disconnected; requires power cycle. |
| **`E-04`** | `"QCC_DEAD"` | **Hardware Critical** | Qualcomm QCC5125 Bluetooth Co-processor | Power detect pin (GPIO 5) failed to transition HIGH within 2 seconds of power activation. |
| **`E-05`** | `"DS18B20_MISSING"` | **Hardware Warning** | DS18B20 1-Wire Digital Thermal Sensor (GPIO 27) | Sensor bus communication failure. System activates failsafe cooling (Fan 100%) as a precaution. |
| **`E-06`** | `"DS3231_MISSING"` | **Hardware Warning** | DS3231 Real-Time Clock (I2C `0x68`) | Hardware RTC unreadable. System falls back to FreeRTOS software tick timekeeping. |
| **`E-07`** | `"GAIA_TIMEOUT"` | **Communication** | QCC5125 GAIA SPP Protocol Connection | SPP command handshaking timed out. Bluetooth audio streaming may still function, but app telemetry is disabled. |
| **`E-08`** | `"OTP"` | **Hardware Critical** | Over-Temperature Protection (Heatsink $\ge 85^\circ\text{C}$) | Thermal trip threshold exceeded. Main power relay trips to standby; fan runs at 100% until heatsink cools. |
| **`E-09`** | `"SMPS_LOW_VOLTAGE"` | **Hardware Critical** | SMPS 65V Rail Under-Voltage ($< 50.0\text{V}$) | SMPS rail sagged below cutoff while amplifier is active. Standby power rail is strictly excluded from triggering this alarm. |
| **`ERR `** | `"GENERIC_ERROR"` | **General** | Unclassified System Exception | Fallback code for unhandled critical states or multiple compounding non-critical faults. |

---

## 2. Fault Handling Policies

1. **Hardware Critical Policy**:
   - Any fault classified as `Hardware Critical` immediately aborts the active sequence, turns off the main SMPS relay (GPIO 14), disengages the speaker relay (GPIO 25), pulses the acoustic alarm buzzer, and transitions the amplifier to safe **Standby Mode**.
2. **Hardware Warning Policy**:
   - Warnings do not shut down audio playback. The cooling fan is ramped to 100% (failsafe mode), and the telemetry flags the warning for operator review in the WebUI and Android Panel.
3. **Under-Voltage Scoping**:
   - `SMPS_LOW_VOLTAGE` is evaluated exclusively when the amplifier is powered on (`powerIsOn() == true`). The continuous Standby rail (4.3V – 12V) is intentionally prohibited from tripping under-voltage alarms to prevent false triggers during USB-powered bench testing.
