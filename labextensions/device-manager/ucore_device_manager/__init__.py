from pathlib import Path


def _jupyter_labextension_paths():
    return [{"src": str(Path(__file__).parent / "labextension"), "dest": "@ucore/device-manager"}]
