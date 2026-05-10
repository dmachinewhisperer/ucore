---
name: peripherals
description: Generic ESP32 peripheral guidelines — GPIO, ADC, PWM, I²C, communications. Things the LLM commonly gets wrong.
keywords: [gpio, pin, adc, pwm, i2c, spi, uart, wifi, bluetooth, ble, antenna, peripheral]
---

ESP32-family peripherals via MicroPython's `machine` module. The notes below highlight the easy-to-miss bits — the rest is general MicroPython knowledge.

## GPIO

- "Pin number" everywhere means the chip GPIO number, **not** the silkscreen label on a particular dev board. Check the board's pinout sheet to map silkscreen → GPIO.
- Some pins are **strapping pins** (held low/high at boot to select boot mode). Driving them externally at reset can prevent the chip from booting. Common offenders: GPIO 0, 2, 12, 15 on classic ESP32. Verify against the chip's datasheet before driving.
- Some pins are **input-only** on certain variants — they can read but cannot drive. Trying to set `Pin.OUT` on them silently does nothing.

## ADC

- Default attenuation reads ~0–1 V; readings will saturate above that. Call `adc.atten(ADC.ATTN_11DB)` for the full ~0–3.3 V range.
- Default `width` is 12-bit (0–4095). Set explicitly if a different range is wanted.
- ADC2 channels share the radio; reading ADC2 while Wi-Fi is active will fail or block.

## PWM

- Duty range on ESP32 MicroPython is **0..1023** by default. Not 0..255, not 0..65535 — both common LLM misfires. Some forks expose `duty_u16()` (0..65535); check what the firmware ships before assuming.
- Frequency range depends on the LEDC peripheral; sub-1 Hz and >40 kHz both fail silently.
- Always `pwm.deinit()` before reassigning the same pin to another peripheral.

## I²C and SPI

- The device has multiple hardware I²C buses (typically `I2C(0)` and `I2C(1)`); pin assignment is configurable via constructor. There is no "default" SDA/SCL — the values vary per board and must be passed explicitly.
- I²C scans are cheap and the canonical first-step debug for a sensor that isn't responding.
- SPI is similar — pins are not fixed; pass them in the constructor.

## Communications

- Wi-Fi: `network.WLAN(network.STA_IF)`. Connecting can take several seconds; don't busy-loop without yielding. Wi-Fi power state shares with ADC2.
- Bluetooth (BLE) is available but heavy on RAM — enabling BLE alongside Wi-Fi can OOM smaller variants.
- The on-chip antenna is shared between Wi-Fi and BT. Concurrent use degrades both.

## Sleep / power

- `machine.lightsleep(ms)` and `machine.deepsleep(ms)` differ significantly: lightsleep preserves RAM and resumes; deepsleep is a reset-with-wake-pin. State across deepsleep must be persisted (RTC memory, flash).
- GPIO state is not guaranteed during sleep transitions — drive pins to known states before sleeping.
