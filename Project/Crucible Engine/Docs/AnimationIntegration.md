# Milestone 4: ECS, editor and lifecycle integration

Milestone 4 completes the normal renderer/editor workflow for complete models.
Controllers, blending, graph authoring and sockets remain Milestones 5-7.

## Ownership and flow

```
MeshRenderer.modelAsset -> ResourceManager shared compiled model
AnimatedModel authored playback -> AnimatedModelSystem -> per-entity CPU pose
MeshRenderer + optional CPU pose -> RenderExtractSystem -> existing Renderer proxies
MeshRenderer material assignments -> existing material editor / .mat assets
```

MeshRenderer owns the complete-model reference, visibility, optional global
material assignment, and materialOverrides indexed by imported material index.
AnimatedModel owns clip, speed, loop, Preview In Editor and Play On Start. Its
clock, running state, pose and initialization fields are runtime-only. CPU pose
vectors are values, so copied/duplicated components have independent mutable data.
No bone entities or renderer/GPU handles are added to the animation component.

AnimatedModelSystem no longer references Renderer, RenderedModel or render
proxies. It reads MeshRenderer's model reference and samples the shared CPU
definition. RenderExtractSystem owns every draw, including static models and
animated models, and runs after both pose evaluation and hierarchy propagation.
It combines entityWorld with mesh-node globals and retains the Milestone 3
palette/bounds contracts for all existing render passes.

A complete model without AnimatedModel displays its authored rest pose. Removing
only animation leaves that renderer alive. The legacy MeshRenderer meshAsset/mesh
path still draws ordinary meshes, procedural geometry and existing scenes.
Choosing a complete Model takes precedence over that legacy mesh reference.

## Material workflow

Material precedence is slot override, global material, imported material, renderer
default. Zero slot GUIDs inherit. Missing/unusable override assets fall back through
that chain and ResourceManager reports/caches failures through its existing policy.
MaterialParams applies to every part of the entity. Visibility hides every part;
selection and framing continue to use their shared entity key and current bounds.

The MeshRenderer Inspector contains a Model picker, global All Materials section,
and indexed Material Slots. Every section reuses DrawMaterialSection: selecting
.mat files, New, Make Editable Copy/Make Unique, texture drag/drop, shading,
factors and Save Material. Slot indices disambiguate duplicate/empty names.
Changing the Model in the Inspector clears its slot overrides. The global
assignment remains until cleared. Programmatic callers can deliberately retain
index-based overrides when reassigning a compatible model.

Imported defaults remain shared and read-only. Editable copies are normal .mat
assets. Standalone texture references retain their existing hexadecimal GUID
strings. An embedded image is saved as a compiled-model reference:

```json
"albedo": { "model": "<compiled-model GUID in hex>", "texture": 0 }
```

ResourceManager resolves this through GetModelTexture, preserving the compiled
model's texture index and the slot's sRGB/linear policy, using the existing renderer
texture cache. No GLB is read at runtime and no new importer is introduced.
This also preserves embedded textures after a copied material is loaded in a new
process. Untracked procedural textures still cannot be serialized as assets.

Save Material writes texture/factor edits. Save Scene writes the renderer's
assignments. Shared .mat edits affect all users, as in the existing editor.

## Playback and scene lifecycle

Play On Start is authored; runtime playing/time are not serialized. Clip completion
or pressing the component Pause/Stop buttons cannot silently change the next
session's authored startup behavior. Scrubbing pauses that instance temporarily.

- Edit preview advances only with Preview In Editor and runtime playing enabled.
- Global Play starts a fresh clock from authored settings (reverse playback starts
  at the clip's end). Global Pause/Resume holds/resumes the same clock and pose.
- Global Stop resets clocks. A restoring Stop uses the existing scene snapshot;
  a non-restoring Stop also resets retained animation components. Enabled edit
  preview may resume on subsequent frames, as before.
- PlayMode exposes SessionRevision, incremented on successful Play and Stop.
  Animation sees restarts even when Play/Pause or Stop/Play/Pause are queued in
  one frame and the final play state alone would hide those transitions.
- Clip/model reassignment reinitializes the matching instance state. Scene reload
  recreates authored components and independent runtime poses, including reload
  while Playing. Entity hierarchy transforms are applied by normal extraction.
- RenderExtractSystem sweeps removed renderers/entities, releases old model-part
  proxies on reassignment, and releases all remaining proxies at Shutdown.
  Generation checks prevent entity-slot reuse from inheriting old draw identity.
- Invalid assets/poses hide their parts; valid replacement/evaluation restores them.
  An unrepresentable rest pose does not prevent a valid animated pose from drawing.

Definitions and GPU resources remain shared/cached until engine shutdown, matching
the ResourceManager's existing policy. Hot reload and GPU eviction are not added.

## Compatibility and files

EntityIO migrates the early preview schema for scenes, prefabs and snapshots:
AnimatedModel.modelAsset/visible move to MeshRenderer when its new fields are not
already supplied; the old playing setting becomes playOnStart. Saving writes only
the new ownership layout. Current Animation Preview.scene uses the new format.

The primary implementation changes are Components.h and their reflection files,
AnimatedModelSystem, RenderExtractSystem, PlayMode session revision, EntityIO,
ResourceManager embedded texture persistence, Hierarchy creation and Inspector/
SceneView controls. Existing shaders/deformation are reused.

## Verification

Normal Debug Editor and Game builds pass with VS 2022/v143. An ignored native
probe links the real Engine DLL and drives its ordinary frame loop, ECS systems,
renderer, PlayMode and SceneManager. No permanent test target was added.

The expanded run passes 103 checks across 80 frames, including:

- independent poses/component copies; edit preview gating, scrubbing, reverse,
  zero speed, one-shot completion and separation of authored/runtime settings;
- global Play/Pause/Resume/Stop with and without scene restoration, batched
  requests, hierarchy motion, and scene reload during Play;
- global/slot material batching, renderer visibility, complete rest rendering
  after removing animation, renderer removal/re-addition, model failure/recovery,
  model reassignment, entity destruction/reuse, scene replacement and shutdown;
- scene/component round trips of model/material GUIDs and slots, exclusion of
  runtime pose/clock fields, and old preview schema migration.

A fresh process passes another five checks, reloading an editable .mat with an
embedded model image and uploading that image from the persisted reference.
Eight temporary GPU readbacks verify the material path visually: both golems
receive the global orange material, and a slot override changes 1,162 pixels on
one instance with zero changed pixels in the other half of the image. Hiding its
MeshRenderer changes 19,340 pixels on that half and removes all of its parts.
The copied embedded-texture material is visible after scene reload. Captures have
no Vulkan validation messages. The missing-model warning in the lifecycle run is
intentional. All processes exit 0, and the test layer checks proxy cleanup after
system shutdown. Temporary capture hooks are restored from exact byte backups.
The temporary .mat and its .meta are removed after the final reload verification.

Ignored artifacts live in build/setup-tools: milestone4-build.log,
milestone4-lifecycle-stdout.log, milestone4-reload-stdout.log,
milestone4-capture-verification.log, milestone4-captures/ and the native probe.

## Manual check

1. Open Animation Preview.scene, select a golem and expand MeshRenderer.
2. Under All Materials select an existing .mat; every part should update.
3. Expand one Material Slot and assign another .mat; only that slot on that entity
   should change. Clear the slot/global assignments to restore inheritance.
4. Make an editable copy, drag a texture onto a material slot, Save Material,
   then Save Scene and reload. Its assignment and texture should remain.
5. Exercise animation controls, global Play/Pause/Stop, renderer visibility,
   component removal, model replacement and scene reload. Removing only
   AnimatedModel leaves the full model in rest pose.

Milestone 5 now adds a configurable controller runtime that writes this same
per-entity pose contract; see AnimatorController.md. A controller graph UI and
weapon/socket playback remain later milestones and are not required to use the
current material workflow.
