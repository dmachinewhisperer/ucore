#!/usr/bin/env bash
# Editable dev install for ucore.
#
# Why this exists: `pip install -e` doesn't honour
# `[tool.setuptools.data-files]`, so the editable installs of ukernel/
# silently strip the kernelspec (share/jupyter/kernels/ucore/) and the
# server-extension config (etc/jupyter/jupyter_server_config.d/ukernel.json).
# This script does the editable install and then re-stages those data-files
# into the active venv so JupyterLab finds the kernel and the /ucore/* HTTP
# routes resolve.
#
# Usage:
#   scripts/dev-install.sh           # editable install of ukernel + uagent
#   scripts/dev-install.sh ukernel   # just one
#
# Idempotent — safe to re-run after edits.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [[ -z "${VIRTUAL_ENV:-}" ]]; then
    if [[ -d "$REPO_ROOT/.venv" ]]; then
        # shellcheck disable=SC1091
        source "$REPO_ROOT/.venv/bin/activate"
    else
        echo "no active venv and no .venv/ in repo root — activate one first" >&2
        exit 1
    fi
fi

PREFIX="$(python -c 'import sys; print(sys.prefix)')"
echo "→ venv: $PREFIX"

targets=("$@")
if [[ ${#targets[@]} -eq 0 ]]; then
    targets=(ukernel uagent)
fi

for pkg in "${targets[@]}"; do
    echo "→ pip install -e ./$pkg"
    pip install -e "./$pkg"
done

# Re-stage ukernel data-files that editable installs drop on the floor.
if printf '%s\n' "${targets[@]}" | grep -qx ukernel; then
    echo "→ registering kernelspec (rewrites argv[0] to $(command -v python))"
    python -m ukernel install

    echo "→ staging server-extension config"
    install -D -m 0644 \
        "$REPO_ROOT/ukernel/jupyter-config/jupyter_server_config.d/ukernel.json" \
        "$PREFIX/etc/jupyter/jupyter_server_config.d/ukernel.json"
fi

echo "✓ dev install complete"
