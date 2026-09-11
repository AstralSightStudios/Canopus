"""Regression checks for the embedded recovery bundle and real Lua C frames."""
import importlib.util
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("band11_builder", ROOT / "scripts/build_band11_installer.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class Band11InstallerTests(unittest.TestCase):
    def test_embedded_entry_is_current_and_device_is_pending(self):
        source, profile = builder.render()
        self.assertEqual((builder.FAMILY / "main.lua").read_text(), source)
        self.assertEqual(profile["loader_status"], "STATIC_TEST_CANDIDATE")
        self.assertEqual(profile["device_status"], "NOT_PROBED")
        self.assertNotIn("-- @CANOPUS_", source)
        with self.assertRaises(ValueError):
            builder.render("xiaomi-band-11-4.100.108")

    def test_flat_resource_root_and_archive(self):
        names = {p.name for p in builder.FAMILY.iterdir() if p.is_file()}
        self.assertEqual({n for n in names if not n.endswith(".bin")}, {"main.lua"})
        expected = {"main.lua", "manager_icon.bin"} | {
            f"canopus_{kind}-{builder.TARGET}.bin" for kind in
            ("loader_profile", "stage1", "stage2", "supervisor")}
        if not expected <= names:
            self.skipTest("build Band 11 resources to check binaries and ZIP")
        self.assertEqual(names, expected)
        archive = builder.FAMILY / "build" / builder.TARGET / f"canopus-installer-prod-{builder.TARGET}.zip"
        with zipfile.ZipFile(archive) as bundle:
            self.assertEqual(set(bundle.namelist()), expected)
            for name in expected:
                self.assertEqual(bundle.read(name), (builder.FAMILY / name).read_bytes())

    def test_native_lua_orchestration(self):
        if not shutil.which("lua") or not (builder.FAMILY / f"canopus_stage1-{builder.TARGET}.bin").exists():
            self.skipTest("Lua and built Band 11 resources required")
        subprocess.run(["lua", "scripts/lua/test_band11_native_loader.lua"], cwd=ROOT, check=True)

    def test_arm_bootstrap_when_emulator_is_installed(self):
        python = ROOT / "build/band11-tests/bin/python"
        if not python.exists() or not (builder.FAMILY / f"canopus_stage1-{builder.TARGET}.bin").exists():
            self.skipTest("optional local Unicorn environment and native resources required")
        subprocess.run([str(python), "scripts/tests/band11_arm_bootstrap.py"], cwd=ROOT, check=True)
        subprocess.run([str(python), "scripts/tests/band11_pmain_firmware.py"], cwd=ROOT, check=True)
        subprocess.run([str(python), "scripts/tests/band11_firmware_integration.py"], cwd=ROOT, check=True)
        subprocess.run([str(python), "scripts/tests/band11_native_ui_firmware.py"], cwd=ROOT, check=True)
        subprocess.run([str(python), "scripts/tests/band11_bluetooth_firmware.py"], cwd=ROOT, check=True)

    def test_recovered_records_are_not_callable(self):
        directory = ROOT / "targets" / builder.TARGET
        for name in ("register_driver", "open", "close", "read", "write", "lseek", "heap_malloc", "heap_free"):
            record = json.loads((directory / "symbols" / f"{builder.TARGET}.{name}.json").read_text())
            self.assertEqual(record["status"], "STATIC_RECOVERED")
            self.assertEqual(record["policy"], "restricted")
            self.assertEqual(record["approval_state"], "PENDING")
        record = json.loads((directory / "symbols" / f"{builder.TARGET}.insmod.json").read_text())
        self.assertEqual(record["approval_state"], "REJECTED")
        self.assertNotIn("callable_address", record)

    def test_real_lua_c_frames(self):
        if not all(shutil.which(tool) for tool in ("lua", "cc", "pkg-config")):
            self.skipTest("Lua interpreter, C compiler and pkg-config required")
        version = subprocess.check_output(["lua", "-e", "io.write(_VERSION:match('%d+%.%d+'))"], text=True)
        flags = None
        for package in (f"lua{version}", f"lua-{version}", "lua"):
            result = subprocess.run(["pkg-config", "--modversion", package], capture_output=True, text=True)
            if result.returncode == 0 and result.stdout.strip().startswith(version + "."):
                flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", package], text=True))
                break
        if flags is None:
            self.skipTest("development headers matching the Lua interpreter required")
        with tempfile.TemporaryDirectory(prefix="canopus-band11-lua-") as out:
            library = str(Path(out) / "band11_fixture.so")
            subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "scripts/tests/band11_lua_fixture.c"), *flags,
                            "-o", library], check=True)
            for mode in ("built", "fresh"):
                subprocess.run(["lua", "scripts/lua/test_band11_execute_recovery.lua", library, mode], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
