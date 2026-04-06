# binary jmp protocol — encode/decode for the jupyter messaging protocol
# over bandwidth-constrained serial links.
#
# wire format: 5x uint16 length prefix, then header, parent_header,
# metadata, content, buffers — each as length-prefixed binary blobs.
# all multi-byte integers are big-endian.

import struct
from datetime import datetime, timezone
from enum import IntEnum


class MsgType(IntEnum):
    KERNEL_INFO_REQUEST = 0x01
    KERNEL_INFO_REPLY   = 0x02
    EXECUTE_REQUEST     = 0x03
    EXECUTE_REPLY       = 0x04
    STREAM              = 0x05
    ERROR               = 0x06
    DISPLAY_DATA        = 0x07
    STATUS              = 0x08
    INPUT_REQUEST       = 0x09
    INPUT_REPLY         = 0x0A
    COMPLETE_REQUEST    = 0x0B
    COMPLETE_REPLY      = 0x0C
    INSPECT_REQUEST     = 0x0D
    INSPECT_REPLY       = 0x0E
    IS_COMPLETE_REQUEST = 0x0F
    IS_COMPLETE_REPLY   = 0x10
    SHUTDOWN_REQUEST    = 0x11
    SHUTDOWN_REPLY      = 0x12
    INTERRUPT_REQUEST   = 0x13
    INTERRUPT_REPLY     = 0x14
    EXECUTE_RESULT      = 0x15
    COMM_OPEN           = 0x16
    COMM_MSG            = 0x17
    COMM_CLOSE          = 0x18
    AUTH_REQUEST        = 0x64
    AUTH_REPLY          = 0x65
    TARGET_NOT_FOUND    = 0x66


MSG_TYPE_BY_NAME = {m.name.lower(): m for m in MsgType}
MSG_TYPE_BY_VALUE = {m.value: m.name.lower() for m in MsgType}

STATUS_NAMES = ["ok", "error", "abort"]
EXECUTION_STATE_NAMES = ["busy", "idle", "starting", "dead"]
IS_COMPLETE_NAMES = ["complete", "incomplete", "invalid", "unknown"]


# ── binary reader / writer ──────────────────────────────────────────

class BinaryReader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def uint8(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def uint16(self):
        v, = struct.unpack_from(">H", self.data, self.pos)
        self.pos += 2
        return v

    def uint32(self):
        v, = struct.unpack_from(">I", self.data, self.pos)
        self.pos += 4
        return v

    def raw(self, n):
        v = self.data[self.pos:self.pos + n]
        self.pos += n
        return v

    def string(self):
        n = self.uint16()
        v = self.data[self.pos:self.pos + n].decode("utf-8")
        self.pos += n
        return v

    def version(self):
        return f"{self.uint8()}.{self.uint8()}.{self.uint8()}"

    def bool(self):
        return self.uint8() != 0

    def remaining(self):
        return len(self.data) - self.pos


class BinaryWriter:
    def __init__(self):
        self.parts: list[bytes] = []

    def uint8(self, v):
        self.parts.append(struct.pack("B", v))

    def uint16(self, v):
        self.parts.append(struct.pack(">H", v))

    def uint32(self, v):
        self.parts.append(struct.pack(">I", v))

    def raw(self, data: bytes):
        self.parts.append(data)

    def string(self, s: str):
        encoded = s.encode("utf-8")
        self.uint16(len(encoded))
        self.parts.append(encoded)

    def version(self, v: str):
        parts = v.split(".")
        for i in range(3):
            self.uint8(int(parts[i]) if i < len(parts) else 0)

    def bool(self, v):
        self.uint8(1 if v else 0)

    def to_bytes(self) -> bytes:
        return b"".join(self.parts)


# ── header ──────────────────────────────────────────────────────────

def encode_header(header: dict) -> bytes:
    w = BinaryWriter()
    w.string(header.get("msg_id", ""))
    w.string(header.get("session", ""))
    w.string(header.get("username", ""))
    name = header.get("msg_type", "")
    w.uint8(MSG_TYPE_BY_NAME.get(name, 0))
    w.version(header.get("version", "5.3.0"))
    return w.to_bytes()


def decode_header(data: bytes) -> dict:
    r = BinaryReader(data)
    return {
        "msg_id": r.string(),
        "session": r.string(),
        "username": r.string(),
        "date": datetime.now(timezone.utc).isoformat(),
        "msg_type": MSG_TYPE_BY_VALUE.get(r.uint8(), "unknown"),
        "version": r.version(),
    }


# ── content converters ──────────────────────────────────────────────
# each pair: encode_<type>(content) -> bytes, decode_<type>(data) -> dict

def _encode_empty(_c):
    return b""

def _decode_empty(_d):
    return {}


def _encode_error(c):
    w = BinaryWriter()
    w.uint8(1)
    w.string(c.get("ename", ""))
    w.string(c.get("evalue", ""))
    tb = c.get("traceback", [])
    w.string("\n".join(tb) if isinstance(tb, list) else str(tb))
    w.uint16(c.get("execution_count", 0))
    return w.to_bytes()

def _decode_error(data):
    r = BinaryReader(data)
    status = r.uint8()
    if status == 2:
        return {"status": "abort"}
    ename = r.string()
    evalue = r.string()
    raw_tb = r.string()
    execution_count = r.uint16()
    lines = [l for l in raw_tb.split("\n") if l.strip()]
    return {
        "status": "error",
        "ename": ename,
        "evalue": evalue,
        "traceback": lines,
        "execution_count": execution_count,
    }


def _decode_string_map(r):
    count = r.uint16()
    return {r.string(): r.string() for _ in range(count)}


# ── individual message types ────────────────────────────────────────

def encode_kernel_info_reply(c):
    if c.get("status") != "ok":
        return _encode_error(c)
    w = BinaryWriter()
    w.uint8(0)
    w.version(c.get("protocol_version", "5.3.0"))
    w.string(c.get("implementation", ""))
    w.version(c.get("implementation_version", "1.0.0"))
    li = c.get("language_info", {})
    w.string(li.get("name", ""))
    w.version(li.get("version", "0.0.0"))
    w.string(li.get("mimetype", ""))
    w.string(li.get("file_extension", ""))
    w.string(c.get("banner", ""))
    w.bool(c.get("debugger", False))
    return w.to_bytes()

def decode_kernel_info_reply(data):
    r = BinaryReader(data)
    status = r.uint8()
    if status != 0:
        return _decode_error(data)
    return {
        "status": "ok",
        "protocol_version": r.version(),
        "implementation": r.string(),
        "implementation_version": r.version(),
        "language_info": {
            "name": r.string(),
            "version": r.version(),
            "mimetype": r.string(),
            "file_extension": r.string(),
        },
        "banner": r.string(),
        "debugger": r.bool(),
    }


def encode_execute_request(c):
    w = BinaryWriter()
    w.string(c.get("code", ""))
    flags = (
        (1 if c.get("silent") else 0) |
        (2 if c.get("store_history") else 0) |
        (4 if c.get("allow_stdin") else 0) |
        (8 if c.get("stop_on_error") else 0)
    )
    w.uint8(flags)
    return w.to_bytes()

def decode_execute_request(data):
    r = BinaryReader(data)
    code = r.string()
    flags = r.uint8()
    return {
        "code": code,
        "silent": bool(flags & 1),
        "store_history": bool(flags & 2),
        "allow_stdin": bool(flags & 4),
        "stop_on_error": bool(flags & 8),
        "user_expressions": {},
    }


def encode_execute_reply(c):
    if c.get("status") != "ok":
        return _encode_error(c)
    w = BinaryWriter()
    w.uint8(0)
    w.uint16(c.get("execution_count", 0))
    return w.to_bytes()

def decode_execute_reply(data):
    r = BinaryReader(data)
    status = r.uint8()
    if status != 0:
        return _decode_error(data)
    return {"status": "ok", "execution_count": r.uint16()}


def encode_stream(c):
    w = BinaryWriter()
    w.uint8(1 if c.get("name") == "stderr" else 0)
    w.string(c.get("text", ""))
    return w.to_bytes()

def decode_stream(data):
    r = BinaryReader(data)
    return {"name": "stderr" if r.uint8() == 1 else "stdout", "text": r.string()}


def encode_display_data(c):
    w = BinaryWriter()
    entries = list((c.get("data") or {}).items())
    w.uint16(len(entries))
    for k, v in entries:
        w.string(k)
        w.string(str(v))
    w.uint16(0)
    return w.to_bytes()

def decode_display_data(data):
    r = BinaryReader(data)
    d = _decode_string_map(r)
    r.uint16()
    return {"data": d, "metadata": {}}


def encode_status(c):
    w = BinaryWriter()
    table = {"busy": 0, "idle": 1, "starting": 2}
    w.uint8(table.get(c.get("execution_state", "busy"), 0))
    return w.to_bytes()

def decode_status(data):
    r = BinaryReader(data)
    return {"execution_state": EXECUTION_STATE_NAMES[r.uint8()]}


def encode_input_request(c):
    w = BinaryWriter()
    w.string(c.get("prompt", ""))
    w.bool(c.get("password", False))
    return w.to_bytes()

def decode_input_request(data):
    r = BinaryReader(data)
    return {"prompt": r.string(), "password": r.bool()}


def encode_input_reply(c):
    w = BinaryWriter()
    w.string(c.get("value", ""))
    return w.to_bytes()

def decode_input_reply(data):
    r = BinaryReader(data)
    return {"value": r.string()}


def encode_complete_request(c):
    w = BinaryWriter()
    w.string(c.get("code", ""))
    w.uint16(c.get("cursor_pos", 0))
    return w.to_bytes()

def decode_complete_request(data):
    r = BinaryReader(data)
    return {"code": r.string(), "cursor_pos": r.uint16()}


def encode_complete_reply(c):
    w = BinaryWriter()
    matches = c.get("matches", [])
    w.uint16(len(matches))
    for m in matches:
        w.string(m)
    w.uint16(c.get("cursor_start", 0))
    w.uint16(c.get("cursor_end", 0))
    w.uint16(0)
    w.uint8(0 if c.get("status") == "ok" else 1)
    return w.to_bytes()

def decode_complete_reply(data):
    r = BinaryReader(data)
    count = r.uint16()
    matches = [r.string() for _ in range(count)]
    cursor_start = r.uint16()
    cursor_end = r.uint16()
    r.uint16()
    status = r.uint8()
    return {
        "matches": matches,
        "cursor_start": cursor_start,
        "cursor_end": cursor_end,
        "metadata": {},
        "status": STATUS_NAMES[status] if status < len(STATUS_NAMES) else "ok",
    }


def encode_inspect_request(c):
    w = BinaryWriter()
    w.string(c.get("code", ""))
    w.uint16(c.get("cursor_pos", 0))
    w.uint8(c.get("detail_level", 0))
    return w.to_bytes()

def decode_inspect_request(data):
    r = BinaryReader(data)
    return {"code": r.string(), "cursor_pos": r.uint16(), "detail_level": r.uint8()}


def encode_inspect_reply(c):
    if c.get("status") != "ok":
        return _encode_error(c)
    w = BinaryWriter()
    w.uint8(0)
    w.bool(c.get("found", False))
    entries = list((c.get("data") or {}).items())
    w.uint16(len(entries))
    for k, v in entries:
        w.string(k)
        w.string(str(v))
    w.uint16(0)
    return w.to_bytes()

def decode_inspect_reply(data):
    r = BinaryReader(data)
    status = r.uint8()
    if status != 0:
        return _decode_error(data)
    found = r.bool()
    d = _decode_string_map(r)
    r.uint16()
    return {"status": "ok", "found": found, "data": d, "metadata": {}}


def encode_is_complete_request(c):
    w = BinaryWriter()
    w.string(c.get("code", ""))
    return w.to_bytes()

def decode_is_complete_request(data):
    r = BinaryReader(data)
    return {"code": r.string()}


def encode_is_complete_reply(c):
    w = BinaryWriter()
    table = {"complete": 0, "incomplete": 1, "invalid": 2, "unknown": 3}
    w.uint8(table.get(c.get("status", "unknown"), 3))
    w.string(c.get("indent", ""))
    return w.to_bytes()

def decode_is_complete_reply(data):
    r = BinaryReader(data)
    status = IS_COMPLETE_NAMES[r.uint8()]
    return {"status": status, "indent": r.string()}


def encode_shutdown_request(c):
    w = BinaryWriter()
    w.bool(c.get("restart", False))
    return w.to_bytes()

def decode_shutdown_request(data):
    r = BinaryReader(data)
    return {"restart": r.bool()}


def encode_shutdown_reply(c):
    if c.get("status") != "ok":
        return _encode_error(c)
    w = BinaryWriter()
    w.uint8(0)
    w.bool(c.get("restart", False))
    return w.to_bytes()

def decode_shutdown_reply(data):
    r = BinaryReader(data)
    status = r.uint8()
    if status != 0:
        return _decode_error(data)
    return {"status": "ok", "restart": r.bool()}


def encode_interrupt_reply(c):
    w = BinaryWriter()
    w.uint8(0 if c.get("status") == "ok" else 1)
    return w.to_bytes()

def decode_interrupt_reply(data):
    r = BinaryReader(data)
    s = r.uint8()
    return {"status": STATUS_NAMES[s] if s < len(STATUS_NAMES) else "ok"}


def encode_execute_result(c):
    w = BinaryWriter()
    w.uint16(c.get("execution_count", 0))
    entries = list((c.get("data") or {}).items())
    w.uint16(len(entries))
    for k, v in entries:
        w.string(k)
        w.string(str(v))
    w.uint16(0)
    return w.to_bytes()

def decode_execute_result(data):
    r = BinaryReader(data)
    execution_count = r.uint16()
    d = _decode_string_map(r)
    r.uint16()
    return {"execution_count": execution_count, "data": d, "metadata": {}}


def encode_comm_open(c):
    w = BinaryWriter()
    w.string(c.get("comm_id", ""))
    w.uint16(c.get("target_id", 0))
    return w.to_bytes()

def decode_comm_open(data):
    r = BinaryReader(data)
    return {"comm_id": r.string(), "target_id": r.uint16()}


def encode_comm_msg(c):
    w = BinaryWriter()
    w.string(c.get("comm_id", ""))
    w.uint32(c.get("data", 0))
    return w.to_bytes()

def decode_comm_msg(data):
    r = BinaryReader(data)
    return {"comm_id": r.string(), "data": r.uint32()}


def encode_comm_close(c):
    w = BinaryWriter()
    w.string(c.get("comm_id", ""))
    return w.to_bytes()

def decode_comm_close(data):
    r = BinaryReader(data)
    return {"comm_id": r.string()}


def encode_auth_request(c):
    w = BinaryWriter()
    w.string(c.get("device_id", ""))
    w.uint32(c.get("timestamp", 0))
    hmac = c.get("hmac", [0] * 32)
    w.raw(bytes(hmac))
    return w.to_bytes()

def decode_auth_request(data):
    r = BinaryReader(data)
    return {"device_id": r.string(), "timestamp": r.uint32(), "hmac": list(r.raw(32))}


def encode_auth_reply(c):
    w = BinaryWriter()
    w.uint8(c.get("status", 0))
    return w.to_bytes()

def decode_auth_reply(data):
    r = BinaryReader(data)
    return {"status": r.uint8()}


# ── converter registry ──────────────────────────────────────────────

CONVERTERS = {
    "kernel_info_request": (_encode_empty,            _decode_empty),
    "kernel_info_reply":   (encode_kernel_info_reply,  decode_kernel_info_reply),
    "execute_request":     (encode_execute_request,    decode_execute_request),
    "execute_reply":       (encode_execute_reply,      decode_execute_reply),
    "stream":              (encode_stream,             decode_stream),
    "error":               (_encode_error,             _decode_error),
    "display_data":        (encode_display_data,       decode_display_data),
    "status":              (encode_status,             decode_status),
    "input_request":       (encode_input_request,      decode_input_request),
    "input_reply":         (encode_input_reply,        decode_input_reply),
    "complete_request":    (encode_complete_request,   decode_complete_request),
    "complete_reply":      (encode_complete_reply,     decode_complete_reply),
    "inspect_request":     (encode_inspect_request,    decode_inspect_request),
    "inspect_reply":       (encode_inspect_reply,      decode_inspect_reply),
    "is_complete_request": (encode_is_complete_request, decode_is_complete_request),
    "is_complete_reply":   (encode_is_complete_reply,  decode_is_complete_reply),
    "shutdown_request":    (encode_shutdown_request,   decode_shutdown_request),
    "shutdown_reply":      (encode_shutdown_reply,     decode_shutdown_reply),
    "interrupt_request":   (_encode_empty,             _decode_empty),
    "interrupt_reply":     (encode_interrupt_reply,    decode_interrupt_reply),
    "execute_result":      (encode_execute_result,     decode_execute_result),
    "comm_open":           (encode_comm_open,          decode_comm_open),
    "comm_msg":            (encode_comm_msg,           decode_comm_msg),
    "comm_close":          (encode_comm_close,         decode_comm_close),
    "auth_request":        (encode_auth_request,       decode_auth_request),
    "auth_reply":          (encode_auth_reply,         decode_auth_reply),
    "target_not_found":    (_encode_empty,             _decode_empty),
}


# ── top-level message encode / decode ───────────────────────────────

def encode_message(msg: dict) -> bytes:
    # encode header, parent_header, content into the jmp binary frame
    header_bin = encode_header(msg["header"])

    ph = msg.get("parent_header", {})
    parent_bin = encode_header(ph) if ph.get("msg_type") else b""

    msg_type = msg["header"]["msg_type"]
    encoder, _ = CONVERTERS[msg_type]
    content_bin = encoder(msg.get("content", {}))

    # 5x uint16 length prefix: header, parent, metadata, content, buffers
    prefix = struct.pack(">HHHHH",
        len(header_bin), len(parent_bin), 0, len(content_bin), 0)

    return prefix + header_bin + parent_bin + content_bin


def decode_message(data: bytes) -> dict:
    r = BinaryReader(data)
    header_len = r.uint16()
    parent_len = r.uint16()
    metadata_len = r.uint16()
    content_len = r.uint16()
    _buffers_len = r.uint16()

    header = decode_header(r.raw(header_len))

    parent_header = {}
    if parent_len > 0:
        parent_header = decode_header(r.raw(parent_len))

    if metadata_len > 0:
        r.raw(metadata_len)

    content = {}
    if content_len > 0:
        _, decoder = CONVERTERS[header["msg_type"]]
        content = decoder(r.raw(content_len))

    return {
        "header": header,
        "parent_header": parent_header,
        "metadata": {},
        "content": content,
    }
