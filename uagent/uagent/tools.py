# uagent tools — tool schemas for the LLM.
#
# these MUST match the actual MCP tool signatures exposed by jupyter_ai_tools.
# the agent sends these schemas to the LLM so it knows what it can call.
# actual execution is handled by the MCP server.

TOOL_SCHEMAS = [
    {
        "type": "function",
        "function": {
            "name": "add_cell",
            "description": "Add a new code cell to a notebook. Use %%ucore as the first line of content to run on the ESP32 device.",
            "parameters": {
                "type": "object",
                "properties": {
                    "file_path": {
                        "type": "string",
                        "description": "Path to the notebook file",
                    },
                    "content": {
                        "type": "string",
                        "description": "The code to put in the cell",
                    },
                    "cell_type": {
                        "type": "string",
                        "description": "Cell type: 'code' or 'markdown'. Defaults to 'code'.",
                    },
                },
                "required": ["file_path", "content"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "edit_cell",
            "description": "Replace the content of an existing notebook cell.",
            "parameters": {
                "type": "object",
                "properties": {
                    "file_path": {
                        "type": "string",
                        "description": "Path to the notebook file",
                    },
                    "cell_id": {
                        "type": "string",
                        "description": "The ID of the cell to edit",
                    },
                    "content": {
                        "type": "string",
                        "description": "The new code for the cell",
                    },
                },
                "required": ["file_path", "cell_id", "content"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "run_cell",
            "description": "Execute a notebook cell by ID. Has a 10s timeout but the kernel keeps running after timeout. Does NOT return output — call read_cell after to see results.",
            "parameters": {
                "type": "object",
                "properties": {
                    "cell_id": {
                        "type": "string",
                        "description": "The cell ID (UUID) or cell index as string (e.g. '0', '1')",
                    },
                    "timeout": {
                        "type": "number",
                        "description": "Max seconds to wait. Default 10, max 10.",
                    },
                },
                "required": ["cell_id"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "read_cell",
            "description": "Read a specific cell's content and outputs.",
            "parameters": {
                "type": "object",
                "properties": {
                    "file_path": {
                        "type": "string",
                        "description": "Path to the notebook file",
                    },
                    "cell_id": {
                        "type": "string",
                        "description": "The ID of the cell to read",
                    },
                    "include_outputs": {
                        "type": "boolean",
                        "description": "Whether to include cell outputs. Defaults to true.",
                    },
                },
                "required": ["file_path", "cell_id"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "read_notebook",
            "description": "Read the full notebook — all cells, their content, and outputs.",
            "parameters": {
                "type": "object",
                "properties": {
                    "file_path": {
                        "type": "string",
                        "description": "Path to the notebook file",
                    },
                    "include_outputs": {
                        "type": "boolean",
                        "description": "Whether to include cell outputs. Defaults to true.",
                    },
                },
                "required": ["file_path"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "get_cell_id_from_index",
            "description": "Get the cell ID for a cell at a given index position in the notebook.",
            "parameters": {
                "type": "object",
                "properties": {
                    "file_path": {
                        "type": "string",
                        "description": "Path to the notebook file",
                    },
                    "cell_index": {
                        "type": "integer",
                        "description": "The zero-based index of the cell",
                    },
                },
                "required": ["file_path", "cell_index"],
            },
        },
    },
]

TOOL_MAP = {s["function"]["name"]: s for s in TOOL_SCHEMAS}
