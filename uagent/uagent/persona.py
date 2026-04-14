# uagent persona — registers with Jupyter AI as an ACP persona.

import os
import sys

from jupyter_ai_persona_manager import PersonaDefaults
from jupyter_ai_acp_client.base_acp_persona import BaseAcpPersona


class UCorePersona(BaseAcpPersona):
    def __init__(self, *args, **kwargs):
        executable = [sys.executable, "-m", "uagent"]
        super().__init__(*args, executable=executable, **kwargs)

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
