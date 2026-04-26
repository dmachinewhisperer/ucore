# ucore ESP32 generic

Generic ESP32 dev board profile (e.g. ESP32-DevKitC, NodeMCU-32S, Wemos D1 R32, plain WROOM-32 modules).

## Identity
- MCU: ESP32 (Xtensa LX6, dual-core, up to 240 MHz)
- Flash: typically 4 MB QSPI (board-dependent)
- No PSRAM by default

## ucore wire
- JMP frames over UART0 at 115200 baud, 8N1, no flow control.
- UART0 is the same line ESP-IDF logs print on, exposed via the onboard USB-to-UART bridge (CP2102 / CP2104 / CH340 — board-dependent).

## Onboard hardware
- LED: GPIO 2. Plain GPIO, active high. `machine.Pin(2, machine.Pin.OUT).value(1)` lights it.
- BOOT button: GPIO 0. Also a boot-strap pin — pull low at reset to enter download mode.

## Capabilities present
- WiFi 802.11 b/g/n
- Bluetooth Classic + BLE
- I²C, SPI, UART (multiple), I²S, PWM, ADC, DAC, RMT, capacitive touch (T0–T9)

## Notes
- GPIOs 6–11 are wired to the SPI flash; do not use.
- GPIOs 34–39 are input-only (no output, no internal pull-ups).
