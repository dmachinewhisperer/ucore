---
name: device_basics
description: How code is split between the ESP32 device and the host kernel, how cells are routed, and how the kernel handles unplug/replug.
keywords: [magic, cell, ucore, device, host, micropython, serial, attach, replug, hotplug, restart, kernel]
---

A notebook running the µcore kernel has two execution targets:

- **Device** — MicroPython on the ESP32, reached via the JMP transport over USB serial.
- **Host** — a regular CPython sub-kernel running inside the same Jupyter kernel process.

A cell is dispatched by its first non-blank line. If it starts with `%%ucore`, the body runs on the device. Otherwise it runs on the host.

```python
%%ucore
# runs on the ESP32
import sys
print(sys.implementation)
```

```python
# runs on the host (regular Python)
import platform
print(platform.python_version())
```

**Variables do not cross the boundary.** A name bound in a `%%ucore` cell is not visible from a host cell, and vice versa. To move data between them, use a pipe (see the `streaming` skill) or print structured output and read it back.

## Hot-plug behaviour

The kernel discovers the ESP32 over USB serial at startup and again on every cell run. If the device wasn't attached at startup, no restart is needed — plug it in and re-run the cell. The kernel will probe the bus and bind to the first JMP-speaking serial device it finds.

If you replug the device while a cell is mid-execution, that cell will fail with `ConnectionError: Device not connected (serial)`. Re-run the cell; the kernel will reattach automatically.

## What requires a kernel restart

Almost nothing. You generally only need to restart when:

- You install or upgrade an `ipykernel`-side Python package and want it imported fresh.
- The serial link has gotten into a desynchronised state (rare; symptoms are `SyntaxError` on a previously-working `%%ucore` cell — see the `debugging` skill).
