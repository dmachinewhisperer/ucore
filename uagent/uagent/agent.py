# uagent agent — standalone agent loop for non-ACP usage (testing, scripts).
#
# for Jupyter AI integration, use acp_agent.py instead.
# this module provides a simpler interface for running the agent
# outside of the Jupyter AI chat sidebar.

import json
import logging

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

log = logging.getLogger(__name__)


class Agent:
    """Standalone agent loop for testing and scripting."""

    def __init__(self, execute_tool=None, on_event=None):
        self.messages = [{"role": "system", "content": build_system_prompt()}]
        self._execute_tool_fn = execute_tool or (lambda name, args: {"error": "no executor"})
        self.on_event = on_event or (lambda *a: None)

    def run(self, user_message):
        """Handle a user message. Returns the agent's final text response."""
        for name in match_skills(user_message):
            content = load_skill(name)
            if content:
                self.messages.append({
                    "role": "system",
                    "content": f"## Skill: {name}\n\n{content}",
                })
                self.on_event("skill_loaded", name)

        self.messages.append({"role": "user", "content": user_message})

        while True:
            self._maybe_compact()
            response = self._infer()

            if not response.get("tool_calls"):
                text = response.get("content", "")
                self.messages.append({"role": "assistant", "content": text})
                return text

            if response.get("content"):
                self.on_event("reasoning", response["content"])

            self.messages.append({
                "role": "assistant",
                "content": response.get("content"),
                "tool_calls": response["tool_calls"],
            })

            for tool_call in response["tool_calls"]:
                result = self._handle_tool_call(tool_call)
                self.messages.append({
                    "role": "tool",
                    "tool_call_id": tool_call["id"],
                    "content": json.dumps(result),
                })

    def _infer(self):
        response = litellm.completion(
            model=config.MODEL,
            messages=self.messages,
            tools=TOOL_SCHEMAS,
            timeout=config.LLM_TIMEOUT,
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

    def _handle_tool_call(self, tool_call):
        name = tool_call["function"]["name"]
        try:
            arguments = json.loads(tool_call["function"]["arguments"])
        except (json.JSONDecodeError, TypeError) as e:
            return {"error": f"Malformed arguments: {e}"}

        self.on_event("tool_call", f"{name}({arguments})")
        result = self._execute_tool_fn(name, arguments)
        self.on_event("tool_result", result)
        return result

    def _maybe_compact(self):
        tokens = estimate_tokens(self.messages)
        threshold = config.MAX_CONTEXT_TOKENS - config.RESERVE_TOKENS
        if tokens > threshold:
            self.messages, _ = compact_messages(
                self.messages,
                keep_recent_tokens=config.KEEP_RECENT_TOKENS,
            )
