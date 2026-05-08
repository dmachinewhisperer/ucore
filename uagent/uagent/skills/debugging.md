---
name: debugging
description: Recover from common µcore failures — connection errors, syntax errors on previously-working cells, pipe timeouts, missing kernel.
keywords: [error, fail, crash, debug, reconnect, timeout, syntaxerror, connection, restart, reset, broken, troubleshoot, fix]
---

Most µcore failures fall into one of four buckets. Match the symptom to the bucket and apply the fix.

## `ConnectionError: Device not connected (serial)`

The host can't see the ESP32 on a USB serial port.

1. Check the bus: in a host (non-`%%ucore`) cell, run `!ls /dev/ttyACM* /dev/ttyUSB*`. If nothing shows, the device isn't enumerated.
2. On WSL, attach via usbipd from PowerShell: `usbipd attach --wsl --busid <BUSID>`. List with `usbipd list`.
3. If the device is visible but the cell still fails, replug it once and re-run the cell. The kernel re-resolves the serial port on every reconnect attempt — no kernel restart needed.

## `SyntaxError: invalid syntax` on a `%%ucore` cell that previously worked

This is almost never a real syntax error. It means the JMP serial link has desynchronised — the host and device disagree about frame boundaries.

1. Look at `/tmp/ucore-kernel.log` for `frame decode error: not enough input bytes for length code` or similar. That confirms desync.
2. Fix: detach + reattach the USB device (this resets the chip), then restart the kernel. Do not try to "fix" the cell — the cell is fine.

## `TimeoutError: no samples received on pipe ...`

`collect` waited the full window and got zero bytes from the named pipe. Possible causes:

1. **No producer is running.** The most common case. Start the device-side producer cell first, then call `collect`.
2. **Pipe name mismatch.** The string passed to `ucore.open_pipe` on the device must match the string passed to `collect` / `subscribe` / `live_plot` on the host. They're case-sensitive.
3. **Producer thread crashed.** Check the device cell's output for a traceback. A common cause is forgetting `is_pipe_open()` and the thread exiting cleanly.

To distinguish (1) vs (2)/(3), pass `allow_empty=True` and inspect the empty array — if you also can't `subscribe`, the producer side is the issue.

## µcore kernel missing from the launcher

After a `pip install -e ./ukernel`, the kernelspec and server-extension config get stripped (editable installs don't honour `[tool.setuptools.data-files]`).

Fix: run `scripts/dev-install.sh` from the repo root. It restages both. You'll know it worked when `jupyter kernelspec list` shows `ucore` and `jupyter server extension list` shows `ukernel.server_extension` enabled.

## General first-aid

- `/tmp/ucore-kernel.log` — kernel-side log, including transport state and frame errors. Always the first place to look.
- `Kernel → Restart Kernel` from the JupyterLab menu — recovers from most in-process state corruption without losing notebook output.
- Detach + reattach the USB device — recovers serial desync and most device-side hangs.
