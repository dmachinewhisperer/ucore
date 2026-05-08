---
name: streaming
description: Stream values from the device to the host using named pipes — producer threads, subscribe / collect / live_plot, and clean shutdown.
keywords: [pipe, stream, subscribe, collect, live_plot, producer, thread, plot, matplotlib, animate, broadcast, sample]
---

Named **pipes** are how the device pushes a continuous stream of bytes to the host. They're the right tool any time you want more than one printed value — sensor sampling, audio, periodic state, anything you'd plot.

The pattern has three parts:

1. A device-side producer (usually a thread) writes packed bytes into a named pipe.
2. The host subscribes to that name and consumes the stream — either by collecting a finite batch (`collect`) or animating live (`live_plot`).
3. The producer exits cleanly when the pipe is closed from either side.

## Device-side producer

```python
%%ucore
import ucore, _thread, time, struct, math

pipe = ucore.open_pipe("sine")

def _run():
    t0 = time.ticks_ms()
    while ucore.is_pipe_open("sine"):
        t = time.ticks_diff(time.ticks_ms(), t0) / 1000.0
        pipe.write(struct.pack("<f", math.sin(2 * math.pi * 1.0 * t)))
        time.sleep_ms(100)            # 10 Hz

_thread.start_new_thread(_run, ())
print("sine producer started")
```

The `is_pipe_open()` guard is important — it lets you stop the thread later from another cell (`%%ucore` → `ucore.close_pipe("sine")`) without `_thread.exit()` gymnastics. The thread sees the pipe close on its next loop iteration and returns.

## Host-side consumers

Three APIs, all in `ukernel.pipes`. Pick by use case:

### `collect` — finite batch, then return

Blocks until `n` samples *or* `seconds` of wall time pass (whichever first). Returns a numpy array. Default safety ceiling: 30 s, so a missing producer can't hang the kernel.

```python
from ukernel.pipes import collect
samples = collect("sine", n=200)            # at most 200 samples or 30 s
samples = collect("sine", seconds=5)        # 5 s of data, however many samples
```

Raises `TimeoutError` if zero samples arrived (almost always means no producer is running). Pass `allow_empty=True` to suppress.

### `live_plot` — animated chart in a sliding window

```python
%matplotlib widget
from ukernel.pipes import live_plot
live_plot("sine", window=200, ylim=(-1.2, 1.2), title="sine (device)")
live_plot("temp", window=120, ylim=None, title="MCU temp")     # autoscale
live_plot("sine", window=200, duration=10)                     # stops after 10 s
```

Returns immediately. The chart updates in the background until: the producer closes the pipe, `stop_live("sine")` is called, or `duration` elapses. Re-running the cell tears down the previous instance cleanly.

### `subscribe` — low-level iterator

For custom processing (your own animation, writing to disk, etc.):

```python
import struct
from ukernel.pipes import subscribe

with subscribe("sine") as p:
    for i, chunk in enumerate(p):
        if i >= 30: break
        (v,) = struct.unpack("<f", chunk)
        print(f"[{i:3d}] {v:+.4f}")
```

One iteration = one device-side `pipe.write(...)` call. The `with` block closes the socket on exit.

## Shutting down

To stop a producer and clean up:

```python
%%ucore
import ucore
ucore.close_pipe("sine")     # producer thread exits on next loop
```

```python
from ukernel.pipes import stop_live
stop_live()                  # tear down all host-side animations
```

`stop_live()` with no args stops every live plot; pass a name to stop just one.

## Encoding

The default decoder on the host expects little-endian `float32` (`struct.pack("<f", value)` on the device). For other types pass `decoder=` to `collect` / `live_plot` / iterate raw bytes via `subscribe`.
