"""Shared local tool discovery for Canopus emulator workflows.

Explicit environment variables win. A sibling Mirage checkout is the default;
no machine-specific /Volumes paths or changes to Mirage's user preferences.
"""
import json
import os
from pathlib import Path
import shutil
import socket
import time
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MIRAGE_ROOT = Path(os.environ.get("MIRAGE_ROOT", ROOT.parent / "Mirage")).expanduser()
EMU_HOME = Path(os.environ.get("CANOPUS_EMU_HOME", ROOT / ".local/mirage")).expanduser()
LIBRARY = Path(os.environ.get("MIRAGE_LIBRARY",
                                  EMU_HOME / "Library" if os.name == "nt" else MIRAGE_ROOT / ".scratch/Library")).expanduser()
if os.name == "nt":
    os.environ.setdefault("MIRAGE_LIBRARY", str(LIBRARY.resolve()))


def executable(variable, name, candidates):
    override = os.environ.get(variable)
    if override:
        return Path(shutil.which(override) or override).expanduser()
    suffix = ".exe" if os.name == "nt" else ""
    paths = [Path(str(path) + suffix) for path in candidates]
    found = next((path for path in paths if path.is_file()), None)
    return found or Path(shutil.which(name + suffix) or paths[0])


MIRAGECTL = executable("MIRAGECTL", "miragectl", [
    MIRAGE_ROOT / "Packages/MirageKit/.build/debug/miragectl",
    MIRAGE_ROOT / "Packages/MirageKit/.build/release/miragectl",
    MIRAGE_ROOT / "Packages/MirageKit/.build/x86_64-unknown-windows-msvc/debug/miragectl",
    MIRAGE_ROOT / "Packages/MirageKit/.build/x86_64-unknown-windows-msvc/release/miragectl",
    MIRAGE_ROOT / ".build/out/Products/Debug/miragectl",
    MIRAGE_ROOT / ".build/Debug/miragectl",
    MIRAGE_ROOT / ".build/Release/miragectl",
])
if os.name == "nt" and not os.environ.get("MIRAGE_QEMU"):
    for directory in ("qemu-fixed", "qemu"):
        bundled_qemu = ROOT.parent / ".tools/mirage" / directory / "qemu-system-arm.exe"
        if bundled_qemu.is_file():
            os.environ["MIRAGE_QEMU"] = str(bundled_qemu)
            break
BRIDGE = executable("MAB_SMOKE", "smoke", [MIRAGE_ROOT / "AstroBoxBridge/target/smoke"])
PACKAGER = executable("FACETORY_PACKAGER", "package_lua_watchface", [
    ROOT.parent / "Facetory/rust/target/debug/examples/package_lua_watchface",
    ROOT.parent / "Facetory/rust/target/release/examples/package_lua_watchface",
    ROOT.parent / "facetory/rust/target/debug/examples/package_lua_watchface",
    ROOT.parent / "facetory/rust/target/release/examples/package_lua_watchface",
])
CANOPUS = executable("CANOPUS_BIN", "canopus", [ROOT / "target/debug/canopus"])
FW_043 = "miwear.watch.p67tc_v3.101.043_full_a4ce8564"
OTA_043 = Path(os.environ.get("CANOPUS_FW_OTA_DIR", LIBRARY / "Firmwares" / FW_043)).expanduser()


def run_capture(command, *, timeout=None):
    """Capture miragectl without waiting for handles inherited by QEMU on Windows.

    Swift Foundation's Process can let its background child inherit capture
    pipes. File-backed capture waits for miragectl itself rather than pipe EOF.
    """
    if os.name != "nt":
        return subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        result = subprocess.run(command, stdout=out, stderr=err, timeout=timeout)
        out.seek(0)
        err.seek(0)
        return subprocess.CompletedProcess(command, result.returncode,
                                           out.read().decode("utf-8", errors="replace"),
                                           err.read().decode("utf-8", errors="replace"))


class QemuExitCheck:
    """Retain a Windows process handle before shutdown so its exit is observable."""

    def __init__(self, device):
        self.handle = None
        if os.name != "nt":
            return
        import ctypes
        from ctypes import wintypes
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.api.OpenProcess.restype = wintypes.HANDLE
        self.api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.api.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        library = Path(os.environ["MIRAGE_LIBRARY"])
        for config in (library / "Devices").glob("*/device.json"):
            if json.loads(config.read_text(encoding="utf-8"))["name"] == device:
                runtime = json.loads(config.with_name("runtime.json").read_text(encoding="utf-8"))
                self.pid = runtime["pid"]
                self.nor = config.with_name("nor.img")
                self.nor_mtime = self.nor.stat().st_mtime_ns
                self.handle = self.api.OpenProcess(0x00100000 | 0x1000, False, self.pid)
                if not self.handle:
                    raise ctypes.WinError(ctypes.get_last_error())
                return
        raise RuntimeError(f"No running QEMU metadata for {device}")

    def wait(self):
        if self.handle is None:
            return {}
        import ctypes
        from ctypes import wintypes
        try:
            if self.api.WaitForSingleObject(self.handle, 15000) != 0:
                raise RuntimeError(f"QEMU {self.pid} did not exit after shutdown")
            code = wintypes.DWORD()
            if not self.api.GetExitCodeProcess(self.handle, ctypes.byref(code)):
                raise ctypes.WinError(ctypes.get_last_error())
            if code.value:
                raise RuntimeError(f"QEMU {self.pid} exited abnormally: 0x{code.value:08X}")
            written = self.nor.stat().st_mtime_ns > self.nor_mtime
            if not written:
                raise RuntimeError(f"QEMU {self.pid} exited without NOR writeback")
            return {"pid": self.pid, "exit_code": code.value, "nor_written": written}
        finally:
            self.close()

    def close(self):
        if self.handle is not None:
            self.api.CloseHandle(self.handle)
            self.handle = None


def shutdown_checked(miragectl, device, process):
    result = run_capture([str(miragectl), "shutdown", device], timeout=60)
    # miragectl can report success even when its detached QEMU child crashed.
    evidence = process.wait() if process else {}
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    return evidence


def wake_panel(device):
    """Double-tap over one QMP connection, within Zinitix's 500 ms window."""
    library = Path(os.environ["MIRAGE_LIBRARY"])
    config = next((p for p in (library / "Devices").glob("*/device.json")
                   if device in (p.parent.name, json.loads(p.read_text(encoding="utf-8"))["name"])), None)
    if config is None:
        raise RuntimeError(f"No device metadata for {device}")
    runtime = json.loads(config.with_name("runtime.json").read_text(encoding="utf-8"))
    kind, address = runtime["endpoints"]["qmpAux"].split(":", 1)
    if kind == "tcp":
        connection = socket.create_connection(("127.0.0.1", int(address)), timeout=10)
    else:
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        connection.settimeout(10)
        connection.connect(address)
    with connection, connection.makefile("rwb", buffering=0) as stream:
        json.loads(stream.readline())  # QMP greeting

        def send(name, **arguments):
            stream.write((json.dumps({"execute": name, "arguments": arguments}) + "\r\n").encode())
            while True:
                response = json.loads(stream.readline())
                if "error" in response:
                    raise RuntimeError(str(response["error"]))
                if "return" in response:
                    return

        send("qmp_capabilities")
        for _ in range(2):
            send("input-send-event", events=[
                {"type": "abs", "data": {"axis": "x", "value": 16384}},
                {"type": "abs", "data": {"axis": "y", "value": 16384}},
                {"type": "btn", "data": {"button": "left", "down": True}},
            ])
            time.sleep(0.06)
            send("input-send-event", events=[{"type": "btn", "data": {"button": "left", "down": False}}])
            time.sleep(0.06)
    time.sleep(0.8)
