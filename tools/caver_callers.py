#!/usr/bin/env python3
"""Call graph queries over the Caver binary dumps.

The dumps ship `index.tsv` (address -> demangled) and `callgraph.tsv`
(caller -> callee) per ABI. The two files spell addresses differently, and a
join that misses silently reports "0 callers" for every symbol — which reads
exactly like "nothing calls this" rather than like a bug. So every address here
is normalised to lowercase hex without leading zeros before joining.

    python3 tools/caver_callers.py --callers 'ProgramState::Update'
    python3 tools/caver_callers.py --callees 'ProgramState::ProgramState'
    python3 tools/caver_callers.py --top GameSceneView     # most-called members
    python3 tools/caver_callers.py --callers 'GameViewController::Update' --abi arm32_13

`--callers` is the useful direction for recovery: a leaf whose only callers are
one subsystem belongs to that subsystem. `--callees` shows what a function is
made of. Both print the demangled name of every endpoint, and mark the ones that
have no index entry (thunks, libc, boost) as `<external>`.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ABIS = ("arm64_13", "arm32_13")


def norm(addr: str) -> str:
    """0x0000000000545800 / 545800 / 00000000545800 -> 545800."""
    a = addr.strip().lower()
    if a.startswith("0x"):
        a = a[2:]
    a = a.lstrip("0")
    return a or "0"


def load(abi: str):
    base = os.path.join(ROOT, "OpenSwordigo", abi)
    index: dict[str, str] = {}
    with open(os.path.join(base, "index.tsv"), encoding="utf-8", errors="replace") as fh:
        for line in fh:
            p = line.rstrip("\n").split("\t")
            if len(p) < 3 or p[0] == "address":
                continue
            # Prefer a real demangled name over IDA's sub_XXXX.
            if p[0] not in index or index[p[0]].startswith("sub_"):
                index[norm(p[0])] = p[2]

    edges: list[tuple[str, str]] = []
    with open(os.path.join(base, "callgraph.tsv"), encoding="utf-8", errors="replace") as fh:
        for line in fh:
            p = line.rstrip("\n").split("\t")
            if len(p) < 4 or p[0] == "caller_address":
                continue
            edges.append((norm(p[0]), norm(p[2])))
    return index, edges


def name_of(index: dict[str, str], addr: str) -> str:
    return index.get(addr, "<external>")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pattern", nargs="?", help="regex matched against demangled names")
    ap.add_argument("--abi", default="arm64_13", choices=ABIS)
    ap.add_argument("--callers", action="store_true", help="show callers of each match")
    ap.add_argument("--callees", action="store_true", help="show callees of each match")
    ap.add_argument("--top", action="store_true", help="rank unique callers of a whole class")
    ap.add_argument("--limit", type=int, default=40)
    args = ap.parse_args()

    if not args.pattern:
        sys.exit("need a name pattern")

    rx = re.compile(args.pattern)
    index, edges = load(args.abi)

    if args.top:
        # Which symbols outside this class call into it? That is the class's
        # integration surface, i.e. what has to exist for it to be usable.
        inside = {a for a, n in index.items() if rx.search(n)}
        if not inside:
            sys.exit(f"no {args.abi} symbol matches {args.pattern!r}")
        outsiders: Counter[str] = Counter()
        for src, dst in edges:
            if dst in inside and src not in inside:
                outsiders[name_of(index, src)] += 1
        print(f"# {len(inside)} symbol(s) in {args.abi} match {args.pattern!r}")
        print(f"# {len(outsiders)} external caller(s), ranked by how many members they touch\n")
        for n, c in outsiders.most_common(args.limit):
            print(f"{c:5d}  {n}")
        return 0

    by_addr: dict[str, str] = {}
    for a, n in index.items():
        if rx.search(n):
            by_addr[a] = n
    if not by_addr:
        sys.exit(f"no {args.abi} symbol matches {args.pattern!r}")

    for addr in sorted(by_addr, key=lambda x: int(x, 16)):
        print(f"\n=== 0x{addr.upper()}  {by_addr[addr]}")
        if args.callers:
            seen = [name_of(index, s) for s, d in edges if d == addr]
            for n in sorted(set(seen)):
                print(f"    <- {n}")
            if not seen:
                print("    <- (no callers)")
        if args.callees:
            seen = [name_of(index, d) for s, d in edges if s == addr]
            for n in sorted(set(seen)):
                print(f"    -> {n}")
            if not seen:
                print("    -> (no callees)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
