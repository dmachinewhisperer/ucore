---
name: debugging
description: Recover from common µcore failures — connection errors, syntax errors on previously-working cells, pipe timeouts, missing kernel.
keywords: [error, fail, crash, debug, reconnect, timeout, syntaxerror, connection, restart, reset, broken, troubleshoot, fix]
---

Most µcore failures fall into one of four buckets. Match the symptom to the bucket and apply the fix.

## `ConnectionError: Device not connected (serial)`

The host can't see the device on a USB serial port.

1. Check the bus: in a host cell, `!ls /dev/ttyACM* /dev/ttyUSB*`. If nothing shows, the device isn't enumerated.
2. On WSL, attach via usbipd from PowerShell: `usbipd attach --wsl --busid <BUSID>`.
3. If the device is visible but the cell still fails, replug once and re-run the cell. The kernel re-resolves the serial port on every reconnect — no kernel restart needed.

## `SyntaxError: invalid syntax` on a `%%ucore` cell that previously worked

Almost never a real syntax error — it's JMP serial-link desync. `/tmp/ucore-kernel.log` will show `frame decode error: not enough input bytes for length code` or similar. That confirms it.

Recovery: detach + reattach the USB device (this resets the chip), then restart the kernel. Don't try to "fix" the cell — the cell is fine.

## `TimeoutError` from `collect(...)`

`collect` waited the full window and got zero bytes. Almost always one of:

1. **No producer is running.** Most common case. Start the device-side producer cell first, then call `collect`.
2. **Pipe name mismatch.** The string passed to `ucore.open_pipe` on the device must match the string passed to `collect` / `subscribe` / `live_plot` on the host. Case-sensitive.
3. **Producer thread crashed.** Check the device cell's output for a traceback. Common cause: forgetting the `is_pipe_open()` guard.

To distinguish (1) from (2)/(3), pass `allow_empty=True` and try `subscribe` directly — if that also stays silent, the producer side is broken.

## µcore kernel missing from the launcher

After `pip install -e ./ukernel`, the kernelspec and server-extension config get stripped (editable installs don't honour `[tool.setuptools.data-files]`).

Fix: run `scripts/dev-install.sh` from the repo root. Verify with `jupyter kernelspec list` (should show `ucore`) and `jupyter server extension list` (should show `ukernel.server_extension` enabled).

## General first-aid

- `/tmp/ucore-kernel.log` — kernel-side log including transport state and frame errors. First place to look.
- Kernel → Restart Kernel from JupyterLab — recovers most in-process state corruption without losing notebook output.
- Detach + reattach the USB device — recovers serial desync and most device-side hangs.
