# transport layer — connects to the firmware over tcp, serial, or websocket.
# all transports use cobs framing with a 36-byte session-id prefix.

import asyncio
import logging
import uuid
from abc import ABC, abstractmethod

from cobs import cobs

from .protocol import encode_message, decode_message

log = logging.getLogger(__name__)

SESSION_ID_LEN = 36


class Transport(ABC):
    def __init__(self):
        self.session_id = str(uuid.uuid4()).encode("utf-8")[:SESSION_ID_LEN]
        self._on_message = None
        self._on_disconnect = None
        self.connected = False

    def on_message(self, callback):
        self._on_message = callback

    def on_disconnect(self, callback):
        """Called once when the read loop ends (EOF, error, or explicit close).
        Lets the kernel fail any in-flight requests instead of hanging."""
        self._on_disconnect = callback

    def _dispatch_disconnect(self, reason: str):
        if self._on_disconnect:
            try:
                self._on_disconnect(reason)
            except Exception as e:
                log.error("on_disconnect callback raised: %s", e)
            # one-shot — clear so a subsequent reconnect re-registers cleanly
            self._on_disconnect = None

    @abstractmethod
    async def connect(self): ...

    @abstractmethod
    async def disconnect(self): ...

    @abstractmethod
    async def send(self, msg: dict) -> bool: ...

    def _frame_encode(self, msg: dict) -> bytes:
        binary = encode_message(msg)
        payload = self.session_id + binary
        return cobs.encode(payload) + b"\x00"

    def _frame_decode(self, frame: bytes) -> dict:
        decoded = cobs.decode(frame)
        if len(decoded) < SESSION_ID_LEN:
            raise ValueError(f"frame too short ({len(decoded)} bytes)")
        payload = decoded[SESSION_ID_LEN:]
        msg = decode_message(payload)
        msg["_session_id"] = decoded[:SESSION_ID_LEN].decode("utf-8", errors="replace")
        return msg

    def _dispatch(self, msg):
        if self._on_message:
            self._on_message(msg)


class TCPTransport(Transport):
    def __init__(self, host="localhost", port=5555):
        super().__init__()
        self.host = host
        self.port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._read_task: asyncio.Task | None = None

    async def connect(self):
        self._reader, self._writer = await asyncio.open_connection(self.host, self.port)
        self.connected = True
        self._read_task = asyncio.create_task(self._read_loop())
        log.info("tcp connected to %s:%d", self.host, self.port)

    async def disconnect(self):
        self.connected = False
        if self._read_task:
            self._read_task.cancel()
            try:
                await self._read_task
            except asyncio.CancelledError:
                pass
        if self._writer:
            self._writer.close()
            try:
                await self._writer.wait_closed()
            except Exception:
                pass
        self._reader = self._writer = None
        log.info("tcp disconnected")

    async def send(self, msg: dict) -> bool:
        if not self._writer:
            return False
        try:
            self._writer.write(self._frame_encode(msg))
            await self._writer.drain()
            return True
        except Exception as e:
            log.error("tcp send failed: %s", e)
            return False

    async def _read_loop(self):
        buf = b""
        reason = "tcp eof"
        try:
            while self.connected:
                chunk = await self._reader.read(4096)
                if not chunk:
                    break
                buf += chunk
                while b"\x00" in buf:
                    idx = buf.index(b"\x00")
                    frame = buf[:idx]
                    buf = buf[idx + 1:]
                    if not frame:
                        continue
                    try:
                        msg = self._frame_decode(frame)
                        self._dispatch(msg)
                    except Exception as e:
                        log.error("frame decode error: %s", e)
        except asyncio.CancelledError:
            reason = "tcp closed"
        except Exception as e:
            reason = f"tcp read error: {e}"
            if self.connected:
                log.error("tcp read error: %s", e)
        finally:
            self.connected = False
            self._dispatch_disconnect(reason)


class SerialTransport(Transport):
    def __init__(self, port_path="/dev/ttyUSB0", baud_rate=115200):
        super().__init__()
        self.port_path = port_path
        self.baud_rate = baud_rate
        self._serial = None
        self._read_task: asyncio.Task | None = None

    async def connect(self):
        import serial
        self._serial = serial.Serial(self.port_path, self.baud_rate, timeout=0)
        self.connected = True
        self._read_task = asyncio.create_task(self._read_loop())
        log.info("serial connected to %s @ %d", self.port_path, self.baud_rate)

    async def disconnect(self):
        self.connected = False
        if self._read_task:
            self._read_task.cancel()
            try:
                await self._read_task
            except asyncio.CancelledError:
                pass
        if self._serial:
            self._serial.close()
            self._serial = None
        log.info("serial disconnected")

    async def send(self, msg: dict) -> bool:
        if not self._serial or not self._serial.is_open:
            return False
        try:
            self._serial.write(self._frame_encode(msg))
            return True
        except Exception as e:
            log.error("serial send failed: %s", e)
            return False

    async def _read_loop(self):
        buf = b""
        loop = asyncio.get_event_loop()
        reason = "serial closed"
        try:
            while self.connected:
                # non-blocking read via executor to avoid blocking the event loop
                data = await loop.run_in_executor(None, self._serial.read, 4096)
                if not data:
                    await asyncio.sleep(0.01)
                    continue
                buf += data
                while b"\x00" in buf:
                    idx = buf.index(b"\x00")
                    frame = buf[:idx]
                    buf = buf[idx + 1:]
                    if not frame:
                        continue
                    try:
                        msg = self._frame_decode(frame)
                        self._dispatch(msg)
                    except Exception as e:
                        log.error("frame decode error: %s", e)
        except asyncio.CancelledError:
            pass
        except Exception as e:
            reason = f"serial read error: {e}"
            if self.connected:
                log.error("serial read error: %s", e)
        finally:
            self.connected = False
            self._dispatch_disconnect(reason)


class WebSocketTransport(Transport):
    def __init__(self, url="ws://localhost:8080/kernel"):
        super().__init__()
        self.url = url
        self._ws = None
        self._read_task: asyncio.Task | None = None

    async def connect(self):
        import websockets
        self._ws = await websockets.connect(self.url)
        self.connected = True
        self._read_task = asyncio.create_task(self._read_loop())
        log.info("websocket connected to %s", self.url)

    async def disconnect(self):
        self.connected = False
        if self._read_task:
            self._read_task.cancel()
            try:
                await self._read_task
            except asyncio.CancelledError:
                pass
        if self._ws:
            await self._ws.close()
            self._ws = None
        log.info("websocket disconnected")

    async def send(self, msg: dict) -> bool:
        if not self._ws:
            return False
        try:
            await self._ws.send(self._frame_encode(msg))
            return True
        except Exception as e:
            log.error("websocket send failed: %s", e)
            return False

    async def _read_loop(self):
        buf = b""
        reason = "websocket eof"
        try:
            async for data in self._ws:
                if isinstance(data, str):
                    data = data.encode()
                buf += data
                while b"\x00" in buf:
                    idx = buf.index(b"\x00")
                    frame = buf[:idx]
                    buf = buf[idx + 1:]
                    if not frame:
                        continue
                    try:
                        msg = self._frame_decode(frame)
                        self._dispatch(msg)
                    except Exception as e:
                        log.error("frame decode error: %s", e)
        except asyncio.CancelledError:
            reason = "websocket closed"
        except Exception as e:
            reason = f"websocket read error: {e}"
            if self.connected:
                log.error("websocket read error: %s", e)
        finally:
            self.connected = False
            self._dispatch_disconnect(reason)


def create_transport(transport_type: str, **kwargs) -> Transport:
    t = transport_type.lower()
    if t in ("tcp", "socket"):
        return TCPTransport(
            host=kwargs.get("host", "localhost"),
            port=kwargs.get("port", 5555),
        )
    if t == "serial":
        return SerialTransport(
            port_path=kwargs.get("port_path", "/dev/ttyUSB0"),
            baud_rate=kwargs.get("baud_rate", 115200),
        )
    if t in ("websocket", "ws"):
        return WebSocketTransport(
            url=kwargs.get("url", "ws://localhost:8080/kernel"),
        )
    raise ValueError(f"unknown transport type: {transport_type}")
