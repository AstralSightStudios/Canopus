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


if __name__ == "__main__":
    unittest.main()
