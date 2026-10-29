# Jacktor Audio - Universal USB Serial Driver (`usb-serial.kpm`)

A high-performance, multi-device, hotplug-safe KernelPatch Module (KPM) driver implementing native Linux TTY serial support (`/dev/ttyUSB0` through `/dev/ttyUSB7`) for Qualcomm Snapdragon 460 / 662 (Bengal - SM4250 / SM6115) running on Linux Kernel `4.19.152-perf+` (Android 14 GSI / Treble). Version: `1.0.0`.

---

## 1. Problem Statement & Motivation

The stock vendor kernel of the Realme C15 (`4.19.152-perf+`) does not compile or include the standard Linux USB-to-Serial converter driver subsystem (`CONFIG_USB_SERIAL=n`, `CONFIG_USB_SERIAL_CP210X=n`).

Without this driver, plugging a Silicon Labs CP210x USB-to-UART bridge (or any USB serial device) into the Android panel's USB OTG port only registers as an unknown USB interface in `sysfs` (`/dev/bus/usb/...`). Standard userspace tools (`stty`, Python `pyserial`, `minicom`, `esputil`, shell redirects) cannot access the device because the `/dev/ttyUSBx` character device node does not exist.

`usb-serial.kpm` bridges this gap at the kernel level by implementing a freestanding **KernelPatch Module (KPM)** driver that hooks directly into the running kernel's TTY core and USB host stack via exported kernel symbols (`kallsyms`).

---

## 2. Key Features & Specifications

### 2.1 Default Baud Rate & Full Dynamic Termios Support
* **Standard Default Baud Rate**: Defaults to **115,200 bps** (`0x1cb2` on `init_termios`, `c_ispeed = 115200`, `c_ospeed = 115200`) in accordance with standard Linux serial behavior.
* **Dynamic Runtime Reconfiguration**: Fully responsive to POSIX `termios` configuration calls from userspace via `stty`, `tcsetattr()`, Python `pyserial`, or flasher tools (`esputil`):
  * **Baud Rate**: Automatically decodes POSIX `CBAUD` bitmasks (50 to 3,000,000 bps) and arbitrary non-standard baud rates via `c_ospeed` (e.g. 921,600 bps for Jacktor Audio telemetry stream). Translates to `CP210X_SET_BAUDRATE` (`0x1E`) control packets sent to endpoint 0.
  * **Character Size (Data Bits)**: Translates `CSIZE` (`CS5`, `CS6`, `CS7`, `CS8`) into hardware data bits 5, 6, 7, or 8.
  * **Parity**: Configures `None`, `Even`, `Odd`, `Mark`, and `Space` parity (`PARENB`, `PARODD`, `CMSPAR`).
  * **Stop Bits**: Configures 1 stop bit, 1.5 stop bits (CS5), or 2 stop bits (`CSTOPB`).
  * **Hardware Flow Control (RTS/CTS)**: Translates `CRTSCTS` into `CP210X_SET_FLOW` (`0x13`) packets to enable/disable hardware auto-RTS and CTS handshaking.
  * **Line Break Control**: Implements TTY ops callback `break_ctl` via `CP210X_SET_BREAK` (`0x05`).
  * **Buffer Flush**: Implements TTY ops callback `flush_buffer` to clear the software TX ring buffer and send `CP210X_PURGE` (`0x12`) hardware FIFO flush requests.

### 2.2 Safe O(1) Dynamic Termios Fingerprint Resolution
Because internal layout offsets of `struct mutex` and `rw_semaphore` can shift across kernel builds due to configuration options (`CONFIG_DEBUG_MUTEXES`, `CONFIG_MUTEX_SPIN_ON_OWNER`), `usb-serial.kpm` locates `struct ktermios` inside `struct tty_struct` dynamically by scanning for the constant 6-byte fingerprint of `tty_std_termios`:
```
c_line = 0, VINTR = 3 (^C), VQUIT = 28 (^\), VERASE = 127 (^?), VKILL = 21 (^U), VEOF = 4 (^D)
```
On the Realme Bengal kernel (`4.19.152-perf+`), this is resolved exactly at offset `0x140` and cached in `g_tty_termios_offset` for instant $O(1)$ memory access without bounds overflows.

### 2.3 Multi-Device Concurrency (`g_ports[8]`)
* Manages up to 8 concurrent USB serial devices simultaneously (`/dev/ttyUSB0` through `/dev/ttyUSB7`, major `188`, minor `0..7`).
* Atomic device slot acquisition via `__sync_bool_compare_and_swap`.
* Independent per-port RX and TX kernel threads (`usb_serial_rx%d`, `usb_serial_tx%d`).
* Matches multiple CP210x hardware devices (CP2101, CP2102, CP2104, CP2105 Dual UART, CP2108 Quad UART, CP2109).

### 2.4 Memory Locking & Lifespan Safety
* **Exact Kernel Slab Allocation**: DMA buffers (`rx_buf` 512 B, `tx_buf` 4096 B, `cp210x_ctrl_buf` 64 B, `tx_ring` 16 KB) and URBs (`usb_alloc_urb`) are allocated using exact Linux 4.19 GFP flags:
  * `GFP_KERNEL = 0x6000c0u`
  * `GFP_ATOMIC = 0x280020u`
* **In-Flight Reference Guard**: All read/write and control transactions pass through `slot_io_enter()` and `slot_io_exit()` tracking `slot->io_in_flight` to prevent any kernel use-after-free or null-pointer dereference during high data rates (921,600 bps).

### 2.5 Hotplug Disconnect Safety
When a USB cable or USB OTG hub is abruptly unplugged:
1. `connected` and `port_active` flags are cleared atomically (`0`).
2. Active URB transfers are immediately aborted via `usb_kill_urb()`.
3. Background RX and TX worker threads are drained until `io_in_flight == 0`.
4. Open user TTY sessions are safely terminated via `tty_port_hangup()`.
5. Device node is deregistered using the 3-argument signature: `tty_port_unregister_device(port, driver, index)`.
6. DMA slab buffers remain pinned for the lifetime of the slot (freed only on `usb_serial_exit`) to avoid page faults on lingering userspace file descriptors.

---

## 3. Supported Silicon Labs Device IDs

| Chip Model | Vendor ID | Product ID | Notes |
| :--- | :---: | :---: | :--- |
| **CP2101 / CP2102 / CP2104 / CP2109** | `0x10c4` | `0xea60` | Single UART (Standard ESP32 NodeMCU / DevKit) |
| **CP210x Custom Single UART** | `0x10c4` | `0xea61` | Custom OEM PID Single UART |
| **CP210x Windows Update PID** | `0x10c4` | `0xea63` | Windows Update Identifier Single UART |
| **CP2105 Dual UART Bridge** | `0x10c4` | `0xea70` | Dual Interface USB-to-UART Bridge |
| **CP2108 Quad UART Bridge** | `0x10c4` | `0xea71` | Quad Interface USB-to-UART Bridge |

---

## 4. Building from Source

### Prerequisites
1. **AArch64 Musl Cross-Toolchain**: `aarch64-linux-musl-gcc` (available in `/tmp/aarch64-linux-musl-cross/bin` or standard system toolchain).
2. **KernelPatch Header Tree**: KernelPatch include directory (`KP_KERNEL_DIR = /tmp/KernelPatch/kernel`).

### Compilation Command
```bash
cd panel/realme-bengal/kpm/usb-serial
make clean && make
```

### Compiler Flags Explanation
`Makefile` specifies the following required compilation flags:
```makefile
CFLAGS = $(INCLUDE_FLAGS) -Wall -O2 -fno-PIC -mno-outline-atomics \
         -fno-asynchronous-unwind-tables -fno-unwind-tables \
         -fno-exceptions -fno-stack-protector -fno-common -ffixed-x18
```
* **`-mno-outline-atomics`**: **CRITICAL**. Forces GCC to emit inline ARM64 atomic instructions (`ldaxr`, `stlxr`) instead of emitting outline function calls (`__aarch64_cas4_acq_rel`) which are not exported by the kernel symbol table.
* **`-ffixed-x18`**: Preserves the ARM64 platform register `x18` reserved by the Linux kernel.
* **`-fno-stack-protector`**: Prevents stack canary calls (`__stack_chk_guard`) in standalone kernel modules.

---

## 5. Deployment & Installation to Android Panel

### Method 1: APatch KPM Directory (Persistent Boot Loading)
1. Ensure the Android panel is rooted using **APatch** (KernelPatch-based root solution).
2. Push the compiled module binary to the dedicated APatch KPM storage:
   ```bash
   adb push usb-serial.kpm /data/local/tmp/
   adb shell su -c 'mkdir -p /data/adb/ap/kpm/usb-serial && \
                    cp /data/local/tmp/usb-serial.kpm /data/adb/ap/kpm/usb-serial/usb-serial.kpm && \
                    chmod 644 /data/adb/ap/kpm/usb-serial/usb-serial.kpm'
   ```
   > [!IMPORTANT]
   > The folder name under `/data/adb/ap/kpm/` **MUST match** the `KPM_NAME` macro in the C source (`usb-serial`), and the binary file **MUST be named** `usb-serial.kpm`. If the names do not match, the APatch daemon (`apd`) will unload the module on `BOOT_COMPLETED`.

3. Reboot the device:
   ```bash
   adb reboot
   ```

### Method 2: Bundled with Jacktor Audio Framework Module
When building the project via the unified build script:
```bash
python3 tools/build.py
```
`usb-serial.kpm` is compiled and automatically bundled into `panel/realme-bengal/modul/jacktor-audio-framework/kpm/usb-serial.kpm` inside `build/jacktor-framework.zip`. When the module is flashed in APatch, `customize.sh` automatically installs the driver into `/data/adb/ap/kpm/usb-serial/usb-serial.kpm`.

---

## 6. Testing & Hardware Diagnostics

### Verify Device Node Creation
Plug in the ESP32 or USB-to-UART converter to the Android USB OTG port, then run:
```bash
adb shell su -c 'ls -l /dev/ttyUSB*'
```
Expected output:
```
crw-rw---- 1 bluetooth bluetooth 188,   0 2026-10-29 19:30 /dev/ttyUSB0
```

### Check Kernel Messages (`dmesg`)
```bash
adb shell su -c 'dmesg | grep usb-serial'
```
Expected output:
```
usb-serial: probe cp210x slot=0 devnum=3 ifnum=0 speed=2
usb-serial: attached cp210x converter to /dev/ttyUSB0 (ep_in=0x81, ep_out=0x01)
usb-serial: termios resolved at offset 0x140 (cflag=0x00001cb2)
```

### Verify Default Baud Rate (115,200 bps)
```bash
adb shell su -c 'stty -F /dev/ttyUSB0'
```
Expected output:
```
speed 115200 baud; line = 0;
hupcl clocal
-brkint -icrnl -imaxbel
-opost -onlcr
-isig -icanon -iexten -echo -echoe -echok -echoctl -echoke
```

### Test Dynamic Baud Rate Switching
```bash
# Switch to 9600 bps:
adb shell su -c 'stty -F /dev/ttyUSB0 9600 && stty -F /dev/ttyUSB0'

# Switch to 921600 bps (Jacktor Audio stream rate):
adb shell su -c 'stty -F /dev/ttyUSB0 921600 && stty -F /dev/ttyUSB0'

# Verify in dmesg:
adb shell su -c 'dmesg | grep -E "baudrate set to"'
```
Expected dmesg logs:
```
usb-serial: ttyUSB0 baudrate set to 9600 bps
usb-serial: ttyUSB0 baudrate set to 921600 bps
```

### Test Custom Framing (e.g. 7 Data Bits, Even Parity, 2 Stop Bits)
```bash
adb shell su -c 'stty -F /dev/ttyUSB0 cs7 parenb -parodd cstopb'
adb shell su -c 'stty -F /dev/ttyUSB0 -a | grep -E "cs|par|stop"'
```
Expected output:
```
parenb -parodd -cmspar cs7 hupcl cstopb cread clocal -crtscts
```

---

## 7. License
This driver is distributed under the **GNU General Public License v2.0 or later (GPL-2.0-or-later)**.
Author: **Ardo Zapp** for Jacktor Audio Framework.
