# ukernel agent client — ACP client that spawns and communicates with
# the uagent subprocess.  streams agent updates back to the kernel
# so %%uagent cells can show progress while blocking until done.

import asyncio
import logging
import os
import sys

import acp
from acp.schema import (
    AgentMessageChunk,
    AgentThoughtChunk,
    ClientCapabilities,
    Implementation,
    TextContentBlock,
    ToolCallStart,
    ToolCallProgress,
)

log = logging.getLogger(__name__)

AGENT_MODULE = "uagent"


class AgentClient:
    """ACP client that manages a uagent subprocess.

    Spawned lazily on first prompt.  Streams session updates to a
    caller-supplied callback so the kernel can relay them to cell output.
    """

    def __init__(self):
        self._conn = None
        self._proc = None
        self._session_id = None
        self._on_update = None

    @property
    def running(self):
        return self._proc is not None and self._proc.returncode is None

    async def ensure_started(self):
        """Spawn the agent subprocess and initialize the ACP session."""
        if self.running:
            return

        self._proc = await asyncio.create_subprocess_exec(
            sys.executable, "-m", AGENT_MODULE,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            env={**os.environ},
        )

        self._conn = acp.connect_to_agent(
            self, self._proc.stdin, self._proc.stdout
        )

        await self._conn.initialize(
            protocol_version=acp.PROTOCOL_VERSION,
            client_info=Implementation(
                name="ucore-kernel",
                title="µcore Kernel",
                version="0.2.0",
            ),
        )

        resp = await self._conn.new_session(
            cwd=os.getcwd(),
            excluded_tools=["run_cell", "run_all_cells"],
        )
        self._session_id = resp.session_id
        log.info("agent session started: %s", self._session_id)

    async def prompt(self, text, on_update):
        """Send a prompt to the agent and block until it finishes.

        on_update is called with (update_type, text) for each streaming
        event so the kernel can publish to iopub.
        """
        await self.ensure_started()
        self._on_update = on_update

        try:
            response = await self._conn.prompt(
                prompt=[TextContentBlock(type="text", text=text)],
                session_id=self._session_id,
            )
            return response
        finally:
            self._on_update = None

    async def shutdown(self):
        """Close the ACP connection and terminate the agent subprocess."""
        if self._conn:
            try:
                if self._session_id:
                    await self._conn.close_session(self._session_id)
                await self._conn.close()
            except Exception:
                log.debug("error closing agent connection", exc_info=True)
            self._conn = None
            self._session_id = None

        if self._proc and self._proc.returncode is None:
            self._proc.terminate()
            try:
                await asyncio.wait_for(self._proc.wait(), timeout=5)
            except asyncio.TimeoutError:
                self._proc.kill()
            self._proc = None

    # ── ACP Client callbacks (called by the agent via ACP) ─────────

    async def session_update(self, session_id, update, **kwargs):
        if self._on_update is None:
            return

        if isinstance(update, AgentMessageChunk):
            if hasattr(update.content, "text"):
                self._on_update("text", update.content.text)

        elif isinstance(update, ToolCallStart):
            self._on_update("tool_start", update.title)

        elif isinstance(update, ToolCallProgress):
            if update.status == "completed":
                self._on_update("tool_done", update.title or "")

    async def request_permission(self, options, session_id, tool_call, **kwargs):
        # auto-approve all tool calls — the agent operates within the
        # notebook sandbox via MCP tools
        return acp.schema.RequestPermissionResponse(
            outcome=acp.schema.AllowedOutcome(
                outcome="selected",
                option_id=options[0].id,
            ),
        )

    def on_connect(self, conn):
        pass
