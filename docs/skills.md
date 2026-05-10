# Skills

The µcore agent ships with a small set of **skills** — short markdown documents that surface the things an LLM is unlikely to know about this project. They are *guidelines*, not tutorials: project-specific APIs, hardware quirks, and recovery patterns the model would otherwise miss. The agent loads a skill on demand when its keywords match the user's message, and the body is injected into the conversation context.

## What's shipped

The shipped skills break into two layers:

### Harness skills (always loaded if matched)

| Name | Covers |
|---|---|
| `device_basics`  | The host/device split, how `%%ucore` cells route, hot-plug behaviour. |
| `streaming`      | Named pipes, device-side producer threads, `subscribe` / `collect` / `live_plot`. |
| `debugging`      | `ConnectionError`, `SyntaxError` from serial desync, pipe `TimeoutError`, missing kernelspec. |

These are about µcore itself and apply to every target. They live in `uagent/skills/*.md`.

### Platform skills (opt-in via `UAGENT_PLATFORM`)

Each platform directory under `uagent/skills/platforms/` carries up to two standardized files:

| File | Covers |
|---|---|
| `platform.md` | Memory, threading, time, file-system, reset behaviour — chip-family-level facts the LLM commonly gets wrong. |
| `peripherals.md` | GPIO, ADC, PWM, I²C, SPI, communications — peripheral guidelines and gotchas. |

Currently shipped:

- `platforms/esp32/` — generic ESP32 (Tensilica/RISC-V variants share most of these).
- `platforms/esp32-s3/` — deltas from generic ESP32 (USB-OTG, Xtensa LX7, no BT Classic).

A platform is loaded by setting `UAGENT_PLATFORM` before launching JupyterLab:

```bash
export UAGENT_PLATFORM=esp32-s3
```

Multiple platforms can be listed comma-separated; later ones override earlier ones on filename collision.

## Platform inheritance

Each platform directory may declare parents in the YAML frontmatter of any of its files:

```yaml
---
name: platform
description: ESP32-S3 platform notes
inherits: [esp32]
---
```

When a child platform is loaded, its parents are loaded **first**, then the child's files are layered on top. Override is at the **filename level** — `esp32-s3/platform.md` fully replaces `esp32/platform.md`, while `esp32-s3/peripherals.md` (if it doesn't exist) lets `esp32/peripherals.md` flow through unchanged.

The implication: a child platform writes only the files it needs to *change*. Files it doesn't touch are inherited verbatim. No content concatenation, no body merging — the file is the unit.

Inheritance is best-effort: unknown platform names in `UAGENT_PLATFORM` are silently skipped (no crash on typos), and missing parent platforms just don't contribute.

## File format

Every skill — harness, platform, or custom — is markdown with YAML frontmatter:

```markdown
---
name: my_skill
description: One-line summary that appears in the agent's skill index.
keywords: [list, of, lowercase, terms, to, match]
inherits: [parent_platform]   # only meaningful inside a platform dir
---

Skill body in regular markdown. Code fences, headings, and lists all work.
The body is what the agent sees when the skill is loaded.
```

Fields:

- `name` — display name shown in the system-prompt skill index. Filename stem is used as a fallback if omitted.
- `description` — one line, used in the index and weighted in keyword matching.
- `keywords` — list of lowercase tokens. Add aliases and jargon the description doesn't already cover.
- `inherits` — list of platform names; only meaningful in a platform directory. Any one file's `inherits` declaration applies to the whole platform (the loader unions across files).

## Resolution order

Skills are discovered from up to four sources, in order of increasing precedence:

1. **Shipped harness:** `uagent/skills/*.md` (top-level only).
2. **Platforms:** `uagent/skills/platforms/<plat>/*.md` for each entry in `UAGENT_PLATFORM`, with parents resolved first via `inherits:`.
3. **Notebook-local:** `<notebook_root>/.ucore/skills/*.md` — the notebook root JupyterLab is rooted at (`ServerApp.root_dir`).
4. **User override:** `$UAGENT_SKILL_DIR/*.md`.

Override is **filename-based**: a file with the same basename in a later source replaces the earlier one entirely.

## Custom skills

### Project-local

```bash
mkdir -p notebooks/.ucore/skills
cat > notebooks/.ucore/skills/my-rig.md <<'EOF'
---
name: my_rig
description: Wiring and pinout for the lab rig on bench 3.
keywords: [rig, bench3, wiring, pinout]
---
The thermistor is on GPIO 34 (ADC1). The fan PWM is on GPIO 25.
EOF
```

Restart the kernel; the agent picks up `my_rig` alongside the shipped skills.

### Personal library

```bash
export UAGENT_SKILL_DIR=~/dotfiles/ucore-skills
```

Set it before launching JupyterLab so the persona inherits the variable.

## Tips for writing skills

- **Don't write tutorials.** The LLM already knows MicroPython, matplotlib, struct packing, and the general shape of GPIO/ADC/PWM APIs. Skills should call out the *non-obvious* — things the LLM would miss without the hint.
- **Keep them short.** 40–80 lines of markdown is enough. If a skill is growing past that, you're probably writing a manual instead of guidance.
- **Lead with the gotcha.** "PWM duty range is 0..1023, not 0..255" is a skill. "Here's how to use PWM" is not.
- **Tune keywords by watching matches.** If the agent fails to load a skill when it should have, add the user's terminology to the keyword list.
