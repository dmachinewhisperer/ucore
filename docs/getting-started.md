# Getting started

## 1 · Flash the firmware

Before installing the kernel, your microcontroller needs the µcore firmware.
Head to the [Firmware](flash.md) page, connect your board over USB, and flash
it directly from the browser — no toolchain required.

## 2 · Install the kernel

```bash
pip install ukernel
```

The wheel ships the µcore kernelspec and the device-manager sidebar extension.
`jupyter lab` picks them up automatically — no extra registration step needed.

??? note "Custom Python setups (pyenv, conda)"
    If `which python3` doesn't resolve to the interpreter you want the kernel
    to run under, install the kernelspec explicitly:

    ```bash
    python -m ukernel install        # register for this interpreter
    python -m ukernel install --user # install to ~/.local/share/jupyter
    ```

## 3 · Launch JupyterLab

```bash
jupyter lab
```

The launcher shows a **µcore** tile next to **Python 3**. Open it to start a
µcore notebook.

!!! tip "Selecting a microcontroller"
    The **Device Manager** panel in the left sidebar lists every detected
    board. Click one to make it the active microcontroller for the session.
    If only one board is connected, the kernel picks it automatically.

## Supported hardware

µcore runs on the **ESP32 chip family**. Any chip in the family can be added
as long as MicroPython supports it — the µcore firmware is compiled per chip
and frozen alongside the standard MicroPython library.

| Chip | Example boards |
|---|---|
| ESP32 | Generic ESP32 dev boards (30-pin, 38-pin) |
| ESP32-S3 | ESP32-S3-DevKitC-1, YD-ESP32-S3, WeAct S3, Waveshare S3 |

Other ESP32 variants (C3, S2, C6, H2) are candidates for future support.
