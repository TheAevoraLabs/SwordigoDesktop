# Ruby project structure — PLAN ONLY (not implemented)

Shared by **Ruby GG (Qt6 desktop)** and **Ruby Touch (Android)**. Nothing here is
built yet; this is the design we agreed on, with the open questions called out at
the end.

## Target layout

```
Ruby/Swordigomod/            # project root == git repo root
├── mymod.rbp                # TOML with a magic header; double-click → opens the project
├── properties.toml          # launcher manifest skeleton (name, package, version, icon…)
├── icon.png                 # launcher icon
├── raw/                     # RAW FileRift markup, human-editable
│                            #   every .scl / .scene decoded, untouched by tools
├── trans/                   # git-tracked decoded text, synced BY Ruby from raw/
│   │                        #   only the files that actually changed
│   └── .build/              # encoded protobuf output ← VFS overlays this over project/
├── project/                 # working copy of unzipped APK assets; Ruby edits HERE
│                            #   (in the VFS it sits BELOW trans/)
├── builds/                  # export output
│   ├── v1.1/
│   │   ├── mymodss-v1.1.zip
│   │   ├── properties.toml  # copy of the TOML outside the zip
│   │   └── build.json        # file list + sha256 + vanilla version + timestamp
│   └── vdev/                 # dev builds
└── .git/                     # repo at the project root
```

## The three layers (and why)

| Layer | Written by | Read by | Purpose |
|-------|-----------|---------|---------|
| `raw/` | the user (and Ruby on import) | human only | pristine FileRift markup for manual editing |
| `trans/` | **Ruby** (synced from `raw/`, changes only) | Ruby | git-tracked text; the reviewable diff |
| `trans/.build/` | **Ruby** | VFS | encoded protobuf (`protoc`-equivalent) output |
| `project/` | the user, via Ruby | Ruby | unzipped APK assets — the working copy |

**VFS precedence (top wins):** `trans/.build/` → `trans/` → `project/`. So an
encoded file shadows the loose project copy, which shadows nothing.

## Ownership rule the user stated

- `raw/` and `trans/.build/` are **created by Ruby / Ruby Touch** but **not
  consumed by Ruby** — they exist for the human and for the VFS/git story.
- `raw/` is **rewritten whenever `project/` changes** (this is the part still
  being designed — see open questions).

## Export / packaging (the "last part", plan only)

1. Ruby Touch copies the target Android app (`com.touchfoo.swordigo`) and
   **takes its `assets/` from the runtime**, placing them under `resources/`.
2. Export works by **diff / hash against the VANILLA assets**:
   - compute the vanilla asset hashes once (reference manifest),
   - hash the modified `project/`,
   - pack only the **changed + new** files into the build zip,
   - write `build.json` = { file list, sha256, vanilla version, timestamp }.
3. `properties.toml` is copied both into `builds/<ver>/` and inside the zip.

## Open questions (unresolved — do NOT implement yet)

1. **`raw/` regeneration.** Rewriting `raw/` on every `project/` change
   discards manual edits in `raw/`. Options:
   (a) `raw/` is generated only on import, never regenerated;
   (b) Ruby three-way-merges `raw/` edits forward;
   (c) `raw/` is regenerated but manual edits live only in `trans/`.
2. **`trans/` sync direction.** Is `trans/` the source of truth (edits flow
   `trans/ → .build/ → project/`), or purely an export of `raw/`?
3. **Change detection.** File-mtime vs content-hash vs a per-file ledger in
   `trans/.build/`.
4. **`.rbp` vs `properties.toml`.** What exactly goes in the `.rbp` (just a
   magic header + project id + schema version?) and what stays in
   `properties.toml`.
5. **VCS.** `.gitignore` policy for `project/`, `builds/`, `trans/.build/`,
   and `raw/`.
6. **Vanilla reference.** Where the vanilla asset manifest is stored (in the
   project, or shipped with Ruby) and how it is versioned.
