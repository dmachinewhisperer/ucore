---
name: streaming
description: Stream values from the device to the host using named pipes — producer threads, subscribe / collect / live_plot.
keywords: [pipe, stream, subscribe, collect, live_plot, producer, thread, plot, matplotlib, animate, broadcast, sample, decoder]
---

µcore exposes a custom **named-pipe** API for moving a continuous stream of bytes from the device to the host. Use this whenever you'd otherwise be tempted to `print()` in a loop — sensor sampling, audio, periodic state, anything to plot.

This API is project-specific; the names below don't exist outside µcore.

## Device-side API (`%%ucore` cells)

- `ucore.open_pipe(name)` — create or attach to a named byte channel; returns an object with `.write(bytes)`.
- `ucore.is_pipe_open(name)` — boolean, polled by producer threads as a graceful-exit signal.
- `ucore.close_pipe(name)` — close the channel from the device side. Producer threads exit on their next `is_pipe_open()` check.

## Host-side API (`from ukernel.pipes import …`)

The two primitives:

- `subscribe(name)` — low-level iterator yielding bytes chunks. Use as `with subscribe("x") as p:` so the socket closes on scope exit. Build whatever you want on top — custom plots, dashboards, async pipelines, file recorders.
- `collect(name, *, n=None, seconds=30, decoder=None, allow_empty=False)` — block until a finite batch arrives, return a numpy array. Has a 30 s default `seconds=` ceiling to prevent runaway hangs. Raises `TimeoutError` on zero samples (override with `allow_empty=True`).

The convenience layer on top:

- `live_plot(name, *, window=200, ylim=(-1.5, 1.5), title=None, interval=40, duration=None, decoder=None)` — fire-and-forget animated line chart. Returns immediately, runs in the background. Useful for the common case but not the only way; for anything beyond a single line in a sliding window, drop down to `subscribe` and drive your own matplotlib / plotly / whatever.
- `stop_live(name=None)` — tear down a single live plot, or all of them.

## Semantics worth knowing

- One device-side `pipe.write(bytes)` = one host-side chunk on the iterator. Framing is automatic and invisible.
- **Default wire format is little-endian float32.** Override on either side with `decoder=` (host) or your own `struct.pack` (device).
- **Pipe names are case-sensitive strings; mismatch fails silently** — host hangs / times out, device writes to a void. Always cross-check the string between producer and consumer.
- `live_plot` stops on any of: producer closes the pipe, `stop_live()` is called, or `duration=` elapses. Re-running the same cell tears down the previous instance cleanly.
- `live_plot` default ylim is `(-1.5, 1.5)`. **Constant signals near zero look blank with the default ylim** — pass `ylim=None` to autoscale, or set explicit bounds matching your data range.
- Closing direction matters: closing the host socket alone does **not** stop the device producer. Call `ucore.close_pipe(name)` on the device to actually halt it.

## Producer-thread pattern (non-obvious bit)

A device-side producer **must** check `ucore.is_pipe_open(name)` each loop iteration. That's how the producer exits cleanly when the pipe is closed. Forgetting it leaks a thread on the device — it'll keep `pipe.write()`-ing into a closed channel forever (or until the next reset).

## Cell placement

Producer in a `%%ucore` cell. Consumer in a plain Python cell. The magic split applies — see `device_basics`.

## Plotting backend

`live_plot` requires `%matplotlib widget` somewhere in the notebook (the ipympl backend) for the animation to render. `collect` works with any backend.
