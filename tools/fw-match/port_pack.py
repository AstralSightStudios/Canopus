#!/usr/bin/env python3
"""Port a reviewed target pack to another build of the same firmware line.

Inputs are the source pack, the ensemble matcher output, both corpora and a small
manual-overrides file. Every function match must be accepted by independent
checks that do not come from the matcher's own score, otherwise the ported record
becomes a CANDIDATE without a callable address:

  content   the normalized Thumb body is equal (fingerprint) or the decompiled
            pseudocode is identical (port_review.py)
  callees   every callee that has a unique full-body anchor maps to the matching
            callee in the target
  support   one of: the pair is itself a mutually unique full-body anchor;
            >= 5 anchored callers of which >= 90 % call the target; the previous
            and next anchors keep their relative distance in functions

Ported records never carry an approval: policy stays restricted, approval_state is
PENDING and device proof is reset. Prototypes, contexts and ownership are copied
from the source record, which is only valid within the audited port scope.

  python tools/fw-match/port_pack.py \
      --source-dir targets/xiaomi-band-11-4.100.155 \
      --target-id xiaomi-band-11-4.100.179 --target-sha256 <sha> \
      --source-corpus ... --target-corpus ... --ensemble ensemble.json \
      --review review.json --overrides overrides.json \
      --evidence-id EVID-PORT-4179-001 --out-dir build/port179/pack --report port.json
"""
import argparse
import bisect
import copy
import glob
import json
import re
from collections import defaultdict
from pathlib import Path

MIN_ANCHOR_INSNS = 8


def addr(text) -> int:
    return int(text, 16) if isinstance(text, str) else text


class Corpus:
    def __init__(self, path):
        data = json.load(open(path))
        self.functions = {addr(f["addr"]): f for f in data["functions"]}
        self.sorted = sorted(self.functions)
        self.by_hash = defaultdict(list)
        for entry, f in self.functions.items():
            fp = f.get("body_fingerprint")
            if fp and fp.get("instructions", 0) >= MIN_ANCHOR_INSNS:
                self.by_hash[fp["sha256"]].append(entry)

    def enclosing(self, address):
        i = bisect.bisect_right(self.sorted, address) - 1
        if i < 0:
            return None
        entry = self.sorted[i]
        return entry if address < entry + self.functions[entry]["size"] else None

    def index(self, entry):
        return bisect.bisect_left(self.sorted, entry)


class Mapper:
    def __init__(self, src: Corpus, dst: Corpus):
        self.src, self.dst = src, dst
        self.anchor = {}
        for h, entries in src.by_hash.items():
            if len(entries) == 1 and len(dst.by_hash.get(h, [])) == 1:
                self.anchor[entries[0]] = dst.by_hash[h][0]
        self.anchors = sorted(self.anchor)

    def neighbourhood(self, s, t):
        fs, ft = self.src.functions[s], self.dst.functions[t]
        callees = [addr(x) for x in fs["callees"] if addr(x) != s]
        known = [x for x in callees if x in self.anchor]
        target_callees = {addr(x) for x in ft["callees"]}
        callees_ok = all(self.anchor[x] in target_callees for x in known)
        callers = [addr(x) for x in fs["callers"]]
        known_callers = [x for x in callers if x in self.anchor]
        target_callers = {addr(x) for x in ft["callers"]}
        hit = sum(1 for x in known_callers if self.anchor[x] in target_callers)
        return {
            "callees_known": len(known),
            "callees_ok": callees_ok,
            "callers_known": len(known_callers),
            "callers_hit": hit,
        }

    def position_ok(self, s, t):
        i = bisect.bisect_left(self.anchors, s)
        if i == 0 or i >= len(self.anchors):
            return False
        prev = self.anchors[i - 1]
        nxt = self.anchors[i] if self.anchors[i] != s else (self.anchors[i + 1] if i + 1 < len(self.anchors) else None)
        if nxt is None:
            return False
        ip, inn = self.anchor[prev], self.anchor[nxt]
        before_s = self.src.index(s) - self.src.index(prev)
        before_t = self.dst.index(t) - self.dst.index(ip)
        after_s = self.src.index(nxt) - self.src.index(s)
        after_t = self.dst.index(inn) - self.dst.index(t)
        return ip < t < inn and before_s == before_t and after_s == after_t


def judge(mapper: Mapper, match, review):
    """(accepted, basis) for one ensemble function match."""
    s = addr(match["source_address"]) & ~1
    t = addr(match["predicted_address"]) & ~1
    if s not in mapper.src.functions or t not in mapper.dst.functions:
        return False, "no function at the source or predicted address"
    top = match["top_candidates"][0]
    body = top.get("full_body_equal") is True
    content = body or review.get("verdict") == "IDENTICAL"
    if not content:
        return False, "body differs (instruction fingerprint and pseudocode)"
    kind = "equal body" if body else "identical pseudocode"
    hood = mapper.neighbourhood(s, t)
    if not hood["callees_ok"]:
        return False, "a known callee maps elsewhere"
    if mapper.anchor.get(s) == t:
        return True, "mutually unique full-body anchor; known callees consistent"
    if hood["callers_known"] >= 5 and hood["callers_hit"] >= 0.9 * hood["callers_known"]:
        return True, (
            f"{kind}; {hood['callers_hit']}/{hood['callers_known']} anchored callers call the target; "
            "known callees consistent"
        )
    if mapper.position_ok(s, t):
        return True, f"{kind}; neighbouring anchors keep their relative function distance; known callees consistent"
    return False, f"{kind} but no caller or positional support"


def thumb(entry):
    return entry | 1


def has_hex_address(text: str) -> bool:
    return re.search(r"0x[0-9a-fA-F]{5,}", text) is not None


def ported_record(src, target_id, sha, evidence_id, accepted, basis, entry, source_id):
    rec = copy.deepcopy(src)
    old_target = src["target_id"]
    rec["target_id"] = target_id
    rec["symbol_id"] = src["symbol_id"].replace(old_target, target_id)
    rec.pop("promotion", None)
    rec["policy"] = "forbidden" if src["policy"] == "forbidden" else "restricted"
    rec["approval_state"] = "REJECTED" if src.get("approval_state") == "REJECTED" or src["policy"] == "forbidden" else "PENDING"
    rec["proof"] = {
        "static": "recovered" if accepted else "candidate",
        "device": "not_probed",
        "evidence_ids": [evidence_id],
    }
    status = "STATIC_RECOVERED" if accepted else "CANDIDATE"
    if src["status"] in ("FORBIDDEN",):
        status = "FORBIDDEN"
    elif src["status"] == "CANDIDATE" or src.get("approval_state") == "REJECTED":
        status = "CANDIDATE"
        rec["proof"]["static"] = "candidate"
    rec["status"] = status
    rec["provenance"] = {
        "firmware_sha256": sha,
        "evidence_ids": [evidence_id],
        "source": f"Exact {target_id} image/IDB compared with {source_id}: {basis}.",
    }
    semantic = src.get("notes", "")
    kept = "" if has_hex_address(semantic) else semantic
    rec["notes"] = (
        f"{basis}. Source semantic status was {src['status']}; matching does not upgrade an uncertain source "
        "name/prototype. ABI and ownership follow the source record only within the audited port scope. "
        "Physical device validation pending; no approval carries over from the source target."
    )
    if kept and "port" not in kept.lower()[:12]:
        rec["notes"] += " " + kept
    if entry is not None:
        rec["entry_address"] = hex(entry)
        if rec.get("kind") == "function" and accepted and status == "STATIC_RECOVERED":
            rec["callable_address"] = hex(thumb(entry))
        else:
            rec.pop("callable_address", None)
    else:
        rec.pop("entry_address", None)
        rec.pop("callable_address", None)
    return rec


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--source-dir", required=True, type=Path)
    p.add_argument("--target-id", required=True)
    p.add_argument("--target-sha256", required=True)
    p.add_argument("--source-corpus", required=True)
    p.add_argument("--target-corpus", required=True)
    p.add_argument("--ensemble", required=True)
    p.add_argument("--review", required=True)
    p.add_argument("--overrides", required=True)
    p.add_argument("--evidence-id", required=True)
    p.add_argument("--out-dir", required=True, type=Path)
    p.add_argument("--report", required=True, type=Path)
    a = p.parse_args()

    src_corpus, dst_corpus = Corpus(a.source_corpus), Corpus(a.target_corpus)
    mapper = Mapper(src_corpus, dst_corpus)
    ensemble = json.load(open(a.ensemble))
    review = json.load(open(a.review))
    overrides = json.load(open(a.overrides))
    source_id = a.source_dir.name

    a.out_dir.mkdir(parents=True, exist_ok=True)
    report, counts = [], defaultdict(int)
    for path in sorted(glob.glob(str(a.source_dir / "symbols" / "*.json"))):
        src = json.load(open(path))
        name = src["name"]
        entry, accepted, basis, state = None, False, "", "UNMATCHED"
        if name in overrides:
            o = overrides[name]
            entry = addr(o["entry"]) if o.get("entry") else None
            accepted, basis, state = o.get("accepted", entry is not None), o["basis"], "MANUAL"
        elif src["kind"] == "function" and name in ensemble["matches"]:
            m = ensemble["matches"][name]
            entry = addr(m["predicted_address"]) & ~1
            accepted, basis = judge(mapper, m, review.get(name, {}))
            state = "ACCEPTED" if accepted else "REVIEW"
        elif name in ensemble.get("global_matches", {}):
            m = ensemble["global_matches"][name]
            if m.get("predicted_address"):
                entry = addr(m["predicted_address"])
                accepted = True
                basis = (
                    "global ownership by access: the same accessor functions reference it "
                    f"({len(m['top_candidates'][0].get('supporting_functions', []))} matched owners)"
                )
                state = "ACCEPTED"
            else:
                basis = "no unique global candidate"
        elif src.get("entry_address") is None:
            basis = "source has no address"
            state = "NO_ADDRESS"
        else:
            basis = "no candidate from the matcher"
        if state == "ACCEPTED" and src["kind"] == "function" and src.get("status") in ("CANDIDATE", "FORBIDDEN"):
            accepted = False
        rec = ported_record(src, a.target_id, a.target_sha256, a.evidence_id, accepted, basis, entry, source_id)
        out = a.out_dir / Path(path).name.replace(src["target_id"], a.target_id)
        out.write_text(json.dumps(rec, ensure_ascii=False, indent=2) + "\n")
        counts[(state, rec["status"])] += 1
        report.append({
            "name": name,
            "kind": src["kind"],
            "source_address": src.get("entry_address"),
            "target_address": rec.get("entry_address"),
            "status": rec["status"],
            "state": state,
            "basis": basis,
        })
    a.report.write_text(json.dumps({
        "source_target": source_id,
        "target_id": a.target_id,
        "firmware_sha256": a.target_sha256,
        "unique_full_body_seeds": len(mapper.anchor),
        "symbols": report,
    }, indent=1) + "\n")
    for key, n in sorted(counts.items()):
        print(n, key)


if __name__ == "__main__":
    main()
