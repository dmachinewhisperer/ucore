"""Device discovery, probing, and reset for the ucore device manager.

Cross-platform (Windows COM*, macOS /dev/cu.*, Linux/WSL /dev/ttyACM*).

Two-stage discovery: cheap VID:PID enumeration via pyserial (no port
open, harmless), then opt-in `probe_jmp` which actually opens each
candidate to send a kernel_info_request. Probing is opt-in because
opening a serial port asserts DTR by default on most platforms, which
on standard ESP32 dev boards is wired to EN and resets the chip.
`probe_jmp` opens with `dsrdtr=False` and explicitly clears DTR/RTS
before any I/O so probing is non-disruptive.
"""

from __future__ import annotations

import logging
import time
from dataclasses import asdict, dataclass
from typing import Any

import serial
from serial.tools import list_ports

from .protocol import decode_message, encode_message
from .transport import SESSION_ID_LEN

from cobs import cobs

log = logging.getLogger(__name__)

# (vid, pid) → human label. Common ESP32 USB-serial bridges and the
# native USB-JTAG endpoint on S2/S3/C3. Keep this short and only add
# entries we have actually verified — false positives waste user clicks.
_KNOWN_VID_PID: dict[tuple[int, int], str] = {
    (0x303A, 0x1001): "esp32-s3",      # native USB-Serial-JTAG
    (0x303A, 0x1002): "esp32-s2",
    (0x1A86, 0x55D3): "ch343",         # ESP32-S3 dev boards
    (0x1A86, 0x7523): "ch340",         # classic ESP32 dev boards
    (0x10C4, 0xEA60): "cp210x",        # NodeMCU / older ESP32
    (0x0403, 0x6001): "ftdi-ft232",
    (0x0403, 0x6010): "ftdi-ft2232",
    (0x0403, 0x6014): "ftdi-ft232h",
}

# Probing budget: opening the port often pulses DTR briefly even with
# `dsrdtr=False`, which on standard ESP32 dev boards triggers a hardware
# reset. The chip's first-stage ROM and our app then take ~1.5–2s to
# come up. Total probe latency = BOOT_SETTLE + REPLY_TIMEOUT.
BOOT_SETTLE = 2.0
REPLY_TIMEOUT = 1.5
PROBE_TIMEOUT = BOOT_SETTLE + REPLY_TIMEOUT
PROBE_BAUD = 115200


@dataclass
class Device:
    id: str                 # stable per-port identifier (basename / COM name)
    path: str               # full device path/name passed to pyserial
    label: str              # human-readable description from the OS
    vid: str | None         # 4-char hex, lowercase
    pid: str | None
    kind: str               # "esp32-s3", "ch343", "unknown", etc.
    speaks_jmp: bool | None = None     # None = not yet probed
    info: dict[str, Any] | None = None  # kernel_info_reply if probed OK

    def to_dict(self) -> dict:
        return asdict(self)


def enumerate_devices() -> list[Device]:
    """Cheap, side-effect-free enumeration. Does NOT open any port."""
    devices: list[Device] = []
    for p in list_ports.comports():
        vid = f"{p.vid:04x}" if p.vid is not None else None
        pid = f"{p.pid:04x}" if p.pid is not None else None
        kind = "unknown"
        if p.vid is not None and p.pid is not None:
            kind = _KNOWN_VID_PID.get((p.vid, p.pid), "unknown")
        devices.append(Device(
            id=_device_id(p.device),
            path=p.device,
            label=p.description or p.device,
            vid=vid,
            pid=pid,
            kind=kind,
        ))
    devices.sort(key=lambda d: d.id)
    return devices


def probe_jmp(device: Device,
              boot_settle: float = BOOT_SETTLE,
              reply_timeout: float = REPLY_TIMEOUT) -> Device:
    """Send a kernel_info_request and wait for a reply.

    Mutates and returns the same Device with `speaks_jmp` and `info`
    populated. On any failure, `speaks_jmp` is set to False.

    Sequence:
        1. Open the port (this may pulse DTR and reset the chip).
        2. Sleep `boot_settle` to let the bootloader and ucore start.
        3. Drain anything that arrived during boot (ROM banner, etc.).
        4. Send the request and wait up to `reply_timeout` for a reply.
    """
    device.speaks_jmp = False
    device.info = None
    try:
        ser = _open_no_reset(device.path, timeout=0.2)
    except (serial.SerialException, OSError) as e:
        log.debug("probe %s: open failed: %s", device.path, e)
        return device
    try:
        time.sleep(boot_settle)
        # Discard whatever the ROM/bootloader printed while we waited;
        # otherwise the COBS scanner will spend the reply window trying
        # to make sense of it.
        ser.reset_input_buffer()

        ser.write(_kernel_info_request_frame())
        ser.flush()

        deadline = time.monotonic() + reply_timeout
        buf = b""
        while time.monotonic() < deadline:
            chunk = ser.read(4096)
            if not chunk:
                continue
            buf += chunk
            while b"\x00" in buf:
                idx = buf.index(b"\x00")
                packet, buf = buf[:idx], buf[idx + 1:]
                if not packet:
                    continue
                reply = _try_decode(packet)
                if reply and reply.get("header", {}).get("msg_type") == "kernel_info_reply":
                    device.speaks_jmp = True
                    device.info = reply.get("content", {})
                    return device
        log.debug("probe %s: no reply within %.1fs", device.path, reply_timeout)
    finally:
        try:
            ser.close()
        except OSError:
            pass
    return device


def _kernel_info_request_frame() -> bytes:
    msg = {
        "header": {
            "msg_id": "probe",
            "msg_type": "kernel_info_request",
            "session": "probe",
            "username": "probe",
            "version": "5.3.0",
        },
        "parent_header": {},
        "metadata": {},
        "content": {},
    }
    return cobs.encode(b"0" * SESSION_ID_LEN + encode_message(msg)) + b"\x00"


def reset_device(path: str, hold: float = 0.1) -> None:
    """Pulse DTR/RTS to perform a hardware reset.

    Same sequence esptool uses with `--before default_reset`: hold the
    chip in reset for `hold` seconds, then release. Caller is responsible
    for ensuring no other process holds the port open (kernel must
    detach first).
    """
    ser = _open_no_reset(path, timeout=0.1)
    try:
        # Drive into reset: DTR low, RTS high (EN held low via the
        # auto-reset transistor pair on standard dev boards).
        ser.dtr = False
        ser.rts = True
        time.sleep(hold)
        # Release.
        ser.dtr = True
        ser.rts = False
        time.sleep(0.05)
    finally:
        try:
            ser.close()
        except OSError:
            pass


# ── helpers ─────────────────────────────────────────────────────────────

def _device_id(path: str) -> str:
    """Stable id from a port path. Drop the directory part on POSIX so
    /dev/ttyACM0 → ttyACM0; Windows COM names are already unqualified."""
    return path.rsplit("/", 1)[-1]


def _open_no_reset(path: str, timeout: float) -> serial.Serial:
    """Open the port without bouncing the chip. pyserial asserts DTR by
    default on open; we explicitly disable hardware flow control and
    clear DTR/RTS before returning."""
    ser = serial.Serial()
    ser.port = path
    ser.baudrate = PROBE_BAUD
    ser.timeout = timeout
    ser.dsrdtr = False
    ser.rtscts = False
    # Cleared *before* open() on platforms that honor it (Linux); also
    # asserted-low immediately after, which is the ESP32-friendly state.
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.dtr = False
    ser.rts = False
    return ser


def _try_decode(packet: bytes) -> dict | None:
    try:
        decoded = cobs.decode(packet)
    except cobs.DecodeError:
        return None
    if len(decoded) < SESSION_ID_LEN:
        return None
    try:
        return decode_message(decoded[SESSION_ID_LEN:])
    except Exception:
        return None
