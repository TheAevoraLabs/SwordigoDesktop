# Swordigo GUI layout reference — per-screen sprites, frames and margins

Extracted from `OpenSwordigo/arm64_13/` (Hex-Rays decompilation of `libswordigo.so`,
**ARM64 v1.4.13**).

- Sprite names come from `strings.tsv` (**verified**: literal string referenced by that
  function's code).
- Geometry comes from reading each screen's `LayoutSubviews` body (**verified** where a
  literal/`SetFrame` was read, **inferred** where the expression was reconstructed).
- Field offsets are documented in [`docs/sre13/`](../../sre13/) (`GUIView.md`,
  `GUIButton.md`, `GUISlider.md`, `GUILabel.md`, `GUIFrameView.md`).

> Conventions used below: `B` = this view's `bounds (+0x94)`, `S` = `safeBounds`
> (bounds ∩ superview safeBounds ∩ `+0x38` inset), `right = B.x + B.w`,
> `midY = B.y + B.h*0.5`. Every rect uses `SetFrame` (`+0x84`) unless stated.
> Hit slop is written to **`touchBounds (+0xA4)`** via `RectangleByOutsetting(bounds, n)`.

---

## 0. Texture atlases (loaded once)

`Caver::CaverShell::InitView` (`0x321DB4`) loads, in order:

| Atlas | Note |
|---|---|
| `ui_game_atlas` | core UI sprites (buttons, bars, menu chrome) |
| `ui_game2_atlas` | newer/second-sheet UI |
| `me_atlas` | menu/character art |
| `game_common_atlas` | shared frames |
| `e2_atlas` | (name suffix of `ui_game2_atlas`) |

All sprite lookups go through `TextureLibrary::TextureForName(name, /*create=*/1)`
(`0x4CC494`), `sharedLibrary()` (`0x4CC308`).
A sprite frame inside an atlas is a **sub-rect of one `Texture`**: pixel size at
`+0x74/+0x78`, UV origin `+0x7C/+0x80`, UV size `+0x84/+0x88` (see `GUIFrameView.md` §2).

---

## 1. `GameInterfaceBuilder` — the canonical widget recipes (verified)

| Factory | Addr | Sprite(s) | Size alloc |
|---|---|---|---|
| `NormalLabel(str, cA, cB)` | `0x41C894` | — (default font) | `GUILabel` 0x170 |
| `SmallLabel(str, cA, cB)` | `0x41CD8C` | — (small font) | `GUILabel` 0x170 |
| `ControlButtonWithImage(tex)` | `0x41D034` | `ui_controls_button`, `ui_controls_button_pressed` | `GUIButton` 0x1D8 |
| `PlainButton(...)` | `0x41D398` | (none — uses `SetButtonType`) | `GUIButton` 0x1D8 |
| `IconButton(tex, texPressed)` | `0x41D870` | `textbubble_border` | — |
| `FramedButtonWithTitle(str, bool)` | `0x41DAAC` | `ui_button`, `ui_button_light`, `ui_frame` | — |
| `MainMenuButtonWithTitle(str)` | `0x41DDE0` | `ui_mainmenu_button`, `ui_mainmenu_button_pressed` | — |
| `AlertView(title, msg, int, str*, int)` | `0x41E064` | — | `GUIAlertView` 0x1C8 |
| `Slider(min, max)` | `0x41E78C` | `ui_slider_frame`, `ui_slider_bar`, `ui_slider_peg` | `GUISlider` 0x168 |
| `Switch()` | `0x41ECEC` | `ui_guide_toggle_on`, `ui_guide_toggle_off` | `GUISwitch` 0x168 |
| `TextGradient(...)` | `0x41F07C` | — | 2×0x18 |
| `InfoBubble(str, float)` | `0x41F25C` | `textbubble_peg`, `ui_frame` | 0x128 |

`MainMenuButtonWithTitle` also stamps: frame size = **texture size** (`+0x74/+0x78`),
`+0x194 = -1`, `+0x198 = xmmword_259E10`, label anchor **`+0x424 = (28.0, 3.0)`**,
then `SetNeedsLayout` (`vtable[0x58]`).

---

## 2. Sprite → screen map (verified — `strings.tsv`)

### Title / main menu (`MainMenuView`, `MainMenuViewController`)

| Sprite | Used by |
|---|---|
| `swordigo_title`, `go_title` | `MainMenuView::MainMenuView` |
| `ui_main_button`, `ui_main_button_pressed` | `MainMenuView`, `GuidePurchaseView`, `ProfilePanelView` |
| `ui_main_button_small(_pressed)` | `ProfilePanelView`, `OfflineAchievementsView` |
| `ui_button_facebook(_pressed)`, `ui_button_twitter(_pressed)` | `MainMenuView` |
| `ui_button_music_blue`, `ui_button_music_off_blue` | `MainMenuView` (title-screen audio toggles) |
| `ui_button_sound_blue`, `ui_button_sound_off_blue` | `MainMenuView` |
| `ui_google_signin(_pressed)` | `MainMenuView` |
| `ui_mainmenu_button(_pressed)` | via `MainMenuButtonWithTitle` |

### Settings / options (`SettingsView`)

| Sprite | Used by |
|---|---|
| `ui_button_music`, `ui_button_music_off` | `SettingsView::SettingsView` |
| `ui_button_sound`, `ui_button_sound_off` | `SettingsView::SettingsView` |
| `ui_slider_frame/bar/peg` | via `GameInterfaceBuilder::Slider` (music/SFX volume) |
| `ui_guide_toggle_on/off` | via `GameInterfaceBuilder::Switch` |
| `ui_controls_button(_pressed)` | `ControlButtonWithImage` (control-mapping rows) |

### In-game HUD (`GameOverlayView`)

| Sprite | Used by |
|---|---|
| `ui_controls_left`, `ui_controls_right`, `ui_controls_jump`, `ui_controls_swing`, `ui_controls_handle` | `GameOverlayView::GameOverlayView` |
| `ui_skill_toggle_button(_pressed)` | `GameOverlayView::GameOverlayView` |
| `ui_skill_<name>` | `SetCurrentSkill`, `MenuItemSlot::SetSkill`, `ItemInfoPopupView` |
| `ui_heartfull/ui_hearthalf/ui_heartempty` | `HealthBar` |
| `ui_manabar`, `ui_manabar_background` | `ManaBar` |
| `ui_xp_bar`, `ui_xp_frame` | `ExperienceBar` |
| `ui_inventory_coin` | `CoinBar::CreateIconSprite` |
| `ui_health_bar` | `TargetFrame` |

### Menus / tabs / inventory

| Sprite | Used by |
|---|---|
| `ui_tab_<name>`, `ui_tab_frame`, `ui_tab_selected` | `NewMenuView`, `TabbedMenuView`, `TabView::UpdateSprite`, `OverlayMenuButton::SetMenuTab` |
| `ui_menu_stripe(_tag)` | `StripeView`, `TabbedMenuView`, `InventoryView`, `CharacterView` |
| `ui_menu_container`, `ui_menu_button(_pressed)` | `MapMenuPage`, `ItemBuyPopupView` |
| `ui_menu_itemslot(_glow)` | `MenuItemSlot::Init(float)`, `ItemInfoPopupView` |
| `ui_button_arrow(_pressed)` | `CharacterView`, `CreditsView`, `GameSceneView`, `OnlineMenuView`, `PortalView`, `ProfilesHeaderView`, `OfflineAchievementsView` |
| `ui_button_close(_pressed)` | `PortalView`, `ProfilePanelView`, `PurchaseView`, `StoreView`, `GuidePurchaseView` |
| `ui_button_help`, `ui_button_plus`, `ui_button_minus` | `AttributePointsView`, `InventoryView` |
| `ui_button_music/sound(_off)` | `SettingsView` |
| `ui_inventory_panel`, `ui_inventory_trinket_slot_icon` | `InventoryItemPanel` |
| `ui_menu_stripe`, `ui_character_frame` | `ProfilePanelView`, `CharacterView` |
| `ui_frame_dark`, `ui_frame_dark_blue` | `ItemInfoView`, `StoreProductView`, `AttributePointsView`, dialogs |
| `ui_notification_background` | `ItemBuyPopupView`, `ItemInfoPopupView`, `MapPopupView` |
| `ui_popover_border`, `ui_popover_arrow` | `GUIPopoverView` |
| `ui_roundrect`, `ui_roundrectbg` | `GUIButton::SetButtonType` (type 1) |
| `textbubble_border`, `textbubble_peg`, `textbubble_indicator` | `IconButton`, `InfoBubble`, `GUITextBubble`, `TextBubbleComponent`, `QuestsView` |
| `ui_coindoubler_buyimage(_small)`, `ui_noads_buyimage(_small)`, `ui_guide_screenshot(_small)`, `ui_guide_buy(_pressed)` | store / purchase screens |
| `ui_map_*` (guide, icons, node, path, marker) | `MapView` |
| `the_guide_arrow`, `ui_guide_arrow` | `GameViewController::UpdateGuideTarget`, `OverlayTargetArrowComponent` |
| `button_taptoenter`, `button_taptobrowse` | `PortalComponent::Activate` (portal "tap to enter" prompt) |

---

## 3. `MainMenuView` — title screen (`LayoutSubviews` `0x45AD88`, verified)

Constructor `0x4572B4`; also builds `swordigo_title`, `go_title`, `ui_main_button(_pressed)`,
social + audio buttons. Members below are `shared_ptr<GUIView>` slots after the 256-byte base.

### 3.1 The 16:9 reference box

```c
safe = safeBounds(this);
W = B.w ; H = B.h;
off = (W - H * (16.0/9.0)) * 0.5;                 // horizontal slack vs a 16:9 box
if (off <= 0)  contentFrame = B;
else           contentFrame = RectangleByOutsetting(B, ceilf(H * (off*1.2) / W));
SetFrame(child[+0x108], contentFrame);
```

**Rule:** the layout container is `bounds`, uniformly **outset** (never inset) by
`ceil(h · 1.2 · (W − 16/9·H) / W)` when the screen is *wider* than 16:9.
On 16:9 or narrower it is exactly `bounds`.
Shared edge inset used by the stack: `inset = min((B.x+B.w) − (S.x+S.w), 10.0)`.

### 3.2 Button column (right side, stacked downward)

For each of `child@+0x128`, `+0x138`, `+0x158`, `+0x168` (in that order):

```
w = inset + backgroundTexture.width        // texture at child's +0x120 (GUIRoundedRect→Texture)
h = backgroundTexture.height               // texture at +0x78
SetFrame(child, { keep x, keep y?, w, h }) // x,y preserved from ctor
next.y = inset + (prevTextureWidth + GAP)  // GAP sequence: 30, 5, 15
```

i.e. **the buttons are laid out vertically with gaps of 30 px, 5 px and 15 px** and
sized to their background texture.

### 3.3 Right-aligned accessory column

Vector of 16-byte slots at `this+0x188/+0x190` (indices 49/50), loop:

```
frame.x = (B.x + B.w) - frame.w          // right aligned
frame.y = cursor                          // starts at 3.0
cursor += frame.h + (count <= 3 ? 8.0 : 3.0)
obj+0x19C (412) = 2*inset ;  obj+0x194 (404) |= 2
```

Container `child@+0x118` gets the final cursor as its height.

### 3.4 Individual placements (verified constants)

| Member (QWORD idx → off) | Frame | Notes |
|---|---|---|
| `+0x118 (280)` | height = stack cursor | container |
| `+0x178 (376)` | `{ S.right − 10 − w, S.y + 9 }` | top-right icon |
| `+0x1C0 (440)` | `{ S.right − w, B.h }` | parked off-screen (animated in) |
| `+0x1C8 (456)` | `{ S.x + 90, 28.0 }`, touchBounds = outset(bounds,10) | |
| `+0x1D8 (472)` | `{ S.x + 40, 48.0 }`, touchBounds = outset(bounds,10) | |
| `+0x208 (520)` | `{ S.x + 15, round(B.y + B.h*0.5 + 50) }` | |
| `+0x210 (536)` | `{ round(B.x + B.w*0.5 − w/2), S.y + 24 }`, touchBounds = outset(bounds,10) | centred button |
| `+0x218 (552)` | label: `y = button.y − 10 − h`, `x = round(button.centreX − w/2)` | caption under it |
| `+0x228 (584)` | `{ round(B.x + B.w*0.5 − w/2), 24.0 }`, touchBounds = outset(bounds,10) | centred button #2 |
| `+0x218 (552)`/`+0x238 (568)` | label: `y = button.y − 10 − h` | |
| `+0x240 (584)` / `+0x248 (600)` | `+0x240 → x = child65.x + 23, y = child65.y + (6 − h)`; `+0x248 → x = child77.right + 6` | small pair, 6 px gap |
| `+0x250 (632)` / `+0x258 (648)` | image + `GUILabel` sized by `TextSize`; `x = round((S.x + S.w*0.5) − (labelH + 10 + imgW)*0.5)`, `y = B.y + 9` | title lockup |

Footer: `AnimateButtonsIn(this, 0.0)` then `byte +0xF9 = 1` (layout dirty).

> **Take-away:** positions are *relative to `safeBounds`*, sizes come from **textures**,
> and every interactive element gets a 10 px touch outset. Reproducing the look means
> reusing the sprites + these rules, not copying pixel coordinates.

---

## 4. `SettingsView` — options screen (`LayoutSubviews` `0x4D39AC`, verified)

Constructor `0x4D1828` (builds `ui_button_music(_off)`, `ui_button_sound(_off)`).
Handlers: `ButtonPressed` `0x4D3180`, `SliderValueChanged` `0x4D3450`,
`SliderTouchUp` `0x4D369C`, `AnimateIn` `0x4D3DD8`.

Let `usableW = (S.x + S.w) − B.x` and `anchor = roundf((B.h − 320) * 0.4)`.

| Member | Frame |
|---|---|
| `+0x130 (304)` | `{ 40, anchor + 50, (usableW − 421)*0.35 + 180, 36 }` |
| `+0x140 (320)` | `{ 40, prev.y + prev.h + 15, (usableW − 421)*0.35 + 180, 36 }` — **row pitch = h + 15** |
| `+0x150 (336)` | `{ 40, prev.y − 15 − h, … }` (label above its row) |
| rows from `vector @ +0x180/+0x188` | `{ usableRight − 30 − w, anchor + 50 + i*(h + 15), w, h }` with `w = (usableW − 480)*0.35 + 170`, `h = 36` |
| `+0x198 (408)` | `{ 35, round((B.y + B.h − 70 − anchor) − h/2) }` |
| `+0x1A8 (440)` | `{ 90, round((B.y + B.h − 70 − anchor) − h/2), w = (usableRight − 30) − 90 }` |
| `+0x1A0 (424)` | `{ 35, round((B.y + B.h − 120 − anchor) − h/2) }` |
| `+0x1B0 (456)` | `{ 90, round((B.y + B.h − 120 − anchor) − h/2), w = right − 90 }` |

Hit slop: `outset(child+0x1A8 bounds, 20)` (width also `+20`), `outset(child@+0x198, 10)`,
`outset(child@+0x1A0, 10)`, same as `+0x1A8` for `+0x1B0`.
Ends with `SettingsView::AnimateIn`.

**Constants in play:** x = `40` / `35` / `90`, row height `36`, row gap `15`,
vertical anchors `50`, `70`, `120`, `−30`, usable-width shaping `421 / 480` and scales
`0.4 / 0.35`.

---

## 5. `GameOverlayView` — in-game HUD

Constructor `0x486F78` (`ui_controls_*`, `ui_skill_toggle_button(_pressed)`),
`InitWithGameState` `0x48A0C4`, `SetLayout` `0x48A984`, `LayoutSubviews` `0x48BBD8`,
`Layout` `0x48CA0C`, `LayoutForSubview` `0x48C8D8`, `LayoutBoundsForSubview` `0x48C7C0`,
`SubviewForIdentifier` `0x48C748`, `SetSubviewLayout` `0x48C58C`,
`HitTest` `0x48D4EC`, `HandleKeyboardEvent` `0x48CFA0`.

### 5.1 The HUD **is** data-driven (verified)

`GameOverlayView::Layout` builds a `GUIViewLayout` tree **at runtime**:

- allocates `GUIViewLayout` (**104 bytes, `0x68`**) and stores it at **`this+624 (0x270)`**
- walks the subview list (head `this+288 = +0x120`) and, per child, calls
  `LayoutForSubview(identifier)` → `GUIViewLayout::SubviewWithIdentifier(root, id)` →
  `LayoutBoundsForSubview` → `GUIViewLayout::LayoutForViewWithFrameInBounds(child+132, bounds, scale)`

`LayoutForViewWithFrameInBounds` (`0x5304AC`) is the anchoring rule:

```
centreDeltaX = (frame.x + frame.w/2) − (bounds.x + bounds.w/2)
if centreDeltaX >= 0 → align RIGHT : dx = (bounds.x+bounds.w) − (frame.x+frame.w), dy = 0
else                 → align LEFT  : dx = (frame.x − bounds.x),                  dy = 0
   (same test on Y → align BOTTOM / TOP)
offset /= scale ;  result written into a fresh GUIViewLayout child with +0x64 = scale
```

So each HUD element stores its **frame in a reference rectangle** and gets pinned to the
nearest edge of the real viewport, divided by a per-element **scale** (`+100`, default
1.0) — a genuine anchored/scalable layout system.

`ConfigureOverlayView::LayoutItem` / `DraggableOverlayItemDidMove` use the same API for
the **HUD edit mode**, so the layout tree is meant to be mutated at runtime.

### 5.2 `GUIViewLayout` field map (verified, 104 bytes)

| Off | Field |
|---|---|
| `+0x00` | `std::string identifier` |
| `+0x18/+0x20/+0x28` | `std::vector<shared_ptr<GUIViewLayout>> subviews` (16 B elements) |
| `+0x30/+0x38/+0x40` | `std::map<std::string, shared_ptr<GUIViewLayout>> byIdentifier` (header at `+0x38`) |
| `+0x48 (72)` .. `+0x57 (87)` | `GUIMargins {left, right, top, bottom}` (4 floats) |
| `+0x58 (88)` | int (anchor flags) — inferred |
| `+0x5C/+0x60` | extra scalars (copied as a qword) — inferred |
| `+0x64 (100)` | **float scale** (default 1.0) |

Methods: ctor `0x48CBD8`, `LoadFromProtobufMessage` `0x52FDF0`,
`AddSubview` `0x52FFB8`, `SaveToProtobufMessage` `0x5301F8`,
`FrameForViewInBounds` `0x530368`, `UpdateWithViewWithFrameinBounds` `0x5303F4`,
`LayoutForViewWithFrameInBounds` `0x5304AC`, `SubviewWithIdentifier` `0x530604`.

> **`LoadFromProtobufMessage` has no callers** in `callgraph.tsv` — the proto serializer
> exists (message `Caver.Proto.GUIViewLayout`: identifier tag `0x0A`, margins tag `0x1A`,
> subviews tag `0x12`; `GUIMargins` left `0x0D`, right `0x0F`, bottom `0x1D`, top `0x25`)
> but nothing loads it at runtime. Layouts are constructed in code. **This is the
> biggest ready-made seam for a data-driven GUI** — see
> [data_driven_gui_design.md](data_driven_gui_design.md).

### 5.3 HUD literal constants

`LayoutSubviews` (`0x48BBD8`): `0.5`, **`10.0`** (`0x41200000`), **`18.0`** (`0x41900000`),
**`40.0`** (`0x42200000`).
`SetLayout` (`0x48A984`): **`0.75`** and **`1.0`** — the two HUD scale presets
(`g_sre_controls_*` equivalents, i.e. small/normal control clusters).

---

## 6. Other screens (sprite inventory verified; geometry not yet extracted)

| Screen | Key sprites | Entry points (1.4.13) |
|---|---|---|
| `NewMenuView` (in-game menu shell) | `ui_tab_<name>` prefix, tabs `char`, `inventory` | ctor `0x4C96D8`, `LayoutSubviews` `0x4C9C4C`, `DrawRect` `0x4CA1A4`, `TouchBegan` `0x4CA1B4`, `Dismiss` `0x4CA1A8` |
| `PauseView` | `continue` action key | ctor `0x4CADF0`, `LayoutSubviews` `0x4CB124`, `TouchEnded` `0x4CB1A8` |
| `TabbedMenuView` | `ui_menu_stripe(_tag)`, `ui_tab_frame` | `TabbedMenuView` dir (27 fns), `AddTab`, `SetSelectedTab`, `UpdateContentLayout`, `Dismiss` |
| `GUIAlertView` | (frame via `AlertView` factory) | `LayoutSubviews` `0x5193D8`, `ButtonPressed` `0x519E8C`, `DrawRect` `0x51A94C`, `TouchBegan` `0x51AA88` |
| `InventoryView` | `ui_menu_stripe`, `ui_button_help` | `InitWithGameState` |
| `CharacterView` | `ui_button_arrow(_pressed)`, `ui_menu_stripe`, `ui_iselon_slot/completed` | `InitWithGameState`, `CreateIselonView`, `CreateAttributesHeaderView` |
| `MapView` | `ui_map_*` (guide, icons, node, path, marker) | ctor |
| `StoreView` / `PurchaseView` | `ui_button_close(_pressed)`, `ui_guide_buy(_pressed)`, `ui_*_buyimage` | ctors |
| `PortalView` | `ui_button_close(_pressed)`, `ui_button_arrow`, `ui_button_help` | ctor |
| `AttributePointsView` | `ui_attrib_` prefix, `ui_button_help/plus/minus`, `ui_frame_dark` | ctor / `Init` |
| `ProfilePanelView` | `ui_main_button(_small)(_pressed)`, `ui_button_close`, `ui_character_frame` | ctor |
| `GameSceneView` (HUD host) | `ui_button_arrow(_pressed)` | `InitWithSceneController` |

---

## 7. How to re-derive anything in this file

```bash
# 1. which function references a sprite?
awk -F'\t' '$5=="ui_main_button" {print $2}' OpenSwordigo/arm64_13/strings.tsv | sort -u

# 2. all functions of a screen class
ls OpenSwordigo/arm64_13/functions/Caver/MainMenuView/

# 3. read the layout pass (ground truth for geometry)
sed -n '/GENERATED EVIDENCE/,$p' \
  OpenSwordigo/arm64_13/functions/Caver/MainMenuView/000000000045AD88__LayoutSubviews.c

# 4. who calls a widget factory / layout helper?
grep 'MainMenuButtonWithTitle' OpenSwordigo/arm64_13/callgraph.tsv
```

Runtime cross-check (recommended before trusting any number): hook
`GUIView::SetFrame` (`0x527490`) and dump `{name, frame}` for every view on first frame
of a screen — one run yields the authoritative per-screen table.
