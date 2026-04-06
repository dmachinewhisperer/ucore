# ucore jupyter kernel — bridges jupyter to micropython on esp32 via
# a binary protocol over tcp/serial/websocket.

import asyncio
import logging
import os
import uuid

from ipykernel.kernelbase import Kernel

from .transport import create_transport

log = logging.getLogger(__name__)


class UCoreKernel(Kernel):
    implementation = "ucore"
    implementation_version = "0.1.0"
    language = "python"
    language_version = "3.4"
    language_info = {
        "name": "micropython",
        "version": "1.24",
        "mimetype": "text/x-python",
        "file_extension": ".py",
    }
    banner = "µcore — MicroPython on ESP32"

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._transport = None
        self._pending: dict[str, asyncio.Future] = {}
        # maps our request msg_id -> jupyter parent header, so side-effect
        # messages from the device can be published with the correct parent
        self._jupyter_parents: dict[str, dict] = {}
        self._transport_type = os.environ.get("UCORE_TRANSPORT", "tcp")
        self._transport_host = os.environ.get("UCORE_HOST", "localhost")
        self._transport_port = int(os.environ.get("UCORE_PORT", "5555"))

    def start(self):
        super().start()
        loop = asyncio.get_event_loop()
        loop.run_until_complete(self._connect_transport())

    async def _connect_transport(self):
        self._transport = create_transport(
            self._transport_type,
            host=self._transport_host,
            port=self._transport_port,
        )
        self._transport.on_message(self._handle_device_message)
        await self._transport.connect()
        log.info("transport connected")

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

        reply_msg = await self._request("execute_request", {
            "code": code,
            "silent": silent,
            "store_history": store_history,
            "allow_stdin": allow_stdin,
            "stop_on_error": True,
            "user_expressions": {},
        }, parent_header=parent)

        content = reply_msg.get("content", {})
        status = content.get("status", "ok")

        if status == "ok":
            return {
                "status": "ok",
                "execution_count": content.get("execution_count", self.execution_count),
                "user_expressions": {},
            }

        # ipykernel expects us to publish the error on iopub before returning
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
        reply_msg = await self._request("complete_request", {
            "code": code,
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
        reply_msg = await self._request("inspect_request", {
            "code": code,
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
        reply_msg = await self._request("is_complete_request", {
            "code": code,
        })
        return reply_msg.get("content", {"status": "unknown"})

    async def do_shutdown(self, restart):
        try:
            await self._request("shutdown_request", {"restart": restart}, timeout=5)
        except (TimeoutError, ConnectionError):
            pass
        if self._transport:
            await self._transport.disconnect()
        return {"status": "ok", "restart": restart}


if __name__ == "__main__":
    from ipykernel.kernelapp import IPKernelApp
    IPKernelApp.launch_instance(kernel_class=UCoreKernel)
