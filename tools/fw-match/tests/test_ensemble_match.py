import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "ensemble_match.py"
SPEC = importlib.util.spec_from_file_location("ensemble_match", MODULE_PATH)
assert SPEC and SPEC.loader
MATCHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MATCHER)


def candidate(target_index: int, score: float) -> dict:
    return {
        "target_index": target_index,
        "base": {"base_score": score},
        "fused": {
            "score": score,
            "families": {"bytes": score, "cfg": score, "context": score},
            "independence_groups": ["bytes", "cfg", "context"],
            "graph": {},
        },
    }


class EnsembleFeedbackTests(unittest.TestCase):
    def test_semantic_symbol_name_drives_hard_negative(self) -> None:
        source = {
            "name": "sub_C1000",
            "size": 8,
            "block_offs": [{"off": 0, "size": 8}],
            "succ": [],
            "entry": "00bf00bf00bf00bf",
        }
        targets = [
            {
                "addr": "0x2000",
                "size": 8,
                "block_offs": [{"off": 0, "size": 8}],
                "succ": [],
                "entry": "00bf00bf00bf00bf",
            },
            {
                "addr": "0x3000",
                "size": 8,
                "block_offs": [{"off": 0, "size": 8}],
                "succ": [],
                "entry": "00bf00bf00bf00bf",
            },
        ]
        pool = MATCHER.candidate_pool(
            "core_bt_adapter_instance",
            source,
            targets,
            {},
            8,
            {("core_bt_adapter_instance", 0x2000): "wrong role"},
        )
        addresses = {MATCHER.addr(targets[item["target_index"]]["addr"]) for item in pool}
        self.assertNotIn(0x2000, addresses)
        self.assertIn(0x3000, addresses)

    def test_one_to_one_assignment_never_reuses_a_target(self) -> None:
        targets = [{"addr": "0x1000"}, {"addr": "0x2000"}]
        problems = [
            {
                "symbol": {"name": "strong"},
                "candidates": [candidate(0, 0.95), candidate(1, 0.50)],
            },
            {
                "symbol": {"name": "weak"},
                "candidates": [candidate(0, 0.80), candidate(1, 0.70)],
            },
        ]
        assignments = MATCHER.assign_one_to_one(problems, targets)
        self.assertEqual(assignments["strong"]["selected_index"], 0)
        self.assertEqual(assignments["weak"]["selected_index"], 1)
        self.assertEqual(assignments["weak"]["top_collision_with"], "strong")
        self.assertEqual(MATCHER.margin_at(problems[1]["candidates"], 1), 0.0)
        self.assertEqual(MATCHER.state_at(problems[1]["candidates"], 1), "CANDIDATE")

    def test_feedback_is_auto_discovered_by_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            corpus = root / "targets" / "fw-corpus" / "target.json"
            corpus.parent.mkdir(parents=True)
            corpus.write_text("{}")
            feedback_dir = root / "targets" / "new-target" / "evidence" / "failure-feedback"
            feedback_dir.mkdir(parents=True)
            feedback = {
                "target_id": "new-target",
                "failure_id": "FAIL-1",
                "negative_matches": [
                    {
                        "source_name": "symbol",
                        "target_entry_address": "0x1234",
                        "reason": "runtime disproved role",
                    }
                ],
                "negative_abi_assumptions": [
                    {
                        "role": "descriptor_owner",
                        "expression": "manager+72",
                        "reason": "outside the manager allocation",
                    }
                ],
            }
            (feedback_dir / "failure.json").write_text(json.dumps(feedback))
            documents, negatives = MATCHER.load_failure_feedback([], corpus, "new-target")
            self.assertEqual(len(documents), 1)
            self.assertEqual(
                documents[0]["negative_abi_assumptions"][0]["expression"],
                "manager+72",
            )
            self.assertEqual(negatives[("symbol", 0x1234)], "runtime disproved role")


class EnsembleGlobalTests(unittest.TestCase):
    @staticmethod
    def function(address: str, refs: list[dict] | None = None) -> dict:
        return {
            "addr": address,
            "size": 100,
            "insn": 40,
            "block_offs": [
                {"off": 0, "size": 40},
                {"off": 40, "size": 60},
            ],
            "succ": [[0, 1]],
            "strings": ["unique theme resource"],
            "constants": [12, 24],
            "data_refs": refs or [],
        }

    @staticmethod
    def data_object(address: str, xrefs: list[dict] | None = None) -> dict:
        return {
            "addr": address,
            "segment": "ram",
            "writable": True,
            "size": 12,
            "alignment": 4,
            "xrefs": xrefs or [],
        }

    def run_match(self, target_refs: list[dict], function_results: dict) -> dict:
        source_fn = self.function(
            "0x1000",
            [{"off": 20, "addr": "0x2000", "access": "write"}],
        )
        target_fn = self.function("0x9000", target_refs)
        source_object = self.data_object(
            "0x2000",
            [{"function": "0x1000", "off": 20, "access": "write"}],
        )
        target_objects = [
            self.data_object(ref["addr"])
            for ref in target_refs
        ]
        symbol = {
            "kind": "global",
            "symbol_id": "source.style",
            "target_id": "source",
            "name": "style",
            "entry_address": "0x2000",
            "callable_address": "0x2001",
        }
        results = MATCHER.match_global_symbols(
            [symbol],
            [source_fn],
            [target_fn],
            [source_object],
            target_objects,
            function_results,
            "EVID-TEST",
            "source",
            "0" * 64,
            "1" * 64,
        )
        return results["style"]

    def test_matched_owner_retrieves_global_candidate(self) -> None:
        result = self.run_match(
            [{"off": 20, "addr": "0xa000", "access": "write"}],
            {
                "owner": {
                    "state": "REVIEW_REQUIRED",
                    "source_address": "0x1000",
                    "predicted_address": "0x9000",
                }
            },
        )
        self.assertEqual(result["predicted_address"], "0xa000")
        self.assertEqual(result["state"], "REVIEW_REQUIRED")

    def test_access_mismatch_blocks_candidate(self) -> None:
        result = self.run_match(
            [{"off": 20, "addr": "0xa000", "access": "read"}],
            {
                "owner": {
                    "state": "REVIEW_REQUIRED",
                    "source_address": "0x1000",
                    "predicted_address": "0x9000",
                }
            },
        )
        self.assertIsNone(result["predicted_address"])
        self.assertEqual(result["state"], "BLOCKED")

    def test_unnamed_owner_uses_strict_structural_fallback(self) -> None:
        result = self.run_match(
            [{"off": 20, "addr": "0xa000", "access": "write"}],
            {},
        )
        self.assertEqual(result["predicted_address"], "0xa000")

    def test_missing_source_object_is_reported_blocked(self) -> None:
        symbol = {
            "kind": "global",
            "symbol_id": "source.missing",
            "name": "missing",
            "entry_address": "0x3000",
        }
        results = MATCHER.match_global_symbols(
            [symbol], [], [], [], [], {}, "EVID-TEST", "source", "0" * 64, "1" * 64
        )
        self.assertEqual(results["missing"]["state"], "BLOCKED")
        self.assertIn(
            "source corpus data-object record",
            results["missing"]["required_evidence_missing"],
        )

    def test_global_candidate_has_no_callable_and_protects_promoted_record(self) -> None:
        result = self.run_match(
            [{"off": 20, "addr": "0xa000", "access": "write"}],
            {},
        )
        source = {
            "kind": "global",
            "symbol_id": "source.style",
            "target_id": "source",
            "name": "style",
            "entry_address": "0x2000",
            "callable_address": "0x2001",
        }
        candidate_record = MATCHER.make_global_candidate(
            source, "target", "0xa000", result, "report.json"
        )
        self.assertNotIn("callable_address", candidate_record)
        self.assertEqual(candidate_record["status"], "CANDIDATE")
        self.assertEqual(candidate_record["approval_state"], "PENDING")

        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "symbol.json"
            path.write_text(json.dumps({
                "status": "STATIC_RECOVERED",
                "approval_state": "APPROVED",
            }))
            self.assertFalse(MATCHER.write_candidate_record(path, candidate_record))
            self.assertEqual(json.loads(path.read_text())["status"], "STATIC_RECOVERED")


if __name__ == "__main__":
    unittest.main()
