# single-instance kernel provisioner for ucore.
# ensures all notebooks share one kernel process. first notebook
# launches it, subsequent ones reuse the same zmq sockets.

import json
import os
import signal
import pathlib
import fcntl
from jupyter_client.provisioning import LocalProvisioner

STATE_FILENAME = ".ucore_kernel_state.json"

# Fields that outlive any single kernel process. The user's chosen device,
# pipe-port reservation, etc. must survive shutdown so a fresh kernel can
# read them on the next launch. Lifecycle fields (pid, connection_info,
# clients) get cleared by _clear_lifecycle().
_PERSISTENT_FIELDS = ("selected_device",)


def _state_path():
    return pathlib.Path(__file__).resolve().parent.parent / STATE_FILENAME


def _read_state():
    p = _state_path()
    try:
        with open(p) as f:
            fcntl.flock(f, fcntl.LOCK_SH)
            data = json.load(f)
            fcntl.flock(f, fcntl.LOCK_UN)
            return data
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return None


def _write_state(state):
    """Replace the whole state dict (lifecycle write)."""
    p = _state_path()
    with open(p, "w") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        json.dump(state, f)
        fcntl.flock(f, fcntl.LOCK_UN)


def _merge_state(updates):
    """Read-modify-write merge of `updates` into the state file under
    flock. Safe to call concurrently with the kernel's own writes."""
    p = _state_path()
    p.parent.mkdir(parents=True, exist_ok=True)
    # Open r+ if it exists, else create fresh.
    mode = "r+" if p.exists() else "w+"
    with open(p, mode) as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        try:
            f.seek(0)
            try:
                state = json.load(f)
            except (json.JSONDecodeError, ValueError):
                state = {}
            state.update(updates)
            f.seek(0)
            f.truncate()
            json.dump(state, f)
        finally:
            fcntl.flock(f, fcntl.LOCK_UN)


def _remove_state():
    """Drop lifecycle fields but keep persistent ones (selected_device,
    user preferences). When nothing persistent remains, delete the file."""
    state = _read_state()
    if not state:
        return
    persistent = {k: state[k] for k in _PERSISTENT_FIELDS if k in state}
    if persistent:
        _write_state(persistent)
    else:
        try:
            _state_path().unlink()
        except FileNotFoundError:
            pass


def _is_our_kernel(pid):
    """True iff `pid` is a running ucore kernel process.

    Checks two things:
      1. /proc/<pid>/status exists with a State that isn't Z/X (zombie/dead).
         os.kill(pid, 0) returns success for zombies, which would make us
         "reuse" a corpse and silently send Jupyter the dead kernel's ZMQ
         ports — this was the bug behind the no-output symptom.
      2. /proc/<pid>/cmdline mentions our entry point (`ukernel`). PIDs get
         reused on long-running systems; without this, a stale state file
         can cause us to attach to an unrelated process that happens to
         have the same pid.
    """
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("State:"):
                    state_chr = line.split()[1] if len(line.split()) > 1 else ""
                    if state_chr in ("Z", "X", "x"):
                        return False
                    break
            else:
                return False
        with open(f"/proc/{pid}/cmdline", "rb") as f:
            cmdline = f.read().replace(b"\x00", b" ").decode("utf-8", "replace")
        return "ukernel" in cmdline
    except (FileNotFoundError, ProcessLookupError, OSError):
        return False


# Backwards-compat alias used by older callers.
_pid_alive = _is_our_kernel


class UCoreProvisioner(LocalProvisioner):
    _is_reused = False

    async def pre_launch(self, **kwargs):
        state = _read_state()

        if state and state.get("pid") and _pid_alive(state["pid"]):
            # Kernel already running — override KernelManager's ports
            # BEFORE the base class writes the connection file
            km = self.parent
            if km:
                conn = state["connection_info"]
                km.ip = conn["ip"]
                km.transport = conn["transport"]
                km.shell_port = conn["shell_port"]
                km.iopub_port = conn["iopub_port"]
                km.stdin_port = conn["stdin_port"]
                km.hb_port = conn["hb_port"]
                km.control_port = conn["control_port"]
                km.session.key = conn["key"].encode("utf-8") if isinstance(conn["key"], str) else conn["key"]

            self._reuse_state = state

        return await super().pre_launch(**kwargs)

    async def launch_kernel(self, cmd, **kwargs):
        state = getattr(self, "_reuse_state", None)

        if state and state.get("pid") and _pid_alive(state["pid"]):
            # Attach to existing kernel — don't spawn a new process
            self._is_reused = True
            self.pid = state["pid"]
            self.pgid = state.get("pgid")
            # Return connection_info with key as bytes (Jupyter expects this)
            conn = dict(state["connection_info"])
            if isinstance(conn.get("key"), str):
                conn["key"] = conn["key"].encode("utf-8")
            self.connection_info = conn
            self.process = None

            state["clients"] = state.get("clients", 1) + 1
            _write_state(state)

            return self.connection_info

        # First notebook — launch the kernel process. Merge instead of
        # overwrite so persistent fields (selected_device, etc.) survive.
        conn_info = await super().launch_kernel(cmd, **kwargs)

        serializable_info = {
            k: (v.decode("utf-8") if isinstance(v, bytes) else v)
            for k, v in conn_info.items()
        }

        _merge_state({
            "pid": self.pid,
            "pgid": self.pgid,
            "connection_info": serializable_info,
            "clients": 1,
        })

        return conn_info

    @property
    def has_process(self):
        if self._is_reused:
            return _pid_alive(self.pid)
        return super().has_process

    async def poll(self):
        if self._is_reused:
            return None if _pid_alive(self.pid) else 1
        return await super().poll()

    async def send_signal(self, signum):
        if self._is_reused:
            if signum == signal.SIGINT:
                try:
                    os.kill(self.pid, signum)
                except OSError:
                    pass
            return
        await super().send_signal(signum)

    async def terminate(self, restart=False):
        if self._is_reused:
            await self._decrement_clients()
            return
        if await self._should_keep_alive():
            return
        await super().terminate(restart)
        _remove_state()

    async def kill(self, restart=False):
        if self._is_reused:
            await self._decrement_clients()
            return
        if await self._should_keep_alive():
            return
        await super().kill(restart)
        _remove_state()

    async def cleanup(self, restart=False):
        if self._is_reused:
            return
        await super().cleanup(restart)

    async def _should_keep_alive(self):
        state = _read_state()
        if state and state.get("clients", 1) > 1:
            state["clients"] -= 1
            _write_state(state)
            return True
        return False

    async def _decrement_clients(self):
        state = _read_state()
        if state:
            state["clients"] = max(0, state.get("clients", 1) - 1)
            if state["clients"] == 0:
                try:
                    os.kill(self.pid, signal.SIGTERM)
                except OSError:
                    pass
                _remove_state()
            else:
                _write_state(state)
