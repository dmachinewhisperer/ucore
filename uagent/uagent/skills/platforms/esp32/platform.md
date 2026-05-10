---
name: platform
description: Generic ESP32 platform notes — memory, threading, time, file system quirks the LLM might miss.
keywords: [esp32, espressif, ram, flash, partition, thread, gc, ticks, gettimeofday, watchdog]
---

The connected device is an Espressif ESP32-family microcontroller running MicroPython.

## Memory and runtime

- SRAM is small (low-hundreds of KB); long-running scripts should call `gc.collect()` periodically to keep heap fragmentation in check.
- File system is on flash, mounted from a `vfs_*` (LittleFS by default). Writes wear the flash — don't log into a file in a tight loop.
- `_thread` is available but minimal: no thread-local state, no join, no daemon flag. Threads are kernel-scheduled across cores. Avoid sharing complex objects without locks.
- The hardware watchdog will reset the chip if the main task starves. Long sync loops should yield (`time.sleep_ms(0)`) occasionally.

## Time

- `time.time()` returns seconds since 2000-01-01 UTC, **not** the Unix epoch. Convert if comparing with host timestamps.
- `time.ticks_ms()` wraps. Use `time.ticks_diff(end, start)` for safe differences — never raw subtraction.

## Reset reasons

- A device that "just rebooted" mid-cell is usually one of: power glitch, watchdog timeout, brownout, or a Python-level uncaught exception. `machine.reset_cause()` returns the reason.
- A panic during boot (REPL spits stack frames) means a crash earlier in the boot sequence — usually a syntax error in `boot.py` or `main.py`.
