#!/usr/bin/env python3
"""Verify fw-match output against target-pack ground truth.

The source target pack (e.g. 036) has symbols with addresses. The target pack
(e.g. 030) records the *same semantic names* at its own addresses. Where a
name exists in BOTH packs, the target-pack address is ground truth for "the
same function, moved". This script scores the matcher's `confirmed` and `matches`
outputs against that ground truth.

Usage:
    verify_match.py --matches <matches.json> \
        --source-symbols <source-target/symbols> \
        --target-symbols <target-target/symbols>

Prints a precision/recall table. Fails non-zero when recall of confirmed
matches falls below a threshold (default 0.85) so CI can gate on it.
"""

import argparse
import json
import sys
from pathlib import Path


def load_symbols(symbols_dir: Path) -> dict[str, int]:
    """name -> entry_address (int) for function-kind records."""
    out = {}
    for p in sorted(symbols_dir.glob("*.json")):
        rec = json.loads(p.read_text())
        if rec.get("kind") != "function":
            continue
        name = rec.get("name")
        entry = rec.get("entry_address")
        if name and entry:
            out[name] = int(entry, 16)
    return out


def norm_addr(s: str | None) -> int | None:
    if not s:
        return None
    try:
        return int(s, 16)
    except ValueError:
        return None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--matches", required=True, type=Path)
    parser.add_argument("--source-symbols", required=True, type=Path)
    parser.add_argument("--target-symbols", required=True, type=Path)
    parser.add_argument("--min-recall", type=float, default=0.85)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    matches = json.loads(args.matches.read_text())
    src = load_symbols(args.source_symbols)
    dst = load_symbols(args.target_symbols)

    # Ground truth: names present in both packs.
    shared = sorted(set(src) & set(dst))
    matched_any = {m["name"]: m for m in matches["matches"]}
    confirmed = {c["name"]: c for c in matches.get("confirmed", [])}

    n_shared = len(shared)
    n_confirmed = len(confirmed)
    n_confirmed_shared = len([n for n in shared if n in confirmed])

    correct_any = 0      # best candidate == ground truth
    correct_confirmed = 0  # confirmed match == ground truth
    wrong_any = 0
    unmatched = 0

    detail = []
    for name in shared:
        gt = dst[name]
        m = matched_any.get(name)
        if m is None or m.get("target_addr") is None:
            unmatched += 1
            detail.append((name, f"0x{gt:x}", None, "no-candidate", 0.0))
            continue
        pred_int = norm_addr(m["target_addr"])
        if pred_int == gt:
            correct_any += 1
        else:
            wrong_any += 1
        if name in confirmed:
            c = confirmed[name]
            c_pred = norm_addr(c["target_addr"])
            if c_pred == gt:
                correct_confirmed += 1
            detail.append((name, f"0x{gt:x}", m["target_addr"],
                           "confirmed" if c_pred == gt else "confirmed-wrong",
                           c.get("score", 0.0)))
        else:
            detail.append((name, f"0x{gt:x}", m["target_addr"], "not-confirmed",
                           m.get("score", 0.0)))

    recall_any = correct_any / n_shared if n_shared else 1.0
    recall_confirmed = correct_confirmed / n_confirmed if n_confirmed else 0.0
    precision_confirmed = (
        correct_confirmed / n_confirmed if n_confirmed else 1.0
    )

    report = {
        "shared_names": n_shared,
        "confirmed_total": n_confirmed,
        "confirmed_shared": n_confirmed_shared,
        "recall_any_candidate": recall_any,
        "recall_confirmed": recall_confirmed,
        "precision_confirmed": precision_confirmed,
        "unmatched": unmatched,
        "wrong_candidates": wrong_any,
    }

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print("ground-truth verification")
        print(f"  names in both packs : {n_shared}")
        print(f"  confirmed total     : {n_confirmed} "
              f"(shared: {n_confirmed_shared})")
        print(f"  recall (any cand)   : {recall_any:.1%} ({correct_any}/{n_shared})")
        print(f"  recall (confirmed)  : {recall_confirmed:.1%} "
              f"({correct_confirmed}/{n_confirmed})")
        print(f"  precision (confirmed): {precision_confirmed:.1%}")
        print(f"  unmatched           : {unmatched}")
        print(f"  wrong candidates    : {wrong_any}")
        if not args.json:
            for name, gt, pred, verdict, score in detail:
                flag = "OK " if (pred and pred == gt) else "XX "
                print(f"  {flag}{name}: gt={gt} pred={pred} [{verdict}] {score:.2f}")

    if recall_confirmed < args.min_recall:
        print(f"FAIL: confirmed recall {recall_confirmed:.1%} < {args.min_recall:.1%}",
              file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
