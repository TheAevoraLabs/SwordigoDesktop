# Data-driven GUI for Swordigo — extending Caver's GUI, not replacing it

Status: **design** (no code yet). Companions:
[layout_reference.md](layout_reference.md) (what the vanilla GUI actually is),
[`docs/sre13/`](../../sre13/) (class layouts + addresses),
[gui_stack/](gui_stack/) (older blueprints — their API names are partly fictional, see §11).

---

## 1. Positioning

We are **not** writing a new GUI. The vanilla `Caver::GUI*` stack already *is* the
Swordigo look: atlas sprites, nine-patch plates, bitmap fonts, anchor/margin layout,
target-action events, press sounds. The goal is to make it **open**:

1. **Extend** — add widget types and screens the original never had.
2. **Data-drive** — describe screens in files instead of recompiling C++.
3. **Expose** — give mods a Lua API so a non-C++ modder can ship a full custom
   options/menu screen with authentic buttons, sliders and switches.

Three facts from the decompilation make this cheap:

| Fact | Why it matters |
|---|---|
| `GameInterfaceBuilder::{MainMenuButtonWithTitle, Slider, Switch, FramedButtonWithTitle, NormalLabel, SmallLabel, AlertView, InfoBubble}` are plain exported functions returning fully-styled widgets | authentic style is a *function call*, not a reimplementation |
| The HUD (`GameOverlayView`) already drives its children through a **`GUIViewLayout` tree** (`+624`) with identifiers, margins and per-node scale | a layout engine with an identifier index already exists and is already mutated at runtime (HUD edit mode) |
| `Caver.Proto.GUIViewLayout` serialisation exists but has **no callers** | the game shipped an unused "save/load a layout to/from a file" path — we can be the first caller |

---

## 2. Layered architecture

```
L5  Graphy / UI editor (later)          — visual authoring, exports TOML/proto
L4  Lua API  (UI.* namespace)           — what modders actually touch
L3  Screen schema  (ui/*.toml)          — declarative screens + styles + bindings
L2  Widget registry (C)                 — type name → create/measure/draw/hit/bind
L1  sre13 GUI bridge (C, in libsre13.so)— resolves engine functions, owns the tree hooks
L0  Vanilla engine  (libswordigo.so)    — GUIView/GUIButton/GUISlider/... as-is
```

Rules:

- **L1 never re-renders vanilla.** It resolves engine functions by *symbol name*
  (host `nm -D` atlas), relays to originals, and adds observe/inject points.
  This is exactly the proven `src/sre/sre12/sre_gui.c` + `sre_gui_native.c` model
  (49 resolved functions, 1.5 k lines) ported to ABI 1.4.13.
- **Anything that can be an engine widget must be an engine widget.** Our own drawing
  code exists only for widget types the engine does not have (L2 extensions).
- **Every offset used at runtime must come from a named symbol or a documented field**
  in `docs/sre13/*.md`, never a bare number in a mod file.

---

## 3. L1 — the sre13 GUI bridge

Port of `sre_gui.c` to 1.4.13. Resolve (all addresses confirmed in `arm64_13`):

| Group | Functions (1.4.13 VA) |
|---|---|
| RenderingContext | `FillRect 0x48c6a0`, `DrawRect 0x48c9f8`, `SetAlpha 0x48b4bc`, `SetMatrix`, `BindTexture 0x48bfa4`, `DrawArrays 0x48c330`, `UseProgram 0x48bcec`, `SetBlendingEnabled 0x48b8d8`, `SetDepthTestEnabled 0x48b990` *(1.4.12 VAs today in `src/main.cpp:5385-5447`; recompute for 13)* |
| FontText | `Draw 0x4C66DC`, `AddText 0x4C53E4`, `Clear 0x4C5124`, `SetColor 0x4C5170`, `Translate 0x4C6210`, `AlignHorizontally 0x4C6280` |
| GUILabel | `AddText 0x52092C/0x520A68`, `UpdateText 0x5204FC`, `TextSize 0x520B80` |
| GUIButton | ctor `0x51DE90`, `SetTitle 0x51E61C`, `SetImage 0x51E684`, `SetFrameTextureForState 0x51E9EC`, `titleLabel 0x41D6CC` |
| GUIView | `SetFrame 0x527490`, `AddSubview 0x5275D0`, `RemoveFromSuperview 0x5279F0`, `safeBounds 0x42A3E4` |
| GUIWindow | `mainWindow 0x529E3C`, `PresentModalView 0x52AECC`, `DismissModalView 0x52B1F0` |
| GUIAlertView | `SetTitle 0x51A94C`-family, `AddButton` |
| GameInterfaceBuilder | `NormalLabel 0x41C894`, `SmallLabel 0x41CD8C`, `FramedButtonWithTitle 0x41DAAC`, `MainMenuButtonWithTitle 0x41DDE0`, `AlertView 0x41E064`, `Slider 0x41E78C`, `Switch 0x41ECEC`, `InfoBubble 0x41F25C`, `IconButton 0x41D870` |
| Navigation | `GUINavigationController::TransitionToViewController 0x521C94`, `PresentModalViewController 0x5217D8` |
| Texture/Font lib | `TextureForName 0x4CC494`, `LoadTextureAtlasWithName 0x551258`, `FontLibrary::DefaultFont 0x4C4364`, `SmallDefaultFont 0x4C44A8` |

**Hooks (relay to original, then act):**

| Hook | Addr | Purpose |
|---|---|---|
| `GUIView::DrawRect` | `0x5271DC` | draw mod overlays after vanilla |
| `GUIView::LayoutSubviews` | `0x527390` | re-apply schema layout after vanilla layout |
| `GUIControl::SendActionsForControlEvent` | `0x51F3FC` | global action bus → Lua callbacks |
| `GUISlider::TouchMoved` | `0x5255F8` | value change → Lua `on_change` |
| `CaverShell::Update` | `0x323944` | per-frame screen/hot-reload tick (already hooked for the console) |

C API surface (mirrors sre12):

```c
void sre13_init_gui(SreGuiAddrs*);                 /* resolve-by-name */
SreView* sre13_gib_main_menu_button(const char* title);
SreView* sre13_gib_slider(float min, float max);
SreView* sre13_gib_switch(void);
SreView* sre13_gib_label(const char* text, int small);
void     sre13_set_frame(SreView*, float x, float y, float w, float h);
void     sre13_add_subview(SreView* parent, SreView* child);
void     sre13_set_touch_slop(SreView*, float px);      /* outset(bounds, px) → +0xA4 */
void     sre13_on_action(SreView*, uint32_t event, SreLuaRef cb);
void     sre13_present_controller(SreScreen*);           /* TransitionToViewController */
SreView* sre13_main_window(void);
SreView* sre13_screen_for_identifier(const char* id);    /* walk the tree by name */
```

---

## 4. L2 — widget registry (how you add a *new* widget)

```c
typedef struct SreWidgetClass {
    const char* type;                 /* "button" | "slider" | … | mod-provided */
    uint32_t    size;                 /* object size in bytes */
    SreView*  (*create)(const SreVal* props);   /* may call GameInterfaceBuilder */
    void      (*apply)(SreView*, const SreVal* props);   /* idempotent re-apply */
    void      (*layout)(SreView*, const SreRect* safe);
    int       (*hit)(SreView*, float x, float y);        /* optional override */
    void      (*draw)(SreView*, SreCtx*);                /* NULL ⇒ engine draws */
    SreBind*    binds;                /* property name → getter/setter */
    const char* lua_name;             /* UI.mywidget{} */
} SreWidgetClass;

void sre13_register_widget(const SreWidgetClass*);
```

Built-in registrations map 1:1 onto engine classes:

| `type` | Engine backing | Created via |
|---|---|---|
| `button` | `GUIButton` | `MainMenuButtonWithTitle` / `FramedButtonWithTitle` / `ControlButtonWithImage` (chosen by `style`) |
| `slider` | `GUISlider` | `GameInterfaceBuilder::Slider(min,max)` |
| `switch` | `GUISwitch` | `GameInterfaceBuilder::Switch()` |
| `label` | `GUILabel` | `NormalLabel` / `SmallLabel` |
| `image` / `panel` | `GUIFrameView` | `new GUIFrameView` + `SetFrameTexture` |
| `alert` | `GUIAlertView` | `GameInterfaceBuilder::AlertView` |
| `bubble` | info bubble | `GameInterfaceBuilder::InfoBubble` |
| `tabs` | `TabbedMenuView` | `new TabbedMenuView` + `AddTab` |
| `list` | `GUIScrollView` | `new GUIScrollView` (engine) |
| **new:** `progress` | custom draw | our `draw` reading `ExperienceBar` sprites |
| **new:** `checkbox` | wraps `GUISwitch` + `GUILabel` | composite |
| **new:** `keybind` | `GUIButton` + listening state | composite |
| **new:** `color_swatch`, `stepper`, `separator`, `tooltip` | composite / custom | ours |

**Recipe for a genuinely new widget type:**

1. Decide whether it can be composed from `GUIButton`/`GUIFrameView`/`GUILabel`
   (prefer this — you inherit the look, touch handling and sounds for free).
2. If it needs new pixels, implement `draw` using **existing engine primitives**
   (`GUITexturedRect` with an atlas sub-rect, or `GUIRoundedRect` nine-patch) —
   never issue raw GL, or you break state caching in `RenderingContext`.
3. Register the class, add a Lua name, add bindable properties.
4. Ship it as a *mod* (`libraries/*.so` or built into `libsre13.so`).

---

## 5. L3 — screen schema (TOML)

Files live in the **mod VFS hierarchy** (already implemented:
`g_sre_vfs_mod_name`, `g_sre_vfs_hierarchy_enabled`, `mods/<id>/…`):

```
mods/my_mod/
  ui/
    theme.toml
    options.toml
    hud_extra.toml
  resources/
    ui/my_icon.atlas  + my_icon.png
```

### 5.1 `theme.toml`

```toml
[theme]
atlas        = "ui_game_atlas"          # default sprite sheet
font         = "default"                # FontLibrary::DefaultFont
small_font   = "small"                  # FontLibrary::SmallDefaultFont
row_height   = 36                       # from SettingsView
row_gap      = 15
edge_x       = 40
hit_slop     = 10                       # RectangleByOutsetting(bounds, 10)
press_sound  = "click"                  # string at GUIButton +0x1B8
label_offset = [28.0, 3.0]              # GUIButton +0x424 default

[theme.styles.main_button]              # → GameInterfaceBuilder::MainMenuButtonWithTitle
factory  = "MainMenuButtonWithTitle"
normal   = "ui_mainmenu_button"
pressed  = "ui_mainmenu_button_pressed"

[theme.styles.framed]
factory  = "FramedButtonWithTitle"
normal   = "ui_button"
light    = "ui_button_light"
frame    = "ui_frame"

[theme.styles.slider]
factory  = "Slider"
frame    = "ui_slider_frame"
bar      = "ui_slider_bar"
peg      = "ui_slider_peg"

[theme.styles.switch]
factory  = "Switch"
on       = "ui_guide_toggle_on"
off      = "ui_guide_toggle_off"
```

### 5.2 A screen — `ui/options.toml`

```toml
[screen]
id         = "options"                  # GUIViewLayout identifier
title      = "OPTIONS"
controller = "GUIViewController"        # or "mod:MyOptionsController"
transition = { fade_in = 0.3, fade_out = 0.3 }   # TransitionToViewController(vc, f, f, bool)
safe_area  = true                       # lay out against safeBounds

# ── rows use the SettingsView rhythm (anchor + row pitch) ──
[layout]
mode       = "rows"                     # "rows" | "free" | "tabs"
anchor_y   = "0.4 * (H - 320) + 50"     # expression language, see §5.4
row_height = 36
row_gap    = 15
label_x    = 40
value_x    = "usableW - 30"
hit_slop   = 10

[[widget]]
type   = "label"
id     = "hdr_graphics"
style  = "small_label"
text   = "GRAPHICS"
anchor = ["left", "top"]
margins = { left = 40, top = 24 }

[[widget]]
type   = "button"
id     = "renderer"
style  = "framed"
text   = "OPENGL"
group  = "renderer"                     # radio-group semantics
action = "lua:set_renderer"
value  = { bind = "engine.renderer" }   # host-backed property

[[widget]]
type   = "button"
id     = "renderer_vk"
style  = "framed"
text   = "VULKAN"
group  = "renderer"
action = "lua:set_renderer"

[[widget]]
type   = "slider"
id     = "music"
style  = "slider"
min    = 0.0
max    = 1.0
step   = 0.05
round  = false
bind   = "engine.music_volume"          # reads/writes a host value
on_change = "lua:on_volume"

[[widget]]
type   = "switch"
id     = "instant_load"
style  = "switch"
text   = "Instant scene load"
bind   = "sre.instant_scene_load"

[[widget]]
type   = "slider"
id     = "fps_cap"
style  = "slider"
min    = 30
max    = 240
step   = 30
round  = true                           # GUIIntegerRound flag at GUISlider +0x164
bind   = "host.fps_cap"

[[widget]]
type   = "button"
id     = "done"
style  = "main_button"
text   = "DONE"
anchor = ["center_x", "bottom"]
margins = { bottom = 40 }
action = "dismiss"
```

### 5.3 Widget keys → engine calls (the contract)

| Schema key | Engine call / field | Address |
|---|---|---|
| `type` + `style` | `GameInterfaceBuilder::<factory>` | see §3 |
| `text` | `GUIButton::SetTitle` / `GUILabel::setText` | `0x51E61C` / `0x41CC5C` |
| `normal`,`pressed` | `GUIButton::SetFrameTextureForState(0/1, tex)` | `0x51E9EC` |
| `min`,`max`,`round` | `GUISlider +0x12C / +0x130 / +0x164` | — |
| `bind` (value) | read/write `GUISlider +0x11C`, `GUISwitch +0x140` | — |
| `anchor`,`margins` | `GUIViewLayout` margins `+0x48`, resolved by `LayoutForViewWithFrameInBounds` | `0x5304AC` |
| `scale` | `GUIViewLayout +0x64` | — |
| `id` | `GUIViewLayout::SubviewWithIdentifier` | `0x530604` |
| `action` | `GUIControl::AddTargetActionForControlEvent` | `0x51F110` |
| `hit_slop` | `GUIView::SetBounds` → write `+0xA4` (`outset(bounds, n)`) | `0x528F94` |
| `sound` | `std::string` at `GUIButton +0x1B8` | — |
| `hidden` | byte at `+0xF4` | — |
| `screen.transition` | `GUINavigationController::TransitionToViewController` | `0x521C94` |

### 5.4 Expressions

Keep the expression language deliberately tiny (no `libm` games):
numbers, `W/H/usableW/S.x/S.y/S.right/S.midY`, `+ - * / ( )`, and
`min/max/round/lerp`. This covers everything in
[layout_reference.md](layout_reference.md) §3–§5 without a general scripting VM.

---

## 6. L4 — Lua API

Registered alongside the existing `Mini.*` button API (`sre13_button_controller.c`)
but built on real widgets:

```lua
-- 1. declarative screen -------------------------------------------------
local scr = UI.screen{
  id = "my_options", title = "MY OPTIONS",
  transition = { fade = 0.3 },
}

scr:slider{
  id = "music", min = 0, max = 1, step = 0.05,
  text = "Music", style = "slider",
  bind = UI.host("music_volume"),          -- host-backed value
  on_change = function(v) Settings.music = v end,
}

scr:switch{ id = "god", text = "God mode", bind = UI.host("godmode") }

scr:button{ id = "reset", text = "RESET", style = "main_button",
            on_click = function() Save.reset() end }

scr:show()                                   -- TransitionToViewController
scr:hide()

-- 2. imperative / overlay ---------------------------------------------
local b = UI.button{ text = "TP to boss", x = 0.9, y = 0.2, w = 0.18, h = 0.06,
                     style = "framed",
                     on_click = function() Scene.teleport("boss_arena") end }
b:set_text("TELEPORT")
b:set_visible(false)
b:destroy()

-- 3. HUD injection ------------------------------------------------------
HUD.add{ type = "label", id = "fps", anchor = ["right","top"],
         margins = { right = 10, top = 9 },            -- mirrors MainMenuView
         text = function() return string.format("%d FPS", Host.fps()) end }

-- 4. inspect the live tree (debugging / theming) ------------------------
for v in UI.walk() do
  print(v.class, v.identifier, v.frame.x, v.frame.y, v.frame.w, v.frame.h)
end
```

Implementation: `UI.*` are thin Lua C functions over the L1 C API; callbacks are
`luaL_ref`'d and fired from the `SendActionsForControlEvent` hook (already the
engine's own action bus), with the existing `sre13_recovery` setjmp guard so a mod
error cannot take down the game.

---

## 7. Walkthrough — a full custom Options screen, vanilla look

Goal: replace the main-menu Shop button with a native-styled Options screen
(the `gui_stack/16` design, executed with verified 1.4.13 addresses).

1. **Relabel** — hook `MainMenuView::ButtonPressed` (`0x45A780`) or override the
   title string via sre12's proven text-override table mechanism (`Offers → Options`).
2. **Intercept** — hook the delegate method the shop button calls and, instead of
   pushing `StoreViewController`, build our screen.
3. **Build** — from `ui/options.toml`: for each widget call the factory, `SetFrame`
   using the `rows` mode constants from [layout_reference.md §4](layout_reference.md)
   (`anchor = round((H-320)*0.4)`, `x=40`, `pitch = 36+15`, value column
   `right-30`), then `outset(bounds,10)` for hit slop.
4. **Wire** — each `bind` maps to a host property already exported by the SRE
   (`g_sre_*` globals; the pattern used for player HP/mana/coins in `src/main.cpp`).
5. **Present** — `GUINavigationController::TransitionToViewController(vc, 0.3, 0.3, true)`
   gives the same fade the vanilla screens use; `AnimateIn` if you want the slide.
6. **Persist** — write values through the existing `sre13` config/TOML helpers.
7. **Reload** — touch `ui/options.toml` → the `CaverShell::Update` hook re-parses and
   re-`apply`s (schema application is idempotent), so iteration is live.

Expected result: indistinguishable from a vanilla screen, because every pixel comes
from `ui_game_atlas` via `GameInterfaceBuilder`.

---

## 8. What already exists that we reuse (do not rebuild)

| Existing asset | Where |
|---|---|
| Retained widget model + host rendering bridge (`SreBtnSlot`, `SreOverlaySlot`) | `src/sre/sre13/ui/sre13_button_controller.{c,h}` |
| Guest↔host shared-memory mailbox, TCP Lua console, live Lua context | `sre13_console.c`, `SwordfareGUI::update_console_backend` |
| TOML parser compiled into the guest lib | `src/sre/sre13/core/toml.c` |
| Mod VFS hierarchy (`mods/<id>/…`) with launcher selection | `g_sre_vfs_*`, `main.cpp` |
| Lua error containment | `sre13_recovery.c` (setjmp stack + VM restore) |
| Proven native GUI bridge + full DrawRect hook set | `src/sre/sre12/sre_gui.c`, `sre_gui_native.c` (1.4.12 only — **port target**) |
| Anchored layout engine with identifiers + scale | `Caver::GUIViewLayout` (`0x5304AC`, tree at `GameOverlayView+624`) |
| HUD edit mode already mutating that tree | `ConfigureOverlayView::LayoutItem` |
| Text-override / button-hide power table | `sre_gui_native.c` (1.4.12) |
| Golden sprite inventory + per-screen geometry | [layout_reference.md](layout_reference.md) |

---

## 9. Phases

| Phase | Deliverable | Effort |
|---|---|---|
| **P1** | `sre13_gui.c` — resolve-by-name for the ~50 functions, `sre_init_gui` exported so `main.cpp` wires it; text-override table live on 1.4.13 | 2–4 days |
| **P2** | Hook set (`DrawRect`, `LayoutSubviews`, `SendActionsForControlEvent`) with relays; `UI.walk()` inspector proves the tree | 2–3 days |
| **P3** | Widget registry + C API; build one screen in code (Options) with vanilla factories | 3–5 days |
| **P4** | TOML schema + loader + row/free layout modes + expressions | 1 week |
| **P5** | Lua `UI.*` / `HUD.*` + bindings to host properties; hot reload | 1 week |
| **P6** | Composite/new widget types (`progress`, `checkbox`, `keybind`, `stepper`) | as needed |
| **P7** | Wire `GUIViewLayout::SaveToProtobufMessage` on screen exit → dump real layouts to file (bootstrap the layout corpus automatically) | 2 days |
| **P8** | Graphy UI editor exporting the same schema | later |

Verification at every phase: golden-image diff against a vanilla screenshot at fixed
resolutions (16:9, 4:3, 21:9) — the only objective test for "still looks like
Swordigo".

---

## 10. Risks

1. **Version lock** — resolve by symbol name via the host atlas; keep the
   `docs/sre13/*.md` offset tables as documentation, not as runtime constants
   where a symbol exists.
2. **Inlining** — 1.4.13 clang inlines small setters (`SetButtonType` has clones).
   Hook the nearest non-inlined caller (`LayoutSubviews`, `ButtonPressed`).
3. **C++ ABI** — never construct `std::string`/`boost::shared_ptr` from C; pass
   engine-made strings (the `libcxx_string_data` reader in `sre13_ui.c` is the model).
4. **Refcount crashes** — any C-side walk of `vector<shared_ptr<GUIView>>` must go
   through engine refcount helpers (sre12 hit STXR bugs here).
5. **Input ownership** — a custom view that consumes touch must set
   `g_sre_overlay_blocking`-style gating so the player doesn't walk while dragging a slider.
6. **Renderer state** — issue GUI draws only through `RenderingContext`/engine sprites;
   raw GL breaks the state cache (this is why sre12's native renderer routes through
   engine primitives).
7. **Emulator exceptions** — Lua callbacks must stay inside the `sre13_recovery`
   setjmp guard.

---

## 11. Anti-goals

- **Do not** re-implement `GUIButton::DrawRect` unless we deliberately choose the
  sre12 "full replacement" mode; for extension work, relay to vanilla.
- **Do not** build against the names in `gui_stack/*.md`
  (`setNinePatchMargins`, `GUIImageView`, `ctx->drawColoredQuad`, `setFont("default",16)`)
  — those APIs do not exist. Use `GameInterfaceBuilder`, `GUITexturedRect`,
  `GUIRoundedRect`, `FontLibrary::*`.
- **Do not** put ImGui widgets in front of players; keep `SwordfareGUI::draw_buttons`
  for host/editor chrome only and route in-game custom UI through native widgets.
- **Do not** hardcode pixel positions from this repo's docs — they are 1.4.13
  measurements, and screens recompute from `safeBounds` anyway.
