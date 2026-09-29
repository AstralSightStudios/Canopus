"""P65 experimental overlay and resident modlib lifecycle regression tests."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "p65_pack", ROOT / "scripts/generate_p65_experimental_pack.py")
PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK)


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
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            *[f"-I{path}" for path in includes], str(source),
                            "-o", str(tmp / "test")], check=True)
            subprocess.run([str(tmp / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
