# uagent CLI — launches the ACP agent over stdio.
# invoked as: python -m uagent

import asyncio
import logging

import acp

from .acp_agent import UCoreAcpAgent

logging.basicConfig(level=logging.INFO, format="%(name)s: %(message)s")


async def main():
    agent = UCoreAcpAgent()
    await acp.run_agent(agent)


if __name__ == "__main__":
    asyncio.run(main())
