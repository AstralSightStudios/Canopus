#!/usr/bin/env python3
"""Promote an ensemble match into a curated target symbol pack.

Candidate addresses replace addresses for existing active records, while the
production record remains authoritative for policy, ownership, forbidden and
withdrawn state. New candidates are recorded as restricted/pending until a
semantic contract exists. This prevents a matcher from silently changing a
callability decision while still making its target address result reproducible.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def ids(value: object) -> list[str]:
    if not isinstance(value, list):
        return []
    return [item for item in value if isinstance(item, str)]


def normalize_record(record: dict) -> None:
    proof = record.setdefault("proof", {})
    if proof.get("static") not in {"candidate", "recovered", "confirmed"}:
        proof["static"] = "candidate"
    ownership = record.get("ownership")
    if isinstance(ownership, dict):
        record["ownership"] = {
            key: value
            for key, value in ownership.items()
            if key in {"argument", "callback_argument", "return_value"}
        }


def add_ids(record: dict, evidence_id: str) -> None:
    proof = record.setdefault("proof", {})
    provenance = record.setdefault("provenance", {})
    proof_ids = ids(proof.get("evidence_ids"))
    provenance_ids = ids(provenance.get("evidence_ids"))
    if evidence_id not in proof_ids:
        proof_ids.append(evidence_id)
    if evidence_id not in provenance_ids:
        provenance_ids.append(evidence_id)
    proof["evidence_ids"] = proof_ids
    provenance["evidence_ids"] = provenance_ids
def load_dir(path: Path) -> dict[str, tuple[Path, dict]]:
    result = {}
    for file in sorted(path.glob("*.json")):
        record = json.loads(file.read_text())
        name = record.get("name")
        if not isinstance(name, str) or not name:
            raise SystemExit(f"record has no name: {file}")
        if name in result:
            raise SystemExit(f"duplicate symbol name {name!r} under {path}")
        result[name] = (file, record)
    return result


def promote(
    candidate_dir: Path,
    production_dir: Path,
    target_id: str,
    firmware_sha256: str,
    evidence_id: str,
) -> tuple[int, int, int]:
    candidates = load_dir(candidate_dir)
    production = load_dir(production_dir)
    updated = 0
    added = 0
    protected = 0

    for name, (_, candidate) in candidates.items():
        if candidate.get("target_id") != target_id:
            raise SystemExit(f"candidate {name} has wrong target_id")
        if candidate.get("provenance", {}).get("firmware_sha256") != firmware_sha256:
            raise SystemExit(f"candidate {name} has wrong firmware SHA-256")

        if name in production:
            path, record = production[name]
            normalize_record(record)
            status = record.get("status")
            policy = record.get("policy")
            if status in {"FORBIDDEN", "WITHDRAWN"} or policy in {"forbidden", "withdrawn"}:
                # A candidate must never resurrect a permanently denied symbol.
                protected += 1
            else:
                if candidate.get("entry_address"):
                    record["entry_address"] = candidate["entry_address"]
                if candidate.get("callable_address"):
                    record["callable_address"] = candidate["callable_address"]
                add_ids(record, evidence_id)
                record["notes"] = (
                    (record.get("notes", "").rstrip() + " ")
                    + "Address refreshed by trusted-036 caller-neighborhood ensemble v2;"
                    " semantic policy retained from the curated 043 record."
                ).strip()
                updated += 1
            path.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n")
            continue

        # The matcher found a source symbol that had no curated 043 semantic
        # contract yet. Record it, but keep it restricted and pending.
        record = json.loads(json.dumps(candidate))
        normalize_record(record)
        record["status"] = "STATIC_RECOVERED"
        record["proof"]["static"] = "recovered"
        record["proof"]["device"] = "not_probed"
        record["policy"] = "restricted"
        record["approval_state"] = "PENDING"
        add_ids(record, evidence_id)
        record["notes"] = (
            "Promoted from ensemble v2 as a restricted address record; "
            "semantic contract and public callable approval remain pending."
        )
        out = production_dir / next(
            file.name for file in candidate_dir.glob("*.json")
            if json.loads(file.read_text()).get("name") == name
        )
        out.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n")
        added += 1

    return updated, added, protected


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate-dir", required=True, type=Path)
    parser.add_argument("--production-dir", required=True, type=Path)
    parser.add_argument("--target-id", required=True)
    parser.add_argument("--firmware-sha256", required=True)
    parser.add_argument("--evidence-id", required=True)
    args = parser.parse_args()
    updated, added, protected = promote(
        args.candidate_dir,
        args.production_dir,
        args.target_id,
        args.firmware_sha256,
        args.evidence_id,
    )
    print(
        f"promoted {updated} existing addresses, added {added} restricted records, "
        f"protected {protected} forbidden/withdrawn records"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
