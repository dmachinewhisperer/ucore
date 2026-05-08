---
name: peripherals
description: Read sensors and drive actuators on the ESP32 from MicroPython — GPIO, PWM, ADC, I2C, WS2812, servos, motors.
keywords: [gpio, pin, adc, pwm, i2c, spi, led, neopixel, ws2812, servo, motor, button, relay, buzzer, sensor, machine, peripheral]
---

All hardware access happens in `%%ucore` cells, using MicroPython's `machine` module and friends. The on-board ESP32-S3 dev board exposes a WS2812 RGB LED on GPIO 48 (`neopixel` module), a USB-serial bridge for the kernel link, and the usual GPIO header.

## Digital pins

```python
%%ucore
from machine import Pin
button = Pin(0, Pin.IN, Pin.PULL_UP)
led    = Pin(2, Pin.OUT)
led.value(button.value())
```

## On-board WS2812 (GPIO 48)

```python
%%ucore
import machine, neopixel
np = neopixel.NeoPixel(machine.Pin(48), 1)
np[0] = (0, 32, 0)   # dim green
np.write()
```

Always end with an explicit `np[0] = (0, 0, 0); np.write()` if you want the LED off.

## ADC — analog reads

```python
%%ucore
from machine import ADC, Pin
adc = ADC(Pin(34))
adc.atten(ADC.ATTN_11DB)   # 0–3.3 V range
adc.width(ADC.WIDTH_12BIT) # 0–4095
print(adc.read())
```

## PWM — LED dim, buzzer, servo

```python
%%ucore
from machine import Pin, PWM
servo = PWM(Pin(13), freq=50)
servo.duty(77)        # ~90° for a typical hobby servo (range ~26–128)
# ...
servo.deinit()        # always release the pin when done
```

PWM duty range on ESP32 MicroPython is 0–1023.

## I2C scan

```python
%%ucore
from machine import I2C, Pin
i2c = I2C(0, scl=Pin(22), sda=Pin(21), freq=400_000)
print(i2c.scan())     # list of 7-bit addresses on the bus
```

## Buttons / debounce

```python
%%ucore
from machine import Pin
import time
btn = Pin(0, Pin.IN, Pin.PULL_UP)
last = btn.value()
while True:
    cur = btn.value()
    if cur != last and cur == 0:    # falling edge = press
        print("pressed")
    last = cur
    time.sleep_ms(20)
```

## Notes

- `time.sleep_ms()` for sub-second waits — `time.sleep()` takes seconds.
- For continuous streams (sampling at a rate, plotting live), don't `print()` in a loop — use a pipe (see the `streaming` skill).
- `deinit()` PWM and close I2C/SPI peripherals when done so the next cell can reuse the pin.
