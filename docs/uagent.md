# uagent

uagent is an AI coding assistant built into µcore. It understands both sides
of a µcore notebook — MicroPython running on the microcontroller and Python
running on the host — and can write, run, and debug cells on your behalf.

## Install

```bash
pip install uagent
```

## Two ways to use uagent

### 1 · `%%uagent` cell magic

Write `%%uagent` as the first line of a cell, then describe what you want in
plain English. The agent writes the necessary cells, runs them, checks the
output, and iterates until the task is done.

```python
%%uagent
Read the ADC on GPIO 34 ten times at 100 ms intervals and plot the values.
```

The agent decides which cells run on the microcontroller (prefixed with
`%%ucore`) and which run on the host, executes them in order, reads the
outputs, and fixes errors automatically — up to three retries before asking
for help.

### 2 · Jupyter AI chat sidebar

When `uagent` is installed alongside `jupyter-ai`, a **uagent** persona
appears in the JupyterLab chat panel. Ask questions or give tasks there and
the agent works in the context of your current notebook.

## Configure the LLM

uagent routes LLM calls through [litellm](https://docs.litellm.ai/), so it
works with any model litellm supports — OpenAI, Anthropic, Gemini, local
Ollama models, and more.

Set the model with the `UAGENT_MODEL` environment variable before launching
JupyterLab:

```bash
# Gemini (default)
export UAGENT_MODEL="gemini/gemma-4-31b-it"

# OpenAI
export UAGENT_MODEL="gpt-4o"

# Anthropic
export UAGENT_MODEL="claude-sonnet-4-6"

# Local Ollama
export UAGENT_MODEL="ollama/llama3.2"
```

The model must be available in your environment (API key set, or Ollama
running locally). litellm reads standard environment variables like
`OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, and `GEMINI_API_KEY`.

### All configuration variables

| Variable | Default | Description |
|---|---|---|
| `UAGENT_MODEL` | `gemini/gemma-4-31b-it` | LLM model string (litellm format) |
| `UAGENT_MAX_TOKENS` | `128000` | Context window size for the chosen model |
| `UAGENT_RESERVE_TOKENS` | `8192` | Tokens reserved for the model's response |
| `UAGENT_KEEP_RECENT` | `20000` | Recent tokens preserved during context compaction |
| `UAGENT_MCP_URL` | `http://127.0.0.1:3001/mcp` | Jupyter MCP server URL |

## What the agent can do

The agent has access to these notebook tools:

| Tool | Description |
|---|---|
| `add_cell` | Add a new code or markdown cell |
| `edit_cell` | Replace the content of an existing cell |
| `run_cell` | Execute a cell and wait for it to complete |
| `read_cell` | Read a cell's content and output |
| `read_notebook` | Read all cells and outputs in the notebook |
| `get_cell_id_from_index` | Look up a cell ID by position |

The agent uses these tools in a loop: write code → run it → read the output →
fix errors → repeat until done.

## Skills

Skills are domain-specific instructions loaded on demand when the agent
detects a relevant request. They give the agent concrete patterns for common
tasks without bloating every prompt.

| Skill | Loaded when |
|---|---|
| `sensor_reading` | Reading ADC, I²C, temperature, humidity, or other sensors |
| `actuator_control` | Controlling LEDs, servos, motors, or relays |
| `plotting` | Creating charts with matplotlib from microcontroller data |
| `data_bridge` | Passing data between the microcontroller and host Python |

Skills are plain text files in `uagent/skills/` and can be extended to cover
new domains.

## Example session

```python
%%uagent
Blink the onboard LED 5 times, then read the internal temperature
and print it in Celsius.
```

The agent will:

1. Write a `%%ucore` cell that blinks the LED and reads `esp32.mcu_temperature()`.
2. Run the cell and read the output.
3. If it errors (wrong pin, missing import, etc.), fix and retry.
4. Report the temperature value back to you.

!!! tip "Long-running tasks"
    For tasks that involve background threads or streaming data, describe the
    full goal and uagent will set up both the producer cell on the
    microcontroller and the consumer cell on the host.
