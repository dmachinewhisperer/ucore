"""Jupyter Server extension exposing the ucore device-management REST API.

Routes (all under /ucore):
    GET    /devices            cheap enumeration (no port open)
    POST   /devices/probe      DTR-safe kernel_info_request to each candidate
    POST   /devices/{id}/reset DTR/RTS toggle
    POST   /devices/select     persist {"id": "..."} for next kernel start
    GET    /active             {"selected_device": "...", "attached": "..."}

The sidebar UI calls these. Selection is intentionally decoupled from
kernel restart — the user picks a device here, then triggers Restart
Kernel from JupyterLab when they're ready to switch.
"""

from __future__ import annotations

import asyncio
import json
import logging

from jupyter_server.base.handlers import APIHandler
from jupyter_server.extension.application import ExtensionApp
from jupyter_server.utils import url_path_join
from tornado import web

from . import devices as ucore_devices
from .provisioner import _merge_state, _read_state

log = logging.getLogger(__name__)


def _device_or_404(device_id: str) -> ucore_devices.Device:
    for d in ucore_devices.enumerate_devices():
        if d.id == device_id:
            return d
    raise web.HTTPError(404, f"device {device_id!r} not connected")


def _kernel_attached() -> bool:
    """True iff a ucore kernel currently holds a port open. We use the
    state file's lifecycle fields as the signal — the provisioner clears
    them on shutdown."""
    state = _read_state() or {}
    return bool(state.get("pid") and state.get("connection_info"))


class DevicesHandler(APIHandler):
    @web.authenticated
    async def get(self):
        items = [d.to_dict() for d in ucore_devices.enumerate_devices()]
        self.finish(json.dumps({"devices": items}))


class ProbeHandler(APIHandler):
    @web.authenticated
    async def post(self):
        loop = asyncio.get_event_loop()
        # Probing opens serial ports; do it off the IOLoop to keep the
        # server responsive for parallel requests.
        items = await loop.run_in_executor(None, self._probe_all)
        self.finish(json.dumps({"devices": items}))

    @staticmethod
    def _probe_all():
        out = []
        for d in ucore_devices.enumerate_devices():
            if d.kind != "unknown":
                ucore_devices.probe_jmp(d)
            out.append(d.to_dict())
        return out


class ResetHandler(APIHandler):
    @web.authenticated
    async def post(self, device_id: str):
        device = _device_or_404(device_id)
        if _kernel_attached():
            # The kernel-mediated reset path needs an in-kernel command
            # channel we haven't built yet. For now, the safe answer is
            # "detach first" — surface it clearly rather than silently
            # racing the kernel for the port.
            raise web.HTTPError(
                409,
                "a ucore kernel is attached; shut it down before resetting",
            )
        loop = asyncio.get_event_loop()
        try:
            await loop.run_in_executor(None, ucore_devices.reset_device, device.path)
        except Exception as e:
            log.exception("reset failed for %s", device.path)
            raise web.HTTPError(500, f"reset failed: {e}") from e
        self.set_status(204)
        self.finish()


class SelectHandler(APIHandler):
    @web.authenticated
    async def post(self):
        body = self.get_json_body() or {}
        device_id = body.get("id")
        if device_id is None:
            raise web.HTTPError(400, "body must include 'id'")
        if device_id == "":
            # Empty string clears the selection (revert to auto-pick).
            _merge_state({"selected_device": None})
            self.finish(json.dumps({"selected_device": None}))
            return
        # Validate against connected hardware so the next kernel start
        # doesn't silently fall through to the auto-pick path.
        _device_or_404(device_id)
        _merge_state({"selected_device": device_id})
        self.finish(json.dumps({"selected_device": device_id}))


class ActiveHandler(APIHandler):
    @web.authenticated
    async def get(self):
        state = _read_state() or {}
        self.finish(json.dumps({
            "selected_device": state.get("selected_device"),
            "kernel_attached": _kernel_attached(),
            "kernel_pid": state.get("pid"),
        }))


class UCoreServerExtension(ExtensionApp):
    name = "ukernel"

    def initialize_handlers(self):
        base = self.serverapp.web_app.settings["base_url"]
        self.handlers.extend([
            (url_path_join(base, "ucore", "devices"), DevicesHandler),
            (url_path_join(base, "ucore", "devices", "probe"), ProbeHandler),
            (url_path_join(base, "ucore", "devices", r"([^/]+)", "reset"), ResetHandler),
            (url_path_join(base, "ucore", "devices", "select"), SelectHandler),
            (url_path_join(base, "ucore", "active"), ActiveHandler),
        ])


def _jupyter_server_extension_points():
    return [{"module": "ukernel.server_extension", "app": UCoreServerExtension}]
