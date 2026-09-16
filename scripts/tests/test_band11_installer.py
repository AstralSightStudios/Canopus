"""Regression checks for the embedded recovery bundle and real Lua C frames."""
import importlib.util
import json
import os
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
        self.assertEqual((builder.FAMILY / "main.lua").read_bytes(),
                         builder.text_bytecode_wrapper(builder.lua_bytecode_entry(source)))
        self.assertEqual(profile["loader_status"], "STATIC_TEST_CANDIDATE")
        self.assertEqual(profile["device_status"], "NOT_PROBED")
        self.assertNotIn("-- @CANOPUS_", source)
        with self.assertRaises(ValueError):
            builder.render("xiaomi-band-11-4.100.108")

    def test_native_bindings_select_separate_payloads_in_one_watchface(self):
        for target in builder.TARGETS:
            directory = ROOT / 'targets' / target
            self.assertEqual((directory / 'generated/canopus_band11_addresses.h').read_text(),
                             builder.native_config.render(target))
            _, _, addresses = builder.native_config.metadata(target)
            expected_insert = 0x0c8f2bb1 if target.endswith('.139') else 0x0c8f2ba1
            self.assertEqual(addresses['FW_NOTIFICATION_INSERT'], expected_insert)
            resources = builder.resource_directory(target)
            if not (resources / 'main.lua').exists():
                continue
            source, _ = builder.render(target)
            self.assertEqual((resources / 'main.lua').read_bytes(),
                             builder.text_bytecode_wrapper(builder.lua_bytecode_entry(source)))
            self.assertEqual(resources, builder.FAMILY)
            expected = {'main.lua', 'manager_icon.bin'} | {
                name for item in builder.TARGETS for name in builder.resource_names(item)}
            self.assertEqual({p.name for p in resources.iterdir() if p.is_file()}, expected)
            archive = resources / 'build/canopus-installer-prod-xiaomi-band-11.zip'
            with zipfile.ZipFile(archive) as bundle:
                self.assertEqual(set(bundle.namelist()), expected)
                for name in expected:
                    self.assertEqual(bundle.read(name), (resources / name).read_bytes())

    def test_text_wrapper_roundtrip_and_format_rejection(self):
        lua = builder.FAMILY / "build/host-lua54/lua-5.4.0/src/lua"
        payload = builder.lua_bytecode_entry("local secret_name = ...; return secret_name, 42")
        self.assertNotIn(b"secret_name", payload)
        with tempfile.TemporaryDirectory(prefix="canopus-wrapper-test-") as out:
            path = Path(out) / "main.lua"
            path.write_bytes(builder.text_bytecode_wrapper(payload))
            script = "local f=assert(loadfile(arg[1], 't')); local a,b=f('ok'); assert(a=='ok' and b==42)"
            # -e argument handling varies; run a separate test driver.
            driver = Path(out) / "test.lua"
            driver.write_text(script)
            subprocess.run([str(lua), str(driver), str(path)], check=True)
            path.write_bytes(builder.text_bytecode_wrapper(payload[:4] + b"X" + payload[5:]))
            result = subprocess.run([str(lua), str(driver), str(path)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("version mismatch", result.stderr)
            path.write_bytes(builder.text_bytecode_wrapper(payload[:35]))
            result = subprocess.run([str(lua), str(driver), str(path)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_flat_resource_root_and_archive(self):
        names = {p.name for p in builder.FAMILY.iterdir() if p.is_file()}
        self.assertEqual({n for n in names if not n.endswith(".bin")}, {"main.lua"})
        expected = {"main.lua", "manager_icon.bin"} | {
            name for target in builder.TARGETS for name in builder.resource_names(target)}
        if not expected <= names:
            self.skipTest("build Band 11 resources to check binaries and ZIP")
        self.assertEqual(names, expected)
        archive = builder.FAMILY / "build/canopus-installer-prod-xiaomi-band-11.zip"
        with zipfile.ZipFile(archive) as bundle:
            self.assertEqual(set(bundle.namelist()), expected)
            for name in expected:
                self.assertEqual(bundle.read(name), (builder.FAMILY / name).read_bytes())

    def test_native_lua_orchestration(self):
        if not shutil.which("lua") or not (builder.FAMILY / f"canopus_stage1-{builder.TARGET}.bin").exists():
            self.skipTest("Lua and built Band 11 resources required")
        for target in builder.TARGETS:
            if (builder.resource_directory(target) / f'canopus_stage1-{target}.bin').exists():
                subprocess.run(["lua", "scripts/lua/test_band11_native_loader.lua", target], cwd=ROOT, check=True)

    def test_arm_bootstrap_when_emulator_is_installed(self):
        python = ROOT / "build/band11-tests/bin/python"
        if not python.exists() or not (builder.FAMILY / f"canopus_stage1-{builder.TARGET}.bin").exists():
            self.skipTest("optional local Unicorn environment and native resources required")
        subprocess.run([str(python), "scripts/tests/band11_arm_bootstrap.py"], cwd=ROOT, check=True)
        subprocess.run([str(python), "scripts/tests/band11_pmain_firmware.py"], cwd=ROOT, check=True)
        subprocess.run([str(python), "scripts/tests/band11_firmware_integration.py"], cwd=ROOT, check=True)
        subprocess.run([str(python), "scripts/tests/band11_notification_firmware.py"], cwd=ROOT, check=True)
        subprocess.run([str(python), "scripts/tests/band11_native_ui_firmware.py"], cwd=ROOT, check=True)
        subprocess.run([str(python), "scripts/tests/band11_bluetooth_firmware.py"], cwd=ROOT, check=True)
        if (builder.resource_directory(builder.TARGETS[1]) / 'main.lua').exists():
            subprocess.run([str(python), "scripts/tests/band11_155_firmware.py"], cwd=ROOT, check=True)

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
            source_path = Path(out) / "assembled.lua"
            source_path.write_text(builder.render()[0])
            template = ROOT / "scripts/templates/band11_bytecode_entry.lua"
            for mode in ("built", "fresh"):
                for target in builder.TARGETS:
                    for protection in ([], [str(template)]):
                        subprocess.run(["lua", "scripts/lua/test_band11_execute_recovery.lua",
                                        library, mode, str(source_path), *protection],
                                       cwd=ROOT, check=True,
                                       env=dict(os.environ, CANOPUS_TEST_BAND11_VERSION=builder.metadata(target)['firmware_version']))


if __name__ == "__main__":
    unittest.main()
