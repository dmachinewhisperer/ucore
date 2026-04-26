# ucore ESP32-S3 generic

Generic ESP32-S3 dev board profile (e.g. Espressif ESP32-S3-DevKitC-1, YD-ESP32-S3, WeAct ESP32-S3 Core, Waveshare ESP32-S3-DevKitC).

## Identity
- MCU: ESP32-S3 (Xtensa LX7, dual-core, up to 240 MHz, vector instructions for ML)
- Flash: typically 8 MB QSPI (board-dependent)
- PSRAM: typically 8 MB octal (board-dependent; requires SPIRAM_OCT sdkconfig variant)

## ucore wire
- JMP frames over UART0 at 115200 baud, 8N1, no flow control.
- ESP32-S3 dev kits typically expose **two USB-C ports**:
  - **UART port** — onboard USB-to-UART bridge (CP2102N or CH343) wired to UART0 (GPIO 43 TX, GPIO 44 RX). **This is the one ucore talks on.**
  - **USB port** — native USB-Serial/JTAG of the chip itself (GPIO 19/20). Used for flashing in download mode and for IDF's USB-JTAG logs; ucore does not transit this port.

## Onboard hardware
- LED: GPIO 48
  - On most S3 dev kits this is a single **WS2812 RGB neopixel**, NOT a plain GPIO LED.
  - To blink it: `import neopixel, machine; np = neopixel.NeoPixel(machine.Pin(48), 1); np[0] = (0, 32, 0); np.write()`.
  - Toggling `Pin(48)` directly will not light it.
- BOOT button: GPIO 0. Also a boot-strap pin — pull low at reset to enter download mode.

## Capabilities present
- WiFi 802.11 b/g/n
- Bluetooth LE 5
- Native USB peripheral (when enabled in firmware)
- I²C, SPI, UART (multiple), I²S, PWM, ADC, RMT, capacitive touch sensor (touch_pad)

## Notes
- GPIOs 26–32 are wired to the SPI flash; do not use.
- GPIOs 33–37 may be reserved for octal PSRAM when SPIRAM_OCT is enabled. Safe on quad-mode boards.
- The native USB port resets the chip on connect — for headless deployment, prefer the UART port.
