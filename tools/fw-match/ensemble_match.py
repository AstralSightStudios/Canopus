#!/usr/bin/env python3
"""BinDiff-inspired multi-evidence matcher for firmware corpus v1.

This is deliberately a candidate generator, not a symbol promoter.  It uses
several evidence families that are available in the current corpus format:

* exact and Thumb relocation-masked entry bytes;
* CFG shape and normalized edge topology;
* string and constant/data references;
* size and caller/callee context;
* iterative caller/callee propagation from independent seeds.

The output keeps per-family evidence, margins, conflicts and states.  It can
also emit candidate symbol records, but those records are written outside the
production ``symbols/`` directory and are marked CANDIDATE/PENDING.

The next corpus schema will add full normalized instructions, data-flow and
ABI callsite records.  This tool intentionally reports those families as
missing instead of pretending that corpus v1 proves them.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


HEX_RE = re.compile(r"^0x[0-9a-fA-F]+$")


def addr(value: Any) -> int:
    if isinstance(value, int):
        return value
    if not isinstance(value, str):
        return 0
    try:
        return int(value.strip().lower().removeprefix("0x"), 16)
    except ValueError:
        return 0


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def read_json(path: Path) -> Any:
    return json.loads(path.read_text())


def load_failure_feedback(
    explicit_paths: list[Path], target_corpus: Path, target_id: str
) -> tuple[list[dict[str, Any]], dict[tuple[str, int], str]]:
    """Load target-specific failure lessons as hard negative evidence.

    A runtime failure can prove that a high-ranked address is semantically the
    wrong object even when its bytes and graph neighborhood look convincing.
    Such lessons must veto a candidate, not merely lower its score: correlated
    evidence cannot compensate for a demonstrated role mismatch. ABI-layout
    failures are retained in the report as hard-negative assumptions; corpus v1
    has no ABI-layout candidate family, so they must not be converted into fake
    address vetoes.
    """
    discovered = target_corpus.parent.parent / target_id / "evidence" / "failure-feedback"
    paths = list(explicit_paths)
    if discovered.is_dir():
        paths.extend(sorted(discovered.glob("*.json")))
    seen: set[Path] = set()
    documents: list[dict[str, Any]] = []
    negative_matches: dict[tuple[str, int], str] = {}
    for path in paths:
        path = path.resolve()
        if path in seen or not path.exists():
            continue
        seen.add(path)
        document = read_json(path)
        if not isinstance(document, dict) or document.get("target_id") != target_id:
            continue
        documents.append({
            "path": str(path),
            "failure_id": document.get("failure_id"),
            "stage": document.get("stage"),
            "observed_error": document.get("observed_error"),
            "root_cause": document.get("root_cause"),
            "negative_matches": document.get("negative_matches") or [],
            "negative_abi_assumptions": document.get("negative_abi_assumptions") or [],
            "corrective_evidence_id": document.get("corrective_evidence_id"),
        })
        for rule in document.get("negative_matches") or []:
            if not isinstance(rule, dict):
                continue
            source_name = rule.get("source_name")
            target_address = addr(rule.get("target_entry_address"))
            if not isinstance(source_name, str) or not source_name or not target_address:
                continue
            negative_matches[(source_name, target_address)] = str(
                rule.get("reason") or document.get("root_cause") or "failure feedback"
            )
    return documents, negative_matches


def entry_bytes(fn: dict[str, Any]) -> bytes:
    try:
        return bytes.fromhex(str(fn.get("entry", "")).strip())
    except ValueError:
        return b""


def thumb_mask(raw: bytes) -> bytes:
    """Port the conservative masks from tools/fw-match/src/thumb.rs."""
    out = bytearray([0xFF] * len(raw))
    if len(raw) >= 8 and raw[:4] == bytes((0x5F, 0xF8, 0x00, 0xF0)):
        for i in range(4, 8):
            out[i] = 0
        return bytes(out)

    i = 0
    while i + 2 <= len(raw):
        lo = raw[i]
        hi = raw[i + 1]
        hw = (hi << 8) | lo
        is32 = (hw & 0xE000) == 0xE000 and (hw & 0xF800) != 0xE000
        if is32:
            if i + 4 > len(raw):
                break
            hw2 = raw[i + 2] | (raw[i + 3] << 8)
            if (hw & 0xF800) == 0xF000 and (hw2 & 0xC000) == 0xC000:
                out[i:i + 4] = bytes((0x00, 0xFB, 0x00, 0xC0))
            elif (hw & 0xFBF0) in (0xF240, 0xF2C0, 0xF280, 0xF2A0, 0xF200):
                out[i:i + 4] = bytes((0xF0, 0xFB, 0x00, 0x8F))
            elif (hw & 0xFF70) == 0xF850:
                out[i:i + 4] = bytes((0x70, 0xFF, 0x00, 0x00))
            i += 4
        else:
            if (hw & 0xF800) == 0xE000:
                out[i:i + 2] = bytes((0x00, 0xF8))
            elif (hw & 0xF000) == 0xD000:
                out[i:i + 2] = bytes((0x00, 0xF0))
            elif (hw & 0xF800) == 0x4800:
                out[i:i + 2] = bytes((0x00, 0xFF))
            i += 2
    return bytes(out)


def masked_byte_score(a: bytes, b: bytes) -> float:
    if not a or not b:
        return 0.0
    n = min(len(a), len(b))
    mask = thumb_mask(a[:n])
    matched = total = 0
    for x, y, m in zip(a[:n], b[:n], mask):
        if not m:
            continue
        bits = m.bit_count()
        total += bits
        matched += ((~(x ^ y)) & m).bit_count()
    if not total:
        return 0.0
    base = matched / total
    length_penalty = 1.0 / (1.0 + abs(len(a) - len(b)) / 64.0)
    return base * length_penalty


def exact_byte_score(a: bytes, b: bytes) -> float:
    if not a or not b:
        return 0.0
    n = min(len(a), len(b))
    return sum(x == y for x, y in zip(a[:n], b[:n])) / max(len(a), len(b))


def ratio(a: int | float, b: int | float) -> float:
    a = float(a)
    b = float(b)
    if a == 0 and b == 0:
        return 1.0
    if a == 0 or b == 0:
        return 0.0
    return min(a, b) / max(a, b)


def jaccard(a: set[Any], b: set[Any]) -> float | None:
    if not a and not b:
        return None
    return len(a & b) / len(a | b) if a | b else 0.0


def weighted_jaccard(a: set[Any], b: set[Any], weights: dict[Any, float]) -> float | None:
    if not a and not b:
        return None
    inter = sum(weights.get(x, 1.0) for x in a & b)
    union = sum(weights.get(x, 1.0) for x in a | b)
    return inter / union if union else 0.0


def block_shapes(fn: dict[str, Any]) -> list[int]:
    size = max(1, int(fn.get("size", 0)))
    raw = fn.get("block_offs") or []
    return sorted(round(int(x.get("size", 0)) * 1024 / size) for x in raw)


def remapped_edges(fn: dict[str, Any]) -> set[tuple[int, int]]:
    blocks = fn.get("block_offs") or []
    order = sorted(range(len(blocks)), key=lambda i: (int(blocks[i].get("off", 0)), i))
    rank = {old: new for new, old in enumerate(order)}
    result = set()
    for edge in fn.get("succ") or []:
        if not isinstance(edge, (list, tuple)) or len(edge) != 2:
            continue
        a, b = int(edge[0]), int(edge[1])
        if a in rank and b in rank:
            result.add((rank[a], rank[b]))
    return result


def degree_signature(fn: dict[str, Any]) -> tuple[tuple[int, int], ...]:
    n = len(fn.get("block_offs") or [])
    incoming = [0] * n
    outgoing = [0] * n
    for edge in fn.get("succ") or []:
        if len(edge) != 2:
            continue
        a, b = int(edge[0]), int(edge[1])
        if 0 <= a < n and 0 <= b < n:
            outgoing[a] += 1
            incoming[b] += 1
    return tuple(sorted(zip(incoming, outgoing)))


def cfg_score(a: dict[str, Any], b: dict[str, Any]) -> tuple[float, dict[str, float]]:
    block_ratio = ratio(len(a.get("block_offs") or []), len(b.get("block_offs") or []))
    shape = jaccard(set(block_shapes(a)), set(block_shapes(b)))
    shape = 0.0 if shape is None else shape
    edge_a = remapped_edges(a)
    edge_b = remapped_edges(b)
    edge = jaccard(edge_a, edge_b)
    edge = 0.0 if edge is None else edge
    degrees = jaccard(set(degree_signature(a)), set(degree_signature(b)))
    degrees = 0.0 if degrees is None else degrees
    edge_count = ratio(len(edge_a), len(edge_b))
    # Topology is deliberately stronger than the old block-size/edge-count
    # score.  Edge-count proximity alone is not CFG evidence.
    topology = 0.45 * edge + 0.30 * degrees + 0.25 * edge_count
    total = 0.25 * block_ratio + 0.25 * shape + 0.50 * topology
    return total, {
        "block_count": block_ratio,
        "block_shape": shape,
        "edge_topology": edge,
        "degree_topology": degrees,
        "edge_count": edge_count,
        "topology": topology,
    }


def constant_categories(values: set[int]) -> set[str]:
    categories: set[str] = set()
    for value in values:
        if value == 0:
            categories.add("zero")
        if value == 1:
            categories.add("one")
        if value and value & (value - 1) == 0:
            categories.add("power_of_two")
        if value and value & (value + 1) == 0:
            categories.add("bitmask")
        if 0 < value <= 0xFF:
            categories.add("u8")
        if 0 < value <= 0xFFFF:
            categories.add("u16")
    return categories


def data_scores(a: dict[str, Any], b: dict[str, Any], string_weights: dict[str, float]) -> tuple[float | None, dict[str, float | None]]:
    strings_a = set(a.get("strings") or [])
    strings_b = set(b.get("strings") or [])
    constants_a = {int(x) for x in a.get("constants") or []}
    constants_b = {int(x) for x in b.get("constants") or []}
    string_score = weighted_jaccard(strings_a, strings_b, string_weights)
    constant_score = jaccard(constants_a, constants_b)
    categories = jaccard(constant_categories(constants_a), constant_categories(constants_b))
    pieces = []
    if string_score is not None:
        pieces.append((0.60, string_score))
    if constant_score is not None:
        pieces.append((0.25, constant_score))
    if categories is not None:
        pieces.append((0.15, categories))
    if not pieces:
        return None, {"strings": None, "constants": None, "constant_categories": None}
    total_weight = sum(weight for weight, _ in pieces)
    total = sum(weight * value for weight, value in pieces) / total_weight
    return total, {
        "strings": string_score,
        "constants": constant_score,
        "constant_categories": categories,
    }


def context_score(a: dict[str, Any], b: dict[str, Any]) -> tuple[float, dict[str, float]]:
    size = ratio(a.get("size", 0), b.get("size", 0))
    insn = ratio(a.get("insn", 0), b.get("insn", 0))
    callers = ratio(len(a.get("callers") or []), len(b.get("callers") or []))
    callees = ratio(len(a.get("callees") or []), len(b.get("callees") or []))
    return (0.35 * size + 0.35 * insn + 0.15 * callers + 0.15 * callees), {
        "size": size,
        "insn": insn,
        "callers": callers,
        "callees": callees,
    }


def base_evidence(a: dict[str, Any], b: dict[str, Any], string_weights: dict[str, float]) -> dict[str, Any]:
    ba, bb = entry_bytes(a), entry_bytes(b)
    exact = exact_byte_score(ba, bb)
    masked = masked_byte_score(ba, bb)
    cfg, cfg_detail = cfg_score(a, b)
    data, data_detail = data_scores(a, b, string_weights)
    context, context_detail = context_score(a, b)
    families: dict[str, float] = {
        "bytes": max(exact, masked),
        "cfg": cfg,
        "context": context,
    }
    if data is not None:
        families["data"] = data
    weights = {"bytes": 0.30, "cfg": 0.35, "data": 0.20, "context": 0.15}
    available = [key for key in weights if key in families]
    total_weight = sum(weights[key] for key in available)
    base = sum(weights[key] * families[key] for key in available) / total_weight
    return {
        "families": families,
        "base_score": base,
        "detail": {
            "bytes": {"exact": exact, "masked": masked},
            "cfg": cfg_detail,
            "data": data_detail,
            "context": context_detail,
        },
        "independence_groups": sorted(families),
    }


def graph_support(src: dict[str, Any], dst: dict[str, Any], anchors: dict[int, int]) -> tuple[float | None, dict[str, Any]]:
    def mapped_fraction(values: list[Any], target_values: list[Any]) -> tuple[float | None, int, int]:
        expected = {anchors[addr(x)] for x in values if addr(x) in anchors}
        if not expected:
            return None, 0, 0
        actual = {addr(x) for x in target_values}
        return len(expected & actual) / len(expected), len(expected & actual), len(expected)

    callee, callee_hits, callee_known = mapped_fraction(src.get("callees") or [], dst.get("callees") or [])
    caller, caller_hits, caller_known = mapped_fraction(src.get("callers") or [], dst.get("callers") or [])
    parts = [x for x in (callee, caller) if x is not None]
    if not parts:
        return None, {"callee": None, "caller": None, "known": 0}
    return sum(parts) / len(parts), {
        "callee": callee,
        "caller": caller,
        "callee_hits": callee_hits,
        "callee_known": callee_known,
        "caller_hits": caller_hits,
        "caller_known": caller_known,
        "known": callee_known + caller_known,
    }


def caller_neighborhood_score(
    src: dict[str, Any],
    dst: dict[str, Any],
    source_by_addr: dict[int, dict[str, Any]],
    target_by_addr: dict[int, dict[str, Any]],
    string_weights: dict[str, float],
    pair_cache: dict[tuple[int, int], float],
) -> tuple[float | None, dict[str, Any]]:
    """Match caller neighborhoods without relying on named anchors.

    This is the useful part of graph matching for short import veneers: the
    veneer bytes are intentionally identical, but the callers are larger
    functions whose normalized features can be matched independently.  A
    target candidate receives credit only for distinct caller correspondences.
    """
    source_callers = [source_by_addr[addr(x)] for x in (src.get("callers") or []) if addr(x) in source_by_addr]
    target_callers = [target_by_addr[addr(x)] for x in (dst.get("callers") or []) if addr(x) in target_by_addr]
    if not source_callers or not target_callers:
        return None, {"source_callers": len(source_callers), "target_callers": len(target_callers)}

    matches = []
    used_targets: set[int] = set()
    for source_caller in source_callers[:64]:
        source_address = addr(source_caller.get("addr"))
        best_score = 0.0
        best_target = None
        for target_caller in target_callers[:64]:
            target_address = addr(target_caller.get("addr"))
            if target_address in used_targets:
                continue
            key = (source_address, target_address)
            score = pair_cache.get(key)
            if score is None:
                score = base_evidence(source_caller, target_caller, string_weights)["base_score"]
                pair_cache[key] = score
            if score > best_score:
                best_score = score
                best_target = target_address
        if best_target is not None:
            used_targets.add(best_target)
            matches.append(best_score)

    if not matches:
        return 0.0, {"source_callers": len(source_callers), "target_callers": len(target_callers), "matched_callers": 0}
    return sum(matches) / len(source_callers[:64]), {
        "source_callers": len(source_callers),
        "target_callers": len(target_callers),
        "matched_callers": len(matches),
        "mean_best_match": sum(matches) / len(matches),
        "min_best_match": min(matches),
    }


def fused_evidence(
    base: dict[str, Any],
    graph: float | None,
    graph_detail: dict[str, Any],
    neighborhood: float | None,
    neighborhood_detail: dict[str, Any],
) -> dict[str, Any]:
    families = dict(base["families"])
    # Caller-neighborhood and anchor propagation are correlated graph evidence.
    # Fuse them into one family instead of double-counting both as independent.
    graph_parts = [x for x in (neighborhood, graph) if x is not None]
    if graph_parts:
        if neighborhood is not None and graph is not None:
            families["callgraph"] = 0.70 * neighborhood + 0.30 * graph
        else:
            families["callgraph"] = graph_parts[0]
    weights = {"bytes": 0.20, "cfg": 0.25, "data": 0.15, "context": 0.10, "callgraph": 0.30}
    available = [key for key in weights if key in families]
    total_weight = sum(weights[key] for key in available)
    score = sum(weights[key] * families[key] for key in available) / total_weight
    return {
        "families": families,
        "score": score,
        "graph": {
            "anchor": graph_detail,
            "caller_neighborhood": neighborhood_detail,
        },
        "independence_groups": sorted(families),
    }


def candidate_pool(
    source_name: str,
    src: dict[str, Any],
    target: list[dict[str, Any]],
    string_weights: dict[str, float],
    limit: int,
    negative_matches: dict[tuple[str, int], str],
) -> list[dict[str, Any]]:
    source_size = max(1, int(src.get("size", 0)))
    source_blocks = len(src.get("block_offs") or [])
    result = []
    for index, dst in enumerate(target):
        target_address = addr(dst.get("addr"))
        if (source_name, target_address) in negative_matches:
            continue
        dst_size = int(dst.get("size", 0))
        if dst_size < max(1, source_size // 4) or dst_size > source_size * 4:
            continue
        dst_blocks = len(dst.get("block_offs") or [])
        if source_blocks and dst_blocks and (max(source_blocks, dst_blocks) / min(source_blocks, dst_blocks)) > 3.0:
            continue
        evidence = base_evidence(src, dst, string_weights)
        # This is intentionally permissive: graph propagation may rescue a
        # structurally weaker candidate.  The final gate is not this filter.
        if evidence["base_score"] < 0.28:
            continue
        result.append({"target_index": index, "base": evidence})
    result.sort(key=lambda item: item["base"]["base_score"], reverse=True)
    return result[:limit]


def rank_with_graph(
    src: dict[str, Any],
    dst_functions: list[dict[str, Any]],
    pool: list[dict[str, Any]],
    anchors: dict[int, int],
    source_by_addr: dict[int, dict[str, Any]],
    target_by_addr: dict[int, dict[str, Any]],
    string_weights: dict[str, float],
    pair_cache: dict[tuple[int, int], float],
) -> None:
    for item in pool:
        dst = dst_functions[item["target_index"]]
        graph, graph_detail = graph_support(src, dst, anchors)
        neighborhood, neighborhood_detail = caller_neighborhood_score(
            src,
            dst,
            source_by_addr,
            target_by_addr,
            string_weights,
            pair_cache,
        )
        item["fused"] = fused_evidence(
            item["base"],
            graph,
            graph_detail,
            neighborhood,
            neighborhood_detail,
        )
    pool.sort(key=lambda item: item["fused"]["score"], reverse=True)


def margin(pool: list[dict[str, Any]]) -> float:
    if not pool:
        return 0.0
    best = pool[0]["fused"]["score"]
    if len(pool) == 1:
        return 1.0
    second = pool[1]["fused"]["score"]
    return (best - second) / best if best > 0 else 0.0


def heuristic_confidence(pool: list[dict[str, Any]]) -> float:
    if not pool:
        return 0.0
    best = pool[0]["fused"]
    score = best["score"]
    mg = margin(pool)
    groups = len(best["independence_groups"])
    # This is a ranking aid, not a calibrated production probability.  The
    # report explicitly labels it heuristic until an independent oracle set is
    # large enough for calibration.
    return max(0.0, min(1.0, 0.55 * score + 0.25 * min(1.0, mg * 2.0) + 0.20 * min(1.0, groups / 4.0)))


def margin_at(pool: list[dict[str, Any]], selected_index: int) -> float:
    if not pool or selected_index < 0 or selected_index >= len(pool):
        return 0.0
    selected = pool[selected_index]["fused"]["score"]
    competitors = [
        item["fused"]["score"] for index, item in enumerate(pool) if index != selected_index
    ]
    if not competitors:
        return 1.0
    strongest = max(competitors)
    return max(0.0, (selected - strongest) / selected) if selected > 0 else 0.0


def confidence_at(pool: list[dict[str, Any]], selected_index: int) -> float:
    if not pool or selected_index < 0 or selected_index >= len(pool):
        return 0.0
    selected = pool[selected_index]["fused"]
    score = selected["score"]
    mg = margin_at(pool, selected_index)
    groups = len(selected["independence_groups"])
    return max(
        0.0,
        min(
            1.0,
            0.55 * score
            + 0.25 * min(1.0, mg * 2.0)
            + 0.20 * min(1.0, groups / 4.0),
        ),
    )


def state_at(
    pool: list[dict[str, Any]], selected_index: int, required_groups: int = 3
) -> str:
    if not pool or selected_index < 0 or selected_index >= len(pool):
        return "BLOCKED"
    selected = pool[selected_index]["fused"]
    if (
        selected_index == 0
        and selected["score"] >= 0.78
        and margin_at(pool, selected_index) >= 0.12
        and len(selected["independence_groups"]) >= required_groups
    ):
        return "REVIEW_REQUIRED"
    return "CANDIDATE"


def assign_one_to_one(
    problems: list[dict[str, Any]], target_functions: list[dict[str, Any]]
) -> dict[str, dict[str, Any]]:
    """Assign candidates in confidence order without duplicate target reuse."""
    priority = sorted(
        problems,
        key=lambda problem: (
            problem["candidates"][0]["fused"]["score"] + margin(problem["candidates"]) * 0.1
            if problem["candidates"]
            else -1.0,
            str(problem["symbol"].get("name") or ""),
        ),
        reverse=True,
    )
    used: dict[int, str] = {}
    assignments: dict[str, dict[str, Any]] = {}
    for problem in priority:
        name = str(problem["symbol"].get("name") or "")
        pool = problem["candidates"]
        selected_index = None
        blocked_by = None
        for index, item in enumerate(pool):
            target_address = addr(target_functions[item["target_index"]].get("addr"))
            owner = used.get(target_address)
            if owner is None:
                selected_index = index
                break
            if index == 0:
                blocked_by = owner
        if selected_index is not None:
            selected_address = addr(
                target_functions[pool[selected_index]["target_index"]].get("addr")
            )
            used[selected_address] = name
        assignments[name] = {
            "selected_index": selected_index,
            "top_collision_with": blocked_by,
        }
    return assignments


def load_source_symbols(symbol_dir: Path) -> list[dict[str, Any]]:
    out = []
    for path in sorted(symbol_dir.glob("*.json")):
        value = read_json(path)
        if value.get("kind") != "function" or not value.get("entry_address"):
            continue
        out.append(value)
    return out


def source_function_for_symbol(symbol: dict[str, Any], by_addr: dict[int, dict[str, Any]]) -> dict[str, Any] | None:
    return by_addr.get(addr(symbol.get("entry_address")))


def state_for(pool: list[dict[str, Any]], required_groups: int = 3) -> str:
    if not pool:
        return "BLOCKED"
    best = pool[0]["fused"]
    if best["score"] >= 0.78 and margin(pool) >= 0.12 and len(best["independence_groups"]) >= required_groups:
        return "REVIEW_REQUIRED"
    return "CANDIDATE"


def make_symbol_candidate(source: dict[str, Any], target_id: str, target: dict[str, Any], result: dict[str, Any], report_rel: str) -> dict[str, Any]:
    record = dict(source)
    source_id = str(source.get("symbol_id", ""))
    record["symbol_id"] = source_id.replace(str(source.get("target_id", "")), target_id, 1)
    record["target_id"] = target_id
    record["entry_address"] = target.get("addr")
    record["callable_address"] = f"0x{addr(target.get('addr')) | 1:x}"
    record["status"] = "CANDIDATE"
    record["approval_state"] = "PENDING"
    record["policy"] = "restricted"
    record.pop("promotion", None)
    proof = dict(record.get("proof") or {})
    proof["static"] = "candidate"
    proof["device"] = "not_probed"
    proof["evidence_ids"] = [result["evidence_id"]]
    record["proof"] = proof
    record["provenance"] = {
        "firmware_sha256": result["target_firmware_sha256"],
        "evidence_ids": [result["evidence_id"]],
        "source": (
            f"{result['matcher']} source={result['source_target_id']} "
            f"report={report_rel}"
        ),
    }
    # Candidate records must never be consumed as production bindings.  The
    # per-candidate score breakdown remains in the ensemble report because the
    # symbol schema intentionally rejects matcher-internal fields.
    record["notes"] = (
        "Generated candidate only; requires independent ABI/callsite review "
        "before promotion. "
        f"state={result['state']} confidence={result['confidence_heuristic']:.4f} "
        f"margin={result['margin']:.4f}"
    )
    return record


def compare_oracle(results: dict[str, dict[str, Any]], oracle: list[dict[str, Any]]) -> list[dict[str, Any]]:
    comparison = []
    for item in oracle:
        name = item["name"]
        expected = addr(item["target_entry_address"])
        result = results.get(name)
        predicted = addr(result["predicted_address"]) if result and result.get("predicted_address") else None
        comparison.append({
            "name": name,
            "expected_address": f"0x{expected:x}",
            "predicted_address": f"0x{predicted:x}" if predicted is not None else None,
            "match": predicted == expected if result else None,
            "source_missing": result is None,
            "confidence_heuristic": result.get("confidence_heuristic") if result else None,
            "margin": result.get("margin") if result else None,
            "top_candidates": result.get("top_candidates", []) if result else [],
            "oracle_evidence_id": item.get("evidence_id"),
        })
    return comparison


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-symbols", required=True, type=Path)
    parser.add_argument("--source-corpus", required=True, type=Path)
    parser.add_argument("--target-corpus", required=True, type=Path)
    parser.add_argument("--target-id", required=True)
    parser.add_argument("--target-firmware-sha256", required=True)
    parser.add_argument("--evidence-id")
    parser.add_argument("--oracle", type=Path)
    parser.add_argument("--failure-feedback", action="append", type=Path, default=[])
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--symbols-output", type=Path)
    parser.add_argument("--pool", type=int, default=96)
    parser.add_argument("--rounds", type=int, default=4)
    args = parser.parse_args()

    source_corpus_doc = read_json(args.source_corpus)
    target_corpus_doc = read_json(args.target_corpus)
    evidence_id = args.evidence_id or (
        "EVID-FW-MATCH-ENSEMBLE-"
        + re.sub(r"[^A-Za-z0-9]+", "-", str(source_corpus_doc.get("target_id") or "source"))
        + "-"
        + re.sub(r"[^A-Za-z0-9]+", "-", args.target_id)
    )
    source_functions = source_corpus_doc.get("functions") or []
    target_functions = target_corpus_doc.get("functions") or []
    source_symbols = load_source_symbols(args.source_symbols)
    source_by_addr = {addr(fn.get("addr")): fn for fn in source_functions}
    target_by_addr = {addr(fn.get("addr")): fn for fn in target_functions}

    string_frequency = Counter()
    for fn in target_functions:
        string_frequency.update(set(fn.get("strings") or []))
    string_weights = {s: 1.0 / math.log2(2 + count) for s, count in string_frequency.items()}
    failure_feedback, negative_matches = load_failure_feedback(
        args.failure_feedback, args.target_corpus, args.target_id
    )

    problems: list[dict[str, Any]] = []
    for symbol in source_symbols:
        src = source_function_for_symbol(symbol, source_by_addr)
        if src is None:
            continue
        problems.append({
            "symbol": symbol,
            "source": src,
            "candidates": candidate_pool(
                str(symbol.get("name") or ""),
                src,
                target_functions,
                string_weights,
                args.pool,
                negative_matches,
            ),
        })

    anchors: dict[int, int] = {}
    pair_cache: dict[tuple[int, int], float] = {}
    anchor_history = []
    for round_number in range(args.rounds):
        for problem in problems:
            rank_with_graph(
                problem["source"],
                target_functions,
                problem["candidates"],
                anchors,
                source_by_addr,
                target_by_addr,
                string_weights,
                pair_cache,
            )

        proposed = []
        for problem in problems:
            pool = problem["candidates"]
            if not pool:
                continue
            top = pool[0]
            mg = margin(pool)
            score = top["fused"]["score"]
            # Caller-neighborhood evidence is allowed to seed a graph anchor,
            # but direct anchor propagation never gets to seed itself.
            groups = len(top["fused"]["independence_groups"])
            if score >= 0.72 and mg >= 0.10 and groups >= 3:
                proposed.append((score + mg * 0.1, addr(problem["source"]["addr"]), addr(target_functions[top["target_index"]]["addr"]), problem["symbol"].get("name", "")))
        proposed.sort(reverse=True)
        claimed_targets = set(anchors.values())
        added = []
        for _, source_address, target_address, name in proposed:
            if source_address in anchors or target_address in claimed_targets:
                continue
            anchors[source_address] = target_address
            claimed_targets.add(target_address)
            added.append({"name": name, "source": f"0x{source_address:x}", "target": f"0x{target_address:x}"})
        anchor_history.append({"round": round_number, "anchors_added": added, "anchor_count": len(anchors)})
        if not added:
            break

    # Final ranking after propagation.  Resolve target collisions deterministically
    # while preserving the unassigned top candidates in the report.
    for problem in problems:
        rank_with_graph(
            problem["source"],
            target_functions,
            problem["candidates"],
            anchors,
            source_by_addr,
            target_by_addr,
            string_weights,
            pair_cache,
        )

    assignments = assign_one_to_one(problems, target_functions)
    result_by_name: dict[str, dict[str, Any]] = {}
    symbol_candidates: list[dict[str, Any]] = []
    for problem in problems:
        symbol = problem["symbol"]
        name = str(symbol.get("name") or "")
        pool = problem["candidates"]
        assignment = assignments[name]
        selected_index = assignment["selected_index"]
        selected = pool[selected_index] if selected_index is not None else None
        top_candidates = []
        for index, item in enumerate(pool[:8]):
            target = target_functions[item["target_index"]]
            top_candidates.append({
                "address": target.get("addr"),
                "score": item["fused"]["score"],
                "families": item["fused"]["families"],
                "evidence": item["fused"]["graph"],
                "independence_groups": item["fused"]["independence_groups"],
                "selected": index == selected_index,
            })
        if selected is not None:
            target = target_functions[selected["target_index"]]
            predicted_address = target.get("addr")
            selected_rank = selected_index + 1
        else:
            target = None
            predicted_address = None
            selected_rank = None
        result = {
            "name": name,
            "symbol_id": symbol.get("symbol_id", ""),
            "source_target_id": source_corpus_doc.get("target_id"),
            "matcher": "bindiff-inspired-ensemble-v2",
            "evidence_id": evidence_id,
            "source_address": symbol.get("entry_address"),
            "predicted_address": predicted_address,
            "selected_rank": selected_rank,
            "margin": margin_at(pool, selected_index if selected_index is not None else -1),
            "confidence_heuristic": confidence_at(
                pool, selected_index if selected_index is not None else -1
            ),
            "state": state_at(pool, selected_index if selected_index is not None else -1),
            "top_candidates": top_candidates,
            "required_evidence_missing": ["normalized-instructions", "abi-callsite", "dataflow"],
            "target_firmware_sha256": args.target_firmware_sha256,
            "target_corpus_sha256": sha256_file(args.target_corpus),
            "top_collision_with": assignment["top_collision_with"],
        }
        result_by_name[result["name"]] = result
        if selected is not None and target is not None:
            symbol_candidates.append({"source": symbol, "target": target, "result": result})

    predicted_addresses = [
        item["predicted_address"]
        for item in result_by_name.values()
        if item.get("predicted_address")
    ]
    quality_summary = {
        "assigned": len(predicted_addresses),
        "unique_assigned": len(set(predicted_addresses)),
        "one_to_one": len(predicted_addresses) == len(set(predicted_addresses)),
        "reassigned_below_top_rank": sum(
            (item.get("selected_rank") or 0) > 1 for item in result_by_name.values()
        ),
        "top_candidate_collisions": sum(
            item.get("top_collision_with") is not None for item in result_by_name.values()
        ),
        "blocked": sum(item["state"] == "BLOCKED" for item in result_by_name.values()),
        "candidate": sum(item["state"] == "CANDIDATE" for item in result_by_name.values()),
        "review_required": sum(
            item["state"] == "REVIEW_REQUIRED" for item in result_by_name.values()
        ),
    }

    report = {
        "schema": 2,
        "matcher": "bindiff-inspired-ensemble-v2",
        "source_target_id": source_corpus_doc.get("target_id"),
        "target_target_id": target_corpus_doc.get("target_id"),
        "target_firmware_sha256": args.target_firmware_sha256,
        "evidence_id": evidence_id,
        "source_corpus_schema": source_corpus_doc.get("schema"),
        "target_corpus_schema": target_corpus_doc.get("schema"),
        "source_corpus_sha256": sha256_file(args.source_corpus),
        "target_corpus_sha256": sha256_file(args.target_corpus),
        "source_symbol_count": len(source_symbols),
        "matched_symbol_count": len(result_by_name),
        "parameters": {
            "pool": args.pool,
            "rounds": args.rounds,
            "failure_feedback_paths": [item["path"] for item in failure_feedback],
        },
        "failure_feedback": failure_feedback,
        "hard_negative_match_count": len(negative_matches),
        "hard_negative_abi_assumption_count": sum(
            len(item["negative_abi_assumptions"]) for item in failure_feedback
        ),
        "quality_summary": quality_summary,
        "evidence_families": ["bytes", "cfg", "data", "context", "callgraph"],
        "missing_families": ["normalized-instructions", "abi-callsite", "dataflow"],
        "confidence_note": "confidence_heuristic is a ranking aid, not an oracle-calibrated probability",
        "anchor_history": anchor_history,
        "oracle_comparison": [],
        "matches": result_by_name,
    }
    if args.oracle:
        oracle = read_json(args.oracle)
        if isinstance(oracle, dict):
            oracle = oracle.get("entries") or []
        report["oracle_comparison"] = compare_oracle(result_by_name, oracle)
        comparable = [x for x in report["oracle_comparison"] if not x["source_missing"]]
        report["oracle_summary"] = {
            "total": len(comparable),
            "matched": sum(x["match"] is True for x in comparable),
            "source_missing": sum(x["source_missing"] for x in report["oracle_comparison"]),
            "precision_on_manual_oracle": (
                sum(x["match"] is True for x in comparable) / len(comparable)
                if comparable else None
            ),
        }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")

    if args.symbols_output:
        args.symbols_output.mkdir(parents=True, exist_ok=True)
        report_rel = str(args.output)
        for item in symbol_candidates:
            candidate = make_symbol_candidate(
                item["source"], args.target_id, item["target"], item["result"], report_rel
            )
            out_name = f"{candidate['symbol_id']}.json".replace("/", "_")
            (args.symbols_output / out_name).write_text(json.dumps(candidate, indent=2, ensure_ascii=False) + "\n")

    oracle_summary = report.get("oracle_summary")
    if oracle_summary:
        print(
            f"manual oracle: {oracle_summary['matched']}/{oracle_summary['total']} "
            f"({oracle_summary['precision_on_manual_oracle']:.1%})"
        )
    print(f"matched {len(result_by_name)}/{len(source_symbols)} source symbols")
    print(f"anchors: {len(anchors)}; report: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
