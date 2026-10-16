# GUISlider + GUISwitch — draggable value controls (v1.4.13 arm64)

Sources: `OpenSwordigo/arm64_13/functions/Caver/GUISlider/*.c`, `GUISwitch/*.c`,
`GameInterfaceBuilder/{Slider,Switch}.c`.
Base layout: [GUIView.md](GUIView.md), event model: [GUIButton.md](GUIButton.md).

Confidence: **verified** = read in decompiled body · **inferred** · **guess**.

---

## 1. Sizes & construction (verified)

| Class | Size | Ctor | Textures |
|---|---|---|---|
| `GUISlider` | **0x168 = 360** | `0x524BC8`, vtable `off_635240` | `ui_slider_frame`, `ui_slider_bar`, `ui_slider_peg` |
| `GUISwitch` | **0x168 = 360** | `0x525828`, vtable `off_6353A0` | `ui_guide_toggle_on`, `ui_guide_toggle_off` |

Both inherit `GUIView` (256 B) and carry the same `std::multimap<Event,TargetAction>`
header at `+0x100` (ptr at `+0x108`, node sentinel at `+0x108`).

---

## 2. `GUISlider` field map

| Off | Type | Name | Confidence |
|---|---|---|---|
| `+0x100 (256)` | map header | target/actions (same as button) | verified |
| `+0x118 (280)` | `int` flags | control flags (ctor 0) | inferred |
| `+0x11C (284)` | `float` | **current value** | verified (`GameInterfaceBuilder::Slider` reads/writes) |
| `+0x120 (288)` | `float` | **previous / committed value** (`TouchUp` snapshot) | verified |
| `+0x124 (292)` | `int` | change counter (reset to 0 on value write) | verified |
| `+0x12C (300)` | `float` | **min** — ctor default **0.0** | verified (ctor writes `0x00000000`) |
| `+0x130 (304)` | `float` | **max** — ctor default **100.0** (`0x42C80000`) | verified (ctor) |
| `+0x138 (312)` | `Texture*` | **frame texture** (`SetFrameTexture`) | verified |
| `+0x140 (320)` | `Texture*` | **bar texture** (`SetBarTexture`) | verified |
| `+0x148 (328)` | `Texture*` | **peg texture** (`SetPegTexture`) | verified |
| `+0x150 (336)` | `Sprite*` | slider's own sprite (drawn with `UseProgram(3)`) | verified (`DrawRect`) |
| `+0x158 (344)` | `byte` | **sprite dirty flag** — set by all three texture setters; `DrawRect` calls `UpdateSprite` then clears | verified |
| `+0x15C (348)` | ? | zeroed by ctor | guess |
| `+0x164 (356)` | `byte` | **integer-round flag** — when set, value writes go through `roundf` | verified (`Slider` factory reads `obj+356`) |
| `+0x165 (357)` | word | zeroed | guess |

Other methods: `LayoutSubviews` `0x524C3C`, `Update(float)` `0x524D80`,
`DrawRect` `0x524DE0`, `UpdateSprite` `0x524FE0`, `IndicatorCenter` `0x5251D8`,
`IndicatorSize` `0x52521C`, `Min/MaxIndicatorCenterX` `0x525428` / `0x525430`,
`SetValueFromViewLocation(float)` `0x525440`, touches `0x5254B0/0x5255F8/0x5256DC/0x525714`.

### `GUISlider::DrawRect` behaviour (verified, `0x524DE0`)

```
if (dirty@+0x158) { UpdateSprite(); dirty=0; }
vtable[0x128] (layout);  SetIdentityMatrix();
bounds = {+0x94 x, +0x98 y, +0x9C w, +0xA0 h}
pegSize = pegTex ? {tex.w@+0x74, tex.h@+0x78} : {10, 10}
if (frameSprite) { UseProgram(3); SetDefaultBlendFunc; blending on; Sprite::Draw }
fraction = (prevValue - min) / (max - min)
rightEdge = bounds.x + bounds.w ; width = bounds.w ; midY = bounds.y + bounds.h*0.5
if (!frameTexture) → draw fallback track: grey 0xFF969696, 4 px tall at midY-2
```

So the slider is a **three-texture composite**: frame (plate), bar (filled portion),
peg (thumb), all loaded from `ui_slider_*` in `ui_game_atlas`.

### `GameInterfaceBuilder::Slider(min, max)` recipe (verified, `0x41E78C`)

1. `TextureForName("ui_slider_frame"|"ui_slider_bar"|"ui_slider_peg", create=1)`
2. `new GUISlider` → `SetFrameTexture` / `SetBarTexture` / `SetPegTexture`
3. clamp `min`/`max` into `+0x12C`/`+0x130`, value into `+0x11C`, honour round flag `+0x164`.

---

## 3. `GUISwitch` field map

| Off | Type | Name | Confidence |
|---|---|---|---|
| `+0x100 (256)` | map header | target/actions | verified |
| `+0x118 (280)` | `int` flags | control flags (ctor 0) | inferred |
| `+0x140 (320)` | `byte` | **isOn** (ctor 0) | inferred |
| `+0x148 (344)` | `std::string` | sound/action name — factory writes **`"click"`** (SSO length byte `0x0A`) | verified (`Switch` factory) |
| `+0x130 (304)` / `+0x138 (312)` | `shared_ptr<GUIFrameView>` | **the knob/track subview** — ctor allocates a `GUIFrameView (0x120)` and `AddSubview`s it via `vtable[0x60]` | verified |

Methods: `SetOnFrameTexture` `0x525A78`, `UpdateFrameTexture` `0x525B40`,
`SetOffFrameTexture` `0x525B64`, `LayoutSubviews` `0x525A50`, `CancelPress` `0x525BE4`,
touches `0x525C50/0x525D34/0x525E30/0x525EE0`.

`GameInterfaceBuilder::Switch()` recipe (verified, `0x41ECEC`):
`new GUISwitch (0x168)` → `SetOnFrameTexture(ui_guide_toggle_on)` →
`SetOffFrameTexture(ui_guide_toggle_off)` → string `+0x148 = "click"`.

---

## 4. Design notes for extending these

- **Value semantics already fit a data-driven binding**: `value`, `min`, `max`,
  `round` are plain floats/bytes at fixed offsets — a mod layer can read/write them
  directly and only needs a `ValueChanged` hook (`SendActionsForControlEvent`) to
  propagate.
- **Textures are per-instance**, so a "theme" layer can swap `ui_slider_*` for mod
  sprites without touching code.
- The 0..100 default range means vanilla sliders are effectively percentages —
  music/sound sliders in `SettingsView` are built on this (see
  [layout_reference.md](../game/caver/gui/layout_reference.md)).

## 5. Proposed SRE hooks (sre13)

| Target | Addr (1.4.13) | Purpose |
|---|---|---|
| `GUISlider::DrawRect` | `0x524DE0` | restyle / add ticks (relay) |
| `GUISlider::TouchMoved` | `0x5255F8` | clamp/snap/quantise values (e.g. FPS presets) |
| `GUISwitch::TouchEnded` | `0x525E30` | intercept toggles |
| `GameInterfaceBuilder::Slider` | `0x41E78C` | stamp mod defaults (font, textures, step) |
