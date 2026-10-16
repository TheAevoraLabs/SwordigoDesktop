# GUIControl + GUIButton — button states, events, textures, sound (v1.4.13 arm64)

Sources: `OpenSwordigo/arm64_13/functions/Caver/GUIButton/*.c`, `GUIControl/*.c`,
`GameInterfaceBuilder/*.c`, `strings.tsv`.

Confidence legend: **verified** = read in a decompiled body · **inferred** · **guess**.
Base `GUIView` layout is in [GUIView.md](GUIView.md).

---

## 1. Hierarchy & size (verified)

```
GUIResponder → GUIView (256 B) → GUIControl → GUIButton (472 B)
```

`GUIButton` size **0x1D8 = 472** (`operator new(0x1D8u)` in `GameInterfaceBuilder::PlainButton`
`0x41D398` and `ControlButtonWithImage` `0x41D034`).
Ctor `0x51DE90`, deleting dtor `0x51F07C`/`0x482F04`.
vtable `off_6346F0`.

## 2. `GUIButton` field map (bytes 0x100–0x1D7)

| Off | Type | Name | Confidence |
|---|---|---|---|
| `+0x100 (256)` | `std::multimap<int /*GUIControl::Event*/, TargetAction>` header (ptr = `this+0x108`) | **target/actions table** | verified (ctor + `AddTargetActionForControlEvent`) |
| `+0x118 (280)` | `int` flags | **state bits**: `0x1` = pressed/highlighted, `0x2` = brighten/double-RGB | verified (`DrawRect`, `TouchBegan`, `TouchEnded`) |
| `+0x11C (284)` | `byte` | **skip-draw / disabled** — `DrawRect` returns immediately when set | verified (`0x51E240` first test) |
| `+0x120 (288)` | `GUIRoundedRect*` (scoped_ptr) | **normal-state frame** | verified (`SetFrameTextureForState`, `SetButtonType`) |
| `+0x128 (296)` | `GUIRoundedRect*` | **highlighted-state frame** | verified |
| `+0x130 (304)` | `GUIRoundedRect*` | **background rounded rect** (drawn first, always) | verified (`DrawRect`, `StateDidChange`) |
| `+0x138 (312)` | ? | zeroed by ctor | guess |
| `+0x140 (320)` | `GUITexturedRect*` | normal-state image | inferred |
| `+0x148 (328)` | `GUITexturedRect*` | highlighted-state image (drawn when `flags&1`) | verified (`DrawRect`) |
| `+0x150 (336)` | `Matrix4` | per-button transform (identity by default) | verified (ctor writes `identityMatrix`) |
| `+0x190 (400)` | `int` | **GUIButtonType** (`SetButtonType` guard) | verified |
| `+0x194 (404)` | `int` | layout flags for the label row (`\|= 2` used by `MainMenuView`) | inferred |
| `+0x198 (408)` | `xmmword` | constant written by `MainMenuButtonWithTitle` (`xmmword_259E10`) | inferred (label alignment knobs) |
| `+0x1A8 (424)` | `Vector2` | **label/icon anchor offset added to bounds centre** — `MainMenuButtonWithTitle` sets **(28.0, 3.0)** | verified (`DrawRect`, factory) |
| `+0x1B0 (432)` | `int` RGBA8 | **background colour** handed to `GUIRoundedRect::SetColor` | verified |
| `+0x1B4 (436..439)` | 4 bytes | **tint RGBA**, ctor default `-1` = `FFFFFFFF` (white); `DrawRect` doubles RGB when `flags&2` | verified |
| `+0x1B8 (440)` | `std::string` | **press sound-effect name** — if non-empty, `AudioSystem::PlaySoundEffectWithName(this+440)` fires in `TouchBegan` | verified (`0x51ED98`) |
| `+0x1C8 (456)` | ? | zeroed by ctor | guess |
| `+0x1D0 (464)` | `uint64` | **active touch id** (set in `TouchBegan`, cleared in `TouchEnded`) | verified |

## 3. Draw pipeline (`GUIButton::DrawRect` `0x51E240`)

1. `if (byte+284) return;` — hidden/disabled short-circuit.
2. `vtable[0x128]` (layout/ApplyTransform).
3. `SetMatrix(identity)`.
4. If `flags & 2`: tint RGB = byte `+436..438` × 2 (clamped to 255).
5. Draw `GUIRoundedRect` at `+304` (background) if present.
6. If `flags & 1` (pressed): draw rect at `+296` with tint, else rect at `+288` with tint.
7. Compute `centre = bounds.origin(+0x94) + 0.5*bounds.size(+0x9C) + labelOffset(+0x424)`,
   `roundf` it, then `M = spriteM * buttonTransform(+0x150)` → draw the highlighted
   `GUITexturedRect` at `+328` when pressed.
8. Base `GUIView::DrawRect` renders children (the title `GUILabel`).

`GUIButton::StateDidChange` (`0x51ECA4`): on entering highlight with a background rect,
`SetColor(+432)`; on leaving highlight while pressed,
`SetColor(-34736 = 0xFFFF7840 → bytes 40 78 FF FF)`.
Exact channel order (RGBA vs ARGB) **unverified**.

## 4. Button type → default skins (`SetButtonType` `0x51DF74`)

| `GUIButtonType` | Effect |
|---|---|
| `1` | allocates `GUIRoundedRect` **0xA8** at `+288` with texture **`ui_roundrect`**, and another at `+304` with **`ui_roundrectbg`**, then `SetColor(+304, +432)` |
| anything else | frees `+288` and `+304` |

Verified texture literals: `ui_roundrect` (SSO len 12), `ui_roundrectbg` (SSO len 14).

`SetFrameTextureForState(state, texture)` (`0x51E9EC`): `state&1` → replace `+296`
(highlighted), else `+288` (normal); creates a `GUIRoundedRect` and `InitWithTexture`;
then, if `bounds.w (+0x9C) > 0.01`, calls `vtable[0x58]` (SetNeedsLayout).

## 5. Input & event model (verified)

`GUIControl::AddTargetActionForControlEvent(void *target,
boost::function<void(void*, GUIEvent*)> const&, GUIControl::Event)` — `0x51F110`.
Storage: `std::multimap<int, GUIControl::TargetAction>` at `+0x100`.
`SendActionsForControlEvent(Event, uint)` — `0x51F3FC`.
`RemoveActionTarget(void*)` — `0x51F334`.

Touch flow:

```
TouchBegan (0x51ED98):  store touchId @+0x464; if (flags&3)==0 → flags|=1;
                        vtable[0x130] (state notify);
                        SendActions(0x200000), SendActions(1);
                        if string@+0x1B8 non-empty → PlaySoundEffectWithName
TouchMoved (0x51EF7C):  drag tracking (press survives leaving the rect)
TouchEnded (0x51EF78):  if touchId matches:
                          if (flags&3)==1 → SendActions(64)          ← TouchUpInside
                          if flags&1  → clear bit0, vtable[0x130], SendActions(0x200000)
                          clear +0x464
TouchCancelled(0x51F000)/CancelPress(0x51ED2C): reset without firing
```

Observed `GUIControl::Event` bit values: **`1`**, **`64`**, **`0x200000`**.
Mapping to named semantics (TouchDown / TouchUpInside / ValueChanged) is **inferred** —
`SendActionsForControlEvent` is the place to confirm by reading its switch.

## 6. Touch geometry

`GUIView::PointInside` uses `touchBounds (+0xA4)` when its width `> 0.001`,
otherwise `bounds (+0x94)`. Screens routinely call
`RectangleByOutsetting(bounds, 10.0)` and store it into `+0xA4` to give small
buttons a **10 px fat finger margin** (seen all over `MainMenuView::LayoutSubviews`).

## 7. The factory that *is* the Swordigo button style

`Caver::GameInterfaceBuilder` (1.4.13 addresses):

| Method | Addr | Alloc | Textures (verified literals) |
|---|---|---|---|
| `NormalLabel(str,color,color)` | `0x41C894` | `0x170` | — |
| `SmallLabel(str,color,color)` | `0x41CD8C` | `0x170` | — |
| `ControlButtonWithImage(tex)` | `0x41D034` | `0x1D8` | `ui_controls_button`, `ui_controls_button_pressed` |
| `PlainButton(...)` | `0x41D398` | `0x1D8` | (no direct texture) |
| `IconButton(tex,tex)` | `0x41D870` | — | `textbubble_border` |
| `FramedButtonWithTitle(str,bool)` | `0x41DAAC` | — | `ui_button`, `ui_button_light`, `ui_frame` |
| `MainMenuButtonWithTitle(str)` | `0x41DDE0` | — | `ui_mainmenu_button`, `ui_mainmenu_button_pressed` |
| `AlertView(title,msg,int,str*,int)` | `0x41E064` | `0x1C8` | — |
| `Slider(float,float)` | `0x41E78C` | `0x168` | `ui_slider_frame`, `ui_slider_bar`, `ui_slider_peg` |
| `Switch()` | `0x41ECEC` | `0x168` | `ui_guide_toggle_on`, `ui_guide_toggle_off` |
| `TextGradient(...)` | `0x41F07C` | 2×`0x18` | — |
| `InfoBubble(str,float)` | `0x41F25C` | `0x128` | `textbubble_peg`, `ui_frame` |

`MainMenuButtonWithTitle` recipe (verified): build a `FramedButtonWithTitle`, then
`SetFrameTextureForState(0, ui_mainmenu_button)`, `SetFrameTextureForState(1, ui_mainmenu_button_pressed)`,
`SetFrame({frame.x, frame.y, tex.w(+116), tex.h(+120)})`,
`+404 = -1`, `+408 = xmmword_259E10`, `SetNeedsLayout`, `+424 = (28.0, 3.0)`.

> **This is the single highest-leverage API for a moddable GUI**: call the factory,
> get a pixel-authentic widget with correct textures, fonts, hit slop and sounds.

## 8. Proposed SRE hooks (sre13)

| Target | Addr (1.4.13) | Purpose |
|---|---|---|
| `GUIButton::DrawRect` | `0x51E240` | skin override / extra decoration (relay to original) |
| `GUIButton::TouchBegan/Ended` | `0x51ED98` / `0x51EF78` | inject mod actions, block input |
| `GUIControl::SendActionsForControlEvent` | `0x51F3FC` | observe every button press game-wide (one hook = global button telemetry) |
| `GameInterfaceBuilder::*` | see table above | wrap factories to stamp mod defaults (font, sound, offset) |

sre12 precedent: `{ 0x49565c, "sre_GUIButton_DrawRect" }` (1.4.12 VA) with a relay stub.
