# ucore jupyter kernel — routes code between a local python kernel
# and a remote micropython device on esp32 via a binary protocol
# over tcp/serial/websocket.
#
# default: execute on local python.  %%ucore magic: execute on device.

import asyncio
import fcntl
import json
import logging
import os
import pathlib
import struct
import sys
import uuid
from queue import Empty

from ipykernel.kernelbase import Kernel
from jupyter_client import KernelManager

from .agent_client import AgentClient
from .provisioner import _state_path as _ucore_state_path
from .transport import create_transport

# Route ucore kernel logs to a stable, readable file. Jupyter captures
# stderr per-kernel and rotates aggressively; debugging this kernel
# end-to-end is much easier with a dedicated log we control.
_LOG_PATH = os.environ.get("UCORE_LOG", "/tmp/ucore-kernel.log")
_log_handler = logging.FileHandler(_LOG_PATH, mode="a")
_log_handler.setFormatter(logging.Formatter(
    "%(asctime)s %(levelname)s %(name)s: %(message)s"))
logging.getLogger("ukernel").addHandler(_log_handler)
logging.getLogger("ukernel").setLevel(logging.DEBUG)

log = logging.getLogger(__name__)

UCORE_MAGIC = "%%ucore"
UAGENT_MAGIC = "%%uagent"

# Hard cap on how many in-flight cell parents we'll track before evicting
# the oldest. Both _jupyter_parents and _local_parent_map are cleaned up on
# the corresponding status:idle, but a transport drop or sub-kernel crash
# mid-cell leaves the entry stranded — over a long-running kernel this
# would slowly leak memory.
_PARENT_TRACK_CAP = 1024


def _bounded_set(dct: dict, key, value, *, cap: int = _PARENT_TRACK_CAP) -> None:
    """Insert into ``dct`` while holding it under ``cap`` entries.

    Evicts the oldest entry on overflow. Relies on Python 3.7+ dicts being
    insertion-ordered.
    """
    if key in dct:
        dct[key] = value
        return
    if len(dct) >= cap:
        dct.pop(next(iter(dct)), None)
    dct[key] = value


def _detect_magic(code):
    """Detect cell magic and return (magic, clean_code).

    Returns one of "ucore", "uagent", or None as the magic.
    """
    first_line = code.split("\n", 1)[0].strip()
    rest = code.split("\n", 1)[1] if "\n" in code else ""

    if first_line == UCORE_MAGIC:
        return "ucore", rest
    if first_line == UAGENT_MAGIC:
        return "uagent", rest
    return None, code


class UCoreKernel(Kernel):
    implementation = "ucore"
    implementation_version = "0.2.0"
    language = "python"
    language_version = f"{sys.version_info.major}.{sys.version_info.minor}"
    language_info = {
        "name": "python",
        "version": f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}",
        "mimetype": "text/x-python",
        "file_extension": ".py",
    }
    banner = "µcore — Python + MicroPython (%%ucore) + Agent (%%uagent)"

    # ipykernel 7's per-cell subshell routing (JupyterLab + jupyter-ai both
    # use it for parallel execution) doesn't survive a kernel restart: the
    # frontend keeps reusing subshell_ids the new process never registered,
    # so every routed shell message hits a KeyError in
    # shell_channel_thread_main and the cell silently drops. Opting out
    # forces shell traffic through the legacy main-shell path, which is
    # sequential per cell but reliably routes after restart.
    _supports_kernel_subshells = False

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        # device transport
        self._transport = None
        self._pending: dict[str, asyncio.Future] = {}
        self._jupyter_parents: dict[str, dict] = {}
        self._transport_type = os.environ.get("UCORE_TRANSPORT", "tcp")
        self._transport_host = os.environ.get("UCORE_HOST", "localhost")
        self._transport_port = int(os.environ.get("UCORE_PORT", "5555"))
        self._serial_port_path = self._resolve_serial_port()
        self._serial_baud_rate = int(os.environ.get("UCORE_BAUD_RATE", "115200"))
        # local python kernel
        self._local_km = None
        self._local_kc = None
        # local_msg_id → originating Jupyter parent header. Lets the
        # background iopub pump attribute side-effect messages (stream,
        # display_data, comm_msg, ...) to the cell that produced them,
        # even when those messages arrive long after the cell returned
        # (FuncAnimation timers, ipywidgets callbacks, etc.).
        self._local_parent_map: dict[str, dict] = {}
        self._local_iopub_task: asyncio.Task | None = None
        # agent
        self._agent = AgentClient()
        # pipe broadcast: localhost TCP listener that consumers (host-side
        # Python in the local sub-kernel) connect to and subscribe by
        # comm_id. JMP_COMM_MSG frames from the device get fanned out to
        # all subscribers for that comm_id.
        self._pipe_subscribers: dict[str, set[asyncio.StreamWriter]] = {}
        self._pipe_server: asyncio.AbstractServer | None = None
        self._pipe_port: int | None = None

        # ipykernel installs comm_open/comm_msg/comm_close shell handlers
        # that route to *this* kernel's CommManager, but every Comm in the
        # ucore stack actually lives in the local sub-kernel (ipympl,
        # ipywidgets state, etc.). Without bridging, frontend → kernel
        # comm traffic gets routed to an empty CommManager and silently
        # dropped — breaking the ipympl handshake (and every interactive
        # widget). Forward to where the comms actually live.
        for msg_type in ("comm_open", "comm_msg", "comm_close"):
            self.shell_handlers[msg_type] = self._forward_comm_to_local

    def start(self):
        super().start()
        log.info("ucore kernel starting (transport=%s, serial=%s@%d, log=%s)",
                 self._transport_type, self._serial_port_path,
                 self._serial_baud_rate, _LOG_PATH)
        loop = asyncio.get_event_loop()
        loop.run_until_complete(self._start_local_kernel())
        loop.run_until_complete(self._start_pipe_listener())
        try:
            loop.run_until_complete(self._connect_transport())
        except Exception as e:
            log.warning("device transport unavailable at startup: %s: %s — "
                        "will retry on first %%%%ucore cell",
                        type(e).__name__, e)

    async def _start_local_kernel(self):
        self._local_km = KernelManager(kernel_name="python3")
        self._local_km.start_kernel()
        self._local_kc = self._local_km.client()
        self._local_kc.start_channels()
        self._local_kc.wait_for_ready(timeout=30)
        self._local_iopub_task = asyncio.create_task(self._local_iopub_pump())
        log.info("local python kernel ready")

    async def _connect_transport(self):
        self._transport = create_transport(
            self._transport_type,
            host=self._transport_host,
            port=self._transport_port,
            port_path=self._serial_port_path,
            baud_rate=self._serial_baud_rate,
        )
        self._transport.on_message(self._handle_device_message)
        self._transport.on_disconnect(self._on_transport_disconnect)
        await self._transport.connect()
        log.info("transport connected (%s)", self._transport_type)

    def _resolve_serial_port(self) -> str:
        """Pick the serial device this kernel will attach to.

        Precedence: explicit env override > selection persisted by the
        sidebar > probe-pick-first JMP-speaking device on the bus. The
        last fallback is "/dev/ttyUSB0" so a kernel started on a system
        with no serial devices fails predictably rather than at random.
        """
        env_port = os.environ.get("UCORE_SERIAL_PORT")
        if env_port:
            return env_port

        from .provisioner import _read_state
        state = _read_state() or {}
        selected = state.get("selected_device")
        if selected:
            from .devices import enumerate_devices
            for d in enumerate_devices():
                if d.id == selected or d.path == selected:
                    return d.path
            log.warning("selected_device %r not present; falling back", selected)

        from .devices import enumerate_devices, probe_jmp
        for d in enumerate_devices():
            if d.kind == "unknown":
                continue
            probe_jmp(d)
            if d.speaks_jmp:
                log.info("auto-attached to %s (%s)", d.path, d.kind)
                return d.path
        return "/dev/ttyUSB0"

    def _on_transport_disconnect(self, reason: str):
        """Fail every in-flight request so cells error out instead of hanging
        when the device drops mid-execution."""
        # Strand-free: parents tracked for in-flight device requests can no
        # longer resolve, so drop them now rather than leak until restart.
        self._jupyter_parents.clear()
        if not self._pending:
            return
        log.warning("transport disconnected (%s); failing %d pending request(s)",
                    reason, len(self._pending))
        pending = self._pending
        self._pending = {}
        err = ConnectionError(f"device disconnected ({reason})")
        for fut in pending.values():
            if not fut.done():
                fut.get_loop().call_soon_threadsafe(fut.set_exception, err)

    # ── pipe broadcast (named comm channels) ──────────────────────────
    #
    # The kernel runs a localhost TCP listener. Consumers (running in the
    # local sub-kernel that handles non-magic cells) connect, send a
    # one-line subscription (b"name\n"), and then receive length-prefixed
    # chunks (4-byte little-endian length + bytes) for every JMP_COMM_MSG
    # arriving from the device with that comm_id.
    #
    # Multiple subscribers per comm_id are allowed (broadcast). Slow or
    # disconnected subscribers get dropped without blocking the producer.

    async def _start_pipe_listener(self):
        self._pipe_server = await asyncio.start_server(
            self._pipe_client_connected, "127.0.0.1", 0)
        self._pipe_port = self._pipe_server.sockets[0].getsockname()[1]
        log.info("pipe listener bound on 127.0.0.1:%d", self._pipe_port)
        # Publish the port via the provisioner state file so ucore_pipes
        # consumers can discover where to connect.
        self._update_state_file({"pipe_port": self._pipe_port})

    async def _pipe_client_connected(self, reader, writer):
        peer = writer.get_extra_info("peername")
        try:
            line = await asyncio.wait_for(reader.readline(), timeout=5.0)
        except asyncio.TimeoutError:
            log.warning("pipe subscriber %s did not send subscription line", peer)
            writer.close()
            return
        name = line.decode("utf-8", "replace").strip()
        if not name:
            writer.close()
            return
        self._pipe_subscribers.setdefault(name, set()).add(writer)
        log.info("pipe subscriber %s -> %r (total=%d)",
                 peer, name, len(self._pipe_subscribers[name]))
        try:
            # Hold the connection until the peer disconnects. We don't read
            # anything from subscribers after the subscription line.
            while True:
                data = await reader.read(4096)
                if not data:
                    break
        finally:
            self._pipe_subscribers.get(name, set()).discard(writer)
            try:
                writer.close()
            except Exception:
                pass
            log.info("pipe subscriber %s disconnected from %r", peer, name)

    # Cap how much we'll let a single subscriber buffer before we drop it.
    # asyncio's StreamWriter.write doesn't raise on a stalled peer — it just
    # appends to a transport-level buffer that grows in process memory. A
    # consumer that stops draining would otherwise OOM the kernel.
    _PIPE_WRITE_BUFFER_LIMIT = 1024 * 1024  # 1 MiB

    def _dispatch_pipe_message(self, content):
        name = content.get("comm_id", "")
        data = content.get("data", b"")
        if isinstance(data, str):
            data = data.encode("utf-8")
        subs = self._pipe_subscribers.get(name)
        if not subs:
            return
        frame = struct.pack("<I", len(data)) + data
        dead = []
        for w in subs:
            transport = w.transport
            if transport is None or transport.is_closing():
                dead.append(w)
                continue
            if transport.get_write_buffer_size() > self._PIPE_WRITE_BUFFER_LIMIT:
                log.warning("pipe subscriber for %r is too slow (buffered=%d B); dropping",
                            name, transport.get_write_buffer_size())
                dead.append(w)
                continue
            try:
                w.write(frame)
            except Exception as e:
                log.debug("pipe subscriber write failed for %r: %s", name, e)
                dead.append(w)
        for d in dead:
            subs.discard(d)
            try:
                d.close()
            except Exception:
                pass

    def _update_state_file(self, fields: dict):
        """Merge `fields` into the provisioner state file under flock."""
        path = _ucore_state_path()
        try:
            with open(path, "r+") as f:
                fcntl.flock(f, fcntl.LOCK_EX)
                try:
                    state = json.load(f)
                except (json.JSONDecodeError, ValueError):
                    state = {}
                state.update(fields)
                f.seek(0)
                f.truncate()
                json.dump(state, f)
                fcntl.flock(f, fcntl.LOCK_UN)
        except FileNotFoundError:
            # state file may not exist yet if the provisioner hasn't written
            # it. Create it minimally so consumers can discover us.
            try:
                with open(path, "w") as f:
                    fcntl.flock(f, fcntl.LOCK_EX)
                    json.dump(fields, f)
                    fcntl.flock(f, fcntl.LOCK_UN)
            except OSError as e:
                log.warning("could not write state file %s: %s", path, e)
        except OSError as e:
            log.warning("could not update state file %s: %s", path, e)

    async def _ensure_transport(self):
        """Retry the device connect if startup failed or the link dropped.

        Called on each %%ucore cell so a transient device disconnect (e.g.
        a flash or a USB re-attach) self-heals on the next attempt rather
        than wedging the kernel until restart.
        """
        if self._transport and self._transport.connected:
            return True
        if self._transport:
            try:
                await self._transport.disconnect()
            except Exception:
                log.debug("error closing dead transport", exc_info=True)
            self._transport = None
        try:
            await self._connect_transport()
            return True
        except Exception as e:
            log.warning("transport reconnect failed: %s: %s",
                        type(e).__name__, e)
            return False

    # ── comm bridge (frontend → local sub-kernel) ──────────────────

    def _forward_comm_to_local(self, stream, ident, msg):
        """Shell handler installed for comm_open/comm_msg/comm_close.

        Resigns the message with the sub-kernel's session key and writes it
        to the sub-kernel's shell socket. Sub-kernel responses (state syncs,
        canvas redraws, etc.) flow back through the existing iopub pump in
        ``_execute_local`` whenever a cell is in flight.

        Forwards metadata and parent_header alongside content/buffers — same
        defensive stance as the sub-kernel→frontend forwarder. Today the
        frontend doesn't usually populate metadata on inbound comm messages,
        but ipywidgets' protocol-version negotiation and any future routing
        hint will live there, so dropping it silently is a sharp edge.
        """
        msg_type = msg["header"]["msg_type"]
        content = msg.get("content", {})
        log.debug("FWD shell→sub-kernel %s comm_id=%s",
                  msg_type, content.get("comm_id"))
        self._local_kc.session.send(
            self._local_kc.shell_channel.socket,
            msg_type,
            content=content,
            parent=msg.get("parent_header"),
            metadata=msg.get("metadata"),
            buffers=msg.get("buffers"),
        )

    # ── local python execution ─────────────────────────────────────
    #
    # The sub-kernel's iopub is drained by a single long-lived task,
    # _local_iopub_pump, that runs for the kernel's whole lifetime.
    # Per-cell helpers only register a parent mapping and wait for the
    # shell reply — they never read iopub themselves. This is what lets
    # FuncAnimation timer redraws, ipywidgets callbacks and any other
    # post-cell side effect reach the frontend instead of piling up
    # unread until the next cell drains and discards them.

    async def _local_iopub_pump(self):
        loop = asyncio.get_running_loop()
        while True:
            try:
                msg = await loop.run_in_executor(
                    None, self._local_kc.get_iopub_msg, 1.0
                )
            except Empty:
                continue
            except asyncio.CancelledError:
                return
            except Exception:
                log.exception("local iopub pump read error")
                await asyncio.sleep(0.1)
                continue
            try:
                self._forward_local_iopub(msg)
            except Exception:
                log.exception("local iopub forward error")

    def _forward_local_iopub(self, msg):
        msg_type = msg["header"]["msg_type"]
        parent_msg_id = msg.get("parent_header", {}).get("msg_id")

        # status: not forwarded — the parent kernel emits its own busy/idle
        # frames around do_execute. We hijack idle as the cleanup signal
        # for the parent map: once a cell goes idle, no more attributable
        # side-effects can come from it.
        if msg_type == "status":
            if (msg["content"].get("execution_state") == "idle"
                    and parent_msg_id):
                self._local_parent_map.pop(parent_msg_id, None)
            return

        # execute_input is just the cell source the frontend already has.
        if msg_type == "execute_input":
            return

        # Attribute to the originating Jupyter cell when we have a mapping.
        # Free-standing messages (e.g. timer-driven canvas redraws after
        # the cell returned) have no mapping; the frontend routes those
        # by comm_id, so an empty parent_header is correct.
        parent = self._local_parent_map.get(parent_msg_id, {})
        # Forward metadata too — comm_open carries the ipywidgets protocol
        # version there, and dropping it makes the frontend reject every
        # widget with "Wrong widget protocol version".
        self._publish(msg_type, msg["content"], parent,
                      buffers=msg.get("buffers"),
                      metadata=msg.get("metadata"))

    async def _await_shell_reply(self, msg_id, timeout=None):
        loop = asyncio.get_running_loop()
        while True:
            reply = await loop.run_in_executor(
                None, self._local_kc.get_shell_msg, timeout
            )
            if reply["parent_header"].get("msg_id") == msg_id:
                return reply["content"]

    async def _execute_local(self, code, silent, store_history, allow_stdin, parent):
        msg_id = self._local_kc.execute(
            code, silent=silent, store_history=store_history,
            allow_stdin=allow_stdin,
        )
        _bounded_set(self._local_parent_map, msg_id, parent)
        return await self._await_shell_reply(msg_id)

    async def _complete_local(self, code, cursor_pos):
        msg_id = self._local_kc.complete(code, cursor_pos)
        return await self._await_shell_reply(msg_id, timeout=10)

    async def _inspect_local(self, code, cursor_pos, detail_level):
        msg_id = self._local_kc.inspect(code, cursor_pos, detail_level)
        return await self._await_shell_reply(msg_id, timeout=10)

    async def _is_complete_local(self, code):
        msg_id = self._local_kc.is_complete(code)
        return await self._await_shell_reply(msg_id, timeout=10)

    # ── device communication ────────────────────────────────────────

    def _make_header(self, msg_type):
        return {
            "msg_id": str(uuid.uuid4()),
            "session": self.session.session,
            "username": "kernel",
            "msg_type": msg_type,
            "version": "5.3.0",
        }

    async def _request(self, msg_type, content, parent_header=None, timeout=30):
        header = self._make_header(msg_type)
        msg = {
            "header": header,
            "parent_header": parent_header or {},
            "metadata": {},
            "content": content,
        }

        # track the jupyter parent so side-effects can reference it
        if parent_header:
            _bounded_set(self._jupyter_parents, header["msg_id"], parent_header)

        future = asyncio.get_running_loop().create_future()
        self._pending[header["msg_id"]] = future

        ok = await self._transport.send(msg)
        if not ok:
            self._pending.pop(header["msg_id"], None)
            self._jupyter_parents.pop(header["msg_id"], None)
            raise ConnectionError("transport send failed")

        try:
            return await asyncio.wait_for(future, timeout)
        except asyncio.TimeoutError:
            self._pending.pop(header["msg_id"], None)
            self._jupyter_parents.pop(header["msg_id"], None)
            raise TimeoutError(f"no reply for {msg_type} within {timeout}s")

    def _resolve_jupyter_parent(self, device_msg):
        # the device's parent_header.msg_id is our request's msg_id,
        # which maps back to the original jupyter parent
        our_msg_id = device_msg.get("parent_header", {}).get("msg_id")
        return self._jupyter_parents.get(our_msg_id, {})

    def _handle_device_message(self, msg):
        msg_type = msg.get("header", {}).get("msg_type", "")
        parent_id = msg.get("parent_header", {}).get("msg_id")

        if msg_type == "comm_msg":
            # Free-running pipe data — broadcast to subscribers, no Jupyter
            # cell routing involved. A device-side _thread can keep emitting
            # these long after the originating cell completed.
            self._dispatch_pipe_message(msg.get("content", {}))
            return

        if msg_type == "comm_close":
            # Forward to the frontend so any host-side consumer (e.g. a
            # comm registered against the device's comm_id) can clean up.
            # Pipe subscribers are still tracked by their TCP connection
            # lifetime — comm_close is just a hint.
            parent = self._resolve_jupyter_parent(msg)
            self._publish(msg_type, msg.get("content", {}), parent)
            return

        if msg_type == "comm_open":
            # JMP carries a numeric target_id; Jupyter expects a string
            # target_name. We have no target_id↔target_name map, so a
            # forwarded message wouldn't dispatch to anything useful — the
            # frontend would just send a comm_close back. Stay informational
            # until/unless that mapping exists.
            log.debug("device %s for comm_id=%r (not forwarded — no target_id↔target_name map)",
                      msg_type, msg.get("content", {}).get("comm_id"))
            return

        if msg_type in ("stream", "error", "display_data", "execute_result"):
            parent = self._resolve_jupyter_parent(msg)
            # Pass metadata and buffers through. Today the JMP wire layer
            # zero-fills both slots, so this is a no-op until/unless the
            # protocol grows real metadata/buffer carriage. Wiring it now
            # means we can't repeat the iopub-metadata bug on this side.
            self._publish(msg_type, msg.get("content", {}), parent,
                          buffers=msg.get("buffers"),
                          metadata=msg.get("metadata"))
            return

        if msg_type == "status":
            # idle is always the last message for a request — safe to clean up
            if msg.get("content", {}).get("execution_state") == "idle":
                self._jupyter_parents.pop(parent_id, None)
            return

        if msg_type == "input_request":
            asyncio.ensure_future(self._handle_input_request(msg))
            return

        # reply messages: resolve the pending future
        # parent tracking is cleaned up on status:idle, not here,
        # because side-effects (execute_result, error) may arrive after the reply
        if parent_id and parent_id in self._pending:
            future = self._pending.pop(parent_id)
            if not future.done():
                future.get_loop().call_soon_threadsafe(future.set_result, msg)
            return

        log.warning("unhandled device message: %s (parent=%s)", msg_type, parent_id)

    # ── publishing to jupyter frontend ──────────────────────────────

    def _publish(self, msg_type, content, parent, buffers=None, metadata=None):
        self.session.send(
            self.iopub_socket, msg_type, content,
            parent=parent, ident=None, buffers=buffers,
            metadata=metadata,
        )

    async def _handle_input_request(self, msg):
        c = msg.get("content", {})
        value = await self.raw_input(c.get("prompt", ""))

        reply_header = self._make_header("input_reply")
        reply = {
            "header": reply_header,
            "parent_header": msg.get("parent_header", {}),
            "metadata": {},
            "content": {"value": value},
        }
        await self._transport.send(reply)

    # ── jupyter kernel interface ────────────────────────────────────

    async def do_execute(self, code, silent, store_history=True,
                         user_expressions=None, allow_stdin=False,
                         *, cell_meta=None, cell_id=None):
        parent = self.get_parent()
        magic, clean_code = _detect_magic(code)

        if magic == "uagent":
            return await self._do_execute_agent(clean_code, parent)
        if magic == "ucore":
            return await self._do_execute_device(
                clean_code, silent, store_history, allow_stdin, parent)
        return await self._do_execute_local(
            clean_code, silent, store_history, allow_stdin, parent)

    async def _do_execute_local(self, code, silent, store_history, allow_stdin, parent):
        content = await self._execute_local(code, silent, store_history, allow_stdin, parent)
        status = content.get("status", "ok")

        if status == "ok":
            return {
                "status": "ok",
                "execution_count": content.get("execution_count", self.execution_count),
                "user_expressions": {},
            }

        error_content = {
            "ename": content.get("ename", "Error"),
            "evalue": content.get("evalue", ""),
            "traceback": content.get("traceback", []),
        }
        return {
            "status": "error",
            "execution_count": content.get("execution_count", self.execution_count),
            **error_content,
        }

    async def _do_execute_agent(self, code, parent):
        def on_update(update_type, text):
            if update_type == "text":
                self._publish("stream", {"name": "stdout", "text": text}, parent)
            elif update_type == "tool_start":
                self._publish("stream", {
                    "name": "stdout", "text": f"\n→ {text}\n",
                }, parent)
            elif update_type == "tool_done":
                self._publish("stream", {
                    "name": "stdout", "text": f"  ✓ done\n",
                }, parent)

        try:
            await self._agent.prompt(code, on_update)
        except Exception as e:
            log.exception("agent execution failed")
            return self._device_error("AgentError", str(e))

        return {
            "status": "ok",
            "execution_count": self.execution_count,
            "user_expressions": {},
        }

    def _device_error(self, ename, evalue):
        error_content = {
            "ename": ename,
            "evalue": evalue,
            "traceback": [f"\x1b[31m{ename}\x1b[0m: {evalue}"],
        }
        self.send_response(self.iopub_socket, "error", error_content)
        return {
            "status": "error",
            "execution_count": self.execution_count,
            **error_content,
        }

    async def _do_execute_device(self, code, silent, store_history, allow_stdin, parent):
        if not await self._ensure_transport():
            return self._device_error(
                "ConnectionError",
                f"Device not connected ({self._transport_type}). "
                f"Check the link and re-run; see {_LOG_PATH} for details.",
            )

        try:
            # No timeout: cell runtime is bounded by user code, not by us.
            # Device death is surfaced via transport read-loop failure, which
            # cancels pending futures with ConnectionError (see _request).
            reply_msg = await self._request("execute_request", {
                "code": code,
                "silent": silent,
                "store_history": store_history,
                "allow_stdin": allow_stdin,
                "stop_on_error": True,
                "user_expressions": {},
            }, parent_header=parent, timeout=None)
        except (ConnectionError, TimeoutError) as e:
            return self._device_error(type(e).__name__, str(e))

        content = reply_msg.get("content", {})
        status = content.get("status", "ok")

        if status == "ok":
            return {
                "status": "ok",
                "execution_count": content.get("execution_count", self.execution_count),
                "user_expressions": {},
            }

        error_content = {
            "ename": content.get("ename", "Error"),
            "evalue": content.get("evalue", ""),
            "traceback": content.get("traceback", []),
        }
        self.send_response(self.iopub_socket, "error", error_content)

        return {
            "status": "error",
            "execution_count": content.get("execution_count", self.execution_count),
            **error_content,
        }

    async def do_complete(self, code, cursor_pos):
        magic, clean_code = _detect_magic(code)
        if magic is None:
            return await self._complete_local(clean_code, cursor_pos)
        if magic == "uagent":
            return {"matches": [], "cursor_start": cursor_pos,
                    "cursor_end": cursor_pos, "metadata": {}, "status": "ok"}
        if not self._transport or not self._transport.connected:
            return {"matches": [], "cursor_start": cursor_pos,
                    "cursor_end": cursor_pos, "metadata": {}, "status": "ok"}

        reply_msg = await self._request("complete_request", {
            "code": clean_code,
            "cursor_pos": cursor_pos,
        })
        return reply_msg.get("content", {
            "matches": [],
            "cursor_start": cursor_pos,
            "cursor_end": cursor_pos,
            "metadata": {},
            "status": "ok",
        })

    async def do_inspect(self, code, cursor_pos, detail_level=0, omit_sections=()):
        magic, clean_code = _detect_magic(code)
        if magic is None:
            return await self._inspect_local(clean_code, cursor_pos, detail_level)
        if magic != "ucore" or not self._transport or not self._transport.connected:
            return {"status": "ok", "found": False, "data": {}, "metadata": {}}

        reply_msg = await self._request("inspect_request", {
            "code": clean_code,
            "cursor_pos": cursor_pos,
            "detail_level": detail_level,
        })
        return reply_msg.get("content", {
            "status": "ok",
            "found": False,
            "data": {},
            "metadata": {},
        })

    async def do_is_complete(self, code):
        magic, clean_code = _detect_magic(code)
        if magic is None:
            return await self._is_complete_local(clean_code)
        if magic != "ucore" or not self._transport or not self._transport.connected:
            return {"status": "unknown"}

        reply_msg = await self._request("is_complete_request", {
            "code": clean_code,
        })
        return reply_msg.get("content", {"status": "unknown"})

    async def do_shutdown(self, restart):
        try:
            await self._request("shutdown_request", {"restart": restart}, timeout=5)
        except (TimeoutError, ConnectionError):
            pass
        if self._transport:
            await self._transport.disconnect()
        await self._agent.shutdown()
        if self._local_iopub_task:
            self._local_iopub_task.cancel()
            try:
                await self._local_iopub_task
            except (asyncio.CancelledError, Exception):
                pass
        if self._local_kc:
            self._local_kc.stop_channels()
        if self._local_km:
            self._local_km.shutdown_kernel(now=True)
        return {"status": "ok", "restart": restart}


if __name__ == "__main__":
    from ipykernel.kernelapp import IPKernelApp
    IPKernelApp.launch_instance(kernel_class=UCoreKernel)
