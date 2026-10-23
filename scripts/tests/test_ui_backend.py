#!/usr/bin/env python3
"""Run both real native UI backends with counted host firmware calls.

No firmware, ARM emulator, or external Python dependencies are needed. Set
CANOPUS_UI_BACKEND_SOURCE and CANOPUS_UI_BACKEND_HEADER to matching files from
another revision, with CANOPUS_UI_BACKEND_BASELINE=1 to report its call counts.
CANOPUS_UI_BACKEND_SANITIZE=1 enables ASan/UBSan on the host harness.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class UIBackendTests(unittest.TestCase):
    def test_native_backends(self):
        compiler = os.environ.get("CC", "cc")
        if shutil.which(compiler) is None:
            self.skipTest(f"{compiler} is unavailable")
        source = Path(os.environ.get("CANOPUS_UI_BACKEND_SOURCE",
                                     ROOT / "manager/target/canopus_ui_host.c"))
        header = Path(os.environ.get("CANOPUS_UI_BACKEND_HEADER",
                                     ROOT / "manager/target/canopus_ui_host.h"))
        with tempfile.TemporaryDirectory(prefix="canopus-ui-backend-") as tmp:
            tmp = Path(tmp)
            # The copied source is unchanged; only its target includes resolve
            # to these empty headers. The C harness supplies counted functions.
            backend = tmp / "canopus_ui_host.c"
            shutil.copyfile(source, backend)
            shutil.copyfile(header, tmp / "canopus_ui_host.h")
            for include in ("canopus_ui_input.inc", "canopus_ui_feedback.inc"):
                shutil.copyfile(source.parent / include, tmp / include)
            (tmp / "band11").mkdir()
            for name in ("canopus_target_config.h", "canopus_veneer.h",
                         "band11/canopus_band11_abi.h"):
                (tmp / name).write_text("/* Firmware calls are supplied by the host harness. */\n")
            for family in ("band10", "band11"):
                with self.subTest(family=family):
                    binary = tmp / (family + "_tests")
                    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                               f'-DCANOPUS_UI_HOST_SOURCE="{backend}"',
                               "-I" + str(tmp),
                               "-I" + str(ROOT / "sdk/c"),
                               "-I" + str(ROOT / "manager/target"),
                               str(ROOT / "tests/host/test_ui_backend.c"),
                               str(ROOT / "runtime/ui/canopus_ui.c"), "-o", str(binary)]
                    if family == "band11":
                        command.append("-DCANOPUS_MGR_BAND11=1")
                    if os.environ.get("CANOPUS_UI_BACKEND_SANITIZE") == "1":
                        # Firmware event/object fields are only 4-byte aligned;
                        # the harness adapts their opaque pointers for a 64-bit
                        # host. All other undefined behavior remains checked.
                        command += ["-fsanitize=address,undefined", "-fno-sanitize=alignment"]
                    subprocess.run(command, check=True)
                    run = [str(binary)]
                    if os.environ.get("CANOPUS_UI_BACKEND_BASELINE") == "1":
                        run.append("--baseline")
                    result = subprocess.run(run, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0,
                                     f"{family}:\n{result.stdout}\n{result.stderr}")
                    print(f"{family}:\n{result.stdout}", end="", flush=True)


if __name__ == "__main__":
    unittest.main()
