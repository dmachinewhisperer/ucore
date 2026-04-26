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

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        # device transport
        self._transport = None
        self._pending: dict[str, asyncio.Future] = {}
        self._jupyter_parents: dict[str, dict] = {}
        self._transport_type = os.environ.get("UCORE_TRANSPORT", "tcp")
        self._transport_host = os.environ.get("UCORE_HOST", "localhost")
        self._transport_port = int(os.environ.get("UCORE_PORT", "5555"))
        self._serial_port_path = os.environ.get("UCORE_SERIAL_PORT", "/dev/ttyUSB0")
        self._serial_baud_rate = int(os.environ.get("UCORE_BAUD_RATE", "115200"))
        # local python kernel
        self._local_km = None
        self._local_kc = None
        # agent
        self._agent = AgentClient()
        # pipe broadcast: localhost TCP listener that consumers (host-side
        # Python in the local sub-kernel) connect to and subscribe by
        # comm_id. JMP_COMM_MSG frames from the device get fanned out to
        # all subscribers for that comm_id.
        self._pipe_subscribers: dict[str, set[asyncio.StreamWriter]] = {}
        self._pipe_server: asyncio.AbstractServer | None = None
        self._pipe_port: int | None = None

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
        await self._transport.connect()
        log.info("transport connected (%s)", self._transport_type)

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

    def _dispatch_pipe_message(self, content):
        name = content.get("comm_id", "")
        data = content.get("data", b"")
        if isinstance(data, str):
            data = data.encode("utf-8")
        subs = self._pipe_subscribers.get(name)
        if not subs:
            return
        header = struct.pack("<I", len(data))
        dead = []
        for w in subs:
            try:
                w.write(header + data)
                # fire-and-forget: don't await drain so a slow consumer
                # can't block the producer. transport buffer absorbs bursts;
                # if it fills, write() raises and we drop the subscriber.
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

    # ── local python execution ─────────────────────────────────────

    async def _execute_local(self, code, silent, store_history, allow_stdin, parent):
        msg_id = self._local_kc.execute(
            code, silent=silent, store_history=store_history,
            allow_stdin=allow_stdin,
        )
        log.debug("local exec msg_id=%s parent=%r code=%r",
                  msg_id, parent.get("msg_id") if isinstance(parent, dict) else parent,
                  code[:80])
        # proxy iopub messages until we get the execute_reply
        while True:
            try:
                msg = await asyncio.get_event_loop().run_in_executor(
                    None, lambda: self._local_kc.get_iopub_msg(timeout=30)
                )
            except Exception as e:
                log.warning("local exec iopub timeout/error msg_id=%s: %s", msg_id, e)
                break

            ph_msgid = msg["parent_header"].get("msg_id")
            mtype = msg["header"]["msg_type"]
            log.debug("local iopub mtype=%s parent_msg_id=%s (target=%s) match=%s",
                      mtype, ph_msgid, msg_id, ph_msgid == msg_id)

            if ph_msgid != msg_id:
                continue

            msg_type = msg["header"]["msg_type"]
            content = msg["content"]

            if msg_type in ("stream", "error", "display_data", "execute_result"):
                self._publish(msg_type, content, parent)
            elif msg_type == "status" and content.get("execution_state") == "idle":
                break

        # get the execute_reply matching our msg_id
        while True:
            reply = await asyncio.get_event_loop().run_in_executor(
                None, lambda: self._local_kc.get_shell_msg(timeout=30)
            )
            if reply["parent_header"].get("msg_id") == msg_id:
                return reply["content"]

    async def _complete_local(self, code, cursor_pos):
        msg_id = self._local_kc.complete(code, cursor_pos)
        while True:
            reply = await asyncio.get_event_loop().run_in_executor(
                None, lambda: self._local_kc.get_shell_msg(timeout=10)
            )
            if reply["parent_header"].get("msg_id") == msg_id:
                return reply["content"]

    async def _inspect_local(self, code, cursor_pos, detail_level):
        msg_id = self._local_kc.inspect(code, cursor_pos, detail_level)
        while True:
            reply = await asyncio.get_event_loop().run_in_executor(
                None, lambda: self._local_kc.get_shell_msg(timeout=10)
            )
            if reply["parent_header"].get("msg_id") == msg_id:
                return reply["content"]

    async def _is_complete_local(self, code):
        msg_id = self._local_kc.is_complete(code)
        while True:
            reply = await asyncio.get_event_loop().run_in_executor(
                None, lambda: self._local_kc.get_shell_msg(timeout=10)
            )
            if reply["parent_header"].get("msg_id") == msg_id:
                return reply["content"]

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
            self._jupyter_parents[header["msg_id"]] = parent_header

        future = asyncio.get_event_loop().create_future()
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

        if msg_type in ("comm_open", "comm_close"):
            # Currently informational on the host side; pipe subscribers are
            # tracked by connection lifetime, not by these JMP messages.
            log.debug("device %s for comm_id=%r", msg_type,
                      msg.get("content", {}).get("comm_id"))
            return

        if msg_type in ("stream", "error", "display_data", "execute_result"):
            parent = self._resolve_jupyter_parent(msg)
            self._publish(msg_type, msg.get("content", {}), parent)
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

    def _publish(self, msg_type, content, parent):
        log.debug("publish iopub mtype=%s parent_type=%s parent_keys=%s",
                  msg_type, type(parent).__name__,
                  list(parent.keys()) if isinstance(parent, dict) else None)
        self.session.send(
            self.iopub_socket, msg_type, content,
            parent=parent, ident=None,
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
            reply_msg = await self._request("execute_request", {
                "code": code,
                "silent": silent,
                "store_history": store_history,
                "allow_stdin": allow_stdin,
                "stop_on_error": True,
                "user_expressions": {},
            }, parent_header=parent)
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
        if self._local_kc:
            self._local_kc.stop_channels()
        if self._local_km:
            self._local_km.shutdown_kernel(now=True)
        return {"status": "ok", "restart": restart}


if __name__ == "__main__":
    from ipykernel.kernelapp import IPKernelApp
    IPKernelApp.launch_instance(kernel_class=UCoreKernel)
