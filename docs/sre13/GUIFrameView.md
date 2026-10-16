# GUIFrameView + GUITexturedRect + GUIRoundedRect — the drawing primitives (v1.4.13 arm64)

Sources: `OpenSwordigo/arm64_13/functions/Caver/{GUIFrameView,GUITexturedRect,GUIRoundedRect}/*.c`.

Confidence: **verified** = read in decompiled body · **inferred** · **guess**.

These three classes are the *entire* visual vocabulary of the Swordigo skin: every
button, panel, plate and rounded background is one of them composited by
`GUIButton::DrawRect` / `GUIFrameView::DrawRect`.

---

## 1. Sizes

| Class | Size | Evidence |
|---|---|---|
| `GUIFrameView` | **0x120 = 288** | `GUISwitch` ctor `operator new(0x120u)` |
| `GUIRoundedRect` | **0xA8 = 168** | `GUIButton::SetButtonType`, `SetFrameTextureForState` |
| `GUITexturedRect` | ~112+ | fields read through `+0x6C`; exact size **unverified** |
| `Sprite` (inside rounded rect) | at `+0x50` | `Sprite::Init(this+0x50, this, 18, 16, 1)` → **18 verts × 16 B, dynamic** |

## 2. `Texture` (shared) layout used by all three

| Off | Type | Name | Confidence |
|---|---|---|---|
| `+0x00` | `vtable*` | | verified |
| `+0x08` | `int` | refcount | verified |
| `+0x74 (116)` | `float` | **pixel width** | verified (read twice: `MainMenuButtonWithTitle`, `MainMenuView::LayoutSubviews`, `GUISlider::DrawRect`) |
| `+0x78 (120)` | `float` | **pixel height** | verified |
| `+0x7C (124)` / `+0x80 (128)` | `float` | UV origin | inferred |
| `+0x84 (132)` / `+0x88 (136)` | `float` | UV size | inferred |

> Sprite frames inside atlases are addressed as **sub-rects of a `Texture`**, which is
> how `ui_main_button`, `ui_slider_peg`, … are all individual entries of
> `ui_game_atlas` / `ui_game2_atlas` / `game_common_atlas`.

## 3. `GUITexturedRect` (a textured quad)

Field map:

| Off | Type | Name | Confidence |
|---|---|---|---|
| `+0x00` | `Texture*` | texture (intrusive, refcounted) | verified |
| `+0x08` | `byte` | **scale mode flag** (`CreateWithBounds` `a3 & 1`) | verified |
| `+0x0C..0x1B` | `Rectangle` | **bounds / source sub-rect** | verified |
| `+0x28..0x6B` | vertex block | 4 corners: pos at `+0x20/+0x24`, `+0x2C/+0x30`, `+0x38/+0x3C`, `+0x48/+0x4C`; per-corner colour at `+0x28/+0x3C/+0x50/+0x64` | inferred |

`CreateWithBounds(result, rect, scaleFlag)` (`0x532694`):
- `scaleFlag & 1` → `u = x / texW`, i.e. **stretch the sub-rect across the quad**;
- else `u = uvOrigin + x`, i.e. **treat the rect as UV offsets** (atlas sub-frame).

Methods: ctor `0x53228C`, `Reset` `0x5322A8`, `InitWithTexture` `0x53257C` / `0x532410`,
`InitWithTextureName` `0x53249C`, `CreateVertexData` `0x532410`,
`CreateWithBounds` `0x532694`, `Draw` `0x532734`.

## 4. `GUIRoundedRect` (nine-patch rounded plate)

Field map:

| Off | Type | Name | Confidence |
|---|---|---|---|
| `+0x00` | `Texture*` | plate texture (`ui_roundrect`, `ui_roundrectbg`) | verified |
| `+0x0C..+0x38` | 12 floats (`+0x0C,+0x10,…,+0x38`) | **nine-patch insets / corner sizes** used by `CreateSprite` | inferred |
| `+0x50 (80)` | `Sprite` | 18-vertex dynamic sprite rebuilt when geometry changes | verified |

Methods: ctor `0x531158`, `InitWithTextureName` `0x531190`, `InitWithTexture` `0x531268`,
`CreateWithOuterBounds` `0x53132C`, `CreateSprite` `0x531378`,
`CreateWithInnerBounds` `0x5314D0`, `SetColor` `0x531540`, `Draw` `0x531590`.

`CreateSprite` builds the plate from `texture.uvOrigin/uvSize` (+124..+136) and the
inset floats — i.e. **a real nine-patch**, which is why Swordigo's frames look crisp at
any size. `SetColor` tints it (this is how pressed/disabled buttons recolour).

## 5. `GUIFrameView` (panel = textured rect + rounded rect + children)

Size 288; ctor `0x51FDD0`, vtable `off_634B58`.

| Off | Type | Name | Confidence |
|---|---|---|---|
| `+0x00..0xFF` | base `GUIView` | | |
| `+0x100 (256)` | `GUITexturedRect*` | **frame image** | verified (`DrawRect` `*((QWORD*)this+32)`) |
| `+0x108 (264)` | `GUIRoundedRect*` | **rounded overlay** | verified (`+33`) |
| `+0xF9 (249)` | dword (init `-1`) | tint/colour byte(s) written by ctor | inferred |
| `+0x118 (280)` | `byte` | **centre-origin flag** — when set, `DrawRect` builds a matrix translating to `bounds` centre and back, so children are positioned around the middle | verified (`0x5200F0`) |

`GUIFrameView::DrawRect` (`0x5200F0`) order (verified):
1. `vtable[0x128]` (layout)
2. `SetIdentityMatrix`
3. if centre flag: `PreTranslate(bounds centre)` / `PostTranslate(-centre)`
4. draw `GUITexturedRect (+0x100)`, then `GUIRoundedRect (+0x108)`
5. `GUIView::DrawRect` → children

Other methods: `LayoutSubviews` `0x51FE14`, `SetFrameTexture(tex,bool)` `0x51FE5C`,
`SetFrameColor` `0x520080`.

## 6. Reusable recipe for a mod widget

```
new GUIFrameView            (0x120)
  → SetFrameTexture(atlas_subframe, /*stretch=*/1)     // panel art
  → SetFrameColor(colour)                              // tint
  → add children: GUILabel (FontLibrary::DefaultFont), GUIButton from GameInterfaceBuilder
  → SetFrame({x,y,w,h}) ; touchBounds = outset(bounds, 10)
  → parent->AddSubview(this)
```

Because `GUITexturedRect` accepts a **sub-rect of any texture**, a mod can ship one PNG,
register it with `TextureLibrary::LoadTextureAtlasWithName`, and get authentic
nine-patch panels without touching engine code.

## 7. Proposed SRE hooks (sre13)

| Target | Addr (1.4.13) | Purpose |
|---|---|---|
| `GUIFrameView::DrawRect` | `0x5200F0` | panel skin override (sre12 hooks this at `0x497658`) |
| `GUITexturedRect::Draw` | `0x532734` | global texture redirect / atlas remap |
| `GUIRoundedRect::CreateSprite` | `0x531378` | change corner radius / insets globally (theme) |
| `TextureLibrary::TextureForName` | see `TextureLibrary.md` | mod texture substitution |
