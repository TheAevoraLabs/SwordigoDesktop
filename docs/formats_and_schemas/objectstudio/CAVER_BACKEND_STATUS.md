# Caver backend — what actually landed

> Status note for `CAVER_CORE_RUNTIME_PLAN.md`. Everything below is code on disk
> under `src/ruby/caver/`, built into `libcaver.so` and linked by `ruby_gg`.
> Last edited: 2026-09-12.

## 1. The naming question, answered

`Caver` was never a product name: it appears only in the mangled symbols of
`libswordigo.so` (`_ZN5Caver…`), i.e. it is the engine's internal namespace. Names
and short phrases are not protected by copyright (copyright covers expression,
not labels); the real risk vector would be trademark, which needs use in commerce
as a source identifier — and Touch Foo never shipped "Caver" as a brand. So:

* `caver::` as an internal namespace and "Caver — Swordigo's internal engine" in
  comments: fine.
* Do not use it as a product name, do not imply affiliation/endorsement, keep
  "Swordigo"/"Touch Foo" out of any marketing, and keep the attribution line.
* Separate and larger question: the repo ships game binaries and extracted
  assets. That is a distribution question, not a naming one. Not legal advice.

## 2. One engine runner, not two

caver does **not** run `libswordigo`. Ruby already does, through the existing
path:

```
ruby_gg ──EnginePod──▶ `swordfare --pod-preview` ──loader + Dynarmic──▶ libswordigo
   ▲            │ shm pod::FrameRing (frames)          │
   └──ILiveEngine◀ stdin control msgs + TCP Lua console┘
```

* `ruby/emulator/engine_pod.{h,cpp}` + `platform/pod_ipc.{h,cpp}` — the genuine
  engine, spawned as a child, frames over shared memory, input/pause/scene-shift
  over stdin, guest Lua console over TCP.
* `caver::ILiveEngine` (`ruby/caver/live_engine.h`) — the seam. caver asks a live
  engine to attach / load a scene / step / pause, and may ask for per-object
  readback.
* `ruby/emulator/live_engine_pod.{h,cpp}` — the adapter implementing that seam
  over `EnginePod`. It adds **no** loading path: `attach()` boots the pod and
  `load_scene()` goes through the engine's own Scene Shifter. Calls from the
  preview worker thread are marshalled to the GUI thread; state is cached in
  atomics.
* `caver::Engine` (`Backend::Native` | `Backend::LiveEngine`) runs one backend on
  its own thread at a fixed 60 Hz with catch-up capped at 250 ms and publishes
  immutable snapshots. `Backend::LiveEngine` is a thin forwarder to the injected
  `ILiveEngine`; a fake "libswordigo backend" stub no longer exists anywhere.

Honest limitation, stated in code: `PodLiveEngine::read_state()` returns `false`
today and records why — the pod's control channel carries pixels, not object
transforms. Per-object readback will come from the guest Lua console
(`EnginePod::lua_console_port()`) or SRE hooks, never from a second emulator.

## 3. What is ported

| File | Contents |
|---|---|
| `component.h` / `component_registry.cpp` | All 80 component classes with their verbatim payload tags and ClassName aliases. The `Stage` bitmap is **derived** from the code that implements each stage (`behaviour_has_tick`, `visual_has_item`), so the coverage report can never overclaim. |
| `runtime.h` / `runtime.cpp` | `RuntimeScene`: unique identifiers, schema-decoded fields, resolved `*Id` edges with dangling detection, embedded programs (`String`/`Bytes` split, keep-active triage), the SCL-spawn path, hero lookup, a ground-query seam to the `av::` collision layer, scripted-event log, and the update order — **program pass → object pass → events**, which is `Caver::Scene::Update`'s own order (see `frame_runner.h`; the port previously ran components first and was wrong). |
| `program_state.{h,cpp}` | `Caver::ProgramState` — the engine's script **scheduler**, transcribed function by function from `arm64_13` (`ProgramState::Update` 0x545480, `Resume` 0x545624, `Execute` 0x5456E0, `Wait` 0x5457AC, `Abort` 0x5457BC, the ctor 0x544C68, `ProgramTable` 0x54765C). The recovered field layout with offsets and `sizeof == 96`, the value-marshalling rules (`IsInt`/`IsFloat` are the *same* `lua_isnumber` predicate; `IsBool` is a strict `lua_type == 1`; `PushString` is `lua_pushlstring`; `Pop(n)` is `lua_settop(~n)`), the geometry userdata payloads (`Vector3` 12 bytes, `Rectangle` 16), and the whole state machine: a `runs || runs_when_gated` gate, dt scaled by the owner's speed multiplier *then* the state's time scale, a stored wait counted down to `< 0`, the child recursion on the scaled dt, and the abort sweep. Resuming is deliberately unforgiving — `lua_resume` must return exactly `LUA_YIELD` or the state aborts, which is why every yield in the shipped corpus is a `Program.Wait`. |
| `frame_runner.{h,cpp}` | The recovered **frame order** of `Caver::Scene::Update` (0x4F81AC) as data, plus its tuning constants and pure policies. The two findings that changed this port: scripts run **before** components (step 2 of the frame, not after it), and collision is three separate phases (broadphase, narrowphase, then a pair purge that is skipped while paused or on a step ≤ 0.001). Also pins the cull volume (`-300..300` in z, camera AABB outset 200, activation radius 200) and the `Scene` field offsets. |
| `anim_bones.{h,cpp}` | The **animation node tree and bone matching**. `AnimNode` is a tree: `AnimKeysNode` samples one clip, `AnimBlendNode` crossfades two children. The recovered pieces are the ones that are unguessable — the clip's stored duration is the clip's own **minus 0.001** (0x325364); a node reports "changed" only when the *(frame, fraction)* pair moves by more than 0.001 (0x3254B0), which is what decouples the pose from the frame rate; `FloatDataTrack` is a 12-byte `{type, start, stride}` record and the buffer is **two interleaved strided blocks**, not an array of bone records; **quaternions blend by linear accumulation with a hemisphere flip** (0x325AD4), not by slerp, and are normalised later on ingest into the bone (0x4E6880); and the crossfade is **destructive on the left child's buffer** (0x324FB8), which is why `resultBuffer()` answers "left" until alpha reaches 0.999. Then the chain `bone.local = T·R`, `bone.world = parent.world · local`, `bone.skin = world · inverse_bind` (0x4E692C), the 220-byte bone stride confirmed from three independent functions, and the name lookup `BoneForName` (0x3269E4) that a bone-controlled collision shape uses. |
| `render_passes.{h,cpp}` | `Caver::Scene::Draw` (0x4F905C) — the **renderer**. Nineteen ordered passes with no queue and no sorting: setup, lights, light-overlay prepare, opaque geometry, blended geometry, water, stencil shadow volumes, light-overlay draw, a *second* model pass under `glDepthFunc(GL_LEQUAL)`, background, batched sprite shadows, weapon trails, glows, particles, sprites, health bars, magic explosions, the debug draw, and overlays. Notes the two non-obvious rules: ground meshes and models are drawn twice and the split is a per-component byte (`+184` equals the pass index, so the passes *partition* the collection, and only depth-writing changes between them), and the collision debug draw is part of the frame — objects the narrowphase flagged magenta, everything else white, with the flag consumed as they are drawn. |
| `char_anim.{h,cpp}` | The **character animation controller** — the layer that *chooses* what plays, where `anim_bones` samples and skins it. `CharAnimControllerComponent` binds six outlets by **identifier** (100..105: stand / walk / jump / airJump / fall / cast Animation, the names confirmed by the constructor's own `size << 1` size byte), and `Prepare` (0x357090) builds exactly three blend nodes: `moveBlend(stand, walk)` at 0.1, `airBlend(jump, fall)` at 0.35, `rootBlend(moveBlend, airBlend)` at 0.1. The last one's children are the other two, so the tree is a decision **over two decisions**, not a clip list. Every transition is one 16-bit flag write and both spellings occur — `0x0100` advances toward the right child, `0x0101` retreats toward the left, and `0x0101` sets *both* bytes, so a backward move is a fade and not a teleport. `reverse` is byte 44 and `blending` byte 45, so the **low** byte of the little-endian word is `reverse`; `Reset` (0x3574B4) clears `blending` and alpha and deliberately leaves `reverse` standing, which is why the word after a reset is `0x0001` and not the parked `0x0000`. `IsMoving` (0x3578B4) is `!moveBlend.reverse` — a flag, not a speed. The clip entry points are **phases**, so they scale with the clip: `set_phase(0.25)` in `StartMoving`, `set_phase(0.5)` when an action hands back to locomotion. `SetCurrentRunSpeed(s)` (0x35782C) writes `s / 100` into the node's `+52` factor and leaves its own `+48` speed alone; the node multiplies the two. Above the tree sits one more blend (`+368`), the action clip (`+352`), the `+344` auto-return gate and the `+376` countdown. `BlendFromAnimationNode` (0x3583DC) parks the action at alpha 0 with the countdown armed, and the *single* alpha sweep back out is what the countdown expires into. The derived `BlendToAnimationNode` (0x358638) **replaces** the base's (0x32CF38) rather than extending it: it fades from the previous *destination*, so chained transitions (swing → cast → stop) each leave from where the character visually was, and it never touches `m_currentNode` — the derived `Update` rewrites that once per frame to `actionBlend ? actionBlend : rootBlend`, which is the whole reason the override exists. Also recovered: the blend node **drives both children's clocks** every frame, so all six clips keep running and only the weights change. |
| `char_controller.{h,cpp}` | The **character animation driver** — `CharControllerComponent::Update` (0x35A960) and the two movement-path calls that bracket it, transcribed in body order. It is a state machine over *latched flags*, and the latch is the interesting part: **`+556` is not the physics test**, it is the movement path's own ground flag, written `1` by `StartMovingToDirection` (0x35D578) and `0` by `StopMovingToDirection` (0x35D6AC), and `Update` only reads it. So the run-cycle stop is not a stop at all: `StopMovingToDirection` clears the flag (and `+552`), the next `Update` takes the airborne branch, and the **0.11 s air rule cuts the cycle** — one stop path for leaving the ground and for standing still. Its ordering is observable too: the swing-tail test (`(duration * 0.9 - time) * speed * speed_multiplier > 0.01` on the `+360` node) runs *before* everything and its result is what the jump branch passes as `air_jump`, so a jump mid-swing plays `jumpAnimation` and a jump otherwise plays `airJumpAnimation`; `StartFalling` is entered **once** (`+596`) and only on a frame that is both clear of the ground (`+120 > 0.15`) and moving *down* (`+208 < 0.0`) with `CanStartFalling`; the jump grace `+572` is cleared once the air timer is strictly past `0.2`; and `Update` copies `+552` **into** `+548` (never the reverse, gated on the movement behaviour being one of Walk/Follow/Flee) rather than writing `+552`, which is what makes `Start/StopMovingToDirection` meaningful instead of overwritten each frame. `StartMoving` appears nowhere in `Update` — its only two callers are this file's `start_moving_to_direction` and `SetInitialAnimationControllerState`, where `moving` is a **separate `if`** and not part of the if/else-if chain, so a character that spawns moving *and* airborne gets both calls. `char_anim.h`'s `ClipBinding` is the port's side of this: it owns the six nodes, runs the recovered step (`speed * dt * speed_multiplier`) itself because the runtime layer has no clip fps or duration, and maps a node back to the clip name the render stream carries. |
| `level_mechanics.{h,cpp}` | `DoorControllerComponent` and `ElevatorControllerComponent`, transcribed function by function from `arm64_13`. They are **not** rates or lerps: the door is an animation seek plus a collision toggle (`Update` is a tail-call to the bound AnimationController, and `SetOpen` writes playback scale 0.5/1.0, a direction bit, a seek to the clip's open frame, a sound, and `SetEnabled` across the object's collision shapes); the elevator is a velocity-driven state machine that captures an origin, aims the owner's velocity at `origin + travel` at its speed, and **integrates the owner's own position itself**, snapping onto the target and zeroing the velocity on arrival. Mode is 1-based in the payload (`LoadFromProtobufMessage` stores `Mode - 1`); the constructor's speed is the literal 180.0 (`0x43340000`); a mode-1 lift keeps rising while `HandleMessage` keeps resetting its timer and returns one second after the last trigger. |
| `behaviour.h` / `behaviour.cpp` | The recovered per-component behaviours: physics (gravity vector + magnitude, ground/air deceleration, max speed, elasticity, grounding), Entity/EntityController (action state machine, facing, roam/patrol via `RoamAreaId`, targeting), the whole MonsterController family (shared recovered core + per-class traits: walking, charging, snapping, leaping, skelly, static, shooting, bat, bouncing, generic), Attack damage windows, Health/Damage, CollisionShape enable/friction/unsafe-ground, PressureTrigger/Touchable/Door/Elevator/Bush/Breakable/Collectable/ItemDrop, MagicBolt/Bomb/Explosion/Hookshot cast, ProjectileController, Skill/SwingableWeapon, ModelTransformController, OrbitController, Light, SimpleGlow pulse, particle emitters, WaterMesh, MonsterDeath. |
| `program_host.h` / `program_host.cpp` | The **real** Lua 5.1 VM (the host runtime filerift already embeds). Loads `Program.Bytes` via `luaL_loadbuffer` — the exact call `Caver::Program::LoadIntoState` makes — with `String` as fallback only. Keep-active programs run as coroutines resumed on the scene clock; `Program.Wait` yields; one-shot handlers run only when fired. Binds the API the corpus actually uses and records usage counts per function. |
| `visual.h` / `visual.cpp` | Components → draw items (Model/Sprite/Particle/Light/Glow/Shadow/Ground/Water/Background/Shape/Trail/Overlay) with live clock values. **Data only:** no GL, no Qt — the caller decides whether the viewport, an offline capture, or a studio diorama consumes it. The 3D viewport is untouched. |
| `library_manager.{h,cpp}` | The SCL runtime: name-keyed `ObjectLibrary` registry, bounded search paths, content-hash gated re-parse, import DAG, `refresh_changed()` for hot swap. |
| `engine.{h,cpp}` | The worker thread, the two backends, published snapshots. |

`RuntimeScene::spawn()` is `SceneObject::InitWithTemplate`: it resolves the
archetype through `LibraryManager::find_template`, instantiates its components,
resolves references, initialises behaviour, and starts its keep-active programs —
which is why `Scene.CreateObject`, item drops, death drops and spell spawns all
work through one path.

## 4. Why the Lua VM is the API layer, not "AI"

The decompilation and the corpus agree: the C++ controllers are **primitives**,
and the authored per-archetype AI is Lua stored in the data. So the port is
complete only when both halves are in place — which is why `behaviour.cpp` and
`program_host.cpp` landed together. What used to be a hardcoded `AiKind` enum in
`scene_player.cpp` is now: real scripts, running in a real Lua state, calling
real recovered primitives.

## 5. Still open (deliberately)

1. **Emitter cadence for generic `ParticleEmitter`** is `Type`-derived in the
   engine; a neutral 20 Hz default is used and marked as unverified in code.
   `FireEmitter` uses its authored `ParticleInterval`/`ParticleMaxAge`.
2. **`KeyframeAnimation` duration** is not in the component payload (it comes
   from the ANI clip), so `TimeToCompletion`/`TimeToFrame` are honest zeros until
   the ANI loader is wired in.
3. ~~**Door/elevator travel rates** are placeholders~~ — **resolved, and the
   premise was wrong.** Neither controller has a rate. The door's travel is the
   animation clip and the elevator's is a velocity; both were recovered into
   `level_mechanics.{h,cpp}` and the fabricated 1.5 / 2.0 units-per-second
   constants are gone from `behaviour.cpp`. What remains blocked here is only the
   door's 0..1 *amount*, which needs a loaded clip duration:
   `DoorController::open_amount()` returns `false` (not a number) until the ANI
   loader below lands, and `tick_door` deliberately leaves `open_amount` at 0
   rather than inventing one. The door's **collision** half — `Open()` disabling
   every `CollisionShape` on the object, `Close()` re-enabling them — is fully
   recovered and wired.
4. **Snapping/charging coefficient tables** are trait defaults; the specialised
   monsters are staged through the coverage matrix rather than guessed.
5. **Per-object readback from the live engine** (see §2) — the one thing the
   recovered/reference diff needs.6. **Hero archetype instantiation** from the shipped assets, and rbsrc — the ObjectStudio milestones, unchanged.
7. **Per-object speed multiplier.** `ProgramState::Update` scales dt by
   `SceneObject::updateSpeedMultiplier` and again by the state's own time scale
   (+92) before counting a wait down. `RuntimeObject` has no recovered
   equivalent, so `program_host.cpp` passes `1.0f` and names the gap rather than
   inventing a number; the consequence is that a slowed owner does not stretch
   its scripts' waits yet.
8. **The program states are flat in this port.** The engine keeps a root
   `ProgramState` embedded in the `Scene` (+40) with one child per
   component-program, and `Update` recurses over that tree with the parent's
   scaled dt. `ProgramHost` keeps a flat instance list keyed by object instead.
   `program_state.h` implements the recovered recursion and is tested, so the
   remaining work is adopting it — not discovering it.
9. **`Scene::DrawGroundMeshes` / `DrawModels` inner loops** are recovered only to
   the depth the bucket rule needs (the `+184` selector and the three model
   passes). The per-component draw calls and their texture binds are not yet
   transcribed.
10. **The rotation matrix's element signs.** `EvaluateMatrices` calls
    `Matrix4::FromRotationQuaternion`, and `PreTranslate` (0x5365BC) settles the
    *storage* — column-major, translation in the last column — but the rotation
    matrix's cross-term signs in `anim_bones.cpp` follow from that layout rather
    than from a line-by-line read of `FromRotationQuaternion`'s body. The
    composition order is pinned by test; the sign convention is not yet checked
    against the guest.
11. **`Skeleton`'s name storage.** `Skeleton::BoneIndexForName` (0x603600) is a
    Skeleton method, but its container is not in the functions transcribed, so
    `anim_bones` models the lookup's contract (first match, negative when
    absent) over a plain name list rather than over the recovered structure.
12. **The character controller is now recovered; two things around it are not.**
    `char_anim` transcribes `CharAnimControllerComponent` and its base,
    `char_controller` transcribes the driver, and the remaining call sites are
    pinned — `Swing` → `SwingWithAnimation`, `BeginCasting` / `CancelCasting` /
    `FinishCasting`, `HandleMessage` → `StandOnGround`, `Die` / `Hurt` →
    `StopAction`. Two corrections to what an earlier pass of this document
    claimed, both from reading the bodies rather than the call list:
    * `SetInitialAnimationControllerState` (0x35DC68) is `Reset`, then `if (+552)
      StartMoving()`, then an `if (+584 == 3) StartJumping(0) else if (+596)
      StartFalling()` — **moving is a separate `if`**, so the jump does not beat the
      run cycle; both run, and the run cycle's quarter-phase survives under the air
      call. Collapsing it into one chain loses that;
    * `Update` (0x35A960) does **not** write `+552`; it writes `+548` from `+552`.
      The moving direction is the movement path's, and the `+556` ground flag it
      clears is how a stop is expressed — see the `char_controller` row above.

    What remains:
    * the **Lua library** `AnimationController` that `AnimationControllerComponent::
      Prepare` (0x32CC18) registers has its function table at `off_6194E0` in
      `.data.rel.ro`, and the dumps are text-only — no data sections — so the
      script-facing method names are unrecovered. Only the library name is, and
      `GetBindings` / `GetEnumValuesForBindedProperty` (0x357060, 0x35706C) are
      the same shape: an index into a table the dumps do not carry;
    * the `+584` air sub-state is recovered only as the literal `3` (the compare
      that selects the jump clip), so `SpawnState::air_state` is named for what
      it does and not for what it means;
    * `KeyframeAnimation` and `ModelInstance`'s remaining members — the recovered
      node clocks are only as real as the clip the POD loader hands them. This one
      has a visible consequence in the port: the runtime layer has no clip fps or
      duration, so `ClipBinding` runs the recovered step itself and `set_phase(p)`
      — which the guest writes as `time = duration * p` — degrades to a restart
      from frame 0 until something calls `set_duration()`. A viewer-side pod
      loader can; the runtime cannot.

    Also worth recording from this pass: the blend node's `Update` recurses into
    **both** children with the same `dt` and folds their two booleans into its
    own answer, so every clip in the tree keeps its own clock and only the
    weights change. That is what lets `StartMoving` re-phase the run cycle
    without having had to start it, and it means an idle character can skip
    re-skinning while a running one cannot.
13. **`AnimKeysNode::UpdateFrameBlend` (0x325438) is recorded but not wired.**
    It is the same frame/fraction cache as `Update` without the time advance —
    the path used when something else drives the clock — and `anim_bones` keeps
    `update_time = false` plus `Update` as the equivalent, rather than a second
    entry point that could drift.

## 6. The component table is now measured, not transcribed

`tools/extract_component_schema.py` reads the schema straight out of any
`libswordigo.so`. Every component extension is a `static const int` global —
`Caver::Proto::<Class>::kExtensionFieldNumber` — and every payload field is
`…::k<Field>FieldNumber`, so `nm -D` plus a VA→file-offset map yields the whole
`Scene.proto` without reverse engineering a descriptor. Run against v1.4.13
(armeabi-v7a) it reports **81 extension slots and 624 named fields**.

What that settled, against the previous hand-carried table:

* the 78 rows we had were **right** (101 Model … 558 Spell), and the three
  classes we were missing are `ParticleFieldComponent` (257),
  `DimensionObjectComponent` (559) and `DimensionSpellComponent` (560) — the
  last two are distinct from `SpellComponent` (558), which is the shared base.
* `Caver::DefaultComponents::RegisterAll` (arm32_13, 0x2A2C38) registers **86**
  concrete classes. The 8 that have no extension symbol at all are
  **runtime-only** — `Transform`, `TransformController`, `ShatterComponent`,
  `TextBubbleComponent`, `RotatingBackgroundComponent`,
  `MagicParticleEmitterComponent`, `ProjectileMonsterControllerComponent` and
  `UtilityShape`. They appear in `.scl` files as nothing but ClassName +
  Identifier (verified in `dragonkin.scl`, `collectibles.scl`,
  `game_common.scl`). `ShapeComponent`, `MonsterControllerComponent` and
  `SpellComponent` are the inverse: they own an extension but are never
  registered directly — they are the bases the concrete classes share.

## 7. The decoder bug this exposed: a component has many payloads

A `Component` message does **not** carry one payload submessage. It carries the
base class's slot *and* the derived class's, and shipping data proves it:

| ClassName | slots written | seen in 98 shipping files |
|---|---|---|
| `CollisionShape` | 120 Shape + 121 Collision | 947 |
| `MonsterEntity` | 152 Entity + 158 MonsterEntity | 178 |
| every `*MonsterController` | 302 MonsterController + its own 303–314 | 60 |
| `HeroEntity` | 152 Entity + 165 HeroEntity | 2 |
| `CharAnimController` | 149 AnimationController + 150 CharAnim | 6 |
| `BoneControlledCollisionShape` | 120 + 121 + 124 | 26 |
| `MagicBolt` / `MagicBomb` / `MagicHookshot` / `FireBreath` | own slot + 558 Spell | 56 |

`RuntimeScene::decode_component` used to keep only the slot the ClassName named,
so **every extra slot was silently dropped**: `FacingDirection` from the base
Entity slot on all 178 monsters, `Friction`/`IsGround`/`OnCollide` from the Shape
slot on 947 collision shapes, the entire `SpellComponent.OnCast` base on every
cast spell. It now splits the message into `RuntimeComponent::payloads` (each
with its own schema class), decodes each against **its own** schema, tags every
`RuntimeField` with the slot it came from, and collects Lua handlers from all of
them.

`tests/caver_runtime_test.cpp` is the guard: it spawns all 136 templates of six
shipping `.scl` files and asserts the multi-slot reads. Today: 243 components
with ≥2 slots, 140 shape+collision pairs, 54 entity+monster pairs, 6 552 named
fields, zero unidentified classes.

## 7b. How much is left, measured

`tools/caver_recovery_census.py` answers "how far along is this" without a
maintainer's memory. It joins `OpenSwordigo/arm64_13/index.tsv` and
`arm32_13/index.tsv` (every indexed function), dedupes across the two ABIs, drops
what is not Caver's (`boost::`, `std::`, protobuf's generated `Proto::*`
accessors), and classifies each remaining symbol as `addr` (its VA is cited
somewhere in `src/`, `tests/` or `docs/`), `name` (its name is cited), or
`unseen`. It writes `src/ruby/caver/recovery/ledger.tsv` (one row per symbol,
with the decompiled body path) and `census_by_class.tsv`, and prints ranked next
targets.

The number it reports is a **floor**, and it says so: behaviour ported under a
new name is invisible to it, so `unseen` means "no citation found", not "not
implemented". That is deliberate — a census that could overclaim is worse than
none. Current reading:

```
RECOVERED (address or name in tree)     842 / 6535  = 12.9%
  of which address-cited                435
  name-only                             407
```

Two cautions, both learned by getting them wrong. The ledger names every symbol
in both binaries, so it must stay in `EVIDENCE_EXCLUDE` — when it was scanned as
input the census reported 83%, because it was citing itself. And the reported
percentage is a floor that moves only when prose cites a symbol or its VA, so it
is a progress *signal*, not the port's real size.

Regenerate the ledger after any recovery work:

```
tools/caver_recovery_census.py                    # report
tools/caver_recovery_census.py --write            # + the ledgers
tools/caver_recovery_census.py --class CharControllerComponent
tools/caver_recovery_census.py --body GameSceneController::CreateHeroObjectAt
```

### Reading the dumps

`tools/caver_show.py` and `tools/caver_callers.py` are the two readers the
recoveries in this port were actually transcribed with, and they exist because
both jobs were previously done by scrolling a 1.3 MB ledger:

```
tools/caver_show.py 'ProgramState::Push'                 # decompiled bodies
tools/caver_show.py 'GameSceneView' --calls --limit 8     # + each one's callees
tools/caver_show.py 'ProgramState::Wait' --abi arm32_13   # the other ABI

tools/caver_callers.py --callers 'ProgramState::Update'   # who drives it
tools/caver_callers.py --callees 'ProgramState::ProgramState'
tools/caver_callers.py --top GameSceneView                # its integration surface
```

`--callers` is the direction that finds a subsystem's entry points: the whole
frame order was found by asking what calls `ProgramState::Update` (only
`Scene::Update`) and what calls `Scene::Update` (only the view controllers).
Both tools normalise addresses before joining — the dumps spell the same VA
several ways, and a naive join reports "0 callers" for every symbol, which reads
exactly like "nothing calls this".

## 8. Build

```
cmake --build build-cmake --target caver     # libcaver.so
cmake --build build-cmake --target ruby_gg   # links libcaver + the pod adapter
ctest -R "caver_runtime_test|library_manager_test"
```

Regenerating the component table for a new engine version:

```
tools/extract_component_schema.py <libswordigo.so> --components   # slots
tools/extract_component_schema.py <libswordigo.so> --fields       # per-class fields
tools/scan_scl_components.py <assets-dir>                         # what shipping data uses
```

`caver` links `filerift` for the host Lua 5.1 runtime. If a 32-bit host ever
emits chunks for the arm64 guest, revisit the bytecode assumption
(`sizeof(size_t)==8`, little-endian, `lua_Number=double`) recorded in
`OBJECTSTUDIO_AND_RBSRC.md`.
