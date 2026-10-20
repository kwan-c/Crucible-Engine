# Milestone 5: Animator Controller runtime

Controllers select and blend clips on a complete model. They use the existing
CPU pose evaluator, AnimatedModel component, ResourceManager and MeshRenderer
render extraction. No model importer, renderer, shader or bone entities are added.

## Try it in the engine

Build Debug/x64 Editor and open `Scenes/Animator Controller Preview.scene`.
Alternatively, from `build/Editor/Debug`:

```powershell
.\Editor.exe --scene "Scenes/Animator Controller Preview.scene"
```

Two independent golems use `Animation/Golem Demo.animcontroller`. Each cycles
from Heavy_Walk (clip 4) to Fast_Boost (clip 0) with a 0.6-second crossfade;
the right instance runs at 0.7 speed. This is example asset configuration, not
engine policy. The original Animation Preview scene is not required to change.

Select either golem and expand AnimatedModel:

- Controller selects a `.animcontroller` asset; None restores direct clip controls.
- State, destination, fade percentage, sample time and normalized progress show
  that instance's current runtime state.
- Enable **Boost** under Runtime Parameters to enter/hold Boost. Disable it to
  return to Walk when Boost reaches its exit time. **Pulse** requests a transition
  to Boost without changing the bool. A trigger remains pending until a matching
  transition consumes it; requesting Boost while already there does not consume it.
- Pause/Play holds/resumes that instance. Stop resets it and pauses; Restart resets
  defaults and starts it. Global Pause freezes all clocks, fades and transitions.
- Parameters and playback buttons are temporary. Save Scene stores the controller
  GUID and authored playback settings. Controller defaults live in its asset.
- MeshRenderer still owns visibility, All Materials and Material Slots.

The demo switches to Boost after two Walk cycles even without input. It returns
after a Boost cycle when Boost is false. These automatic exits make the feature
visible immediately. Use the Inspector state display to distinguish a blend
from a direct clip change.

## Ownership and runtime flow

```text
.animcontroller GUID -> ResourceManager::GetAnimatorController -> shared const definition
gameplay parameters -> AnimatedModel.controller (per-entity AnimatorInstance)
states/transitions/clocks -> SampleModelLocalPose(source and destination)
local TRS crossfade -> BuildModelPose (one hierarchy/palette build)
AnimatedModel.pose -> RenderExtractSystem + MeshRenderer -> existing render passes
```

The optional `AnimatedModel.controllerAsset` is the only new authored component
field. Zero preserves single-clip behavior. Existing speed is a signed global
controller playback multiplier; loop and clip apply only to direct playback.
Controller states own their individual clip index, speed and loop setting.
Play On Start and Preview In Editor apply to both modes.

Definitions contain no clocks, parameters that mutate during playback, ECS or GPU
handles. Every instance owns its parameter values, clocks and blend buffers by
value. Copies do not share mutable arrays. ResourceManager retains immutable
definitions until shutdown, following the current resource cache policy.

Model/controller reassignment and global Play/Stop reset the instance. Pause and
Resume preserve it, including pending triggers and active fades. Batched Play/Pause
uses the existing PlayMode session revision. Scene/prefab serialization and play
snapshots retain controllerAsset; runtime fields are not reflected or serialized.
Removal/destruction uses the Milestone 4 renderer cleanup. Removing AnimatedModel
still leaves the complete model in rest pose.

A controller whose clip indices do not exist on the assigned model fails clearly,
logs once per assignment and hides its animated parts. A missing controller also
hides stale poses; valid reassignment recovers. It does not silently play another
clip. Clip indices are authoritative, including when names are duplicate/empty.
Using a controller with another compatible index layout is an authoring decision;
the engine cannot infer that an index has the same gameplay meaning on two models.

## Definition format and validation

`.animcontroller` is versioned, engine-owned JSON, analogous to `.mat`. It does not
parse GLB or another source animation format at runtime. Existing .meta GUIDs,
AssetManager indexing and Inspector drag/drop are reused. No dependencies are added.

Required top-level fields are version (1), entryState, parameters, states and
transitions. Arrays use zero-based indices. A minimal definition is:

```json
{
  "version": 1,
  "entryState": 0,
  "parameters": [{"name": "Moving", "type": "Bool", "default": false}],
  "states": [
    {"name": "Idle", "clip": 0, "speed": 1, "loop": true},
    {"name": "Walk", "clip": 1, "speed": 1, "loop": true}
  ],
  "transitions": [
    {"from": 0, "to": 1, "duration": 0.25,
     "conditions": [{"parameter": 0, "mode": "If"}]},
    {"from": 1, "to": 0, "duration": 0.25,
     "conditions": [{"parameter": 0, "mode": "IfNot"}]}
  ]
}
```

The clip indices above are illustrative; choose the actual model's indices.
`4294967295` selects authored rest pose. Names are configurable and must be
nonempty/unique within parameters or states; no name has special engine behavior.

Parameter types are Float, Integer (signed 32-bit), Bool and Trigger. Defaults
must have the matching JSON type. Triggers default false. Float/Integer conditions
use Greater, Less, Equals or NotEqual and a typed `value`; Bool uses If/IfNot;
Trigger uses If. All conditions on a transition must pass. Numeric Equals is an
exact comparison; use thresholds for continuous gameplay values.

Transitions support:

- `from`: a state index, or `"Any"`; `to`: a valid state index.
- `duration`: nonnegative seconds, default 0.2. Zero switches immediately.
- `offset`: destination normalized start offset in [0,1], default 0.
- `hasExitTime`: default false; `exitTime`: nonnegative normalized cycles, default 1.
- `canTransitionToSelf`: default false. Any State ignores its current state unless
  this is enabled. An explicit self edge requires it.
- `conditions`: required array; empty means unconditional once the exit gate passes.

Validation rejects invalid references, types, enum names, duplicate names,
non-finite numbers, unsupported versions and oversized resources. Limits are
4 MiB JSON, 4096 states/parameters, 16384 transitions and 64 conditions per edge.
Decode returns no partial definition. `EncodeAnimatorController` validates and
produces the same format for future graph authoring.

## Timing, blending and transition policy

Transition array order sets priority, including Any State edges. At most one
automatic transition is accepted per update. Accepted triggers are consumed;
unmatched triggers remain pending. Automatic transitions wait until an active
crossfade completes. Parameters can still change during a fade.

Exit progress is entry offset plus clip cycles travelled; it is separate from the
wrapped sample clock. Reverse entry starts at the clip end and progress increases
as it plays backwards. Non-looping clocks clamp. An offset entry can still reach
an exit time of 1 at the clip's end. For a looping exit below 1, the gate opens when
that phase is crossed on each cycle; conditions missed at that gate wait for the
next cycle. Exit times of 1 or more, and non-looping exits, remain eligible once
reached. Long frames can cross multiple cycles. Zero-length/rest states reach
progress 1 when advanced. Exit time 0 is eligible on the first active update.

State clocks advance at `state.speed * AnimatedModel.speed`. Fade time advances
at the absolute global speed, so negative playback reverses clips while fades
still complete. Zero global speed holds clocks/fades. Invalid elapsed time/speed
freezes advancement. Pause also prevents condition evaluation/trigger consumption.
A completed non-looping controller state holds its final pose while the controller
continues checking exits; it does not pause the whole state machine.

On an accepted automatic transition, the source has advanced to this frame's
sample and the destination begins at its offset with blend weight zero. Its fade
advances from the next update. Fade completion commits the destination; another
automatic transition can be accepted on a subsequent update. This is a discrete
frame policy, not sub-frame traversal through arbitrarily many transitions.

Both local poses start from authored defaults, so properties absent from either
clip do not retain stale values. Translation/scale use linear interpolation;
normalized quaternions use shortest-arc slerp. Static matrix nodes retain their
exact authored matrix. Globals and all mesh-local skin palettes are rebuilt once
from the blended locals; matrices/palettes themselves are never interpolated.
Rigid mesh nodes and skeletal joints therefore follow the same blend.

## Gameplay API

Gameplay systems set parameters before PreRender animation evaluation. The ECS
instance is initialized on its first animation update; setters return false before
initialization, for an unknown name, a wrong type or a non-finite float. For example:

```cpp
if (auto* animation = world.Write<AnimatedModel>(entity)) {
    animation->controller.SetFloat("Speed", resolvedSpeed);
    animation->controller.SetBool("Grounded", grounded);
    if (attackAccepted) animation->controller.SetTrigger("Attack");
}
```

These names must be declared by that entity's controller asset. They are not engine
defaults and this milestone does not bind movement/combat inputs automatically.
SetInteger, ResetTrigger and Parameter(index) provide the other typed operations.

`CrossFade(stateIndex, seconds, normalizedOffset, globalSpeed)` lets gameplay
request a state directly. If it interrupts another fade, its source is a frozen
copy of the last displayed blended local pose, preventing a jump back to the
original source clip. Manual requests do not consume unrelated triggers.

The CPU API can also run without Core, ECS, JSON or an editor:

```cpp
AnimatorInstance instance;
std::string error;
if (instance.Reset(definition, model, &error)) {
    instance.SetBool("Moving", true);
    instance.Update(deltaSeconds, 1.0f, true, pose);
}
```

Keep definition/model alive and immutable until Reset. Check Update's return value
before using output. BuildModelPose's finite-transform/failure contract still applies.

## Verification performed

Normal Debug Editor and Game targets build successfully using VS 2022/v143 and
the existing Vulkan SDK. The final build reports no compiler warnings/errors.
Five source models are unchanged and compile successfully through the existing
offline build dependency.

An ignored CPU executable compiles the production AnimatorController, codec,
ModelPose and compiled-model reader without an ECS, renderer or Editor. It uses
the real 38-node, 19-clip golem and checks typed parameters, validation/JSON round
trips, independent instances/copies, blend translation/scale midpoints and
shortest-arc quaternion midpoints, paused/zero-speed holds, explicit interruption,
trigger consumption, AND conditions, Any State priority, looping/reverse/one-shot
exit gates, offset exits and rest/zero-duration states. A 360-frame run evaluates
two independent instances and checks finite output matrices. The run passes
221393 assertions, most of which are per-matrix finite-value checks.

A separate ignored native probe links the actual Engine DLL and drives Core's
ordinary frame loop, ECS, PlayMode, SceneManager and Vulkan renderer. It passes
61 checks including controller startup, global/slot material batching, independent
parameters, held fades, batched Play/Pause resets, pending triggers, Resume/Stop,
component/scene serialization, scene reload, removal/re-addition, direct-clip
fallback, invalid controller/model rejection and recovery, destruction, scene
replacement, snapshot restoration and render proxy cleanup at shutdown.

The Milestone 4 regression probe passes 103 lifecycle/render/material checks and
five fresh-process embedded-texture reload checks against the new DLL. Its local
setup explicitly enables Play On Start so it does not change the existing preview
scene's authored settings. The scratch .mat and .meta are removed afterward.

The normal Editor loads Animator Controller Preview, remains running for eight
seconds, and closes normally with exit code 0. All verification processes exit 0
with empty stderr; runtime logs contain no Vulkan validation errors. Two warnings
in the controller integration probe are deliberate: an incompatible static model
and a missing controller GUID. The regression probe also deliberately requests a
missing model. Inspector interactions have not been manually clicked through;
use the preview checklist above for interactive review. No GPU pixel readbacks or
long-duration stability claims are added in this milestone.

Verification artifacts are ignored under build/setup-tools:
milestone5-build.log, milestone5-verify-build.log, milestone5-verification.log,
milestone5-integration-stdout/stderr.log, milestone5-regression-stdout/stderr.log,
milestone5-regression-reload-stdout/stderr.log and milestone5-editor-stdout/stderr.log.
No permanent test target or test model asset is added.

## Scope and handoff

This is one state-machine layer with clip/rest states. Blend trees, layer masks,
root-motion extraction and automatic transition interruption policies are not added.
Explicit gameplay CrossFade can interrupt. Controller hot reload follows the existing
resource limitation: edit the asset and restart. The runtime has no editor dependency.

Milestone 6 now authors these same definitions and inspects these instances through
the graph panel; see AnimatorGraphEditor.md. Its saves publish definition revisions
safely and layout-only saves preserve playback. Milestone 7 adds sockets and
independent modular/weapon playback; see [ModularAnimation.md](ModularAnimation.md).
