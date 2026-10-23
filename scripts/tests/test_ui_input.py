#!/usr/bin/env python3
"""Exercise the production native input include for both target families."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class UIInputTests(unittest.TestCase):
    def test_native_input(self):
        compiler = os.environ.get("CC", "cc")
        if shutil.which(compiler) is None:
            self.skipTest(f"{compiler} is unavailable")
        with tempfile.TemporaryDirectory(prefix="canopus-ui-input-") as temporary:
            for family in ("band10", "band11"):
                with self.subTest(family=family):
                    binary = Path(temporary) / family
                    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                               "-I" + str(ROOT / "sdk/c"),
                               "-I" + str(ROOT / "manager/target"),
                               str(ROOT / "tests/host/test_ui_input.c"),
                               str(ROOT / "runtime/ui/canopus_ui.c"), "-o", str(binary)]
                    if family == "band11":
                        command.append("-DCANOPUS_MGR_BAND11=1")
                    if os.environ.get("CANOPUS_UI_BACKEND_SANITIZE") == "1":
                        command.append("-fsanitize=address,undefined")
                    subprocess.run(command, check=True)
                    result = subprocess.run([str(binary)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0,
                                     f"{family}:\n{result.stdout}\n{result.stderr}")


if __name__ == "__main__":
    unittest.main()
