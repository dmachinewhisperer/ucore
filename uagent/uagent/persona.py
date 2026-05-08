# uagent persona — registers with Jupyter AI as an ACP persona.

import os
import sys

from jupyter_ai_persona_manager import PersonaDefaults
from jupyter_ai_acp_client.base_acp_persona import BaseAcpPersona


class UCorePersona(BaseAcpPersona):
    def __init__(self, *args, **kwargs):
        executable = [sys.executable, "-m", "uagent"]
        super().__init__(*args, executable=executable, **kwargs)

        # Forward the notebook root to the agent subprocess so it can discover
        # per-project skill overrides at <root>/.ucore/skills/. Setting the env
        # var on the parent process is enough — BaseAcpPersona's
        # _init_agent_subprocess inherits parent env when no override is given,
        # and the subprocess task hasn't run yet at this point in __init__.
        try:
            root_dir = self.parent.root_dir
        except AttributeError:
            root_dir = None
        if root_dir:
            os.environ["UAGENT_NOTEBOOK_ROOT"] = str(root_dir)

    @property
    def defaults(self) -> PersonaDefaults:
        avatar_path = str(os.path.abspath(
            os.path.join(os.path.dirname(__file__), "static", "uagent.svg")
        ))

        return PersonaDefaults(
            name="uagent",
            description="AI agent for ESP32 development — writes and runs MicroPython and Python code in your notebook.",
            avatar_path=avatar_path,
            system_prompt="unused",
        )
