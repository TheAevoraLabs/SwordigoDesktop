#!/usr/bin/env python3
"""Print the decompiled body of Caver functions by name pattern.

The arm64_13 / arm32_13 dumps carry one file per function plus an index.tsv with
address, mangled, demangled and the file. Reading them by hand means copying
paths out of a ledger, so this resolves a name pattern to bodies directly:

    python3 tools/caver_show.py ProgramState::Push
    python3 tools/caver_show.py GameSceneView --class --abi arm32_13
    python3 tools/caver_show.py GameViewController::Update --full

The bodies are IDA/Hex-Rays output, not original source. Nothing here is a
recovery — it is the raw evidence a recovery is transcribed from, so it prints
the address with every body and never renames anything.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ABIS = ("arm64_13", "arm32_13")


def load_index(abi: str):
    path = os.path.join(ROOT, "OpenSwordigo", abi, "index.tsv")
    if not os.path.exists(path):
        sys.exit(f"no index for {abi}: {path}")
    rows = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 4 or parts[0] == "address":
                continue
            rows.append(parts)
    return rows


def body_of(abi: str, rel: str) -> str:
    path = os.path.join(ROOT, "OpenSwordigo", abi, rel)
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError as exc:
        return f"<unreadable: {exc}>"
    # The header is a block comment ending in " */"; the body follows it.
    marker = "GENERATED EVIDENCE"
    cut = text.find(marker)
    if cut == -1:
        return text.strip()
    end = text.find("*/", cut)
    rest = text[end + 2 :] if end != -1 else text[cut:]
    return rest.strip()


def callees(abi: str, rel: str) -> list[str]:
    path = os.path.join(ROOT, "OpenSwordigo", abi, rel)
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            head = fh.read(4000)
    except OSError:
        return []
    out = []
    in_callees = False
    for line in head.splitlines():
        if line.startswith(" * Callees:"):
            in_callees = True
            continue
        if in_callees:
            if not line.startswith(" *   "):
                in_callees = False
                continue
            out.append(line.strip(" *").strip())
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pattern", help="regex matched against the demangled name")
    ap.add_argument("--abi", default="arm64_13", choices=ABIS, help="which dump (default arm64_13)")
    ap.add_argument("--limit", type=int, default=0, help="max functions to print (0 = all)")
    ap.add_argument("--calls", action="store_true", help="also print each function's callees")
    ap.add_argument("--list", action="store_true", help="names only, no bodies")
    ap.add_argument("--full", action="store_true", help="include the evidence header")
    args = ap.parse_args()

    try:
        rx = re.compile(args.pattern)
    except re.error as exc:
        sys.exit(f"bad pattern: {exc}")

    rows = load_index(args.abi)
    hits = [r for r in rows if rx.search(r[2])]
    if not hits:
        print(f"no {args.abi} symbol matches {args.pattern!r}", file=sys.stderr)
        return 1

    print(f"# {len(hits)} match(es) in {args.abi} for {args.pattern!r}")
    for i, r in enumerate(hits):
        if args.limit and i >= args.limit:
            print(f"# ... {len(hits) - args.limit} more (raise --limit)")
            break
        addr, _mangled, demangled, rel = r[0], r[1], r[2], r[3]
        print(f"\n=== 0x{addr.lstrip('0').rjust(6, '0')}  {demangled}")
        if args.list:
            continue
        if args.calls:
            for c in callees(args.abi, rel):
                print(f"    -> {c}")
        text = body_of(args.abi, rel)
        if args.full:
            print(f"    [file] OpenSwordigo/{args.abi}/{rel}")
        for line in text.splitlines():
            print(f"    {line}" if line.strip() else "")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
