"""Encrypt the fixture's host-address-patched Lua for the real 5.4 C-frame test."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import build_band11_installer as builder
path=Path(sys.argv[1])
path.write_bytes(builder.text_bytecode_wrapper(builder.lua_bytecode_entry(path.read_text())))
