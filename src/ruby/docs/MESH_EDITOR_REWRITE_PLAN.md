# In-scene mesh editor — rewrite to the Ruby Touch (mobile) model

Decision: **rewrite the Ruby GG desktop in-scene mesh editor mobile-style and
delete the old one.** This is a staged rewrite, not a rename of files: the mobile
editor is QML (`SceneViewportView.qml` + `MeshStyleBar.qml` + `BubbleStepper.qml`)
over C++ in `ruby_quick_viewport.cpp`; the desktop editor is C++/Qt Widgets inside
`viewport_3d_widget.cpp`. The shipped Qt Widgets UI is rebuilt to match the mobile
UX; the generation/geometry engine is kept.

> NOTE: a partial removal does not compile — `set_mesh_edit`, `mesh_edit_active`,
> the `activate_document` mesh guard, the gizmo-bar "Mesh" button and the mesh
> menu all reference the old editor. Stage 0 must land as one green build.

## What the mobile UX actually is (the target)

| Mobile piece | Behaviour to reproduce on desktop |
|---|---|
| `meshToolbarRow` | generator segmented picker (Boulder / Zenith / Zypher) + pencil toggle |
| pencil toggle | opens/closes the style dock |
| `styleDock` + `MeshStyleBar` | compact TOP/SIDE style bar, horizontally scrollable |
| `zypBubbles` (`BubbleStepper`) | hold-to-step FREQ / DEPTH / MIN R pads |
| `setGenerator(id)` | switch engine, persist, `refreshMeshEditPreview()` |
| `refreshMeshEditPreview()` | re-run the open edit immediately (live) |
| tap-select / drag | projection-locked vertex select + move, live apply |
| haptics / toast | (desktop: status-bar message instead) |

**Already ported in the previous task:** the desktop mesh bar with the generator
picker and the SIDE/TOP style combos + FREQ/DEPTH/MIN R spin boxes
(`viewport_3d_widget.cpp`, `mesh_bar_*`), sharing `ruby/tools/zypher_look.h`.
Stage 1 below is mostly about the *editing interaction*, not the style GUI.

## Stages

**Stage 0 — freeze the interface, build stays green.**
- Introduce `IMeshEditor` (header-only interface) with: `begin/end`, `import`,
  `apply`, `live preview`, `hover/select/move`, `pads()`.
- Make the current implementation satisfy it behind the interface (no behaviour
  change). Build + tests stay green.

**Stage 1 — new UI shell (mobile-shaped).**
- Replace the floating mesh bar's temporary widgets with the mobile layout:
  a bottom dock (compact bar) + a floating pad cluster (hold-to-step), matching
  `MeshStyleBar` + `BubbleStepper`.
- Reuse `ruby/tools/zypher_look.h` for the look (already shared).
- Status-bar messages replace mobile toasts/haptics.

**Stage 2 — editing interaction parity.**
- Tap-select vertex, drag-move, live apply (keep the existing 90 ms throttle).
- Long-press / context = insert / delete vertex (mobile ring gestures).
- Snap + hover feedback reused from the current overlay while it is being
  replaced.

**Stage 3 — delete the old editor.**
- Remove `draw_mesh_edit_overlay()` and the old mouse/key mesh handlers from
  `viewport_3d_widget.cpp`, plus the now-dead members.
- Update every call site in one commit (gizmo bar, menu, `activate_document`,
  `end_mesh_edit` on scene switch).

**Stage 4 — improve on mobile.**
- Desktop-only wins the mobile can't have: keyboard nudge, precise numeric entry,
  multi-vertex selection, undo grouping per drag, mouse-wheel depth.

## Risk / sequencing rule

Delete nothing until Stage 1–2 pass a real scene round-trip (open ground-mesh
object → edit vertices → switch generator → commit → save → reopen). Stage 3 is
the only destructive step and lands last, as a single green build.
