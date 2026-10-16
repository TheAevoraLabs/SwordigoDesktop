# SRE13 — Swordigo Engine Internals Reference

Reverse-engineered documentation of the internal structs, vtables and exported
symbols of `libswordigo.so` (Swordigo Desktop engine, **ARM64, v1.4.13**),
recovered from:

- IDA Pro / Hex-Rays decompilation in `OpenSwordigo/arm64_13/`
  (per-function `.c` files under `functions/Caver/<Class>/`)
- `nm -D`/`objdump` of
  `~/.local/share/swordigo-desktop/engine/v1.4.13/arm64-v8a/libswordigo.so`
- Relocation-applied vtable dumps (see the scripts pattern in `/tmp/vtables_full.py`)
- Live hooking of the game's Lua runtime (the original `src/sre/sre13/caver/*.h`
  headers)

## Purpose

Two audiences:

1. **Onboarding** for SRE contributors — how each subsystem is laid out, who
   owns what, and how the pieces tick per frame.
2. **Design** for expanding SRE's runtime control surface — camera overrides,
   character-state injection, scene/level manipulation, save-state tooling.

## Confidence legend

Every field/function in these docs carries one of:

- **verified** — confirmed in IDA pseudocode (ctor/decompiled body read).
- **inferred** — deduced from call sites, vtable scans, or adjacent functions;
  not read directly.
- **guess** — hypothesis with no direct evidence; do not build on it.

## Subsystem index

| Doc | Covers | Key verified facts |
|---|---|---|
| [Camera.md](Camera.md) | Projection/view matrices, aspect, ortho/perspective | `viewProjection` at +0xAC, projection at +0x4C, `EvaluateViewMatrix` |
| [CameraController.md](CameraController.md) | Focus/follow/rumble state machine | `followObject` at +0x68 (header said 0x60), follow lerp fields, `GotoTargetImmediately` |
| [CaverShell.md](CaverShell.md) | Application bootstrap, game engine shell | vtable, `InitApplication`, pause/lifecycle globals |
| [CharacterState.md](CharacterState.md) | Hero stats (level, XP, health, mana, skill points) | XP at +0x8C/level at +0x90 (Save path) vs +0x9C/+0xA0 (GSC reads) — **documented quirk**, 0x60-byte object |
| [Component.md](Component.md) | Component base + vtable | 10-slot base vtable at 0x61C018, `label` at +0x38, refcount at +0x08 |
| [GameSceneController.md](GameSceneController.md) | Scene controller: hero, camera, state | hero at +0xD8, embedded `CameraController` at +0x38 (header said separate ptr), `GameState*` at +0x08 |
| [GameState.md](GameState.md) | Persisted game state | stats at +0x90..+0xAC, `nodesBegin/nodesEnd` free list, `GameState_Clear` symbol present but **not exported** |
| [GameViewController.md](GameViewController.md) | Main game view controller | scene/state wiring, `LoadGameState` copies stats at gs+0x90..+0xAC |
| [ProgramState.md](ProgramState.md) | Lua execution context (coroutines) | `lua_State*` at +0x00, child-list at +0x10, `parentObject` intrusive_ptr at +0x28, state/wait/speed block at +0x50–0x5C, size 0x60 |
| [Scene.md](Scene.md) | World container | embedded `ProgramState` at +0x28, `pauseCount` at +0x20, `objectLibrary` at +0xA8, object/group trees at +0xB8/+0xD0, SceneGrids at +0x2D8/+0x318, 8-slot vtable |
| [PlayerProfile.md](PlayerProfile.md) | Save slot | `identifier` at +0x18, `level` at +0x68, `timePlayed` double at +0x70, `gameState*` at +0xF0, counters `std::map` at +0x100 |
| [RenderingContext.md](RenderingContext.md) | GL state wrapper | `api` at +0x00, global current-context at `qword_651860`, budget/state flags; field mapping partially inferred |
| [ModelLibrary.md](ModelLibrary.md) | Model asset cache (singleton) | 0x48-byte object, three tree headers at +0x00/+0x18/+0x30, `ModelForName` POD-load path |
| [TextureLibrary.md](TextureLibrary.md) | Texture asset cache (singleton) | textures map at +0x08, memory budget `1e9` at +0x24, unused-textures vector at +0x30 |
| [SceneObject.md](SceneObject.md) | Base scene entity | 10-slot vtable + IBindable vtable at +0x10, `scene` at +0x20, `position` Vector3 at +0x80, `rotation` at +0x90, `scale` at +0x9C, `localAABB` at +0xA4, components vector at +0xD0, flags at +0x103/+0x104 |
| [GUIView.md](GUIView.md) | GUI base node: frame/bounds/subviews/layout | frame +0x84, bounds +0x94, touchBounds +0xA4, superview +0x60, hidden +0xF4, clips +0xF8, subview list +0x20, `safeBounds` inset +0x38 |
| [GUIButton.md](GUIButton.md) | `GUIControl` events + button states/textures/sound | flags +0x118 (bit0 pressed, bit1 brighten), normal/highlight/bg rects +0x120/+0x128/+0x130, tint +0x1B4, press sound `std::string` +0x1B8, touch id +0x1D0, events `1`/`64`/`0x200000` |
| [GUISlider.md](GUISlider.md) | `GUISlider` + `GUISwitch` (both 0x168) | slider value +0x11C, min/max +0x12C/+0x130 (default 0..100), frame/bar/peg textures +0x138/+0x140/+0x148, dirty +0x158, round flag +0x164 |
| [GUILabel.md](GUILabel.md) | Bitmap text + `FontText` | owns `FontText` (0xF0) at +0x140, interaction off by default (+0xF5), `TextSize` used by every layout pass |
| [GUIFrameView.md](GUIFrameView.md) | Drawing primitives: frame/textured/rounded | `Texture` size +0x74/+0x78, nine-patch `GUIRoundedRect` (0xA8) with 18-vert sprite at +0x50, `GUIFrameView` textured +0x100 / rounded +0x108 |

## Cross-cutting facts worth remembering

- **Refcounts** are `boost::intrusive_ptr` style: an object's count lives at
  `obj+0x08` (SceneObject, Component, Texture) and the deleting destructor is
  always vtable slot +0x08 (`vtable[1]`).
- **Shared state ownership**: `GameSceneController` holds `GameState*` at
  +0x08, `CameraController` embedded at +0x38, hero `SceneObject*` at +0xD8.
  `GameState` embeds `CharacterState` at +0x10 and `PlayerProfile` embeds
  `GameState*` at +0xF0.
- **Lua roots**: every `Scene` embeds a root `ProgramState` at +0x28;
  every scripted `SceneObject` embeds one at +0x118 (per the component model;
  see `ProgramState.md`).
- **The XP/level offset quirk**: `CharacterState::SaveToProtobufMessage` writes
  level at cs+0x90/XP at cs+0x8C while `GameSceneController::Update` reads
  level at `GameState+0xA0` (= `CharacterState+0xA0`? no — `GameState+0x10` is
  the embedded CharacterState, so `CharacterState+0xA0` would be `GameState+0xB0`;
  the +0xA0 read is GameState-relative). See `CharacterState.md` for the full
  reconciliation; treat stats offsets as version-locked.
- **Missing symbols**: `GameState_Clear` is resolved by SRE but is not in the
  dynamic symbol table of v1.4.13 (present in `.symtab`/code, not exported).
- **Vtable dumps** were produced by applying `.rela.dyn` (R_AARCH64_RELATIVE +
  R_AARCH64_ABS64) over the file image of segment 02; the same technique is
  reusable for any class with a ctor that stores `off_XXXXXX`.
- **GUI is not data-driven today, but could be.** The HUD (`GameOverlayView`) drives
  its children through a runtime-built `GUIViewLayout` tree at `this+624` (identifier
  index + margins + per-node scale), and `Caver.Proto.GUIViewLayout` serialisation
  exists with **no callers**. Menus are built in code via `GameInterfaceBuilder`.
  See `docs/game/caver/gui/layout_reference.md` and
  `docs/game/caver/gui/data_driven_gui_design.md`.
- **sre12 already replaces the GUI render path** (`sre_gui.c` / `sre_gui_native.c`,
  full `DrawRect` hook table) — **libsre13.so has no GUI subsystem at all**; that port
  is the enabling step for any moddable-GUI work.

## Workflow for adding a new subsystem doc

1. `ls OpenSwordigo/arm64_13/functions/Caver/<Class>/` — read the ctor first
   (it is the ground truth for layout).
2. Read `SaveToProtobufMessage`/`LoadFromProtobufMessage` if present — they
   pin field semantics via the proto field numbers.
3. Dump the vtable with the relocation script and cross-reference slots against
   the functions the decompiled call sites invoke via `vtable + N`.
4. Reconcile against the existing `src/sre/sre13/caver/*.h` header and **call
   out every discrepancy explicitly** — the headers are ground truth to
   reconcile against, not to trust blindly.
5. Always end with "Proposed SRE hooks" and flag anything that must be an
   accessor function rather than a raw field write.