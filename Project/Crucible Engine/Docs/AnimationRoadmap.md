# Crucible animation roadmap

The attached seven-milestone plan supersedes the previous five-milestone plan.
Each source model is self-contained. Source assets compile offline into the engine's
own representation; runtime does not parse GLB. No source names, bone names, clip
counts, joint orders or weapon combinations determine generic import behavior.
VR tracking and motion capture are outside this roadmap.

| Milestone | Scope | Current status |
|---|---|---|
| 1 - Model and animation asset compilation | Complete source interpretation, validated .cmodel output, GUID integration and compiled-only runtime loading | Completed in the preceding session. See ModelImport.md. |
| 2 - Animation runtime and pose evaluation | Sampling, playback, looping/speed, hierarchy, rigid nodes and joint palettes | Completed: existing evaluator reviewed and extended with reusable sampling/build stages, robust playback and numerical handling. See AnimationRuntime.md. |
| 3 - Animated rendering | Vulkan skinning/rigid nodes, independent instances, materials, shadows/outlines/depth-normal and bounds | Completed: GPU deformation and all required passes verified; stable normal/tangent handling, shared conservative bounds and buffer-limit checks added. See AnimatedRendering.md. |
| 4 - ECS, editor and lifecycle integration | Components, scheduling, serialization, Play/Pause/Stop and cleanup | Completed: full models and material slots belong to MeshRenderer/RenderExtractSystem; pose-only animation, editor reuse, legacy migration and lifecycle checks. See AnimationIntegration.md. |
| 5 - Animator Controller runtime | Configurable states/transitions/parameters/conditions/exit times/crossfades independent of editor | Completed: versioned controller assets, typed per-instance parameters, ordered transitions/exit gates, local-pose crossfades, gameplay API and Inspector preview. See AnimatorController.md. |
| 6 - Visual Animator graph editor | Graph authoring, saving/loading and live controller inspection | Completed: dockable state graph, typed parameter/transition authoring, layout persistence, undo/redo, safe save/reload and live inspection. See AnimatorGraphEditor.md. |
| 7 - Modular animations and weapon integration | Independent instances, animated sockets and modular weapon playback | Completed: model-part selection, hierarchy-integrated sockets, independent weapon controllers, editor/lifecycle support and a real-asset preview. See ModularAnimation.md. |

## Contracts for later milestones

- ResourceManager owns GUID lookup/caching. ModelResource remains shared immutable
  definition data; per-instance clocks, TRS/global poses, palettes and controller
  parameters do not belong in the compiled resource or shared definition.
- Keep nodes/bones in model arrays; do not require an ECS entity for every bone.
- nodeOrder is parent-first regardless of source index order. Preserve exact static
  matrix nodes, initialize TRS from defaults, replace only animated properties.
- A mesh's skin binding belongs to its node. JOINTS_0 indexes that skin's jointNodes,
  not model nodes. Meshes may have different bindings on different instances.
- Evaluate rigid mesh nodes and skeletal joints using the same hierarchy. A model
  may contain both, multiple skins, or no skin. Never fuse separate source exports.
- Clip names are optional/non-unique. Indices identify clips; controller/gameplay
  names and selection policy belong to configuration in later milestones.
- Existing rendering uses entityWorld * meshNodeGlobal and binding palettes
  inverse(meshNodeGlobal) * jointGlobal * inverseBindMatrix. Keep one consistent
  coordinate contract across pose evaluation, rendering and later sockets.
- Preserve conservative animated bounds and frame-owned, per-instance palette
  ranges shared across main/shadow/outline/depth-normal passes. Keep static APIs.
- Integrate lifecycle and scheduling through current engine systems: freeze clocks
  deliberately, release proxies on destruction/reassignment/scene replacement,
  and distinguish authored settings from runtime state.
- Controllers must work without the graph editor. Weapons eventually use independent
  instances and animated sockets, not combined clips for every action combination.

See AnimationUsage.md and ModularAnimation.md for preview controls and limitations.
All seven milestones are complete. VR tracking and motion capture remain outside this roadmap.
