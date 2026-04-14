# uagent config — model and agent settings.

import os

MODEL = os.environ.get("UAGENT_MODEL", "gemini/gemma-4-31b-it")

# Maximum context window tokens (model-dependent)
MAX_CONTEXT_TOKENS = int(os.environ.get("UAGENT_MAX_TOKENS", "128000"))

# Tokens to reserve for the model's response
RESERVE_TOKENS = int(os.environ.get("UAGENT_RESERVE_TOKENS", "8192"))

# Recent tokens to keep intact during compaction
KEEP_RECENT_TOKENS = int(os.environ.get("UAGENT_KEEP_RECENT", "20000"))

# Max retry attempts for tool call errors before giving up
MAX_TOOL_RETRIES = 3

# Request timeout for LLM calls (seconds)
LLM_TIMEOUT = 120

# Jupyter MCP server URL (jupyter_server_mcp default port)
MCP_SERVER_URL = os.environ.get("UAGENT_MCP_URL", "http://127.0.0.1:3001/mcp")
