# Qualcomm QCC5125 Bluetooth Co-Processor & DSP Subsystem

This directory is reserved for the custom firmware, DSP parameter calibration, and configuration workspaces for the **Qualcomm QCC5125 Bluetooth 5.1 Audio Co-Processor** utilized in the Jacktor Audio Amplifier architecture.

---

> [!IMPORTANT]
> ### Firmware Staging & Release Status
> The custom QCC5125 firmware and Qualcomm MDE/ADK workspace files are **currently under active development, hardware laboratory calibration, and audio qualification testing**.
> 
> To ensure deterministic audio stability, code releases for this subsystem are staged outside the public commit history until:
> 1. Multi-point Bluetooth connection stability and automated reconnect routines are fully verified.
> 2. High-resolution codec streaming (LDAC 990 kbps, aptX HD, and aptX Adaptive) passes full hardware bit-depth audio compliance.
> 3. RF power dynamic attenuation (-12 dBm to -6 dBm after handshake) is verified on spectrum analyzers to guarantee zero RF induction noise into the analog amplifier input stage.
> 4. GAIA V3 protocol integration with the ESP32 amplifier ECU and Android Panel is finalized.

---

## 1. Hardware Architecture & Specification

The Qualcomm QCC5125 functions as a dedicated wireless audio co-processor and GAIA peripheral to offload high-bandwidth Bluetooth streaming and DSP routing from the main ESP32 Electronic Control Unit (ECU):

- **Processor Core Architecture**:
  - Dual-Core 32-bit Kalimba DSP @ 120 MHz (dedicated to digital audio filtering and codec decoding).
  - 32-bit Applications Subsystem @ 32 MHz (manages Bluetooth stack, L2CAP, SDP, and RFCOMM).
- **Bluetooth Specification**: Bluetooth 5.1 dual-mode radio (BR/EDR + BLE).
- **Supported Audio Codecs**:
  - **Sony LDAC** (up to 96 kHz / 24-bit @ 990 kbps).
  - **Qualcomm aptX Adaptive** (dynamically scales latency and bitrate).
  - **Qualcomm aptX HD** (48 kHz / 24-bit high-resolution audio).
  - **Qualcomm aptX Low Latency** (ultra-low gaming latency).
  - **AAC & SBC** (legacy and iOS streaming support).
- **Audio Output Stage**:
  - Direct digital I2S stream to external high-performance differential DAC.
  - Native USB Audio class 2.0 interface.

---

## 2. Hardware Pinout & Inter-Subsystem Control

The QCC5125 co-processor interfaces directly with the ESP32 amplifier ECU and Android Control Panel:

| Function | ESP32 GPIO | Direction | Description |
| :--- | :---: | :---: | :--- |
| **QCC Power Enable** | `GPIO 4` | Output | Master power gate for the QCC5125 module. Driven HIGH during system power-up. |
| **QCC Power Detect** | `GPIO 5` | Input | Status feedback from QCC5125 LDO. If LOW after power sequence, ECU triggers `E-04` alarm. |
| **GAIA SPP Bridge** | Bluetooth RFCOMM | Bi-Directional | Serial Port Profile (SPP) channel used by ESP32 to monitor codecs and issue GAIA commands. |

### Safety Finite State Machine (FSM) Integration:
- **Strict Power Detection**: If `GPIO 5` remains LOW during amplifier startup self-test (Step 6), the ESP32 immediately aborts the Bluetooth connection sequence and logs error `E-04 (QCC_DEAD)`.
- **Low-Noise Dynamic RF Control**: During initial pairing, TX power is set to high (+3 dBm to +9 dBm) for maximum pairing range. Once the link remains stable for 3 seconds, TX power is automatically attenuated to low-noise power (-12 dBm to -6 dBm) to prevent RF bleed into the high-gain analog input stages.

---

## 3. Development & Toolchain Requirements (When Available)

When firmware source packages are published to this repository, building and flashing will require:

1. **Qualcomm MDE (Model Development Environment)**: Integrated development environment for QCC51xx / QCC30xx series.
2. **Qualcomm Audio Development Kit (ADK)**: Version 20.x or newer with Kymera audio framework.
3. **Hardware Programmer**: Qualcomm TRBI200 or USB-SPI debug programmer connected to the module's SPI debug test points (PIO SPI / CS, CLK, MISO, MOSI).
4. **Qualcomm BlueSuite / TestFlash**: Utility suite for reading/writing configuration NVRAM (PSKeys / MIB keys) and Bluetooth Device Address (BD_ADDR).

---

## 4. Documentation & References

- [Amplifier ECU Firmware Documentation](../amplifier/README.md)
- [System Error Codes & Hardware Alarms](../../docs/ERROR_CODES.md)
- [Android Control Panel Application](../../panel/app/README.md)
