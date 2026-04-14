# uagent MCP client — connects to the Jupyter MCP server to execute
# notebook tools (add_cell, run_cell, read_notebook_cells, etc.)
#
# jupyter_server_mcp uses FastMCP with streamable HTTP transport on /mcp.

import logging

from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client

from . import config

log = logging.getLogger(__name__)


class McpToolClient:
    """Client for calling MCP tools on the Jupyter server."""

    def __init__(self, server_url=None):
        self._server_url = server_url or config.MCP_SERVER_URL
        self._session = None
        self._transport_cm = None
        self._session_cm = None

    async def connect(self):
        """Connect to the MCP server via streamable HTTP."""
        self._transport_cm = streamablehttp_client(self._server_url)
        read_stream, write_stream, _ = await self._transport_cm.__aenter__()
        self._session_cm = ClientSession(read_stream, write_stream)
        self._session = await self._session_cm.__aenter__()
        await self._session.initialize()
        log.info("connected to MCP server at %s", self._server_url)

    async def disconnect(self):
        """Disconnect from the MCP server."""
        if self._session_cm:
            await self._session_cm.__aexit__(None, None, None)
        if self._transport_cm:
            await self._transport_cm.__aexit__(None, None, None)
        self._session = None
        log.info("disconnected from MCP server")

    async def call_tool(self, name, arguments=None):
        """Call an MCP tool and return the result.

        Always returns a dict, never raises.
        """
        if not self._session:
            return {"error": "Not connected to MCP server"}

        try:
            result = await self._session.call_tool(name, arguments or {})
            texts = []
            for block in result.content:
                if hasattr(block, "text"):
                    texts.append(block.text)
            return {"result": "\n".join(texts) if texts else "ok"}
        except Exception as e:
            return {"error": f"{type(e).__name__}: {e}"}

    async def list_tools(self):
        """List available tools on the MCP server."""
        if not self._session:
            return []
        result = await self._session.list_tools()
        return [
            {"name": t.name, "description": t.description}
            for t in result.tools
        ]
