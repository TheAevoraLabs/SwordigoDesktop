# GUILabel + FontText — bitmap text rendering (v1.4.13 arm64)

Sources: `OpenSwordigo/arm64_13/functions/Caver/GUILabel/*.c`, `Font/`,
`GameInterfaceBuilder/{NormalLabel,SmallLabel}.c`, `docs/opensw_exclusive/10_gui_layout_fonts_and_ui_system.md`.

Confidence: **verified** = read in decompiled body · **inferred** · **guess**.

---

## 1. Size & construction (verified)

`GUILabel` = **0x170 = 368 bytes** (`operator new(0x170u)` in
`GameInterfaceBuilder::NormalLabel` `0x41C894` and `SmallLabel` `0x41CD8C`).
Ctor `0x5202CC`, vtable `off_634CB0`.

Ctor sequence (verified):

- base `GUIView::GUIView`
- `byte +0xF5 (245) = 0` → **userInteractionEnabled = false** (labels never take touch)
- `byte +0x12B (299) = -1`, `byte +0x158 (344) = 0`, `dword +0x15C (348) = 0`
- zero `+0x100..0x11F`, `+0x11B..0x12A`, `+0x130..0x14F`
- `qword +0x150 (336) = 0`, `dword +0x154 (340) = 1`  (packed: `0x400000000`)
- `byte +0x160 (352) = 0`
- `shared_ptr<FontText>::reset` at **`+0x140 (320)`** → owns a **FontText (0xF0 = 240 B)**

## 2. `GUILabel` field map

| Off | Type | Name | Confidence |
|---|---|---|---|
| `+0xF5 (245)` | `byte` | interaction off by default | verified |
| `+0x100..0x14F` | text run list / formatting | zeroed | inferred |
| `+0x140 (320)` / `+0x148 (328)` | `shared_ptr<FontText>` | **the text object** (its own 240-byte buffer) | verified |
| `+0x150 (336)` / `+0x154 (340)` | int pair | alignment / scale knobs (`= 0, 1` default) | inferred |
| `+0x158 (344)` | `byte` | layout dirty | inferred |
| `+0x15C (348)` | `int` | per-run flag | inferred |
| `+0x160 (352)` | `byte` | shadow/outline toggle | guess |

## 3. Methods (1.4.13 addresses)

| Method | Addr |
|---|---|
| `setText` (free fn) | `0x41CC5C` |
| `DrawRect` | `0x520448` |
| `UpdateText` | `0x5204FC` (also called by `GUIButton::DrawRect`) |
| `UpdateBounds` | `0x5207B4` |
| `ClearText` | `0x5208FC` |
| `AddText(str)` / `AddText(str, …)` ×2 | `0x52092C` / `0x520A68` / `0x520A9C` |
| `SetBounds` | `0x520B44` |
| `TextSize` | `0x520B80` — used everywhere to size buttons/labels in layout |
| `TextBounds` | `0x520BA0` |
| `BoundsForCharacterAtIndex` | `0x520BC0` |

`AddText` has **three overloads** (colour / gradient / styled runs) — this is what powers
`GameInterfaceBuilder::TextGradient`.

## 4. Font system

- `Font` / `Font::Glyph` are protobuf messages — `Caver.Proto.Font`, `Caver.Proto.Font.Glyph`
  (RTTI `typeinfo for Caver::Proto::Font`), decoded from the game's font files.
  Field tags documented in
  [docs/opensw_exclusive/10_gui_layout_fonts_and_ui_system.md](../../opensw_exclusive/10_gui_layout_fonts_and_ui_system.md):
  glyph `char_code` tag `0x08`, `draw_bounds` `0x12`, `horiz_advance` `0x18`,
  `texture_bounds` `0x22`; font `name` `0x0A`, `height` `0x1C`, `bounding_box` `0x20`,
  `kerning_table` `0x16`, `glyphs` `0x1A`.
- `FontLibrary` singleton: `sharedLibrary` `0x4C33B0`, `DefaultFont()` `0x4C4364`,
  `SmallDefaultFont()` `0x4C44A8` (1.4.13 VAs).
- `FontText` (240 B) is the draw buffer: `Draw` `0x4C66DC`, `AddText` `0x4C53E4`,
  `Clear` `0x4C5124`, `SetColor` `0x4C5170`, `Translate` `0x4C6210`,
  `AlignHorizontally` `0x4C6280`.
- `GUILabel::UpdateText` rebuilds the `FontText` from the run list; `GUIButton::DrawRect`
  calls it every frame before drawing text.

## 5. Factory recipes (verified)

| Factory | Addr | What it does |
|---|---|---|
| `NormalLabel(str, colorA, colorB)` | `0x41C894` | `new GUILabel (0x170)` + default font + two-colour gradient |
| `SmallLabel(str, colorA, colorB)` | `0x41CD8C` | same with the small font |
| `TextGradient(...)` | `0x41F07C` | 2× `0x18` helper objects (gradient stops) |

No textures — **labels are pure bitmap-font quads**, so any custom screen only needs a
font (from `FontLibrary`) plus `AddText` to get authentic typography.

## 6. Proposed SRE hooks (sre13)

| Target | Addr | Purpose |
|---|---|---|
| `GUILabel::DrawRect` | `0x520448` | text override / recolour (sre12 already does a text-override table in its GUI hook) |
| `GUILabel::UpdateText` | `0x5204FC` | intercept rebuild to inject dynamic values (FPS, coords, counters) |
| `FontLibrary::DefaultFont` | `0x4C4364` | global font swap (theme) |
