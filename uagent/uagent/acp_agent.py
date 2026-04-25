# uagent ACP agent — implements the Agent Client Protocol.
#
# this is the agent process that Jupyter AI launches as a subprocess.
# it communicates over stdio using the ACP protocol, and calls
# Jupyter notebook tools via the MCP server.

import asyncio
import json
import logging
import uuid
from typing import Any

import acp
from acp.schema import (
    AgentCapabilities,
    Implementation,
    InitializeResponse,
    NewSessionResponse,
    LoadSessionResponse,
    ListSessionsResponse,
    PromptResponse,
    TextContentBlock,
)

import litellm

from . import config
from .context import (
    build_system_prompt,
    compact_messages,
    estimate_tokens,
    load_skill,
    match_skills,
)
from .tools import TOOL_SCHEMAS
from .mcp_client import McpToolClient

log = logging.getLogger(__name__)


class Session:
    """Holds conversation state for a single chat session."""

    def __init__(self, session_id, cwd, excluded_tools=None):
        self.session_id = session_id
        self.cwd = cwd
        self.notebook_path = None
        self.excluded_tools = set(excluded_tools or [])
        self.messages = [{"role": "system", "content": build_system_prompt()}]

    def get_tool_schemas(self):
        """Return TOOL_SCHEMAS filtered by this session's exclusions."""
        if not self.excluded_tools:
            return TOOL_SCHEMAS
        return [s for s in TOOL_SCHEMAS if s["function"]["name"] not in self.excluded_tools]


class UCoreAcpAgent:
    """ACP agent that routes between Python and MicroPython on ESP32."""

    def __init__(self):
        self._sessions: dict[str, Session] = {}
        self._conn = None
        self._mcp = McpToolClient()

    def on_connect(self, conn):
        self._conn = conn

    async def initialize(self, protocol_version, client_capabilities=None,
                         client_info=None, **kwargs):
        # connect to the Jupyter MCP server for tool execution
        try:
            await self._mcp.connect()
            tools = await self._mcp.list_tools()
            log.info("MCP tools available: %s", [t["name"] for t in tools])
        except Exception:
            log.warning("MCP server not available — tool execution will fail")

        return InitializeResponse(
            protocol_version=acp.PROTOCOL_VERSION,
            agent_info=Implementation(name="ucore-agent", version="0.1.0"),
            agent_capabilities=AgentCapabilities(load_session=False),
        )

    async def new_session(self, cwd, mcp_servers=None, **kwargs):
        session_id = str(uuid.uuid4())
        excluded_tools = kwargs.get("excluded_tools")
        self._sessions[session_id] = Session(session_id, cwd, excluded_tools)
        log.info("new session: %s (excluded_tools=%s)", session_id, excluded_tools)
        return NewSessionResponse(session_id=session_id)

    async def load_session(self, cwd, session_id, mcp_servers=None, **kwargs):
        return None

    async def list_sessions(self, cursor=None, cwd=None, **kwargs):
        return ListSessionsResponse(sessions=[])

    async def close_session(self, session_id, **kwargs):
        self._sessions.pop(session_id, None)
        log.info("closed session: %s", session_id)
        return None

    async def set_session_mode(self, mode_id, session_id, **kwargs):
        return None

    async def set_session_model(self, model_id, session_id, **kwargs):
        return None

    async def set_config_option(self, config_id, session_id, value, **kwargs):
        return None

    async def authenticate(self, method_id, **kwargs):
        return None

    async def fork_session(self, cwd, session_id, mcp_servers=None, **kwargs):
        return None

    async def resume_session(self, cwd, session_id, mcp_servers=None, **kwargs):
        return None

    async def cancel(self, session_id, **kwargs):
        return None

    async def ext_method(self, method, params):
        return {}

    async def ext_notification(self, method, params):
        return None

    async def _resolve_active_notebook(self):
        """Call get_active_notebook via MCP to find which notebook the user has open."""
        result = await self._mcp.call_tool("get_active_notebook", {})
        path = result.get("result", "")
        if path and "error" not in result:
            return path.strip()
        return None

    async def prompt(self, prompt, session_id, message_id=None, **kwargs):
        session = self._sessions.get(session_id)
        if not session:
            await self._send_text(session_id, "Error: session not found.")
            return PromptResponse(stop_reason="end_turn")

        # resolve the active notebook so the LLM knows the file_path
        notebook_path = await self._resolve_active_notebook()
        if notebook_path:
            session.notebook_path = notebook_path

        # extract text from prompt content blocks
        user_text = " ".join(
            block.text for block in prompt
            if isinstance(block, TextContentBlock)
        )

        # match and load relevant skills
        for name in match_skills(user_text):
            content = load_skill(name)
            if content:
                session.messages.append({
                    "role": "system",
                    "content": f"## Skill: {name}\n\n{content}",
                })
                await self._send_text(session_id, f"_Loaded skill: {name}_\n\n")

        # inject notebook context so the LLM has the file_path
        if session.notebook_path:
            user_text = f"[Active notebook: {session.notebook_path}]\n\n{user_text}"

        session.messages.append({"role": "user", "content": user_text})

        # agent loop — max iterations to prevent runaway loops
        max_iterations = 50
        try:
            for _ in range(max_iterations):
                self._maybe_compact(session)

                response = await self._infer(session)

                # no tool calls → final response
                if not response.get("tool_calls"):
                    text = response.get("content", "")
                    session.messages.append({"role": "assistant", "content": text})
                    await self._send_text(session_id, text)
                    return PromptResponse(stop_reason="end_turn")

                # show reasoning alongside tool calls
                if response.get("content"):
                    await self._send_text(session_id, response["content"] + "\n\n")

                session.messages.append({
                    "role": "assistant",
                    "content": response.get("content"),
                    "tool_calls": response["tool_calls"],
                })

                # execute tool calls
                for tool_call in response["tool_calls"]:
                    tc_name = tool_call["function"]["name"]
                    tc_id = tool_call["id"]

                    await self._conn.session_update(
                        session_id,
                        acp.start_tool_call(tc_id, f"{tc_name}"),
                    )

                    result = await self._execute_tool(tool_call)

                    await self._conn.session_update(
                        session_id,
                        acp.update_tool_call(
                            tc_id,
                            status="completed",
                            raw_output=json.dumps(result),
                        ),
                    )

                    session.messages.append({
                        "role": "tool",
                        "tool_call_id": tc_id,
                        "content": json.dumps(result),
                    })

            # hit max iterations
            await self._send_text(session_id, "Reached maximum steps. Stopping.")
            return PromptResponse(stop_reason="max_turn_requests")

        except Exception as e:
            log.exception("agent loop failed")
            await self._send_text(session_id, f"Error: {type(e).__name__}: {e}")
            return PromptResponse(stop_reason="end_turn")

    # ── LLM interaction ────────────────────────────────────────────

    async def _infer(self, session):
        """Call the LLM with retry on rate limit errors."""
        tools = session.get_tool_schemas()
        max_retries = 3
        for attempt in range(max_retries):
            try:
                response = await asyncio.get_event_loop().run_in_executor(
                    None,
                    lambda: litellm.completion(
                        model=config.MODEL,
                        messages=session.messages,
                        tools=tools,
                        timeout=config.LLM_TIMEOUT,
                    ),
                )
                choice = response.choices[0].message
                return {
                    "content": choice.content,
                    "tool_calls": [
                        {
                            "id": tc.id,
                            "function": {
                                "name": tc.function.name,
                                "arguments": tc.function.arguments,
                            },
                        }
                        for tc in (choice.tool_calls or [])
                    ] if choice.tool_calls else None,
                }
            except litellm.RateLimitError as e:
                if attempt < max_retries - 1:
                    wait = 60 * (attempt + 1)
                    log.warning("rate limited, waiting %ds before retry", wait)
                    await asyncio.sleep(wait)
                else:
                    raise

    async def _execute_tool(self, tool_call):
        """Execute a tool call via the MCP server."""
        name = tool_call["function"]["name"]
        try:
            arguments = json.loads(tool_call["function"]["arguments"])
        except (json.JSONDecodeError, TypeError) as e:
            return {"error": f"Malformed arguments: {e}"}

        log.info("executing tool: %s(%s)", name, arguments)
        result = await self._mcp.call_tool(name, arguments)
        log.info("tool result: %s", result)
        return result

    # ── helpers ─────────────────────────────────────────────────────

    async def _send_text(self, session_id, text):
        await self._conn.session_update(
            session_id,
            acp.update_agent_message_text(text),
        )

    def _maybe_compact(self, session):
        tokens = estimate_tokens(session.messages)
        threshold = config.MAX_CONTEXT_TOKENS - config.RESERVE_TOKENS
        if tokens > threshold:
            session.messages, _ = compact_messages(
                session.messages,
                keep_recent_tokens=config.KEEP_RECENT_TOKENS,
            )
