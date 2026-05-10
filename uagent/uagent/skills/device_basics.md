---
name: device_basics
description: How code is split between the device and the host kernel, and how the kernel handles device unplug/replug.
keywords: [magic, cell, ucore, device, host, micropython, serial, attach, replug, hotplug, restart, kernel, sub-kernel]
---

A notebook running the µcore kernel has two execution targets:

- **Device** — MicroPython on the connected microcontroller, reached via the JMP transport over USB serial.
- **Host** — a regular CPython sub-kernel running inside the same Jupyter kernel process.

Routing is by cell magic. A cell whose first non-blank line is `%%ucore` runs on the device; everything else runs on the host.

Host and device share **no state**. Names bound in a `%%ucore` cell are not visible from a host cell, and vice versa. Imports, module state, and `globals()` do not cross the boundary. To move data between them, use a pipe (see the `streaming` skill) or print structured output and parse it.

A `%%ucore` cell's stdout is the device's `print()` output, transported over JMP and rendered in the cell — not a captured pipe.

## Hot-plug

The kernel re-resolves the serial port on every cell run. Plug, unplug, and replug do not require a kernel restart; the next cell will reattach. A cell that fails mid-execution because the device went away returns a clear `ConnectionError`; rerunning the cell after the device is back will succeed.

If multiple devices are on the bus, the kernel picks the first JMP-speaking one and logs which.

## When you actually need to restart the kernel

Almost never. Two cases:

- A `SyntaxError` on a `%%ucore` cell that just worked — this is JMP frame desync, not a real syntax error. See the `debugging` skill.
- You changed an installed Python package on the host side and want it re-imported into the sub-kernel.
