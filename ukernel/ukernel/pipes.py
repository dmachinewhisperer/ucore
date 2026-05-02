"""Host-side consumer for ucore device pipes.

A ucore pipe is a named byte channel from the ESP32 device to the host.
The device-side code (running on MicroPython) opens a pipe and writes
arbitrary bytes:

    import ucore
    p = ucore.open_pipe("adc")
    p.write(struct.pack("<f", value))

The host-side code (running in a regular notebook cell, NOT a %%ucore
cell) consumes those bytes:

    from ucore_pipes import open as open_pipe
    with open_pipe("adc") as pipe:
        for chunk in pipe:
            value, = struct.unpack("<f", chunk)
            ...

Each chunk corresponds to one device-side `pipe.write(...)` call.

Discovery: the µcore kernel writes its pipe-listener port into the
provisioner state file at startup. This module reads it from there.
Override with `UCORE_PIPE_PORT=12345` if you need to.
"""

from __future__ import annotations

import json
import os
import pathlib
import socket
import struct


_DEFAULT_STATE_PATH = (
    pathlib.Path(__file__).resolve().parent / ".ucore_kernel_state.json"
)


def _resolve_port(timeout: float = 5.0) -> int:
    """Find the µcore kernel's pipe-listener port. Tolerates a small
    startup race: if the consumer is opened immediately after the kernel
    spawns, the state file may not exist yet — retry briefly."""
    forced = os.environ.get("UCORE_PIPE_PORT")
    if forced:
        return int(forced)
    state_path = pathlib.Path(
        os.environ.get("UCORE_STATE_PATH", _DEFAULT_STATE_PATH)
    )
    import time as _time
    deadline = _time.monotonic() + timeout
    last_err: Exception | None = None
    while _time.monotonic() < deadline:
        try:
            with state_path.open() as f:
                state = json.load(f)
            port = state.get("pipe_port")
            if port:
                return int(port)
            last_err = ConnectionError("state file has no pipe_port yet")
        except (FileNotFoundError, json.JSONDecodeError) as e:
            last_err = e
        _time.sleep(0.1)
    raise ConnectionError(
        f"could not read ucore pipe_port from {state_path}: {last_err}. "
        "Is the µcore kernel running?"
    )


class Pipe:
    """A subscription to a named pipe. Iterating yields bytes chunks; one
    chunk per device-side `pipe.write(...)` call. Iteration blocks until
    the next chunk arrives, ends when the kernel closes the connection."""

    def __init__(self, name: str, host: str = "127.0.0.1", port: int | None = None):
        self.name = name
        if port is None:
            port = _resolve_port()
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.connect((host, port))
        # send subscription line
        self._sock.sendall(name.encode("utf-8") + b"\n")

    def __iter__(self):
        return self

    def __next__(self) -> bytes:
        header = self._recvn(4)
        if header is None:
            raise StopIteration
        (length,) = struct.unpack("<I", header)
        body = self._recvn(length)
        if body is None:
            raise StopIteration
        return body

    def _recvn(self, n: int) -> bytes | None:
        buf = bytearray()
        while len(buf) < n:
            try:
                chunk = self._sock.recv(n - len(buf))
            except (OSError, ConnectionError):
                return None
            if not chunk:
                return None
            buf.extend(chunk)
        return bytes(buf)

    def close(self) -> None:
        try:
            self._sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self._sock.close()
        except OSError:
            pass

    def __enter__(self) -> "Pipe":
        return self

    def __exit__(self, *exc) -> None:
        self.close()


def open(name: str, host: str = "127.0.0.1", port: int | None = None) -> Pipe:
    """Subscribe to a named pipe. Returns a Pipe you iterate for chunks."""
    return Pipe(name, host=host, port=port)


# ── high-level: one-call live plot ─────────────────────────────────
#
# Calling ``live_plot("sine")`` subscribes, animates, and displays the
# canvas. Calling it again with the same name (e.g. from re-running the
# cell, or from another cell) tears down the previous reader/figure and
# replaces it cleanly — no manual cleanup pattern to remember.

_live: dict[str, "_LivePlot"] = {}


class _LivePlot:
    """Internal lifecycle holder for a single live_plot instance."""

    def __init__(self, name, decoder, window, ylim, title, interval):
        # Lazy imports — keeping the rest of this module dependency-free
        # for consumers that don't want matplotlib.
        import collections
        import threading
        import matplotlib.pyplot as plt
        from matplotlib.animation import FuncAnimation
        from IPython.display import display

        self.name = name
        self.stop = threading.Event()
        self.buf = collections.deque([0.0] * window, maxlen=window)
        self.pipe = Pipe(name)

        self.fig, self.ax = plt.subplots()
        self.line, = self.ax.plot(range(window), list(self.buf))
        if ylim is not None:
            self.ax.set_ylim(*ylim)
        if title:
            self.ax.set_title(title)
        display(self.fig.canvas)

        def reader():
            try:
                for chunk in self.pipe:
                    if self.stop.is_set():
                        break
                    try:
                        self.buf.append(decoder(chunk))
                    except Exception:
                        # one bad chunk shouldn't kill the stream
                        continue
            except Exception:
                pass

        threading.Thread(target=reader, daemon=True).start()

        def update(_frame):
            self.line.set_ydata(list(self.buf))
            # Force a full PNG instead of an ipympl diff to avoid the
            # accumulating-trail artifact that diff-rendering causes.
            try:
                self.fig.canvas._force_full = True
            except AttributeError:
                pass
            return (self.line,)

        # blit=False keeps the widget backend clean across renders;
        # cache_frame_data=False silences a benign FuncAnimation warning
        # when frames is unbounded.
        self.ani = FuncAnimation(
            self.fig, update, interval=interval,
            blit=False, cache_frame_data=False,
        )

    def stop_now(self):
        self.stop.set()
        try:
            self.pipe.close()
        except Exception:
            pass
        try:
            import matplotlib.pyplot as plt
            plt.close(self.fig)
        except Exception:
            pass


def live_plot(
    name: str,
    *,
    decoder=None,
    window: int = 200,
    ylim: tuple[float, float] | None = (-1.5, 1.5),
    title: str | None = None,
    interval: int = 40,
) -> None:
    """Subscribe to a named pipe and animate the values in a sliding window.

    Calling this twice with the same ``name`` cleanly tears down the
    previous instance — re-run the cell or call from a new cell, no
    manual cleanup needed.

    Args:
        name: Pipe name (must match the device-side ``ucore.open_pipe(name)``).
        decoder: Bytes → float function. Defaults to ``struct.unpack("<f", b)[0]``.
        window: Number of samples retained in the sliding window.
        ylim: ``(low, high)`` y-axis limits. Pass ``None`` to autoscale.
        title: Optional axes title.
        interval: Frame interval in milliseconds.
    """
    if decoder is None:
        def decoder(b, _u=struct.unpack):
            return _u("<f", b)[0]
    prev = _live.pop(name, None)
    if prev is not None:
        prev.stop_now()
    _live[name] = _LivePlot(name, decoder, window, ylim, title, interval)


def stop_live(name: str | None = None) -> None:
    """Stop a single live_plot by name, or all of them when ``name`` is None."""
    if name is None:
        names = list(_live.keys())
    else:
        names = [name]
    for n in names:
        plot = _live.pop(n, None)
        if plot is not None:
            plot.stop_now()
