# Milestone 2: animation runtime and pose evaluation

Completed on 2026-10-20 against the compiled model resources from Milestone 1.
The earlier preview already contained a working evaluator. This milestone reviews
and extends that evaluator rather than adding a parallel animation subsystem.
No source parser, compiled format, shader, Vulkan buffer layout, asset, controller
or new editor panel changes are part of this milestone.

## Runtime flow and ownership

```text
ResourceManager::GetModel -> shared const ModelResource
per-instance time/speed/playing/loop -> StepModelPlayback
ModelResource + clip/time -> SampleModelLocalPose -> pose.local
pose.local + ModelResource -> BuildModelPose -> pose.global + pose.palettes
AnimatedModelSystem -> per-entity AnimatedModel.pose -> RenderExtractSystem -> render proxies
```

ModelPose arrays belong to an instance. Definitions, keys, joint mappings and
inverse binds remain immutable/shared. The existing ECS component retains its
clock and authored playback settings. No entity per bone is created. The CPU API
has no renderer, Vulkan, ECS, JSON or source-model dependency beyond EngineExport.
ModelResource must have been validated by the compiled-resource reader; validation
of every index/vertex is not repeated on each animation frame.

## Public API (Animation/ModelPose.h)

```cpp
ModelPlaybackStep StepModelPlayback(float time, double elapsedSeconds,
                                   float speed, float duration, bool loop);
bool SampleModelLocalPose(const ModelResource&, uint32_t clip, float time,
                          std::vector<ModelLocalTransform>& local);
bool BuildModelPose(const ModelResource&, ModelPose&);
bool EvaluateModelPose(const ModelResource&, uint32_t clip, float time, ModelPose&);
```

EvaluateModelPose calls the sampling and build stages. Keeping them separate lets
later controller/blending code modify local TRS and rebuild globals/palettes once.
No blending or controller logic is implemented now. AdvanceModelTime remains as
the compatibility helper for an already-signed float delta.

Sampling resets every local node to authored defaults each time. Only targeted
properties are replaced, preventing stale values when changing clips. Static
matrix nodes keep their exact matrix. InvalidModelIndex requests authored rest
pose; other invalid indices return false. The existing ECS bridge intentionally
uses rest pose for a static model or an unavailable clip selection.

Time is relative to the clip's first key. Sampling clamps to the clip range and
each channel's key range, without extrapolation. Channels may have different key
ranges. STEP uses the preceding key and switches exactly at the next key; a
single-key channel remains constant. LINEAR interpolates translations/scales;
rotations normalize both endpoint quaternions, use shortest-path slerp, and
normalize the result. Key timestamps plus relative time and interval arithmetic
use double precision, preserving midpoints even at large original timestamps.
Non-finite requested sample time is treated as zero.

## Playback behavior

StepModelPlayback returns time and a completion flag, with no shared state.
Callers implement Pause by withholding steps, and Stop by setting time to zero
and pausing. The existing component/editor controls retain those semantics.

- Positive/negative speeds advance/reverse. A newly selected reverse clip that
  is already playing starts at its final key. Paused selection and Stop use zero.
- Looping wraps in both directions, including multi-cycle elapsed time. The result
  stays in the half-open range [0,duration), including float-rounding edge cases.
- Non-looping playback clamps at either boundary and reports completion only when
  motion reaches that boundary. AnimatedModelSystem then clears playing.
- Zero speed or elapsed time holds a scrubbed final key instead of wrapping it.
- Zero-length clips have a constant pose and finish immediately, including when
  looping was requested. Nonpositive/non-finite durations also finish at zero.
- Invalid speed/elapsed time, negative elapsed time or double arithmetic overflow
  holds the sanitized current time. Non-finite time is sanitized to zero.
- Speed multiplication uses double precision before wrapping/clamping, avoiding
  float overflow from otherwise finite speed/frame-time inputs. Stored time and
  output matrices retain the existing float engine representation.

No frame-delta cap changes authored playback speed or drops whole loops.

## Hierarchy and skin coordinate contract

BuildModelPose consumes local transforms, composes T * R * S (or the exact authored
matrix), then follows nodeOrder with parent globals applied first. All model nodes
are evaluated, including intermediate nodes, rigid mesh nodes and unused nodes.
There is no separate skeleton-only transform tree.

For each mesh node bound to a skin:

```text
palette[j] = inverse(meshNodeGlobal) * jointNodeGlobal[j] * inverseBind[j]
world vertex = entityWorld * meshNodeGlobal * sum(weight[j] * palette[j] * vertex)
```

JOINTS indexes that binding's skin-local joint array, not model nodes. Multiple
skins, different joint orders, reused meshes and separate mesh-node transforms
therefore produce separate palettes. Rigid/non-mesh nodes have empty palettes.
The renderer remains responsible for entityWorld and GPU application.

Inversion and palette multiplication use double precision before conversion to
float. The old fixed float-determinant threshold incorrectly discarded small but
invertible scales; the new code checks actual singularity and finite output.
A singular/unrepresentable binding exposes an empty palette, letting the existing
bridge hide only that binding and continue with other usable parts.

Non-finite local/global matrices fail the pose, clearing global/palette arrays.
The caller must check the bool result. The ECS bridge hides that instance's proxies
and warns once per continuous failure, then resumes normally after recovery.
Local arrays remain available for diagnosis. No invalid evaluated matrices are
passed to the existing renderer.

## Files changed

- Engine/src/Animation/ModelPose.h and .cpp: public contracts, clock stepping,
  separate local sampling/build stages, quaternion/time/inversion corrections.
- Engine/src/Animation/AnimatedModelSystem.h and .cpp: consume the clock helper,
  reverse-clip initialization and failed-pose handling in the existing bridge.
- Docs/AnimationRuntime.md (new), AnimationRoadmap.md, AnimationUsage.md and
  ModelImport.md: completion state, controls, contracts and next-milestone handoff.

## Verification actually performed

The normal Debug/x64 Editor and Game targets built successfully with VS 2022/v143:

```powershell
& ".\build\setup-tools\cmake-3.30.3-windows-x86_64\bin\cmake.exe" --build build --config Debug --target Editor Game -- /m:1 /v:minimal /nologo
```

A temporary CPU-only executable compiled the production ModelPose.cpp and
CompiledModel.cpp using the installed cl.exe, C++23 and existing GLM headers. It
read the five actual .cmodel resources, without loading GLBs or linking a renderer.
Its independent double-precision reference used explicit key searches/quaternion
interpolation and ancestor walks instead of the production traversal. Results:

- 403 pose evaluations passed, covering all 19 actual golem clips, keyframes,
  between-key times, clamping, rest poses and the four existing static models.
- Hierarchy matrices and skin/CPU-vertex results matched the independent reference.
  Separate instances retained independent arrays, and re-encoding shared resources
  after evaluation matched the original compiled bytes.
- In-memory numerical variants exercised STEP, single-key clips, nonzero/large
  timestamp offsets, approximate-unit/shortest-path rotations, multiple skins,
  reordered joints and multiple mesh-node bindings. No synthetic source asset was
  created or required. Three active bindings were exercised in these variants.
- Forward/reverse wrapping/completion, large deltas, zero-speed endpoint holds,
  zero duration, invalid time/speed and overflow handling passed.
- Small invertible scales retained palettes; singular bindings were suppressed;
  non-finite global poses failed cleanly and recovered on valid evaluation.
- The actual Editor loaded Animation Preview.scene and reported two compiled golem
  instances with 11 render parts and 19 clips each. It exited with code 0 and
  logged no warnings/errors. It closed before the six-second observation interval
  finished, so this establishes loading/clean shutdown, not an interactive control
  or sustained-window-responsiveness check. Visual review remains manual.

No test project or source-model fixture was added. Local verification logs are
under build/setup-tools: milestone2-build.log, milestone2-verification.log,
milestone2-verify-build.log and milestone2-editor-stdout/stderr.log.

## Manual check and Milestone 3 handoff

Build Editor, open Scenes/Animation Preview.scene, select either golem and try:

1. Pause and scrub: pose follows time; zero speed holds the selected pose.
2. Play at positive and negative speeds. Disable looping and confirm the last/
   first key holds after completion. Select another playing reverse clip to start
   from that clip's end. Stop returns to the first key and pauses.
3. Switch clips/rest pose: unanimated properties return to authored defaults.
   The other golem instance retains its own clip/time/speed.
4. Inspect rigid weapons/attachments as well as weighted limbs in the clip list.

Milestone 3 handoff: review the preview's palette uploads, frame ownership,
rigid transforms, normals/tangents, all render passes, bounds and independent
instances against this CPU contract. Do not add another pose evaluator or parse
GLB at runtime. The older preview rendering is preserved, but is not declared
production-complete by this milestone. Blending/controllers, graph authoring,
modular sockets and VR/mocap remain outside this session.

Milestone 3 has now been completed; see [AnimatedRendering.md](AnimatedRendering.md).

Milestone 4 moves GPU/proxy ownership into RenderExtractSystem and keeps CPU poses
on each AnimatedModel component. Authored Play On Start is separate from the
runtime playing/time values; see AnimationIntegration.md.

Milestone 5 now uses these same sampling/build stages for local-pose crossfades
and configurable state machines; see AnimatorController.md.
