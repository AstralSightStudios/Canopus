"""Locate firmware images for tests and tools (refactor-plan P1.7, K6).

Firmware is never stored in Canopus. Search order for `<target_id>/<name>`:
  1. $CANOPUS_FW_DIR/<target_id>/<name>
  2. <repo>/fwbins/<target_id>/<name>
Evidence records refer to images as fw://<target_id>/<name>.
"""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def candidates(target_id, name="vela_ap.bin"):
    env = os.environ.get("CANOPUS_FW_DIR")
    if env:
        yield Path(env) / target_id / name
    yield ROOT / "fwbins" / target_id / name


def locate(target_id, name="vela_ap.bin"):
    """Path of the image, or None when it is not available."""
    return next((p for p in candidates(target_id, name) if p.is_file()), None)


def require(target_id, name="vela_ap.bin"):
    """Path of the image; raises unittest.SkipTest when it is missing."""
    path = locate(target_id, name)
    if path is None:
        import unittest
        raise unittest.SkipTest(f"firmware image fw://{target_id}/{name} not available")
    return path


def resolve_uri(uri):
    """fw://<target_id>/<name> -> Path or None."""
    if not uri.startswith("fw://"):
        return None
    target_id, _, name = uri[5:].partition("/")
    return locate(target_id, name)
