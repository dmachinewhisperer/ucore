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
