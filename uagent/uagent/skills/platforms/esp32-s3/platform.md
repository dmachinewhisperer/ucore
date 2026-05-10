---
name: platform
description: ESP32-S3 platform notes — deltas from generic ESP32 (USB OTG, more RAM, Xtensa LX7).
keywords: [esp32-s3, s3, usb-otg, usb-jtag, psram, xtensa, lx7]
inherits: [esp32]
---

The connected device is an Espressif **ESP32-S3**, a dual-core Xtensa LX7 variant. All generic ESP32 notes apply unless contradicted below.

## What's different from classic ESP32

- **Native USB-OTG / USB-Serial-JTAG controller.** The chip enumerates as `/dev/ttyACM*` rather than the FTDI/CP2102 `/dev/ttyUSB*` you'd see on classic ESP32 dev boards. The kernel handles both, but logs and `usbipd` listings will reflect this.
- **More on-chip SRAM** (typically 512 KB) and many variants ship with PSRAM. Heap pressure is less acute than on classic ESP32 — but the watchdog still runs.
- **No Bluetooth Classic.** BLE only.
- **Vector instructions** for AI / DSP exist but MicroPython does not use them. Don't expect them to speed up your Python code.

## Pins to avoid

- GPIO 19 and 20 are the USB D-/D+ lines on most S3 boards. Driving them as regular GPIO will break the USB connection — meaning the kernel will lose the device until reset.
- GPIO 26–32 may be wired to the on-package SPI flash on certain S3 variants (especially WROOM modules with octal flash). Driving them can corrupt flash access. Check the module datasheet before using.
