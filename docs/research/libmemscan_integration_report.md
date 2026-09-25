# libmemscan / PINCE — what to take, what to leave, and what was ported

Source: `libmemscan-master/` (Zig, **LGPL-3.0**), the scan engine behind PINCE
(GPL-3.0). This supersedes the earlier `pince_integration_report.md` in one
important way: **the engine is no longer missing.** The first report had to work
from the Python GUI alone because the `libmemscan` submodule was empty. The real
thing is now present — 348 KB of Zig across `scanner.zig` (287 KB),
`targetmem.zig`, `scanroutines.zig`, `pointerscan.zig`, `value.zig`,
`process.zig` — and it changes the ranking of what is worth taking.

Licence: libmemscan is LGPL-3.0, PINCE is GPL-3.0. Porting into our GPLv3
component (`src/game/research/**`) is compatible. The MIT components
(`src/loader/**`, `src/jni/**`) and the ARR SRE must not receive any of it —
which is why everything below lands in the research component.

---

## 1. The structural difference, stated once

libmemscan is a **cross-process** scanner: `process.zig` handles ptrace,
`/proc/<pid>/maps`, `RegionKind` classification, `ReadProcessMemory` equivalents,
and a `MemoryCache` that exists purely because a positional read is expensive.

Swordfare owns the guest. `g_guest_memory` is one flat host buffer, so a "first
scan" is a linear pass over an array — no region walk, no syscall, no cache
miss, no page-fault handling. **None of `process.zig` applies.** That is roughly
a third of the tree, and it is the third that carries all the ptrace complexity.

What survives, and what is where the value actually is: the **model layer**
(`value.zig`, `scanroutines.validateCombo`, `MatchFlags`) and the **performance
engineering for very large match sets** (`targetmem.zig`, `scanner.zig`).

---

## 2. Ported in this change

### `MatchFlags` → `LiteralParse::fits` (from `value.zig`)

The central idea, and the fix for a real bug the operator hit: **user text must
not be forced into one type at parse time.**

`value.zig:parseInt("255")` records that the value is representable as
u8/u16/u32/u64 **and** s16/s32/s64, but *not* s8 — it sets eight independent
boolean flags rather than picking a width. `MatchFlags.i8b` etc. are named
combinations, and `possibleMatchBits(data_type, user_value)` intersects that
against what the selected scan type can even consider.

Our port is `LiteralParse` in `mem_scanner.h`:

```
fits        // bitmask: every numeric width this literal is representable in
narrowest   // smallest width that fits
fits_type() // exact integer-width test
fits_as()   // ...plus floats, because "100" is how people write 100.0
fits_desc() // "2 Bytes, 4 Bytes, 8 Bytes" — names what WOULD have worked
```

It exists because the old edit box seeded itself from `read_value()` — a
*rendering*, `"32 (0x20)"` — and `parse_typed_text` then rejected that exact
string. Every write failed, the editor closed, and the cell redisplayed the old
value. From outside that is indistinguishable from "writes don't work", and the
only diagnostic available was `does not fit Dword`, which names the one width
the operator had already chosen and says nothing about what would have worked.

**Deliberate divergence from libmemscan:** `parseFloatAndBackfillInts` backfills
integer candidates from a float literal, so `12.75` also matches byte `12`. That
is right for a scan — you want to find the thing — and wrong for a write, where
it means 12 lands in memory because someone typed 12.5. Our `fits` mask is
lossless-only: `12.75` fits `Float`/`Double` and nothing else. Recorded in the
code comment so the divergence is not "fixed" back later by someone reading
libmemscan.

### `validateCombo` → `scan_type_supported` / `_unsupported_reason`

`scanroutines.validateCombo(data_type, match_type, user_values)` refuses
combinations before scanning rather than during. Three rules, all ported:

| libmemscan rule | our analogue |
|---|---|
| `BYTEARRAY`/`STRING` support only `MATCHANY`/`MATCHEQUALTO`/`MATCHUPDATE` | `Str`/`AoB` support `Exact` only — variable-length data has no ordering or arithmetic |
| equality/threshold/`*BY` need a value; `MATCHRANGE` needs two | `scan_type_needs_second_value()` (only `Between`) |
| delta/change filters return a `null` kernel on an initial scan | `scan_type_needs_previous()`; unsupported on a first scan |

**This caught a live bug.** Our `first_scan` did this:

```cpp
const ScanType seed_scan = scan_seeds(opts.scan) ? opts.scan : ScanType::UnknownInitial;
```

Pick "Increased Value" on a first scan and it silently seeded *every aligned
slot* — no error, no note, and a full result table that looks like the filter
worked. `ScanStats` now carries `requested`, `applied` and `filter_ignored`, and
the UI states the drop in words. The scan-type combo is also filtered to what
can run, with the count of hidden filters and the reason on hover.

### Scan scope (`process.zig:ScanLevel`) — confirmed, not yet ported

`ScanLevel` is `ALL` / `ALL_RW` / `HEAP_STACK_EXE` / `HEAP_STACK_EXE_BSS`, and
the default is deliberately *not* "everything". This is the same finding as the
PINCE report arrived at from the opposite direction: we measured 8 of 12 hits in
a module-scoped scan landing inside `.dynsym`/`.rela.plt`, and patched the
**output** side (`skip ELF tables`). Scope fixes the **input** side. Still open.

---

## 3. Worth porting next, in priority order

**P1 — `MatchesArray` chunked storage (`targetmem.zig`).** The real engineering
insight in the repo. Matches live in aligned "swaths" inside chunks, with
`stride: u16` per entry and `appendRun(address, old_values, raw_bits, count)`
storing a *contiguous run* of addresses as one header plus a packed payload.
`MatchRecord` costs `stride` bytes, not a struct + allocation.

> Ours is `std::vector<ScanCandidate>` at ~24 bytes/entry with a 200k cap and an
> "too large to keep" fallback on saved shots. A run-encoded store would let a
> first scan over a 40 MB image keep its full set instead of dropping it, which
> is exactly the case where the multi-shot workflow gets most useful. This is
> the single highest-value port left.

**P2 — kernel selection (`scanroutines.zig`).** `pickInitialNumericKernel`,
`pickFixedDeltaKernel`, `pickFixedCompareKernel`, `pickAnyNumberCompareKernel`
return a *function pointer* chosen once, comptime-specialised per (type × filter
× endianness), so the inner loop has no per-position branch. Our scan loop
branches on `ScanType` per position. Directly measurable on an unoptimised
build; worth doing after P1.

**P3 — `MemoryCache` (`scanner.zig`).** A 128 KB two-chunk rolling cache with a
required-length fallback. For us this is **free** — we can read `g_guest_memory`
directly — so the port is not the cache but the *lesson*: our `next_scan` is
random-access over a candidate set, and on a 4 GiB buffer those reads miss L2
constantly. A sequential prefetch pass over sorted candidates would get the same
win without the syscall the cache exists to avoid.

**P4 — undo metadata + `RescanSpanWriter` (`scanner.zig`).** An explicit undo log
for a scan, plus span-based output. We have `restore_pass()`, which is the same
idea at shot granularity; theirs is per-operation, so a single mistaken
`next_scan` is undoable without having saved a shot. Natural fit for our SHOTS
table — "undo the last narrowing" is a one-row operation we currently make the
operator do by hand.

**P5 — `reverse_endianness` as a scan option.** We are little-endian ARM64 and
store little-endian, so it looks unnecessary — but Swordigo serialises data, and
a big-endian value inside a save blob or a POD payload is a legitimate thing to
search for. Cheap to add once the kernel dispatch from P2 exists.

**P6 — `pointerscan.zig`.** The largest untouched file, and the one I would
argue *against* porting yet. It maps pointer paths, compares pointer maps across
runs, and resolves module bases — all to answer "which chain of pointers reaches
this address?". Our identity layer plus the symbol table largely replaces the
question: `StructDecoder` walks known layouts by offset, and `mem_identity.h`
gives a deterministic name to a field. Pointer chains matter for a struct we
have *not* recovered — which is precisely what `unclaimed_explorer` is for. Revisit
if the explorer proves insufficient for deep heap graphs.

**P7 — `RegionKind` classification (`process.zig`).** Their `MISC`/heap/stack/exe/
anon tagging is what makes `ScanLevel` meaningful. We already derive sections
from `shdr` (`elf_sections.h`) and have `ModuleExtent`; extending that to a
region-kind tag is what P1's scope work would need.

---

## 4. What we should *not* take

- **`process.zig`** — ptrace, `/proc/<pid>/maps`, `ProcessHandle`. We have no
  target process.
- **`MemoryCache` as written** — it exists to amortise syscalls we do not make.
  Take the locality lesson (P3), not the code.
- **`build.zig` / the C ABI surface** — the Python bindings (`memscan.py`, 1077
  lines) drive a `libmemscan.so` we would have to build and link a Zig toolchain
  for. Porting the three small concepts above into C++ costs less than adopting
  a build dependency, and keeps the GPL boundary in one directory.

---

## 5. Verification

New checks, all passing in `bin/tests/mem_scanner_test`:

- `[edit: the value in the box must parse back]` — the rendered form
  `"32 (0x20)"` parses to 32; `format_editable` is bare and round-trips; a byte
  with bit 7 set reads `0x8C`, not a sign-extended 64-bit value; `fits`/`fits_desc`
  for 255 / 300 / -200 / 9000000000; live counterparts in both directions;
  `0x`/`0b`/`0o` prefixes; `12.5` stays a float.
- `[scan: unusable filter combinations are refused, not swapped]` — the validity
  table, plus the engine reporting `requested` vs `applied`.
- `[address list: the edit actually writes guest memory]` — the whole complaint
  end to end, including pasting the table's own rendering back in.
