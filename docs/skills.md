# Skills

The µcore agent ships with a small set of **skills** — short markdown documents that teach the agent operational patterns it can't reliably infer from the codebase alone. The agent loads a skill on demand when its keywords match the user's message, and the skill body is injected into the conversation context.

This page covers what ships, the file format, and how to add your own.

## Shipped skills

| Name | Covers |
|---|---|
| `device_basics`  | The host/device split, how `%%ucore` cells route, hot-plug behaviour. |
| `peripherals`    | GPIO, PWM, ADC, I²C, on-board WS2812, common sensor/actuator patterns. |
| `streaming`      | Named pipes, device-side producer threads, `subscribe` / `collect` / `live_plot`. |
| `debugging`      | Common failures: `ConnectionError`, `SyntaxError` from serial desync, pipe `TimeoutError`, missing kernelspec. |

The agent decides which to load by keyword-matching the user's message against each skill's frontmatter. Multiple skills can be loaded in one turn.

## File format

A skill is a single markdown file with YAML frontmatter:

```markdown
---
name: my_skill
description: One-line summary that appears in the agent's skill index.
keywords: [list, of, lowercase, terms, to, match]
---

Skill body in regular markdown. Code fences, headings, and lists all work.
The body is what the agent sees when the skill is loaded.
```

Three fields:

- `name` — unique identifier. Filename stem is used as a fallback if omitted.
- `description` — one line, shown in the system-prompt skill index.
- `keywords` — list of lowercase tokens. Matching is case-insensitive substring against the user's message. Add aliases and jargon the description doesn't already cover.

No separate index file is needed — the loader walks the skill directories and reads frontmatter from each `.md` file.

## Custom skill directories

Skills are discovered from up to three locations, in order of increasing precedence (later dirs override earlier ones on name collision):

1. **Shipped:** `uagent/skills/` inside the installed package. Always present.
2. **Notebook-local:** `<notebook_root>/.ucore/skills/` — the notebook root that JupyterLab is configured with (`ServerApp.root_dir`). Use this for project-specific skills that travel with the notebook.
3. **User override:** the path in the `UAGENT_SKILL_DIR` environment variable. Use this for ad-hoc personal skill libraries.

Custom skills with the same `name` as a shipped one replace it entirely. Custom skills with a new `name` are added to the index. Both behaviours are session-scoped — kill the kernel without those env vars / dirs and the agent reverts to the shipped set.

### Example: project-local skills

```bash
mkdir -p notebooks/.ucore/skills
cat > notebooks/.ucore/skills/my-rig.md <<'EOF'
---
name: my_rig
description: Wiring and pinout for the lab rig on bench 3.
keywords: [rig, bench3, wiring, pinout]
---
The thermistor is on GPIO 34 (ADC1). The fan PWM is on GPIO 25.
The buzzer on GPIO 13 is wired through a 2N7000 MOSFET — drive HIGH to sound.
EOF
```

Restart the kernel; the agent will pick up `my_rig` alongside the shipped skills.

### Example: personal skill library

```bash
export UAGENT_SKILL_DIR=~/dotfiles/ucore-skills
```

Set it before launching JupyterLab so the persona inherits the variable.

## Tips for writing skills

- **Keep them short.** 40–80 lines of markdown is usually enough. The skill is *guidance*, not a manual.
- **Lead with the pattern, not the API.** The agent has the API in the codebase; what it needs is "use a `_thread` with an `is_pipe_open()` guard," not the function signature.
- **Use fenced code blocks.** They keep the model's "this is code" boundary crisp.
- **Tune keywords by watching matches.** If the agent fails to load a skill when it should have, add the user's terminology to the keyword list.
