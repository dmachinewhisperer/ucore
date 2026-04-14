# uagent context — assembles and manages the LLM context window.

import os
import yaml
from pathlib import Path

PROMPTS_DIR = Path(__file__).parent / "prompts"
SKILLS_DIR = Path(__file__).parent / "skills"


def load_skill_index():
    """Load skill metadata from index.yaml."""
    index_path = SKILLS_DIR / "index.yaml"
    with open(index_path) as f:
        return yaml.safe_load(f)


def load_skill(name):
    """Load a skill's full content by name."""
    index = load_skill_index()
    for skill in index:
        if skill["name"] == name:
            skill_path = SKILLS_DIR / skill["file"]
            return skill_path.read_text()
    return None


def build_skill_index_text(skills):
    """Format skill index as a compact list for the system prompt."""
    lines = []
    for skill in skills:
        lines.append(f"- **{skill['name']}**: {skill['description']}")
    return "\n".join(lines)


def build_system_prompt():
    """Assemble the system prompt with skill index injected."""
    template = (PROMPTS_DIR / "system.txt").read_text()
    skills = load_skill_index()
    skill_index_text = build_skill_index_text(skills)
    return template.replace("{skill_index}", skill_index_text)


def match_skills(user_message, skills=None):
    """Return skill names that are relevant to the user's message.

    Simple keyword matching for now. Can be replaced with embeddings later.
    """
    if skills is None:
        skills = load_skill_index()

    message_lower = user_message.lower()
    matched = []

    keyword_map = {
        "sensor_reading": [
            "sensor", "temperature", "humidity", "adc", "analog",
            "read", "sample", "measure", "i2c", "spi",
        ],
        "actuator_control": [
            "led", "blink", "servo", "motor", "relay", "pwm",
            "actuator", "output", "pin", "gpio", "buzzer",
        ],
        "plotting": [
            "plot", "graph", "chart", "visualize", "matplotlib",
            "figure", "histogram", "bar chart",
        ],
        "data_bridge": [
            "transfer", "pass data", "bridge", "parse", "stdout",
            "json", "send data", "share data",
        ],
    }

    for skill in skills:
        keywords = keyword_map.get(skill["name"], [])
        if any(kw in message_lower for kw in keywords):
            matched.append(skill["name"])

    return matched


def estimate_tokens(messages):
    """Rough token estimate: ~4 chars per token."""
    total_chars = sum(len(str(m.get("content", ""))) for m in messages)
    return total_chars // 4


def compact_messages(messages, keep_recent_tokens=20000):
    """Summarize older messages when context is too large.

    Returns (compacted_messages, summary_text).
    The summary is prepended as a system message.
    """
    total = estimate_tokens(messages)
    if total <= keep_recent_tokens:
        return messages, None

    # walk backwards to find the cut point
    token_count = 0
    cut_index = len(messages)
    for i in range(len(messages) - 1, -1, -1):
        token_count += estimate_tokens([messages[i]])
        if token_count >= keep_recent_tokens:
            cut_index = i + 1
            break

    # never cut at index 0 (system prompt)
    cut_index = max(cut_index, 1)

    old_messages = messages[1:cut_index]  # skip system prompt
    recent_messages = messages[cut_index:]

    # build a summary of what was compacted
    summary_parts = []
    for msg in old_messages:
        role = msg.get("role", "?")
        content = str(msg.get("content", ""))
        if len(content) > 200:
            content = content[:200] + "..."
        summary_parts.append(f"[{role}]: {content}")

    summary = (
        "## Conversation Summary (compacted)\n\n"
        + "\n".join(summary_parts)
    )

    compacted = [
        messages[0],  # system prompt
        {"role": "system", "content": summary},
        *recent_messages,
    ]

    return compacted, summary
