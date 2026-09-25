#!/usr/bin/env python3
"""caver_recovery_census.py — how much of Caver is actually recovered, measured.

`src/ruby/caver/` is a hand-ported reimplementation of the engine's namespace, not
a symbol-for-symbol clone, so "how far along are we" has no answer you can read
off the tree. This produces one.

It joins three sources:

  1. `OpenSwordigo/arm64_13/index.tsv` and `OpenSwordigo/arm32_13/index.tsv` — the
     IDA function indexes. One row per function: VA, mangled, demangled, and the
     path of the decompiled body under `functions/`.
  2. The repo's own text (`src/`, `tests/`, `docs/`) as the recovery evidence.
     The house convention for provenance is a comment naming the symbol and its
     address (`//   Caver::CollisionShape::CollidesWithShape  0x28EFB8 (arm32_13)`),
     so both the address and the demangled name count as proof of recovery.
  3. Nothing else. No judgement is stored here — every classification is derived
     from those, so the ledger cannot claim coverage the tree does not show.

Symbols owned by someone else (`boost::`, `std::`, `google::`, protobuf's
generated `Proto::*` accessors) are separated out rather than counted as Caver
work, because porting `boost::shared_ptr<Caver::Foo>::~shared_ptr()` means
porting boost.

Usage:
    tools/caver_recovery_census.py                    # report
    tools/caver_recovery_census.py --write            # + write the ledger
    tools/caver_recovery_census.py --targets 40       # more next-target rows
    tools/caver_recovery_census.py --class CharControllerComponent
    tools/caver_recovery_census.py --body GameSceneController::CreateHeroObjectAt
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

INDEXES = [
    ("arm64_13", "OpenSwordigo/arm64_13/index.tsv"),
    ("arm32_13", "OpenSwordigo/arm32_13/index.tsv"),
]

# Where recovery evidence may live, and where it must NOT.
#
# `src/ruby/caver/recovery/` is excluded because it holds THIS TOOL'S OWN OUTPUT:
# `ledger.tsv` names every symbol in both binaries, so scanning it makes the
# census cite itself and report 83% coverage where the real figure is 12%. That is
# not a hypothetical — it happened, and the only reason it was caught is that the
# number moved for no reason. Any generated catalogue belongs in this list.
EVIDENCE_DIRS = ["src", "tests", "docs"]
EVIDENCE_EXCLUDE = (".scratch", "ruby/caver/recovery", ".git", "build")

LEDGER = "src/ruby/caver/recovery/ledger.tsv"
CENSUS = "src/ruby/caver/recovery/census_by_class.tsv"

# ── Symbol classification ────────────────────────────────────────────────────

THUNK_RE = re.compile(r"^(?:non-virtual thunk to|virtual thunk to)'?")
# `Caver::Foo::Bar(args)` → owner `Foo`, leaf `Bar`. A free function in the
# namespace (`Caver::StringForTrigger(...)`) has no owner.
CAVER_RE = re.compile(r"^Caver::(.+)$")


def demangled_leaf(name: str) -> str:
    """`Caver::Foo::Bar(int)` → `Bar`; `Caver::Foo::Bar` → `Bar`."""
    head = name.split("(", 1)[0]
    return head.rsplit("::", 1)[-1]


def owner_of(demangled: str) -> str:
    """`Caver::CharController::Run(...)` → `CharController`; free → `<free>`."""
    head = demangled.split("(", 1)[0]
    parts = head.split("::")
    # parts[0] == 'Caver'
    if len(parts) <= 2:
        return "<free>"
    return parts[1]


def namespace_owner(demangled: str) -> str:
    """`Caver::Proto::SceneObject::kFoo` → `Proto::SceneObject` (for the bucket)."""
    head = demangled.split("(", 1)[0]
    parts = head.split("::")
    if len(parts) <= 2:
        return "<free>"
    return "::".join(parts[1:-1]) or "<free>"


# Template arguments and nested-template noise make a name a poor key for
# dedup, so normalise before counting: `boost::shared_ptr<Caver::Foo>` is one
# symbol per instantiation type but the *work* is one port.
NORMALISE_RE = re.compile(r"<[^<>]*>")


def normalised_key(demangled: str) -> str:
    prev = None
    out = demangled
    while prev != out:                      # strip nested templates innermost-out
        prev = out
        out = NORMALISE_RE.sub("<>", out)
    return out


def is_third_party(demangled: str) -> str | None:
    """Which vendor owns this symbol, or None when it is Caver's own."""
    for vendor in ("boost::", "std::", "__gnu_cxx::", "google::", "testing::"):
        if demangled.startswith(vendor):
            return vendor.rstrip(":")
    if "Caver" not in demangled:
        return "other"
    return None


def is_generated_proto(demangled: str) -> bool:
    """protobuf's generated accessors — 3.5k of the rows, none actual work."""
    return demangled.startswith("Caver::Proto::") and (
        "kExtensionFieldNumber" not in demangled
    )


# ── Index parsing ────────────────────────────────────────────────────────────


def normalise_addr(text: str) -> str:
    """`0x3973E0`, `00000000003973E0`, `3973E0` all → `3973e0`."""
    try:
        return format(int(text, 16), "x")
    except ValueError:
        return ""


def parse_index(arg: str, rel: str):
    """→ list of dicts. Skips rows whose demangled name is unusable."""
    path = os.path.join(REPO, rel)
    if not os.path.exists(path):
        print(f"  ! missing index: {rel}", file=sys.stderr)
        return []
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        header = fh.readline().rstrip("\n").split("\t")
        try:
            c_va = header.index("address")
            c_mangled = header.index("mangled")
            c_dem = header.index("demangled")
            c_file = header.index("file")
        except ValueError:
            return []
        for line in fh:
            parts = line.rstrip("\n").split("\t")
            if len(parts) <= c_file:
                continue
            dem = parts[c_dem].strip()
            if not dem:
                continue
            rows.append(
                {
                    "abi": arg,
                    "va": normalise_addr(parts[c_va]),
                    "mangled": parts[c_mangled].strip(),
                    "demangled": THUNK_RE.sub("", dem),
                    "body": parts[c_file].strip(),
                }
            )
    return rows


# ── Evidence ─────────────────────────────────────────────────────────────────


def gather_evidence():
    """Scan the tree once, returning (addresses, identifier set).

    Addresses are matched as hex words so `0x28EFB8`, `0x0028EFB8` and `28EFB8`
    all land on the same key.

    Names are matched through an **inverted index of identifiers**, not by
    testing each symbol against the corpus: 6.5k symbols × two spellings × ~10⁸
    bytes of prose is minutes of substring scanning, and it re-reads the same
    bytes for every symbol. One pass that harvests every `A::B::C`-shaped word
    costs O(bytes) and makes each lookup a hash hit. A citation is a word in the
    text, so this loses nothing: the house provenance comment is
    `//   Caver::Foo::Bar  0x…`, and `Caver::Foo::Bar` is exactly such a word.
    """
    addrs = set()
    tokens = set()
    addr_re = re.compile(r"\b0x([0-9A-Fa-f]{5,16})\b")
    # `Foo::Bar::baz` — at least two characters, colons allowed, no spaces.
    ident_re = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*")
    for root in EVIDENCE_DIRS:
        for dirpath, dirnames, filenames in os.walk(os.path.join(REPO, root)):
            dirnames[:] = [
                d for d in dirnames
                if not any(x in os.path.join(dirpath, d) for x in EVIDENCE_EXCLUDE)
            ]
            for fn in filenames:
                if not fn.endswith((".c", ".cpp", ".h", ".hpp", ".py", ".md", ".tsv")):
                    continue
                full = os.path.join(dirpath, fn)
                if any(x in full for x in EVIDENCE_EXCLUDE):
                    continue
                try:
                    with open(full, "r", encoding="utf-8", errors="replace") as fh:
                        text = fh.read()
                except OSError:
                    continue
                for m in addr_re.finditer(text):
                    addrs.add(normalise_addr(m.group(1)))
                tokens.update(ident_re.findall(text))
    return addrs, tokens


def symbol_keys(demangled: str):
    """Every spelling of this symbol a comment could plausibly use.

    The engine namespace is dropped inside `namespace caver`, so
    `Caver::CharControllerComponent::Run` is very often cited as
    `CharControllerComponent::Run`. Those are different strings, and testing
    only one of them is how a census reports 0% for work that is on disk.
    """
    keys = []
    for spelling in (demangled, demangled.split("(", 1)[0]):
        if not spelling:
            continue
        keys.append(spelling)
        if spelling.startswith("Caver::"):
            keys.append(spelling[len("Caver::"):])
    # Destructors/operators carry a space (`operator new`) that mangled-name
    # citations do not; the head-with-args form above already covers the rest.
    return list(dict.fromkeys(keys))


def evidence_for(row, addrs, tokens):
    """`addr` / `name` / None — how this symbol is provably already in the tree."""
    if row["va"] and row["va"] in addrs:
        return "addr"
    for key in symbol_keys(row["demangled"]):
        if len(key) >= 8 and key in tokens:
            return "name"
    return None


# ── Report ───────────────────────────────────────────────────────────────────

# Classes whose symbol weight is mostly mechanical rather than behavioural. They
# are reported, but ranked last as targets: "recovering" them means writing a
# protobuf clone, not recovering engine behaviour.
LOW_VALUE_PREFIXES = ("Proto", "Proto::")
NOISE_OWNERS = {"<free>", "0", ""}


def rank_targets(by_class, min_symbols=3):
    """Unrecovered classes, most worth doing first.

    Score = symbol count, halved repeatedly for how much of the class is already
    there (a class 80% done is a worse next target than one at 0%), and suppressed
    for the mechanical buckets.
    """
    out = []
    for owner, rows in by_class.items():
        total = len(rows)
        if total < min_symbols:
            continue
        done = sum(1 for r in rows if r["status"] in ("addr", "name"))
        if done == total:
            continue
        if owner.startswith(LOW_VALUE_PREFIXES):
            continue
        ratio = done / total
        score = total * (1.0 - ratio) ** 2
        out.append((score, owner, total, done, ratio))
    out.sort(reverse=True)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", action="store_true", help="write the ledgers")
    ap.add_argument("--targets", type=int, default=25, help="next-target rows")
    ap.add_argument("--class", dest="klass", help="detail one class")
    ap.add_argument("--body", help="show the decompiled body path for a symbol")
    ap.add_argument("--min-symbols", type=int, default=3,
                    help="ignore classes smaller than this when ranking")
    args = ap.parse_args()

    print("Caver recovery census")
    print("=" * 78)

    raw = []
    for abi, rel in INDEXES:
        rows = parse_index(abi, rel)
        print(f"  {abi:9s} {len(rows):7d} indexed functions   ({rel})")
        raw.extend(rows)

    # Dedup across ABIs on the normalised name: the same function exists in both
    # binaries, and counting it twice would inflate every total.
    Caver_rows = {}
    for row in raw:
        if is_third_party(row["demangled"]):
            continue
        if is_generated_proto(row["demangled"]):
            continue
        key = normalised_key(row["demangled"])
        prev = Caver_rows.get(key)
        if prev is None:
            row["abis"] = {row["abi"]}
            row["va_by_abi"] = {row["abi"]: row["va"]}
            Caver_rows[key] = row
        else:
            prev["abis"].add(row["abi"])
            prev["va_by_abi"][row["abi"]] = row["va"]

    rows = list(Caver_rows.values())
    print(f"  {'Caver own':9s} {len(rows):7d} distinct symbols (both ABIs, deduped)")

    print("\n  scanning repo for evidence …")
    addrs, tokens = gather_evidence()
    print(f"  {len(addrs)} distinct addresses, {len(tokens)} distinct identifiers")

    for row in rows:
        row["owner"] = namespace_owner(row["demangled"])
        row["status"] = evidence_for(row, addrs, tokens) or "unseen"

    by_class = defaultdict(list)
    for row in rows:
        by_class[row["owner"]].append(row)

    done = sum(1 for r in rows if r["status"] != "unseen")
    print("\n" + "-" * 78)
    print(f"RECOVERED (address or name in tree)  {done:6d} / {len(rows):6d}  "
          f"= {100.0 * done / max(1, len(rows)):5.1f}%")
    n_addr = sum(1 for r in rows if r["status"] == "addr")
    n_name = done - n_addr
    print(f"  of which address-cited (a VA in the tree)     {n_addr:6d}")
    print(f"  name-only (the symbol is named in the tree)  {n_name:6d}")
    print(f"UNSEEN                                       {len(rows) - done:6d}")
    print()
    print("  NOTE: this is a floor, not the port's real size. Prose that cites a")
    print("  symbol or its VA counts; behaviour ported under a new name does not,")
    print("  so an 'unseen' row means 'no citation found', not 'not implemented'.")

    # ── per-class table ──────────────────────────────────────────────────────
    table = []
    for owner, crows in by_class.items():
        cdone = sum(1 for r in crows if r["status"] != "unseen")
        table.append((len(crows), owner, cdone, len(crows) - cdone))
    table.sort(reverse=True)

    print("\n" + "-" * 78)
    print("TOP CLASSES BY SYMBOL WEIGHT")
    print(f"  {'symbols':>7} {'done':>6} {'todo':>6}  class")
    for total, owner, cdone, ctodo in table[:30]:
        print(f"  {total:7d} {cdone:6d} {ctodo:6d}  {owner}")

    # ── next targets ─────────────────────────────────────────────────────────
    targets = rank_targets(by_class, args.min_symbols)
    print("\n" + "-" * 78)
    print(f"NEXT TARGETS (unrecovered classes, ranked; score = weight × unfinished²)")
    print(f"  {'score':>8} {'sym':>5} {'done':>5}  class")
    for score, owner, total, cdone, ratio in targets[: args.targets]:
        print(f"  {score:8.1f} {total:5d} {cdone:5d}  "
              f"{owner}  ({100.0 * ratio:4.1f}% done)")

    if args.klass:
        want = args.klass
        print("\n" + "-" * 78)
        print(f"CLASS DETAIL: {want}")
        sel = [r for r in rows if r["owner"] == want or r["owner"].endswith("::" + want)]
        if not sel:
            print("  (no such class)")
        for r in sorted(sel, key=lambda r: r["demangled"]):
            abis = "+".join(sorted(r["abis"]))
            print(f"  [{r['status']:6s}] {abis:7s} {r['demangled']}")
            if r["status"] == "unseen" and r.get("body"):
                print(f"           {r['body']}")

    if args.body:
        needle = args.body
        print("\n" + "-" * 78)
        print(f"BODY LOOKUP: {needle}")
        hits = 0
        for r in rows:
            if needle in r["demangled"]:
                for abi, va in sorted(r["va_by_abi"].items()):
                    idx = next(i for i, _ in INDEXES if i == abi)
                    body = r["body"]
                    if not body.startswith("functions/"):
                        body = ""
                    print(f"  {abi}  {va:>16s}  {os.path.join('OpenSwordigo', idx, body)}")
                hits += 1
        if not hits:
            print("  (no match)")

    if args.write:
        led = os.path.join(REPO, LEDGER)
        cen = os.path.join(REPO, CENSUS)
        os.makedirs(os.path.dirname(led), exist_ok=True)
        with open(led, "w", encoding="utf-8") as fh:
            fh.write("# Caver recovery ledger — generated by tools/caver_recovery_census.py\n")
            fh.write("# status: addr = the VA is cited in the tree; name = the symbol name is;\n")
            fh.write("#         unseen = neither. abis = which binaries define it.\n")
            fh.write("status\tabis\tva_arm64\tva_arm32\tclass\tsymbol\tbody\n")
            for r in sorted(rows, key=lambda r: (r["owner"], r["demangled"])):
                fh.write("\t".join([
                    r["status"],
                    "+".join(sorted(r["abis"])),
                    r["va_by_abi"].get("arm64_13", ""),
                    r["va_by_abi"].get("arm32_13", ""),
                    r["owner"],
                    r["demangled"],
                    r.get("body", ""),
                ]) + "\n")
        with open(cen, "w", encoding="utf-8") as fh:
            fh.write("# Caver recovery by class — generated by tools/caver_recovery_census.py\n")
            fh.write("symbols\tdone\ttodo\tclass\n")
            for total, owner, cdone, ctodo in table:
                fh.write(f"{total}\t{cdone}\t{ctodo}\t{owner}\n")
        print(f"\nwrote {LEDGER}\nwrote {CENSUS}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
