import importlib.util
from pathlib import Path
import unittest

MODULE = Path(__file__).resolve().parents[1] / "emu/run_services_test.py"
spec = importlib.util.spec_from_file_location("services_runner", MODULE)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

BAND11_MODULE = MODULE.with_name("band11_smoke.py")
band11_spec = importlib.util.spec_from_file_location("band11_services_runner", BAND11_MODULE)
band11_runner = importlib.util.module_from_spec(band11_spec)
band11_spec.loader.exec_module(band11_runner)


class ServicesReportTests(unittest.TestCase):
    def test_accepts_consistent_report_with_explicit_skips(self):
        report = "[PASS] one\n[PASS] two\n[SKIP] absent\nSUMMARY: 2/3 PASSED, FAILS=0, SKIPS=1\n"
        probe = "TESTS_COMPLETE:2/3 SKIPS=1\n"
        self.assertEqual(runner.validate_report(report, probe, {"absent"}), (2, 3, 1))

    def test_band11_accepts_complete_report_without_skips(self):
        report = "[PASS] Timers\n[PASS] Filesystem\nSUMMARY: 2/2 PASSED, FAILS=0, SKIPS=0\n"
        probe = "TESTS_COMPLETE:2/2 SKIPS=0\n"
        self.assertEqual(band11_runner.validate_report(report, probe), (2, 2, 0))

    def test_band11_rejects_old_timer_skip(self):
        report = ("[PASS] Filesystem\n"
                  "[SKIP] Timers: no approved platform event source\n"
                  "SUMMARY: 1/2 PASSED, FAILS=0, SKIPS=1\n")
        probe = "TESTS_COMPLETE:1/2 SKIPS=1\n"
        with self.assertRaises(RuntimeError):
            band11_runner.validate_report(report, probe)

    def test_rejects_failed_stale_incomplete_and_unexpected_skip_reports(self):
        report = "[PASS] one\n[SKIP] absent\nSUMMARY: 1/2 PASSED, FAILS=0, SKIPS=1\n"
        probe = "TESTS_COMPLETE:1/2 SKIPS=1\n"
        for candidate, result, allowed in (
            (report, "ALL_TESTS_PASS:18/18\n", {"absent"}),
            (report, "TESTS_FAILED:1/2 SKIPS=1\n", {"absent"}),
            (report.replace("FAILS=0", "FAILS=1"), probe, {"absent"}),
            (report.replace("[PASS] one\n", ""), probe, {"absent"}),
            (report, probe.replace("1/2", "2/2"), {"absent"}),
            (report, probe, set()),
            (report + "[FAIL] hidden\n", probe, {"absent"}),
        ):
            with self.subTest(report=candidate, probe=result, allowed=allowed):
                with self.assertRaises(RuntimeError):
                    runner.validate_report(candidate, result, allowed)


if __name__ == "__main__":
    unittest.main()
