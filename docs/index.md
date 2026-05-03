# µcore

**A Jupyter kernel for microcontrollers.**

µcore lets you write and run MicroPython on an ESP32 directly from a Jupyter
notebook. Regular cells run on the host; add `%%ucore` to the top of any cell
to run it on the microcontroller instead. No toolchain, no flash cycle for
every change.

[Flash a board :material-flash:](flash.md){ .md-button .md-button--primary }
[Get started :material-rocket-launch:](getting-started.md){ .md-button }

---

## Blink an LED

```python
%%ucore
import machine, time

led = machine.Pin(2, machine.Pin.OUT)
for _ in range(10):
    led.on();  time.sleep_ms(200)
    led.off(); time.sleep_ms(200)
```

## Read a capacitive touch sensor

```python
%%ucore
import machine, time

touch = machine.TouchPad(machine.Pin(4))
for _ in range(20):
    print(touch.read())
    time.sleep_ms(200)
```

## Connect to Wi-Fi

```python
%%ucore
import network, time

wlan = network.WLAN(network.STA_IF)
wlan.active(True)
wlan.connect("your-ssid", "your-password")

for _ in range(20):
    if wlan.isconnected():
        break
    time.sleep_ms(500)

print("Connected:", wlan.ifconfig()[0] if wlan.isconnected() else "failed")
```

## Stream sensor data live

A background thread on the microcontroller pushes samples through a named
pipe; a regular Python cell subscribes and plots in real time.

```python
%%ucore
import ucore, _thread, time, esp32, struct

pipe = ucore.open_pipe("temp")

def run():
    while ucore.is_pipe_open("temp"):
        pipe.write(struct.pack("<f", esp32.mcu_temperature()))
        time.sleep_ms(500)

_thread.start_new_thread(run, ())
```

```python
%matplotlib widget
from ukernel.pipes import live_plot

live_plot("temp", ylim=None, title="MCU temperature (°C)")
```

The producing cell returns immediately — the chart updates as data arrives.
See [Pipes](pipes.md) for the full API.
