# Animated models in the engine

## Open the preview

Build Editor (Debug/x64), then open `Assets/Scenes/Animation Preview.scene`
from the editor's asset browser. The Game view uses the scene's camera and
shows two independent instances of the same compiled `golem2_v3.cmodel`:
Heavy_Walk on the left and Fast_Boost on the right at a different speed.
This scene is separate from the team's existing startup scene.

You can also launch the editor from its output folder with:

```powershell
.\Editor.exe --scene "Scenes/Animation Preview.scene"
```

Select either model entity in the Hierarchy. The Inspector's AnimatedModel
section provides a clip selector, Play/Pause, Stop, a time slider, speed,
looping, Play On Start and Preview In Editor. Visibility and materials live on
MeshRenderer. Scrubbing pauses that instance without changing saved playback settings.
Rest pose shows the model's authored transforms. Negative speed plays backward;
a newly selected playing reverse clip starts at its last key. Zero speed holds
the current pose. Non-looping playback stops at the boundary it reaches.
Press F in the Scene view to frame the selected model's current bounds.

## Controllers (Milestone 5)

Open `Scenes/Animator Controller Preview.scene` to see the two golems crossfade
between configured states. In AnimatedModel, select a Controller and use its live
state display, playback buttons and Runtime Parameters. Controller None retains
the direct clip workflow above. Model/material/visibility controls stay on
MeshRenderer. See [AnimatorController.md](AnimatorController.md) for the demo,
asset schema, gameplay API and transition rules. **Edit Controller Graph** opens
the visual authoring panel; see [AnimatorGraphEditor.md](AnimatorGraphEditor.md).

## Modular weapons (Milestone 7)

For attached, independently animated weapons, open **Scenes/Modular Animation Preview.scene**.
See [ModularAnimation.md](ModularAnimation.md) for model-part selection, socket authoring,
weapon controls, gameplay APIs and verification steps. This completes Milestone 7.

## Use another GLB

1. Put the self-contained GLB inside `AssetsSource/Models`, then build Editor.
   CrucibleModelCompiler produces `Assets/Models/<name>.cmodel` automatically.
   Restart the editor after rebuilding changed sources. See ModelImport.md.
2. Hierarchy's + menu -> Create Animated Model. Alternatively add AnimatedModel
   to an existing entity through Add Component.
3. In MeshRenderer, choose the compiled .cmodel in **Model**. In AnimatedModel,
   choose a clip. Both components are created by Create Animated Model. Adding
   AnimatedModel through Add Component also adds a missing MeshRenderer.
4. Save the scene normally. The renderer saves its model/material references; the animation saves clip and
   playback settings. Poses, render handles, running/paused state and runtime
   time are rebuilt after load.

Preview In Editor advances clips while editing. The global Pause state freezes
automatic animation advancement. Global Stop restores the authored scene using
the engine's existing snapshot mechanism; the restored clips begin at time zero.
With Preview In Editor enabled they can then preview again. The component's Stop
button pauses that one instance at its clip's first frame.

## Implementation

- `Animation/ModelPose`: StepModelPlayback updates the per-instance clock;
  SampleModelLocalPose resets defaults and samples LINEAR/STEP TRS;
  BuildModelPose generates parent-first globals and mesh-local joint palettes.
  Rotation endpoints are normalized before shortest-path slerp. Times are
  relative to each clip's first key. See AnimationRuntime.md for API contracts.
- `ResourceManager::GetRenderedModel`: shared uploads of every mesh primitive
  and embedded material. It uses the immutable imported definition and does not
  share per-instance clocks or poses.
- `Animation/AnimatedModelSystem`: per-entity playback and CPU poses, including
  edit-mode preview. It owns no renderer or GPU handles.
- `Graphics/RenderExtractSystem`: draws every MeshRenderer, including complete
  models. It owns all model-part proxies and applies the animation pose or rest
  pose, material overrides, visibility and MaterialParams. Removing MeshRenderer,
  changing its model or replacing/destroying the entity releases its proxies.
  Removing only AnimatedModel retains the complete model in rest pose.
- Skin matrices are `inverse(meshNodeGlobal) * jointGlobal * inverseBind`.
  The draw's world matrix is `entityWorld * meshNodeGlobal`. Rigid nodes use
  that same node hierarchy without skinning.
- Joint palettes are stored once per rendered part in the existing
  frame-owned instance buffer before per-view draw instances. Every pass/view
  uses its own instance offset and the same palette range. Per-frame buffer
  fences and growth follow the existing instance-upload path.
- `ModelVertex.slang` supplies deformation to the main, solid/cutout shadow,
  selection-mask and depth/normal shaders. Normals use inverse-transpose
  transforms; tangents are re-orthogonalized. Static meshes have no palette.
- Per-influence bounds are built once per uploaded primitive and shared across
  instances. Each frame their transformed envelopes conservatively enclose
  weighted vertices, with outward float padding; culling, framing and selection
  follow the animated pose. Rigid animated nodes also use current bounds.
- Normal/tangent directions are computed without depending on absolute model
  scale. Collapsed/parallel tangents have a finite orthogonal fallback.
- The frame-owned instance/palette buffer checks the GPU storage-buffer range;
  all main/shadow/outline views retain independent draw offsets after growth.

## Current limits

- Controllers support configured states/transitions, crossfades, visual graph
  authoring and independently animated socket-mounted weapons. Retargeting,
  root-motion extraction, blend trees and animation layers are not implemented.
- Supported animation interpolation remains LINEAR and STEP. Import rejects
  CUBICSPLINE, morphs and other unsupported features explicitly.
- Weighted meshes are picked through their current conservative bounds;
  rigid meshes retain triangle picking. Selecting a model outlines all parts.
- Shared model/GPU definitions remain cached until shutdown, matching the
  existing resource policy. Rebuild and restart after changing a source GLB; hot reload
  and explicit GPU eviction are not implemented.
- Materials follow the existing renderer's sampler/PBR behavior; BLEND is
  rendered as cutout, and advanced glTF material extensions are not added.
- A singular or numerically unrepresentable mesh binding hides its skinned
  parts for that pose. Small invertible scales remain supported. A non-finite
  global pose is skipped with a warning; valid evaluation restores it.

The earlier dedicated test/inspection projects remain removed. The production
CrucibleModelCompiler is now a normal offline build dependency, not a preview test.
No separate test project needs to be built or run to preview animation.

The initial preview predates the revised roadmap. Milestone 1 added compiled
resource input, Milestone 2 reviewed its CPU pose/playback code, and Milestone 3
reviewed and extended rendering/bounds. See AnimatedRendering.md for GPU checks.
Milestone 4 completes the shared renderer/editor workflow; see AnimationIntegration.md.

## Assign materials and textures

In MeshRenderer, **All materials** can assign one .mat to the entire model.
Leave it empty for imported defaults. Expand **Material slots** to assign a .mat
to an individual imported material index; a slot override takes precedence over
the global assignment. Clearing a slot restores inheritance. Slot names may be
empty or duplicated, so indices identify them.

Each section uses the existing material editor: New, Make Editable Copy/Make
Unique, texture drag/drop, factors and Save Material. Imported defaults are
shared and read-only. Make Editable Copy creates a normal .mat; embedded images
retain a compiled-model reference so they survive restart. Save Material writes
its texture/factor edits; Save Scene writes the assignments.

A model can also be rendered without AnimatedModel, using its complete rest pose.
The legacy Mesh field still supports existing primitive/single-mesh entities.
Older animation-only scenes/prefabs migrate to MeshRenderer when loaded; saving
them writes the new layout.
