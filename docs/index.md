# µcore

**Jupyter notebooks for your microcontroller.**

µcore is a Jupyter kernel that runs MicroPython on a microcontroller
over USB. Write cells, stream sensor data into live plots, no flashing
toolchain required.

[Flash a board :material-flash:](flash.md){ .md-button .md-button--primary }
[Get started :material-rocket-launch:](getting-started.md){ .md-button }

---

## How it works

A `%%ucore` cell sends MicroPython code to the device, runs it on the
microcontroller, streams stdout back. A regular cell on the host can
subscribe to a *named pipe* the device opens — sensor data flows in
real time and you can plot it live.

```python
%%ucore
import _thread, time, struct, ucore, esp32

def producer():
    pipe = ucore.open_pipe("temp")
    while ucore.is_pipe_open("temp"):
        pipe.write(struct.pack("<f", esp32.mcu_temperature()))
        time.sleep_ms(500)

_thread.start_new_thread(producer, ())
```

```python
from ukernel.pipes import live_plot
live_plot("temp", title="Die temp (°C)")
```

That's the whole loop: producer cell starts a background thread on the
device; consumer cell on the host subscribes by name and animates.

## Supported hardware today

- ESP32 (generic)
- ESP32-S3

More boards as the project grows.
