"""Exercise the loader generator CLI with exact target profiles."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
BAND9 = "xiaomi-band-9-3.1.32"
BAND9_PRO = "xiaomi-band-9-pro-3.1.175"


class LoaderConfigTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="canopus-loader-test-")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.header = self.directory / "generated" / "loader.h"
        self.lua = self.directory / "generated" / "loader.lua"

    def profile(self, target_id):
        return (ROOT / "targets" / target_id / "loader/bootstrap.toml").read_text()

    def generate(self, target_id, source=None):
        profile = self.directory / "bootstrap.toml"
        profile.write_text(self.profile(target_id) if source is None else source)
        return subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts/generate_band9_loader_config.py"),
                "--profile", str(profile),
                "--target-toml", str(ROOT / "targets" / target_id / "target.toml"),
                "--header", str(self.header),
                "--lua", str(self.lua),
            ],
            capture_output=True,
            text=True,
        )

    def assert_rejected(self, result, reason):
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(reason, result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse(self.header.exists(), "rejected profile emitted a C header")
        self.assertFalse(self.lua.exists(), "rejected profile emitted Lua config")

    def test_band9_generates_header_and_lua(self):
        result = self.generate(BAND9)
        self.assertEqual(result.returncode, 0, result.stderr)
        header = self.header.read_text()
        lua = self.lua.read_text()
        self.assertIn("#define CANOPUS_FW_KMEM_MALLOC UINT32_C(0x0c16af05)", header)
        self.assertIn("#define CANOPUS_BAND9_CAVE_RESULT UINT32_C(0x200cb440)", header)
        self.assertIn(f"target_id = '{BAND9}'", lua)
        self.assertIn("kmem_malloc = 0x0c16af05", lua)
        self.assertIn("cave_result = 0x200cb440", lua)
        self.assertIn("cave_original_known = false", lua)

    def test_band9_pro_pending_profile_is_rejected(self):
        # Its legacy cave evidence does not supply current stage0/Kmem inputs.
        self.assert_rejected(self.generate(BAND9_PRO), "loader profile is PENDING")

    def test_pending_complete_profile_is_rejected(self):
        source = self.profile(BAND9).replace(
            'status = "STATIC_RECOVERED"', 'status = "PENDING"', 1
        )
        self.assert_rejected(self.generate(BAND9, source), "loader profile is PENDING")

    def test_promoting_legacy_profile_does_not_bypass_missing_stage0(self):
        source = self.profile(BAND9_PRO).replace(
            'status = "PENDING"', 'status = "STATIC_RECOVERED"', 1
        )
        self.assert_rejected(
            self.generate(BAND9_PRO, source), "missing required tables: stage0"
        )

    def test_wrong_target_is_rejected(self):
        self.assert_rejected(
            self.generate(BAND9, self.profile(BAND9_PRO)), "target_id does not match"
        )

    def test_wrong_firmware_hash_is_rejected(self):
        source = self.profile(BAND9).replace('firmware_sha256 = "', 'firmware_sha256 = "bad-', 1)
        self.assert_rejected(self.generate(BAND9, source), "firmware_sha256 does not match")

    def test_result_word_inside_executable_span_is_rejected(self):
        source = self.profile(BAND9).replace("result_word = 0x200CB440", "result_word = 0x200CB420", 1)
        self.assert_rejected(
            self.generate(BAND9, source), "stage0 must be an aligned region containing result_word"
        )


if __name__ == "__main__":
    unittest.main()
