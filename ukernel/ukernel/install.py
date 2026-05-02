"""Install the µcore kernelspec, rewriting argv[0] to sys.executable.

The wheel ships a kernelspec at ``<prefix>/share/jupyter/kernels/ucore/``
with a literal ``"python3"`` in argv. That works in any venv whose
``python3`` resolves to the right interpreter (the common case). For
pyenv shims, conda envs not on PATH, or system-wide installs hitting the
wrong Python, run::

    python -m ukernel install            # current sys.prefix
    python -m ukernel install --user     # ~/.local/share/jupyter
    python -m ukernel install --prefix /opt/foo

This rewrites argv[0] to the absolute path of the Python that ran the
install command, binding the kernelspec to that interpreter.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import sys
import tempfile

from jupyter_client.kernelspec import KernelSpecManager

KERNEL_NAME = "ucore"


def _shipped_spec_dir() -> pathlib.Path:
    """Locate the kernelspec directory installed alongside this package."""
    candidate = pathlib.Path(sys.prefix) / "share" / "jupyter" / "kernels" / KERNEL_NAME
    if candidate.is_dir() and (candidate / "kernel.json").is_file():
        return candidate
    raise SystemExit(
        f"could not find shipped kernelspec under {candidate}. "
        "Reinstall the ukernel wheel to recover."
    )


def install(user: bool = False, prefix: str | None = None) -> str:
    # Default target is the active interpreter's prefix — matches user
    # intent when this command is run inside a venv. KernelSpecManager
    # would otherwise fall back to /usr/local/share/jupyter (root-only).
    if not user and prefix is None:
        prefix = sys.prefix

    src = _shipped_spec_dir()
    with tempfile.TemporaryDirectory() as td:
        staging = pathlib.Path(td)
        for f in src.iterdir():
            shutil.copy2(f, staging / f.name)
        spec_path = staging / "kernel.json"
        spec = json.loads(spec_path.read_text())
        spec["argv"][0] = sys.executable
        spec_path.write_text(json.dumps(spec, indent=2))
        return KernelSpecManager().install_kernel_spec(
            str(staging),
            kernel_name=KERNEL_NAME,
            user=user,
            prefix=prefix,
            replace=True,
        )


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        prog="python -m ukernel install",
        description="Register the µcore kernelspec bound to this Python interpreter.",
    )
    target = parser.add_mutually_exclusive_group()
    target.add_argument(
        "--user", action="store_true",
        help="Install to the per-user Jupyter data dir (~/.local/share/jupyter on Linux).",
    )
    target.add_argument(
        "--prefix", metavar="PATH",
        help="Install under PATH/share/jupyter/kernels/ instead of sys.prefix.",
    )
    args = parser.parse_args(argv)

    dest = install(user=args.user, prefix=args.prefix)
    print(f"Installed µcore kernelspec at: {dest}")
    print(f"  argv[0] = {sys.executable}")


if __name__ == "__main__":
    main()
