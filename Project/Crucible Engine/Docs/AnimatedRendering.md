# Milestone 3: animated rendering

Completed on 2026-10-20. This extends and verifies the existing rendering preview;
it does not replace the renderer or add the later controller/editor/lifecycle
milestones. All uncommitted Milestone 2 work was preserved.

## What works

The compiled ModelResource feeds shared GPU meshes/materials through
ResourceManager::GetRenderedModel. AnimatedModelSystem creates render proxies for
all primitives on each mesh node in the selected scene. It uses the same CPU pose
for rigid nodes and skins. Each instance keeps its own clock, node transforms and
palettes; GPU geometry, materials and influence envelopes are shared.

The draw transform is entityWorld * meshNodeGlobal. Weighted vertices additionally
use the binding's mesh-local palette from Milestone 2. Joint attributes are uint4
at location 5 and float4 weights at location 6. Static primitives keep skin count
zero and retain the ordinary transform path. Full-model drawing retains all mesh,
primitive and material relationships; static GetMesh callers retain their legacy
first-primitive view.

ModelVertex.slang supplies the same ModelVertexTransform function to:

- DefaultShader: the main material pass and its depth/normal output for toon lines.
- Shadow: opaque and cutout shadow casters.
- OutlineMask: opaque and cutout selection masks, for every part of an entity.
- DepthNormal: opaque and cutout depth/normal pre-pass used by ambient occlusion.

Each pass reads the same per-instance palette offset/count. A draw's Vulkan
instance ID already includes firstInstance; it must not add that offset again.
Depth/outline cutout variants include joint/weight attributes as well as UVs.
No pass substitutes rest-pose geometry for the animated geometry.

## Changes in this milestone

### Stable normals and tangents

Normal transforms use inverse-transpose direction via cofactors, without requiring
a matrix inverse in the shader. The shared helper first rescales the matrix by a
positive common factor so small/large model scales cannot under/overflow the
cofactors merely because of their absolute scale. Directions normalize safely;
zero/collapsed directions use finite fallbacks. Tangents are projected against
the transformed normal, and near-parallel inputs use an orthogonal fallback
instead of normalizing rounding noise. Reflections preserve tangent handedness.
The main pass and AO normal pre-pass use the same normal helper.

### Shared conservative animated bounds

Graphics/ModelBounds.h builds immutable rest bounds, unweighted bounds and one
influence envelope per used skin-local joint. RenderedModel owns these once per
mesh primitive. Instances no longer rescan every vertex or retain identical
influence-envelope vectors when created.

At each pose, the instance transforms envelopes by its own palette and unions
them. Positive normalized weights make every weighted vertex a convex combination
of points in these envelopes. TransformModelBounds evaluates each box in double
precision and pads outward using absolute matrix/input magnitudes before
cancellation, including translation. This covers the different float evaluation
orders of GPU matrix blending/composition and CPU envelope transforms. The same
helper transforms the box into world space; unrepresentable boxes are excluded
from the spatial index. This handles rigid animated nodes as well as skins.
Culling, shadow caster selection, framing and skinned bounds-based picking use
current boxes. Exact skinned-triangle picking remains deferred.

### GPU layout and buffer ownership

The existing frame-owned storage buffer carries palette matrices followed by draw
records for every view/pass. The layout remains 96 bytes per InstanceData, with
params at byte 64 and skin metadata at byte 80. Vertex layout remains 92 bytes,
with joint indices at byte 60 and weights at byte 76. Compile-time assertions catch
layout drift; the GPU checks below verify the actual shader interpretation.

Draw batches also separate ordinary and mirrored model transforms. Main and AO
pipelines set dynamic front-face winding per batch, including cutout and
double-sided draws, so reflected rigid nodes/entities keep the intended visible
faces and outward lighting normals. Orientation uses a double-precision
determinant to avoid underflow at small scales. Shadow and selection pipelines
retain their existing no-culling policy.

BeginViews assigns palette ranges before drawing. Main, cascade and selection
batches copy each proxy's own skin reference. All views are prepared before the
single upload, and only the current frame's buffer grows after its fence was
waited. The existing descriptor refresh updates that frame's window and viewport
sets after replacement. Added checks bound palette/draw allocation and buffer
growth by maxStorageBufferRange, preventing silent record overflow or an oversized
storage-buffer descriptor.

## Files changed for Milestone 3

- Assets/Shaders/ModelVertex.slang and DefaultShader.slang: safe direction/frame
  math; generated DefaultShader.spv and DepthNormal.spv and relevant .meta hashes.
- Engine/src/Graphics/ModelBounds.h (new): shared bounds build/evaluation/padding.
- Engine/src/Resources/RenderedModel.h and ResourceManager.cpp: shared envelopes
  beside uploaded mesh/material resources.
- Engine/src/Animation/AnimatedModelSystem.h/.cpp: consume shared envelopes;
  preserve Milestone 2 playback and failed-pose handling.
- Engine/src/Graphics/RenderScene.h/.cpp: animated bounds, GPU range/capacity guards.
- Engine/src/Graphics/RenderTypes.h and Vulkan/RenderLayouts.h: layout assertions.
- Engine/src/Graphics/RenderSystem.h: document rigid/skin bounds API behavior.
- Docs/AnimatedRendering.md, AnimationUsage.md, AnimationRoadmap.md and
  AnimationRuntime.md: implementation, verification and handoff.

No GLB, compiled model, scene, third-party library or permanent test target was
added or changed by this milestone. No commits or pushes were made.

## Verification performed

### Builds and shader compilation

The normal Debug/x64 Editor and Game targets build successfully using the existing
VS 2022/v143 setup and local CMake 3.30.3:

```powershell
& ".\build\setup-tools\cmake-3.30.3-windows-x86_64\bin\cmake.exe" --build build --config Debug --target Editor Game -- /m:1 /v:minimal /nologo
```

Normal shader compilation rebuilds the shared-include consumers through slangc.
Final binaries were rebuilt after removing every temporary capture hook. The normal
16,384-record initial instance capacity was restored. The existing ViewTargets.cpp
warning about a shadowed samples variable remains unrelated to this milestone.

### GPU versus CPU

A temporary headless Vulkan verifier compiled a compute entry point including the
production ModelVertex.slang, using the normal column-major SPIR-V flags. It read
the five actual compiled model resources, sampled every golem clip at start,
intermediate and final times, and submitted every vertex through the shared GPU
deformation functions. The comparison used double-precision CPU skinning and
inverse-transpose/tangent references. It also checked that GPU vertex positions
fell inside the production animated bounds.

On NVIDIA GeForce RTX 3060 Laptop GPU with Khronos validation enabled:

- 5 models, 19 clips and 69 poses exercised.
- 318,054 GPU vertex evaluations passed (318,048 actual-model vertices and six
  numerical cases for tiny/large/nonuniform/reflected/zero scales and a parallel
  tangent).
- 318,052 nondegenerate normal/tangent directions matched their references;
  degenerate cases remained finite and orthonormal.
- Every one of the 318,048 actual-model vertex results stayed inside its current
  conservative world bounds.
- Worst relative position error was 2.38419e-07; zero Vulkan validation errors.

The first run caught a near-parallel tangent normalizing rounding noise. The
production fallback was corrected, and the full GPU comparison then passed.

### Actual graphics passes

Temporary readback instrumentation ran the existing Animation Preview scene and
captured 52 GPU images at four frames. It enabled selection/AO, added two Game
views, resized the three Game views to the same dimensions, and temporarily
reduced initial buffer capacity to 32 records to force growth through the normal
frame/descriptor path. Source snapshots were taken first and restored exactly
after the run; none of this instrumentation ships in the engine.

The run rendered two differently animated compiled golems (11 parts each), their
floor, visible shadows, all-part orange selection outlines and AO depth/normals.
Images were inspected visually and compared numerically:

- Three equal-camera views produced identical color, selection, depth and normal
  images after resizing and buffer growth.
- Moving between the captured poses changed the mech silhouettes, including
  independently posed weighted limbs and rigid parts.
- At a fixed pose, ordinary opaque output and visible-cutout output were byte-for-
  byte equal in all three views for color, mask, depth and normals.
- Raising cutout rejection removed the models from color, mask and depth/normal
  images; their ground shadows also disappeared in the composed color image.
- The raw shadow readback covered the nearest cascade, which contained no mech
  geometry at this camera distance. It matched opaque/cutout-visible runs but did
  not test rejection of the distant mech casters; those shadows were checked in
  the composed color output. No claim is made of raw per-cascade pixel coverage.
- The capture process exited 0 with no runtime/Vulkan warning/error output.

Local logs/artifacts (ignored by Git) are in build/setup-tools:

- milestone3-build.log
- milestone3-gpu-build.log and milestone3-gpu-verification.log
- milestone3-capture-verification.log
- milestone3-captures/24-3-color.png: fixed-pose models, outlines and shadows
- milestone3-captures/30-3-color.png: rejected cutout, floor only
- milestone3-captures/42-3-color.png: visible cutout, same fixed pose

### Correctness review follow-up

The subsequent review found two gaps in the original completion assessment:
fixed rasterizer winding for mirrored model transforms, and bounds padding based
only on the final box after cancellation. Both paths were corrected as described
above. The review's failing weighted triangle (source X around 256, cancelling
joint translation, rotated entity) now stays inside its bounds.

The extended GPU probe passes 318,126 vertices and 318,120 bounds checks across
the five assets and additional rigid/skinned cancellation cases, with zero
validation errors.
An actual Editor run captured 24 images of normal, mirrored, mixed-winding,
cutout and double-sided instances. Mirrored selection masks exactly match the
horizontal flip of the originals. Mirrored AO depth differs above 1e-6 at only
one shared-edge pixel (maximum 5.89e-5); corresponding normals otherwise match
within one RGBA8 quantization step. Visible cutout output is byte-identical to
opaque color/mask/depth/normals. Disabling back-face culling retains identical
silhouette/depth, with one shared-edge normal pixel changing. Rejected cutout
removes the models. Production batching assertions validated every main batch's
orientation, including the mixed-winding case. The Editor exited 0 with no
runtime/Vulkan errors. Temporary capture hooks were restored from exact byte
backups; no saved scene or model was changed.

Follow-up artifacts: milestone3-fixes-gpu.log,
milestone3-fix-capture-verification.log, milestone3-fix-captures/ and
milestone3-fixes-build.log in build/setup-tools. No permanent test target was added.

## Limits and Milestone 4 handoff

Material behavior retains the current engine policy: alpha BLEND is cutout rather
than composited transparency; advanced material extensions/custom sampler policy
and normal-scale/occlusion-strength use are not implemented. Vertex RGB is
rendered; imported vertex-color alpha remains CPU data and is not used by the
current cutout calculation. These are renderer limits, not clip/palette rules.

Skinned picking uses conservative bounds rather than deformed triangles. Bounds
may be loose for large joint rotations. Truly singular mesh bindings remain hidden
for that pose. GPU definitions stay cached until shutdown, as before; hot reload
and explicit GPU eviction are still outside this milestone.

Manual check: build Editor, open Animation Preview.scene, select either golem,
play/scrub several clips, and verify its outline, limb pose, rigid attachments and
ground shadow move together. Move the Scene camera to check view culling, and frame
the selection with F. The test captures intentionally used opaque/cutout overrides
only in memory; the actual saved scene/materials were not edited.

Milestone 4 is next: audit ECS/editor integration, Play/Pause/Stop, serialization,
scene reload, destruction/reassignment and cleanup against the now-verified CPU/GPU
contracts. Preserve the shared definitions/bounds and per-instance state. Do not
add another importer or deformation path. Controllers, graph authoring, modular
sockets and VR/mocap remain outside this session.

Milestone 4 now routes these same GPU contracts through MeshRenderer and
RenderExtractSystem. AnimatedModelSystem only evaluates CPU poses. Material
slots, lifecycle and editor integration are described in AnimationIntegration.md.
