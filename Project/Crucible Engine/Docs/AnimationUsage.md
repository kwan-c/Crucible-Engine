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
looping, visibility and Preview In Editor. Scrubbing pauses that instance.
Rest pose shows the model's authored transforms. Negative speed plays backward.
Press F in the Scene view to frame the selected model's current bounds.

## Use another GLB

1. Put the self-contained GLB inside `AssetsSource/Models`, then build Editor.
   CrucibleModelCompiler produces `Assets/Models/<name>.cmodel` automatically.
   Restart the editor after rebuilding changed sources. See ModelImport.md.
2. Hierarchy's + menu -> Create Animated Model. Alternatively add AnimatedModel
   to an existing entity through Add Component.
3. Pick the compiled .cmodel in `modelAsset` and choose a clip. No MeshRenderer is required.
   If both components are present, AnimatedModel supplies the model's draws.
4. Save the scene normally. Model asset, clip index and playback settings are
   serialized; poses, render handles and runtime time are rebuilt after load.

Preview In Editor advances clips while editing. The global Pause state freezes
automatic animation advancement. Global Stop restores the authored scene using
the engine's existing snapshot mechanism; the restored clips begin at time zero.
With Preview In Editor enabled they can then preview again. The component's Stop
button pauses that one instance at its clip's first frame.

## Implementation

- `Animation/ModelPose`: defaults -> LINEAR/STEP TRS sampling (shortest-path
  quaternion slerp) -> parent-first global node matrices -> joint palettes.
  Times are relative to each clip's original first key.
- `ResourceManager::GetRenderedModel`: shared uploads of every mesh primitive
  and embedded material. It uses the immutable imported definition and does not
  share per-instance clocks or poses.
- `Animation/AnimatedModelSystem`: per-entity runtime state and persistent
  proxies for every mesh node in the selected scene. Runs after hierarchy
  propagation in PreRender, including edit mode. Removing a component, changing
  its asset, destroying an entity or replacing the scene releases its proxies.
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
- Per-influence bounds are built at upload/instance creation. Each frame the
  transformed envelopes conservatively enclose the weighted vertices; culling,
  framing and selection do not depend on the rest-pose box.

## Current limits

- No blending, gameplay state machine, retargeting, root-motion gameplay or
  independent modular weapon animation yet (Milestones 5-7).
- Supported animation interpolation remains LINEAR and STEP. Import rejects
  CUBICSPLINE, morphs and other unsupported features explicitly.
- Weighted meshes are picked through their current conservative bounds;
  rigid meshes retain triangle picking. Selecting a model outlines all parts.
- Shared model/GPU definitions remain cached until shutdown, matching the
  existing resource policy. Rebuild and restart after changing a source GLB; hot reload
  and explicit GPU eviction are not implemented.
- Materials follow the existing renderer's sampler/PBR behavior; BLEND is
  rendered as cutout, and advanced glTF material extensions are not added.
- A mesh node with a singular (zero-scale) transform hides its skinned parts
  for that pose rather than uploading invalid inverse matrices.

The earlier dedicated test/inspection projects remain removed. The production
CrucibleModelCompiler is now a normal offline build dependency, not a preview test.
No separate test project needs to be built or run to preview animation.

The preview's pose, rendering and ECS code predates the latest Milestone 1 session;
that session changes its resource input to compiled models and preserves controls.
