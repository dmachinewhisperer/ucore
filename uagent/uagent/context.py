# uagent context — assembles and manages the LLM context window.
#
# Skills live as .md files with YAML frontmatter:
#
#     ---
#     name: my_skill
#     description: one-line summary used in the prompt index
#     keywords: [a, b, c]
#     ---
#     skill body in markdown ...
#
# Skills are discovered from up to three directories, in increasing order of
# precedence (later overrides earlier on name collision):
#
#   1. shipped:           uagent/skills/                                   (always)
#   2. notebook-local:    $UAGENT_NOTEBOOK_ROOT/.ucore/skills/             (if set & exists)
#   3. user override:     $UAGENT_SKILL_DIR                                (if set & exists)
#
# Custom skills live entirely in env-driven dirs — nothing is written into the
# package on disk, so a kernel restart with no env vars cleanly reverts to the
# shipped set.

from __future__ import annotations

import os
from pathlib import Path

import yaml

PROMPTS_DIR = Path(__file__).parent / "prompts"
SHIPPED_SKILLS_DIR = Path(__file__).parent / "skills"


def _skill_dirs() -> list[Path]:
    """Resolution order: shipped → notebook-local → user override."""
    dirs: list[Path] = [SHIPPED_SKILLS_DIR]
    nb_root = os.environ.get("UAGENT_NOTEBOOK_ROOT")
    if nb_root:
        candidate = Path(nb_root) / ".ucore" / "skills"
        if candidate.is_dir():
            dirs.append(candidate)
    override = os.environ.get("UAGENT_SKILL_DIR")
    if override:
        candidate = Path(override)
        if candidate.is_dir():
            dirs.append(candidate)
    return dirs


def _parse_frontmatter(text: str) -> tuple[dict, str]:
    """Split a markdown file into (frontmatter dict, body).

    Frontmatter is a YAML block delimited by lines containing only ``---``.
    Files without frontmatter parse as ``({}, full_text)``.
    """
    if not text.startswith("---"):
        return {}, text
    lines = text.splitlines(keepends=True)
    if not lines or lines[0].rstrip() != "---":
        return {}, text
    end = None
    for i in range(1, len(lines)):
        if lines[i].rstrip() == "---":
            end = i
            break
    if end is None:
        return {}, text
    fm_text = "".join(lines[1:end])
    body = "".join(lines[end + 1:]).lstrip("\n")
    fm = yaml.safe_load(fm_text) or {}
    if not isinstance(fm, dict):
        fm = {}
    return fm, body


def _load_dir(d: Path) -> dict[str, dict]:
    """Walk one skill directory; return ``{name: skill_dict}``.

    A skill_dict has keys: name, description, keywords (list), body, source.
    Files missing a ``name`` field are skipped.
    """
    out: dict[str, dict] = {}
    for path in sorted(d.glob("*.md")):
        try:
            text = path.read_text()
        except OSError:
            continue
        fm, body = _parse_frontmatter(text)
        name = fm.get("name") or path.stem
        if not name:
            continue
        keywords = fm.get("keywords") or []
        if not isinstance(keywords, list):
            keywords = []
        out[name] = {
            "name": name,
            "description": fm.get("description", ""),
            "keywords": [str(k).lower() for k in keywords],
            "body": body,
            "source": str(path),
        }
    return out


def load_skill_index() -> list[dict]:
    """Return all visible skills, with later dirs overriding earlier ones."""
    merged: dict[str, dict] = {}
    for d in _skill_dirs():
        merged.update(_load_dir(d))
    return list(merged.values())


def load_skill(name: str) -> str | None:
    """Return the body of the named skill, or ``None`` if not found."""
    for skill in load_skill_index():
        if skill["name"] == name:
            return skill["body"]
    return None


def build_skill_index_text(skills: list[dict]) -> str:
    """Format skill index as a compact list for the system prompt."""
    return "\n".join(
        f"- **{s['name']}**: {s['description']}" for s in skills
    )


def build_system_prompt() -> str:
    """Assemble the system prompt with skill index injected."""
    template = (PROMPTS_DIR / "system.txt").read_text()
    skill_index_text = build_skill_index_text(load_skill_index())
    return template.replace("{skill_index}", skill_index_text)


def match_skills(user_message: str, skills: list[dict] | None = None) -> list[str]:
    """Return skill names relevant to the user's message.

    Score each skill by ``(matches in keywords) + 0.5 * (matches in description)``;
    return any skill with a positive score, sorted by descending score.
    """
    if skills is None:
        skills = load_skill_index()

    msg = user_message.lower()
    scored: list[tuple[float, str]] = []
    for skill in skills:
        kw_hits = sum(1 for kw in skill["keywords"] if kw and kw in msg)
        desc_words = [w for w in skill["description"].lower().split() if len(w) > 3]
        desc_hits = sum(1 for w in desc_words if w in msg)
        score = kw_hits + 0.5 * desc_hits
        if score > 0:
            scored.append((score, skill["name"]))
    scored.sort(key=lambda x: x[0], reverse=True)
    return [name for _, name in scored]


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
