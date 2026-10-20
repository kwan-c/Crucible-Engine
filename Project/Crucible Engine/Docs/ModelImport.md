# Milestone 1: model and animation asset compilation

## What changed in this session

The complete hand-written GLB importer and the preview's pose/render/ECS code were
already in the working tree. This session adds the missing offline compilation
boundary. It does not add playback, skinning, controllers or graph-editor features.

```text
AssetsSource/<path>.glb + .meta (authoring source, stable GUID)
    -> CrucibleModelCompiler (existing GLTFDocument / GLTFLoader)
    -> ModelResource -> EncodeCompiledModel
Assets/<path>.cmodel + .meta (same GUID, runtime data)
    -> AssetManager -> ResourceManager::GetModel -> DecodeCompiledModel
    -> shared const ModelResource
```

The five existing GLBs and their original metadata moved from `Assets/Models` to
`AssetsSource/Models`. The generated `.cmodel` metadata retains their GUIDs. Scene
and embedded-material references therefore stay valid without editing scenes.
Runtime Assets contains no GLBs. Engine.dll does not compile GLTFLoader.cpp or
GLTFDocument.cpp, and ResourceManager rejects source extensions before reading
model bytes. A packaged runtime needs Assets and its normal executable/DLLs;
it needs neither AssetsSource nor CrucibleModelCompiler.

## Files added or changed for this session

| Area | Files |
|---|---|
| Compiled resource IO (new) | Engine/src/Resources/CompiledModel.h and .cpp |
| Engine data enums | Engine/src/Resources/ModelResource.h |
| Offline compiler/build dependency (new) | Tools/ModelCompiler/main.cpp and CMakeLists.txt |
| Build wiring/parser exclusion | CMakeLists.txt; Engine/CMakeLists.txt |
| Offline enum translation | Engine/src/AssetPipe/GLTFLoader.cpp |
| Asset classification | Engine/src/AssetPipe/Assetmanager.cpp |
| Compiled runtime loading/static aliases | Engine/src/Resources/ResourceManager.cpp and .h |
| Picker explanatory text | Editor/src/ImGui/Panels/ImGuiPanel_Inspector.cpp |
| Source/runtime asset separation | Five GLB/.meta pairs moved to AssetsSource/Models; five .cmodel/.meta pairs generated in Assets/Models |
| Documentation | Docs/ModelImport.md, AnimationRoadmap.md, AnimationUsage.md |

Other animation, ECS, shader, renderer and preview-scene changes already existed
before this session and were preserved. No third-party files, new dependencies,
test projects, commits or pushes were introduced.

## Workflow

1. Export a supported, self-contained animated GLB into `AssetsSource/Models` (or
   another subdirectory under AssetsSource). Keep its `.meta` when renaming it.
2. Build Editor or Game using the normal generated Visual Studio solution.
   The `CompiledModels` build dependency runs `CrucibleModelCompiler` first.
   New sources receive normal AssetRegistry GUID metadata.
3. Select the generated `.cmodel` in the existing model/mesh picker. Static
   `MeshRenderer` keeps the first-mesh/first-primitive behavior. Existing
   `AnimatedModel` preview code can consume the complete compiled definition.
4. After editing a source, rebuild and restart the editor. Runtime hot reload
   and GPU eviction are not added here. Missing source files do not delete compiled
   outputs automatically; remove unwanted outputs and metadata deliberately.

The tool accepts explicit roots; there are no model filenames or local machine
paths in its import logic. A source rename reuses the runtime path already indexed
under that GUID. Two sources cannot overwrite one output or reuse one GUID.
Source/runtime roots must be separate, non-nested directories.

Commands from the engine root (PowerShell, Debug/x64):

```powershell
# Generate with the project's supported CMake/VS 2022 setup (run.bat).
# In this machine's existing build, the local CMake 3.30.3 is available here:
& ".\build\setup-tools\cmake-3.30.3-windows-x86_64\bin\cmake.exe" -S . -B build
& ".\build\setup-tools\cmake-3.30.3-windows-x86_64\bin\cmake.exe" --build build --config Debug --target Editor Game -- /m:1 /v:minimal /nologo

# Normal builds run compilation automatically. These are optional tool commands:
& ".\build\Tools\ModelCompiler\Debug\CrucibleModelCompiler.exe" --compile-tree AssetsSource Assets
& ".\build\Tools\ModelCompiler\Debug\CrucibleModelCompiler.exe" --verify-tree AssetsSource Assets
& ".\build\Tools\ModelCompiler\Debug\CrucibleModelCompiler.exe" --inspect Assets/Models/golem2_v3.cmodel
```

`--verify-tree` reparses actual source exports, validates compilation, decodes and
re-encodes every field, then requires byte-for-byte agreement with runtime files.
It fails on missing/stale/different outputs. Normal builds skip source parsing
when the byte-exact source hash, version, GUID and validated output agree.
Bump CompiledModelVersion when changing the schema or importer semantics so cached
assets rebuild. A failed compile fails the build and does not replace that model's
last good output. Binary and metadata replacements are individually atomic; an
interruption between them can require restoring the source GUID metadata and
recompiling. Compilation is per asset, not an all-assets transactional publish.

Commit source GLBs with their source `.meta`, the compiled `.cmodel` and runtime
`.meta` files, and the tool/code changes together. Do not commit build/ or generated
Visual Studio files. Compiled outputs can be regenerated; their GUIDs come from
source metadata, not from a new random identity each build.

## Engine data and resource integration

`Resources/ModelResource.h` owns all data without JSON, Vulkan handles, ECS entities
or mutable playback state:

- Every scene/root, node, reciprocal parent/children, default TRS or exact matrix,
  mesh reference, optional skin reference and parent-first node traversal.
- Every mesh/primitive, triangle index, vertex attribute, typed joints/weights and
  primitive-to-material relationship. Attributes include color alpha and tangents.
- Core PBR materials, texture/image relationships, engine filter/wrap enums and
  embedded encoded PNG/JPEG images, including unused definitions.
- Every skin, skeleton root, joint-to-node mapping and column-major inverse bind.
- Every independent clip, channel target node/property, sampler reference,
  LINEAR/STEP enum, original key times/values and clip bounds. Names may repeat;
  indices are authoritative. Missing inverse binds become identities.

`ResourceManager::GetModel(AssetId)` loads/caches the immutable CPU definition
without uploading GPU resources. `GetRenderedModel` shares that definition and
uploads geometry/materials once. Legacy `GetMesh` and derived embedded-material
GUIDs alias its first primitive/material instead of duplicating uploads. The
static compatibility view still does not apply model node transforms or skinning.
No data is borrowed from another source model to fill missing content.

## Compiled format v1

The `.cmodel` format uses explicit little-endian integers and IEEE-754 float32
components. It never serializes C++ structs, pointers, STL layout or GLM padding.
Matrices encode four columns; quaternion components encode XYZW.

| Offset | Header field |
|---|---|
| 0 | 8 bytes: `CRMODEL` followed by zero |
| 8 | uint32 version (1) |
| 12 | uint32 flags (0) |
| 16 | uint64 payload byte count |
| 24 | uint64 byte-exact FNV-style payload checksum |
| 32 | uint64 model GUID, checked against runtime metadata |
| 40 | uint64 byte-exact source fingerprint |
| 48 | payload |

The payload follows the shared `Fields` schema in CompiledModel.cpp: nodes,
meshes, materials, images, textures, samplers, skins, animations, scenes,
defaultScene, sceneRoots, nodeOrder, warnings. Collections/strings have uint32
lengths; booleans have one byte; enums have uint32 codes. All model fields are
written individually in declaration/schema order. There is no GLB container,
JSON, external file pointer, decompressor or runtime source parser in this format.
Encoded image payloads remain PNG/JPEG, matching the existing image decoder.

Loading rejects bad magic/version/flags/GUID, truncation, extra bytes, checksum
mismatches, non-finite values, invalid enums/booleans, hierarchy/reference errors,
invalid triangle/skin/clip data and excessive allocation counts. File size and
cumulative decoded collection/string allocation each have a 1 GiB limit. Failed
loads expose no partial resource. The checksum detects damage; it is not an
authentication mechanism. Compiled model data is currently uncompressed.

## Source format capabilities and limits

Supported: GLB 2.0 with embedded buffer/images; dense SCALAR/VEC2/VEC3/VEC4 and
FLOAT MAT4 accessors; offsets, alignment, interleaving/stride, integer precision
and normalized conversion; triangle lists (including generated non-indexed
indices); POSITION/NORMAL/TANGENT/TEXCOORD_0/COLOR_0/JOINTS_0/WEIGHTS_0; multiple
scenes/meshes/primitives/materials/skins/clips; rigid and skinned node bindings;
LINEAR/STEP TRS channels. Missing attribute flags and original relationships are
preserved. Positive quantized weights are validated and normalized. The GLB
container, accessor, hierarchy, binding and channel checks remain in the existing
hand-written RapidJSON importer; no new importer library was added.

Unsupported sources fail clearly: .gltf/.fbx/.obj/.dae (only GLB is implemented),
external/data-URI buffers/images, required extensions, sparse/bufferView-less
accessors, MAT2/MAT3/integer matrices, morphs, extra UV/influence sets,
non-triangle modes, cameras, non-PNG/JPEG images and CUBICSPLINE. Optional declared
extensions keep their core fallback with an explicit warning. Encoded images are
decoded/validated when uploaded. This is a documented subset, not a full glTF
schema validator.

Material data retention exceeds the current renderer's behavior: custom sampler
settings, normal scale and occlusion strength are retained but rendering keeps
its existing policy. BLEND still renders as cutout; advanced material extensions
and alpha compositing are outside this milestone.

## Verification performed on 2026-10-20

- CMake generation with the existing local 3.30.3 build completed successfully.
  Debug/x64 `Editor Game` built successfully with VS 2022/v143 using the command
  above. An initial compiler build needed the engine's existing
  VULKAN_HPP_NO_STRUCT_CONSTRUCTORS setting; the final build passes.
- `--compile-tree AssetsSource Assets` compiled all five actual project GLBs.
  `--verify-tree AssetsSource Assets` returned verified=5, failed=0, including
  decoded/re-encoded byte equality and agreement with the files on disk.
- Static BoxTextured, Cannon, CrossbowArm and CrossbowBolt each retained their
  mesh, primitive, embedded material and image data. The golem retained 38 nodes,
  11 meshes/primitives, 11 materials, 7 skins and all 19 clips.
- All five source/runtime metadata pairs retain identical GUIDs. An incremental
  compile returned unchanged=5 and changed no binary or metadata timestamps.
- Nine fault checks made temporary damaged copies of the actual BoxTextured
  compiled resource: bad magic/version/flags/checksum, truncation, zero GUID,
  trailing payload, oversized collection count and invalid node parent.
  Every case was rejected. The temporary damaged files were removed; no test
  project or fabricated source model was added.
- A temporary standalone runtime package contained Editor.exe, its DLLs and
  Assets, with no GLB, AssetsSource or CrucibleModelCompiler. A temporary scene
  referenced the actual five compiled models through static MeshRenderer and
  the two existing golem AnimatedModel instances. It loaded all five legacy
  static mesh/material aliases; both animated instances reported 11 render parts
  and 19 clips. The window remained responsive and accepted normal shutdown.
  Logs contained no errors or warnings. This checks runtime loading/upload and
  preserves the prior animation path; visual fidelity and all playback controls
  still need the user's manual review.
- The generated Engine.vcxproj has no GLTFLoader.cpp or GLTFDocument.cpp compile
  entries. `git diff --check` reports no whitespace errors.

Build/verification logs are local under build/setup-tools (ignored by Git).

For manual golem validation, build Editor, open Animation Preview.scene, select
both models, and confirm the clip picker contains the expected 19 clips. Try
rest pose and individual clips; check rigid attachments and weighted limbs. This
uses the already-existing preview, not new Milestone 2/3 work. For a fresh export,
place it in AssetsSource, build, inspect its .cmodel with the compiler command,
restart and select it in modelAsset. Unsupported exports fail the build with an
asset-specific diagnostic rather than falling back to GLB at runtime.

## Next session: Milestone 2

Start from GetModel and ModelResource, never from GLTFLoader or file names. Keep
clocks/poses/palettes per instance, and keep shared definitions immutable. Preserve
matrix nodes, use parent-first nodeOrder, treat joint indices as skin-local, and
evaluate rigid nodes as well as joints. The existing working tree already has
ModelPose/AnimatedModelSystem and preview rendering from the previous request;
review that implementation against the new roadmap instead of adding a parallel
runtime. No Animator Controller or graph editor has been implemented.
