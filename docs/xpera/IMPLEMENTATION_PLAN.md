# Xpera GUI — Audit & Implementation Plan

> Status: **Phases 0–4 landed.** Xpera is now the applied style (opt-out via
> `SWORDFARE_XPERA_UI=0`) and the F11/F4 toolbox, the Memory Research console,
> the F1 bar and the Lua tooling have been migrated. Build verified clean.
>
> See §8 for exactly what changed and what remains.

---

## 8. Migration progress (as built)

**Phase 0 — scaffold.** `xpera_theme.h`, `xpera_style.h/.cpp`, `xpera_gui.h/.cpp`,
plus `xpera_icons.h` / `xpera_fonts.h`.

**Phase 1 — style driver wired, flag-gated.** Added to the `swordfare` target.
`SwordfareGUI::apply_swordfare_theme()` now ends with `if (xpera::ui_enabled())
 xpera::apply_style();`, so the entire app inherits XperaDark. The original
Swordfare palette is untouched and is used when `SWORDFARE_XPERA_UI=0`.

**Phase 2 — shell + nav rail.**
- `draw_mod_overlay` (F11/F4): the launcher-art banner, backdrop image, icon and
  OS-style close button are **gone**. It is now `xpera::begin_window` (frameless
  chrome: `SWORDFARE` eyebrow, title, subtitle, `F11`/`F4` key hint) with a
  **left nav rail** (`Home · Scene & Display · Scene Shifter · Diagnostics`) plus
  a content card, replacing `BeginTabBar`. The existing working content bodies
  (render/display/PostFX, Scene Shifter, Diagnostics) were carried over verbatim.
- `draw_research_overlay` (Insert): wrapped in the same shell, with a
  “Memory Research / Open [Insert]” affordance in the toolbox rail.
- `MemoryResearchTab::draw()`: tab strip replaced with a nav rail
  (`Catalog · Live Inspector · Object Tree · Hex View · Watchpoints · DB Status`).

**Phase 3 — unified surfaces.**
- `SwordfareTheme::*` is now an alias namespace into `xpera::palette`, so every
  consumer (F1 bar, About, Help) is re-skinned with no structural change.
- F1 `draw_control_panel`: crimson accent removed; accent underline is now
  Xpera indigo→teal; right-aligned live status (`speed`/`PAUSED`) + brand.
- Lua console / script editor / script manager: bespoke navy palettes rebased
  onto Xpera surfaces (prompt = accent, error = danger, output = text_hi).
- F1 `Mods` menu: honest per-control provenance (verified / partial / N/A).

**Phase 4 — vocabulary + cleanup.**
- `xpera_icons.h` centralises the glyph slots; `xpera_fonts.h` documents the one
  type scale.
- `GuiRenderer` (legacy bitmap UI) marked DEPRECATED in `platform/gui.h`.
- *Remaining:* the old `draw_control_panel` / `draw_settings_panel` bodies are
  still parked inside a `/* … */` block in `swordfare_gui.cpp`. They are inert;
  deleting them was left out of this pass to avoid a large error-prone hunk.

**Not in scope / unchanged:** F3 debug HUD (explicitly kept).

### Verify
```
cmake --build build-research --target swordfare_boot -j$(nproc)   # links bin/swordfare
SWORDFARE_XPERA_UI=0 ./bin/swordfare   # legacy Swordfare palette (debug escape hatch)
```

---

## 1. Why Xpera

The current in-game overlays were built ad-hoc, one panel at a time, with a
different palette and layout vocabulary in almost every function. The result is
what the maintainer calls "AI slop": duotone accents (red + cyan), banner/icon
art inside a live game, chunky window close buttons, and tab bars that look
nothing like a professional tool.

The good reference in this repo is **Ruby GG Studio** (`src/ruby/theme/ruby_theme.h`)
— a Qt/QSS "Dark Studio" theme inspired by Blender 4 / UE5 / VS Code:

| Token | Ruby GG | 
|---|---|
| background ramp | `#121316 → #181a1f → #21242b → #282c34` |
| borders | `#2d313b` / `#4b5263` |
| text | `#e5e9f0` / `#9aa2b1` / `#5c6370` |
| accent | `#e06c75` (rose) |
| semantic | success `#98c379`, warning `#e5c07b`, info `#61afef` |

**Xpera** takes that language and pushes it darker, flatter and more restrained:
a neutral charcoal ramp with **one** accent (indigo `#7C6CFF`) and **one**
secondary (teal `#38C7C0`). No duotone, no gradients, no glow. Depth comes only
from value steps; separation comes only from hairlines.

---

## 2. What exists today (overall check)

All overlay UI lives in `src/platform/swordfare_gui.cpp` (~4.7k lines) plus the
legacy bitmap renderer `src/platform/gui.cpp`.

| Surface | Entry point | Key | State |
|---|---|---|---|
| F1 command bar | `draw_control_panel` | F1 | Top menu bar; functional but visually generic; Mods menu mixes verified/unverified controls |
| F3 debug HUD | `draw_debug` | F3 | **Good — leave as-is** (per maintainer) |
| F4 Mod Hub / F11 toolbox | `draw_mod_overlay` | F4 / F11 | Full-screen now; tabs = Home / Options / Scene Shifter / Diagnostics; swipe-y banner + icon art |
| Lua console | `draw_lua_console` | `` ` `` | Dark terminal, own palette |
| Script editor / manager | `draw_lua_script_editor` / `draw_lua_script_manager` | — | Own palettes |
| Memory Research | `draw_research_overlay` | Insert | Own window + inner tab bar |
| About / Help | `draw_about_panel` / `draw_help_panel` | menu | Uses a separate `SwordfareTheme` namespace |
| Legacy bitmap shell | `src/platform/gui.cpp` (`GuiRenderer`) | — | Pre-ImGui; separate look again |

### Concrete problems

1. **Four palettes in one app.** `SwordfareTheme` (namespace in the .cpp),
   ad-hoc `ImVec4(...)` literals scattered through `draw_mod_overlay` and
   `draw_debug`, the launcher theme, and the legacy `GuiRenderer`. No single
   source of truth.
2. **Tab bars everywhere.** `BeginTabBar` is used for the toolbox and for the
   research console. With many panels this becomes a cluttered strip that does
   not scale and reads as "mobile app", not "studio tool".
3. **In-game chrome.** `draw_mod_overlay` draws a 66px banner with launcher art
   and shows a centred panel with a close button (`&open`) — game-art inside the
   live game, exactly what the maintainer dislikes.
4. **Duotone accents.** Red `#E94560` + cyan `#52CEFF` are used as brand colours
   across the toolbox, which is the "slop" signature.
5. **No component vocabulary.** Every panel re-implements cards, pills, section
   headers and key-caps inline, so they never match.
6. **Legacy duplication.** `gui.cpp` and `swordfare_gui.cpp` implement two
   unrelated UIs; `draw_settings_panel` and an old `draw_control_panel` sit in a
   commented-out block at the bottom of the file.

---

## 3. Xpera design language

### Palette (see `xpera_theme.h`)

```
void    #08090B   base   #0E1013   surface  #14171C   elevated #1A1E24
overlay #1F242C   input  #12151A   selected #232A34
border_soft #22262E   border #2C323B   border_focus #3D4653
text_hi #E7EAF0   text_mid #A0A8B4   text_lo #616A77
accent #7C6CFF (indigo)   secondary #38C7C0 (teal)
info #4CC2FF   success #46C88A   warning #E6B23E   danger #F0525F
```

### Rules

- **Depth = value steps.** Never a shadow, never a gradient fill on a panel.
- **Hairlines only.** 1px borders; 2px accent only for the "active" indicator.
- **One accent.** Indigo marks selection/focus; teal is informational only.
- **Flat tabs.** `TabRounding = 0`, selected tab = transparent + overline.
- **Frameless chrome.** No OS title bar, no game icon, no big close button —
  a slim strip with an eyebrow/title/subtitle and a keyboard hint (`Esc`).
- **One spacing grid** (`xpera::metrics`): 8px base, 26px rows, 28px controls,
  10px max radius.
- **Type:** small uppercase labels (`text_lo`), high-contrast values (`text_hi`),
  single body size. No per-panel font experiments.

### Toolkit (`xpera_gui.h`)

`begin_window/end_window` (frameless chrome), `section_header`, `divider`,
`accent_rule`, `begin_card/end_card`, `nav_item`, `toolbar_button`,
`status_pill`, `badge`, `metric`, `keyboard_hint`.

### Style driver (`xpera_style.h`)

`xpera::apply_style(StyleConfig{ ui_scale, rounding, compact, high_contrast })`
is the **only** call that mutates `ImGuiStyle` for Xpera surfaces, so every panel
is identical by construction.

### Verify the scaffold

```
g++ -std=c++17 -fsyntax-only -I src \
    src/platform/xpera/xpera_style.cpp \
    src/platform/xpera/xpera_gui.cpp
```

---

## 4. Target architecture

```
src/platform/xpera/
  xpera_theme.h     palette + metrics + colour helpers      [DONE]
  xpera_style.h/.cpp ImGuiStyle driver                      [DONE]
  xpera_gui.h/.cpp  window chrome + widget toolkit          [DONE]
  xpera_icons.h     (phase 2) single icon vocabulary
  xpera_fonts.h     (phase 2) Inter/mono loading spec
```

Every overlay is rebuilt as a **shell + content** pair:

```
XperaShell (fullscreen or panel)
 ├─ chrome strip (eyebrow/title/subtitle/keyhint)
 ├─ left nav rail  (xpera::nav_item)     ← replaces BeginTabBar
 └─ content surface (xpera::begin_card … )
```

Nav rail instead of a tab bar is the single biggest visual upgrade: it gives the
toolbox a stable, scalable navigation model (Blender/VS Code style) and removes
the "floating tab strip" look.

---

## 5. Migration plan (phased, each phase independently shippable)

### Phase 0 — Scaffold (this change) ✅
- Add `xpera_theme.h`, `xpera_style.h/.cpp`, `xpera_gui.h/.cpp`.
- Compile-verify standalone. Do **not** wire.

### Phase 1 — Adopt the style driver behind a flag
- Add `xpera_style.cpp` to `cmake/components/swordfare.cmake`.
- Add a runtime/GUI toggle `xpera::ui_enabled()` (config + `--xpera-ui`).
- When disabled, `apply_swordfare_theme()` runs unchanged → zero visual risk.
- Call `xpera::apply_style()` once after `ImGui::CreateContext()` in
  `SwordfareGUI::init()` and after font load.

### Phase 2 — Shell + nav rail
- Introduce `XperaShell` (thin wrapper over `xpera::begin_window`).
- Rewrite `draw_mod_overlay` (F11/F4) to shell + nav rail:
  `Home · Display · Scene · Diagnostics` and keep the existing **content**
  bodies (Options / Scene Shifter / Diagnostics) verbatim inside cards.
- Delete the banner/icon art. The chrome strip replaces it.
- Rebuild `draw_research_overlay` with the same shell + nav rail
  (`Catalog · Live · Watchpoints · DB`).

### Phase 3 — Unify the remaining surfaces
- `draw_control_panel` (F1): keep a slim top bar, but style it from the palette
  and give it a real status area (speed / paused / catalog), not a duotone brand.
- `draw_lua_console`, `draw_lua_script_editor`, `draw_lua_script_manager`:
  port to `xpera::` primitives; neutralise their bespoke colours.
- `draw_about_panel` / `draw_help_panel`: port to panel-mode shell.
- Collapse `SwordfareTheme` into `xpera::palette` (keep aliases during the
  transition so nothing breaks).

### Phase 4 — Cleanup
- Remove the commented-out legacy `draw_control_panel` / `draw_settings_panel`
  blocks in `swordfare_gui.cpp`.
- Decide the fate of the legacy bitmap `GuiRenderer` path in `gui.cpp`
  (mark deprecated; it is superseded by ImGui everywhere).
- Add `xpera_icons.h` so icon choice is centralised (F3 keeps its own HUD).

### Non-goals
- **F3 debug HUD is explicitly out of scope** — it stays as it is.
- No behavioural change to cheats/scene shifter/render options in this work.

---

## 6. Risks & mitigations

| Risk | Mitigation |
|---|---|
| A style swap changes every panel at once | Phase 1 gated behind a flag; default stays Swordfare |
| Nav-rail rewrite of `draw_mod_overlay` regresses working controls | Move existing content bodies verbatim; only the shell changes |
| Font/DPI differences at 4K | `StyleConfig::ui_scale` + `ScaleAllSizes`; test at 1.0/1.25/1.5 |
| Two styles coexisting causes confusion | Document the switch; keep Xpera opt-in until Phase 3 lands |
| Vulkan vs OpenGL path differences | Style is backend-agnostic (ImGuiStyle only) |

---

## 7. Immediate next step

Wire Phase 1: add the two Xpera `.cpp` files to the swordfare target, call
`xpera::apply_style()` behind a toggle, and screenshot-compare the F11 toolbox
before/after. No layout changes in that step — theme only.
