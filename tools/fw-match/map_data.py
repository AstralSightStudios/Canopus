#!/usr/bin/env python3
"""Map data objects between two builds through the functions that access them.

A global is identified by who touches it and where: every (accessor function,
instruction offset) pair of the source object is translated through the
function mapping (mutually unique full-body anchors plus an optional reviewed
mapping file) and looked up among the target's data objects. The best candidate
must reproduce at least 80 % of the translated pairs and be unique.

  python tools/fw-match/map_data.py --source-corpus A.json --target-corpus B.json \
      [--function-map extra.json] 0x200a5158 0x200c36b8 ...

Static evidence only; a unique answer still needs the consumer-level review that
the callers of this tool perform.
"""
import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from port_pack import Corpus, Mapper  # noqa: E402


def load_objects(path):
    return {int(g["addr"], 16): g for g in json.load(open(path))["globals"]}


def map_objects(src_objs, dst_objs, function_map, addresses):
    # (function, offset) -> target objects
    by_site = defaultdict(set)
    for addr, g in dst_objs.items():
        for x in g["xrefs"]:
            by_site[(int(x["function"], 16), x["off"])].add(addr)
    result = {}
    for address in addresses:
        g = src_objs.get(address)
        if g is None:
            result[address] = {"error": "not a data object in the source corpus"}
            continue
        sites = [(int(x["function"], 16), x["off"], x["access"]) for x in g["xrefs"]]
        mapped = [(function_map.get(f), off, access) for f, off, access in sites]
        usable = [m for m in mapped if m[0] is not None]
        votes = Counter()
        for f, off, _ in usable:
            for t in by_site.get((f, off), ()):
                votes[t] += 1
        # Equal votes: the object in the same kind of memory wins (a RAM variable is
        # not the read-only copy that happens to be read at the same sites).
        ranked = sorted(votes.items(),
                        key=lambda kv: (kv[1], dst_objs[kv[0]]["segment"] == g["segment"]),
                        reverse=True)[:3]
        if not ranked:
            result[address] = {"error": "no candidate", "sites": len(sites), "mapped_sites": len(usable)}
            continue
        best, hits = ranked[0]
        unique = len(ranked) == 1 or (ranked[1][1], dst_objs[ranked[1][0]]["segment"] == g["segment"]) < (
            hits, dst_objs[best]["segment"] == g["segment"])
        result[address] = {
            "target": hex(best),
            "hits": hits,
            "sites": len(sites),
            "mapped_sites": len(usable),
            "ratio": round(hits / len(usable), 3) if usable else 0,
            "unique": unique,
            "runner_up": None if len(ranked) < 2 else [hex(ranked[1][0]), ranked[1][1]],
            "source_size": g["size"],
            "target_size": dst_objs[best]["size"],
            "source_segment": g["segment"],
            "target_segment": dst_objs[best]["segment"],
        }
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--source-corpus", required=True)
    p.add_argument("--target-corpus", required=True)
    p.add_argument("--function-map", help='JSON {"0xsrc": "0xdst"} of reviewed function pairs')
    p.add_argument("addresses", nargs="+")
    a = p.parse_args()
    src, dst = Corpus(a.source_corpus), Corpus(a.target_corpus)
    function_map = dict(Mapper(src, dst).anchor)
    if a.function_map:
        function_map.update({int(k, 16) & ~1: int(v, 16) & ~1 for k, v in json.load(open(a.function_map)).items()})
    out = map_objects(load_objects(a.source_corpus), load_objects(a.target_corpus), function_map,
                      [int(x, 16) for x in a.addresses])
    for address, info in out.items():
        print(hex(address), json.dumps(info))


if __name__ == "__main__":
    main()
