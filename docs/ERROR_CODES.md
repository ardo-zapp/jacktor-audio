# Jacktor Audio - System Error Codes

This document outlines the error codes displayed on the front panel's 7-segment display (`tm1637ShowText`) and reported in JSON telemetry when the system encounters an issue, particularly during setup diagnostics or runtime operations.

| Code | Type | Description / Module | Notes |
|---|---|---|---|
| E-01 | Hardware Critical | **ADS1115 (Voltmeter) Missing / Dead** | System will shutdown to prevent unsafe SMPS/Standby voltage levels. I2C bus might be hung. |
| E-02 | Hardware Critical | **SMPS Fault Detected** | GPIO 15 Optocoupler triggered. Usually caused by short circuit, overcurrent, or SMPS thermal shutdown. |
| E-03 | Hardware Critical | **Speaker Protection Fault** | The speaker relay board detected DC offset or relay failure. Audio relay will not engage. |
| E-04 | Hardware Critical | **QCC5125 (Bluetooth) Dead** | Power detect pin (GPIO 5) did not go HIGH within 2 seconds of power on, or the module crashed permanently. |
| E-05 | Hardware Warning | **DS18B20 (Temperature) Missing** | Sensor read failed. System runs with failsafe maximum cooling (Fan 100%) as a precaution. |
| E-06 | Hardware Warning | **DS3231 (RTC) Missing** | Real-time clock failed. System runs with software timers (time sync lost upon power cycle). |
| E-07 | Communication | **GAIA Connection Timeout** | SPP Connection failed to establish with QCC5125. BT Audio might work, but telemetry/app controls are disabled. |
| ERR  | General | **Generic / Unhandled Error** | An unknown error occurred or multiple non-critical errors escalated. |

*Note: If an error is categorized as `Hardware Critical`, the system will abort the startup sequence and forcefully shut down the main power relay (entering standby mode) to prevent damage.*
