import importlib.util
from pathlib import Path
import unittest

MODULE = Path(__file__).resolve().parents[1] / "emu/run_services_test.py"
spec = importlib.util.spec_from_file_location("services_runner", MODULE)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ServicesReportTests(unittest.TestCase):
    def test_accepts_consistent_report_with_explicit_skips(self):
        report = "[PASS] one\n[PASS] two\n[SKIP] absent\nSUMMARY: 2/3 PASSED, FAILS=0, SKIPS=1\n"
        probe = "TESTS_COMPLETE:2/3 SKIPS=1\n"
        self.assertEqual(runner.validate_report(report, probe, {"absent"}), (2, 3, 1))

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
