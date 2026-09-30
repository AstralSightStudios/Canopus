"""P65 experimental overlay and resident modlib lifecycle regression tests."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zipfile
from unittest import mock
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "p65_pack", ROOT / "scripts/generate_p65_experimental_pack.py")
PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK)
INSTALLER_SPEC = importlib.util.spec_from_file_location(
    "p65_installer", ROOT / "scripts/build_p65_installer.py")
INSTALLER = importlib.util.module_from_spec(INSTALLER_SPEC)
INSTALLER_SPEC.loader.exec_module(INSTALLER)


class P65ExperimentalTests(unittest.TestCase):
    def test_overlay_preserves_identity_and_loader_rules(self):
        with tempfile.TemporaryDirectory() as temporary:
            pack = PACK.generate(temporary)
            self.assertEqual((pack / "target.toml").read_bytes(),
                             (ROOT / "targets" / PACK.TARGET / "target.toml").read_bytes())
            manifest = json.loads((pack / "EXPERIMENTAL.json").read_text())
            self.assertFalse(manifest["production_approved"])
            self.assertIn("0xc8bc759", manifest["allowed_addresses"])
            records = list((pack / "symbols").glob("*.experimental_*.json"))
            self.assertEqual(len(records), len(manifest["allowed_addresses"]))
            for path in records:
                record = json.loads(path.read_text())
                self.assertEqual(record["approval_state"], "PENDING")
                self.assertEqual(record["provenance"]["firmware_sha256"], PACK.SHA)
            stale = pack / "symbols/stale.json"
            stale.write_text("{}")
            PACK.generate(temporary)
            self.assertFalse(stale.exists())

    def test_withdrawn_builders_cannot_create_or_replace_resources(self):
        self.assertFalse(INSTALLER.deployment_enabled())
        with tempfile.TemporaryDirectory() as temporary:
            tmp = Path(temporary)
            output = tmp / "output"
            output.mkdir()
            sentinel = output / "canopus-p65-resources.zip"
            sentinel.write_bytes(b"incident evidence: must not overwrite")
            with mock.patch.object(INSTALLER.subprocess, "run") as verify:
                with self.assertRaisesRegex(ValueError, "withdrawn"):
                    INSTALLER.build(tmp / "missing.elf", output, tmp / "targets")
                verify.assert_not_called()
            environment = dict(os.environ, CANOPUS_P65_OUTPUT_DIR=str(output),
                               CANOPUS_P65_FIRMWARE=str(tmp / "missing-ap.bin"))
            for script in ("build_p65_supervisor.sh", "build_canopus_supervisor.sh"):
                result = subprocess.run(["sh", str(ROOT / "scripts" / script)],
                    env=dict(environment, CANOPUS_TARGET=PACK.TARGET),
                    capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("deployment withdrawn", result.stderr)
            self.assertEqual(list(output.iterdir()), [sentinel])
            self.assertEqual(sentinel.read_bytes(), b"incident evidence: must not overwrite")

    def test_installer_packaging_and_lua_control_flow(self):
        with tempfile.TemporaryDirectory() as temporary:
            tmp = Path(temporary)
            elf = bytearray(64)
            elf[:7] = b"\x7fELF\x01\x01\x01"
            struct.pack_into("<HHI", elf, 16, 1, 40, 1)
            struct.pack_into("<I", elf, 24, 1)
            artifact = tmp / "fixture.elf"
            artifact.write_bytes(elf)
            # Host-only policy override: temporary synthetic ELF, no release.
            # Neither the real packager nor its CLI has this override.
            with mock.patch.object(INSTALLER, "deployment_enabled", return_value=True), \
                    mock.patch.object(INSTALLER.subprocess, "run") as verify:
                archive = INSTALLER.build(artifact, tmp / "first", tmp / "targets")
                self.assertEqual(verify.call_count, 1)
                self.assertIn("verify", verify.call_args.args[0])
                second = INSTALLER.build(artifact, tmp / "second", tmp / "targets")
            self.assertEqual(archive.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(archive) as packed:
                self.assertEqual(set(packed.namelist()),
                                 {"main.lua", "supervisor.bin", "manager_icon.bin"})
                self.assertEqual(packed.read("supervisor.bin"), bytes(elf))
                self.assertIn(b"local EXPECTED_SIZE = 64", packed.read("main.lua"))
            manifest = json.loads((archive.parent / "build-info.json").read_text())
            self.assertFalse(manifest["flashable"])
            self.assertEqual(manifest["resources"]["supervisor.bin"]["size"], 64)
            if shutil.which("lua"):
                smoke = ["lua", str(ROOT / "scripts/lua/test_p65_installer.lua"),
                         str(archive.parent / "main.lua")]
                for fault in ["withdrawn", "already_running", "native_client", "withdrawn_diagnostics"]:
                    subprocess.run([*smoke, fault], check=True, capture_output=True, text=True)
                for fault in ["success", "already_running", "native_client", "resource", "version", "getprop",
                              "icon", "insmod", "endpoint", "torn_status", "short_write",
                              "command_result", "registry_result"]:
                    subprocess.run([*smoke, fault, "enabled-model"], check=True,
                                   capture_output=True, text=True)
            # Malformed entry fails before invoking the verifier.
            elf[24] = 0
            artifact.write_bytes(elf)
            with mock.patch.object(INSTALLER, "deployment_enabled", return_value=True), \
                    self.assertRaises(ValueError):
                INSTALLER.build(artifact, tmp / "bad", tmp / "targets")

    def test_rust_facade_retains_band11_names_without_claiming_bluetooth(self):
        directory = ROOT / "sdk/rust/canopus-target-private/src/targets"
        import re
        names = lambda text: set(re.findall(r"pub (?:unsafe )?fn (\w+)", text))
        baseline = names((directory / "xiaomi_band_11_4_100_139.rs").read_text())
        p65 = (directory / "xiaomi_p65_3_100_043.rs").read_text()
        fallback = names((directory / "static_candidate.rs").read_text())
        self.assertFalse(baseline - names(p65) - fallback)
        self.assertIn('pub const EXPERIMENTAL: bool = true', p65)
        capabilities = p65[p65.index('pub fn capabilities()'):p65.index('/* Unlike Band10')]
        self.assertNotIn('"bluetooth"', capabilities)
        self.assertIn('on_ui_destroy) == 100', p65)
        self.assertIn('P65_REGISTER_DRIVER', p65)

    def test_overlay_cannot_overwrite_registered_pack(self):
        with self.assertRaises(ValueError):
            PACK.generate(ROOT / "targets")

    @unittest.skipUnless(shutil.which("cc"), "host C compiler unavailable")
    def test_resident_entry_preserves_escaped_callbacks_on_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            tmp = Path(temporary)
            (tmp / "canopus_veneer.h").write_text(
                "static int identity_result;\n"
                "static int canopus_identity_guard(void) { return identity_result; }\n")
            policy = (ROOT / "manager/target/p65/canopus_p65_safety.h").read_text()
            (tmp / "canopus_p65_safety.h").write_text(policy)
            source = tmp / "test.c"
            source.write_text(r'''
#include <assert.h>
#include <string.h>
#define CANOPUS_SUP_P65_MODLIB 1
#define CANOPUS_SUP_P65_RESIDENT 1
/* Only the target pointer-size assertion is suppressed in this host harness. */
#define _Static_assert(condition, message)
#ifdef __APPLE__
#define section(name) section("__TEXT,__text,regular,pure_instructions")
#endif
#include "manager/service/canopus_supervisor_module.c"
static int register_result, register_calls, unregister_calls;
static int register_device(void *cookie) {
    (void)cookie; register_calls++; return register_result;
}
static int unregister_device(void *cookie) {
    (void)cookie; unregister_calls++; return 0;
}
const struct canopus_sup_platform_v1 canopus_sup_platform = {
    .target_id = "xiaomi-p65-3.100.043",
    .register_device = register_device,
    .unregister_device = unregister_device,
};
int canopus_supervisor_init(struct canopus_supervisor_v1 *sup, uint32_t revision,
    const struct canopus_sup_platform_v1 *platform, void *cookie) {
    (void)revision; (void)platform; (void)cookie;
    memset(sup, 0, sizeof(*sup)); return 0;
}
int canopus_supervisor_restore_registry_metadata(struct canopus_supervisor_v1 *sup) {
    (void)sup; return 0;
}
int canopus_supervisor_activate_restored_modules(struct canopus_supervisor_v1 *sup) {
    (void)sup; return 0;
}
int main(void) {
    struct canopus_p65_modlib_unload_pair pair;
    assert(canopus_supervisor_module_initialize(0) == -1);
    if (!CANOPUS_P65_DEPLOYMENT_ENABLED) {
        pair.callback = canopus_sup_p65_unload;
        pair.context = &pair;
        assert(canopus_supervisor_module_initialize(&pair) == CANOPUS_P65_ERR_DEPLOYMENT_BLOCKED);
        assert(pair.callback == 0 && pair.context == 0);
        assert(register_calls == 0 && unregister_calls == 0);
        assert(g_device_registered == 0);
        return 0;
    }
    identity_result = -1;
    assert(canopus_supervisor_module_initialize(&pair) == -1);
    assert(pair.callback == 0 && register_calls == 0);
    identity_result = 0;
    register_result = -5; /* A fops pointer may already have escaped. */
    assert(canopus_supervisor_module_initialize(&pair) == 0);
    assert(register_calls == 1 && pair.callback != 0);
    assert(pair.callback(pair.context) == -16);
    assert(unregister_calls == 0);
    register_result = 0;
    assert(canopus_supervisor_module_initialize(&pair) == 0);
    assert(pair.callback(pair.context) == -16);
    assert(unregister_calls == 0);
    return 0;
}
''')
            includes = [tmp, ROOT, ROOT / "sdk/c", ROOT / "manager/service",
                        ROOT / "runtime/control", ROOT / "runtime/lifecycle",
                        ROOT / "runtime/module", ROOT / "runtime/resources",
                        ROOT / "manager/protocol"]
            for enabled in (0, 1):
                # In-memory fixture policy only; no target artifact is packaged.
                (tmp / "canopus_p65_safety.h").write_text(policy.replace(
                    "#define CANOPUS_P65_DEPLOYMENT_ENABLED 0",
                    f"#define CANOPUS_P65_DEPLOYMENT_ENABLED {enabled}"))
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                *[f"-I{path}" for path in includes], str(source),
                                "-o", str(tmp / "test")], check=True)
                subprocess.run([str(tmp / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
