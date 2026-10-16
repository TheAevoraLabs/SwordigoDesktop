# GUIView — base view node, frame/bounds, subview tree, layout (v1.4.13 arm64)

Source of truth: `OpenSwordigo/arm64_13/functions/Caver/GUIView/*.c`, `GUIWindow/*.c`,
RTTI from `caver_rtti.txt`, edges from `callgraph.tsv`.

Confidence legend (see [README.md](README.md)):
**verified** = read directly in a decompiled body · **inferred** = deduced from call sites /
cross-function consistency · **guess** = hypothesis, do not build on it.

---

## 1. Place in the hierarchy (verified — RTTI + ctors)

```
Caver::GUIResponder                (event responder base; enable_shared_from_this)
 └── Caver::GUIView                (base UI node)
      ├── Caver::GUIWindow         (root; owns modal/popover stack + touch state machine)
      ├── Caver::GUIControl        (interactive base → GUIButton / GUISlider / GUISwitch)
      ├── Caver::GUILabel          (bitmap text)
      ├── Caver::GUIFrameView      (bordered panel: textured rect + rounded rect)
      ├── Caver::GUIScrollView, GUIPopoverView, GUIAlertView, GUITextBubble, …
      └── all screen views (MainMenuView, SettingsView, GameOverlayView, …)

Caver::GUIViewController / GUINavigationController   (screen controllers, not views)
Caver::GUIViewLayout / GUIMargins                   (anchor/margin data, proto-backed)
```

`GUIWindow` typeinfo `0x6d6ba0`, `GUIView` `0x6d6900`, `GUILabel` `0x6d6050`,
`GUIButton` `0x6d5a70`, `GUISlider` `0x6d6630`, `GUISwitch` `0x6d67a0`
(v1.4.13 arm64 addresses).

## 2. Object size

| Class | `operator new` | Bytes | Evidence |
|---|---|---|---|
| `GUIView` | — | **256 (0x100)** | inferred: `GUIFrameView` allocates `0x120` and writes its first own field at `+256`; `GUIButton` writes its first own field at `+256` |
| `GUIFrameView` | `0x120` | 288 | `GUISwitch` ctor `operator new(0x120u)` |
| `GUISlider` / `GUISwitch` | `0x168` | 360 | `GameInterfaceBuilder::Slider` / `::Switch` |
| `GUILabel` | `0x170` | 368 | `GameInterfaceBuilder::NormalLabel` / `SmallLabel` |
| `GUIButton` | `0x1D8` | 472 | `GameInterfaceBuilder::PlainButton` / `ControlButtonWithImage` |
| `GUIAlertView` | `0x1C8` | 456 | `GameInterfaceBuilder::AlertView` |
| `GUIRoundedRect` | `0xA8` | 168 | `GUIButton::SetButtonType` |
| `FontText` | `0xF0` | 240 | `GUILabel` ctor |

## 3. `GUIView` field map (base region 0x00–0xFF)

| Off | Type | Name | Confidence |
|---|---|---|---|
| `+0x00` | `vtable*` | vtable (`off_635500` for plain `GUIView`) | verified (ctor) |
| `+0x08` | `int` | **refcount** (`boost::intrusive_ptr` style; deleting dtor = `vtable[1]`) | verified |
| `+0x10`, `+0x18` | ptr | first-responder / window bookkeeping | guess |
| `+0x20` | ptr | **subview list head** (points at itself when empty) | verified (`GUIView` ctor, `AddSubview`) |
| `+0x28` | ptr | **subview list tail/prev link** (iterating starts here in `DrawRect`) | verified |
| `+0x30` | `size_t` | **subview count** (`AddSubview` does `*(this+0x30) += 1`) | verified (`0x5275D0`) |
| `+0x38` | `Rectangle` | **safe-area inset rect**; active only while its width (`+0x40`) `> 0` | verified (`safeBounds` `0x42A3E4`) |
| `+0x60` | `GUIView*` | **superview** (`Window()` = `superview ? superview->vtable[30] : 0`) | verified (`0x5285C0`) |
| `+0x80` | `int` flags | **layout/anchor flags** — bit `0x2` and bit `0x10` select which parent-edge/margin pair `LayoutSubviews` uses | verified (`LayoutSubviews` reads `child+0x80`) |
| `+0x84 (132)` | `Rectangle` | **frame** (position in superview; scissor uses `child+132`) | verified (`SetFrame`, `GUIView::DrawRect`) |
| `+0x94 (148)` | `Rectangle` | **bounds** (`SetBounds` writes the OWORD here; `PointInside`, `safeBounds`, all layout maths read it) | verified (`0x528F94`) |
| `+0xA4 (164)` | `Rectangle` | **touchBounds** (hit-test rect); used only when its width (`+0xAC`) `> 0.001`, else `bounds` | verified (`PointInside` `0x528908`) |
| `+0xB4 (180)` | `Matrix4` | **per-view ortho projection**, rebuilt by `SetBounds` via `Matrix4::Ortho(bounds.x, bounds.w, bounds.h, …)` | verified |
| `+0xF4 (244)` | `byte` | **hidden** (`DrawRect` skips hidden children; `HitTest` bails) | verified |
| `+0xF5 (245)` | `byte` | **userInteractionEnabled** (`GUILabel` ctor sets it to 0) | verified |
| `+0xF8 (248)` | `byte` | **clipsSubviews** (`DrawRect` toggles `GL_SCIFFOR_TEST` = 3089 around that child) | verified |

`safeBounds()` = `bounds`, intersected with superview's `safeBounds` (converted), then with
the `+0x38` inset when enabled. Every screen view starts its layout from `safeBounds`.

## 4. Key methods (1.4.13 arm64 addresses)

| Method | Addr | Note |
|---|---|---|
| `GUIView::GUIView()` | `0x526120` | field ground truth |
| `GUIView::Update(float)` | `0x526368` | drives animations |
| `GUIView::DrawRect(ctx, rect, mat)` | `0x5271DC` | iterates subviews, scissor, calls `DrawSubviewRect` |
| `GUIView::DrawSubviewRect` | `0x5268B0` | virtual dispatch via `vtable[0x50/8]` = **slot 10 = DrawRect** |
| `GUIView::LayoutSubviews()` | `0x527390` | margin/anchor pass over children |
| `GUIView::SetFrame(rect)` | `0x527490` | writes `+0x84`, then `vtable[0xF8/8]` (slot 31) with `{bounds.x, bounds.y, frame.w, frame.h}` |
| `GUIView::SetBounds(rect)` | `0x528F94` | writes `+0x94`, rebuilds ortho at `+0xB4`, then `vtable[0x58/8]` (slot 11) = **SetNeedsLayout** |
| `GUIView::AddSubview(shared_ptr)` | `0x5275D0` | 32-byte list node `{prev, next, child, ctrl}`; increments count at `+0x30` |
| `GUIView::RemoveFromSuperview` | `0x5279F0` | |
| `GUIView::PointInside` / `HitTest` | `0x528908` / `0x52897C` | `!hidden && userInteractionEnabled && vtable[0xE0/8]` |
| `GUIView::safeBounds` | `0x42A3E4` | recursive inset computation |
| `GUIView::Window` | `0x5285C0` | |
| `GUIView::PresentModalView` / `DismissModalView` | `0x5284C8` / `0x52671C` | |
| `GUIView::AddAnimation` / `RemoveAllAnimations` / `AddPopupAnimation` | `0x527E60` / `0x527ED4` / `0x527F90` | animation system entry points (undocumented) |

### Virtual slots observed (1.4.13)

| Slot | Offset | Meaning | Evidence |
|---|---|---|---|
| 10 | `0x50` | `DrawRect` | `DrawSubviewRect` dispatch |
| 11 | `0x58` | `SetNeedsLayout` / relayout request | called after every frame change (`MainMenuButtonWithTitle`, `SetFrameTextureForState`) |
| 12 | `0x60` | `AddSubview` | `GUISwitch` ctor |
| 28 | `0xE0` | hit-test predicate | `HitTest` |
| 30 | `0xF0` | `Window` | `GUIView::Window` |
| 31 | `0xF8` | apply-frame/bounds | `SetFrame` |
| 37 | `0x128` | **layout / ApplyTransform** | first call at top of every `DrawRect` |
| 38 | `0x130` | `StateDidChange`-style notify | `GUIButton::TouchBegan` |

## 5. `GUIView::LayoutSubviews` algorithm (verified, `0x527390`)

For each subview whose `flags (+0x80)` select margin-driven layout:

1. pick the margin pair from the child (`flags&0x2` → parent-right/bottom-anchored pair,
   `flags&0x10` → parent-left/top-anchored pair);
2. compute `newFrame = {parent.bounds.x + mLeft, parent.bounds.y + mTop,
   parent.bounds.w - mLeft - mRight, parent.bounds.h - mTop - mBottom}`;
3. store it at child `+0x84`, then call child `vtable[0xF8]` (apply bounds).

Children **without** those flag bits keep whatever frame their owning screen assigned —
this is why most Swordigo screens set frames imperatively in their own
`LayoutSubviews` override instead of using `GUIViewLayout`.

## 6. `GUIWindow` (root)

Addresses: ctor `0x529A3C`, `mainWindow` `0x529E3C`, `SetRootViewController` `0x529F18`,
`Update` `0x52A274`, `DrawRect` `0x52A544`, `PresentModalView` `0x52AECC`,
`DismissModalView` `0x52B1F0`, touch state machine
`BeginTouch` `0x52B924` / `MoveTouch` `0x52BE94` / `EndTouch` `0x52C2F4` /
`CancelTouch` `0x52C0D0`, keyboard `KeyDown` `0x52C7FC` / `KeyUp` `0x52C990` /
`KeyChar` `0x52CB54`, `HandleMouseEvent` `0x52C7F8`.

`GUIWindow` is the single place raw `FWTouch` / mouse / key events enter the GUI tree.
sre12 already replaces its `DrawRect` (`0x4a28bc` in 1.4.12) — see §8.

## 7. Cross-cutting facts

- **Refcounts** are `boost::intrusive_ptr` style, count at `+0x08`, deleting dtor = `vtable[1]`.
- `Rectangle` is 4 floats; everywhere the pattern is *rect at X, gate float = rect.w at X+8*
  (`touchBounds`, safe-area inset).
- All layout is done in **`LayoutSubviews` overrides on the screen class**, in screen
  coordinates (bounds start at 0,0), then `AnimateButtonsIn` / `AnimateIn` moves things.
- The 16:9 reference box appears in `MainMenuView::LayoutSubviews` — see
  [layout_reference.md](../game/caver/gui/layout_reference.md).

## 8. Proposed SRE hooks (sre13)

| Target | Addr (1.4.13) | Purpose |
|---|---|---|
| `GUIView::DrawRect` | `0x5271DC` | decorate/replace the whole render pass (relay to original) |
| `GUIView::LayoutSubviews` | `0x527390` | post-layout injection / re-flow of mod views |
| `GUIView::AddSubview` | `0x5275D0` | observe tree mutation, attach mod subtrees |
| `GUIWindow::DrawRect` | in `GUIWindow/` | top-of-tree takeover (sre12 pattern) |
| `GUIView::Update` | `0x526368` | drive mod animations (sre12 had to noop this in the emulator — use a relay, not a replacement) |

Access a view from C by **offset**, never by constructing one: read `+0x84` frame,
`+0x94` bounds, `+0xF4` hidden, `+0x20..0x30` subview list; mutate through the
engine's own `SetFrame` / `AddSubview` (resolve by symbol name, not hardcoded VA).
