# Crucible C# scripting roadmap

This roadmap adapts the supplied tech-lead draft to the current engine. It is a
separate S1-S5 roadmap; the animation roadmap M1-M7 is already complete.

The assigned scope is the managed runtime and its execution infrastructure.
Subsystem owners retain responsibility for their future engine API bindings.
The user has now authorized starting S5 with the entity script workflow. That
portion adds a ScriptComponent, EntityScript base/identity, named Inspector
sections with public primitive fields, PlayMode adapter and logging. Public
field overrides use the existing scene/undo/snapshot serialization. An
animation-only follow-up now exposes typed Animator parameters and preview graphs;
see [AnimatorParameterPreview.md](AnimatorParameterPreview.md).
Other subsystem-specific APIs remain owned by the team;
no competing gameplay framework or visual scripting editor is introduced.

## What already exists

- Engine.dll is shared by the Editor and Game. Core owns their application
  lifecycle, layers, resources, scene manager, ECS systems and PlayMode.
- Both executables use EngineConfigLoader, including shared settings and per-app
  overrides. Existing logging can report native and managed-host failures.
- AssetManager recognizes .cs as Script assets; compilation uses
  Assets/Scripts. Add Component searches compiled C# entity classes and
  offers creation for new names. The unused legacy script-creation path is removed.
- There was no Mono SDK, C# runtime host, managed project or script-instance
  manager. Commented ManagedScripts entries were placeholders, not working code.
- The existing GUID/resource pipeline, scene snapshots, PlayMode, renderer,
  animation controllers and editor are retained.

## Milestones

| Milestone | Crucible-specific scope | Completion evidence / next outcome |
|---|---|---|
| S1 - Native runtime host | Optional Windows x64 Mono SDK integration; Core-owned process-lifetime host; shared configuration/logging; portable Editor/Game deployment; managed execution diagnostic | Implemented. See ScriptingRuntime.md for setup and verification. |
| S2 - Assembly management | Build/consume a small managed C# DLL using the chosen framework profile; load assemblies through the host; discover classes, construct objects and invoke methods without gameplay APIs | Implemented. The standalone C# example executes through Core; see ScriptingAssemblies.md for usage and verification. |
| S3 - Managed instance lifecycle | Manage instances using S2's GC handles and exception handling; establish standalone creation/update/disposal conventions and failure policy; release references before runtime teardown | Implemented. Core-lifetime instances, cached synchronous callbacks, enable/fault policy and cleanup work without another ECS or ScriptComponent. See ScriptingLifecycle.md. |
| S4 - Development infrastructure | Complete C# build/deployment workflow, compiler diagnostics, dependency resolution and failure recovery; define a safe reload policy and document extension rules | Implemented. Automatic project source discovery, managed-only targets, compiler logs, explicit DLL references, guarded deployment and close-build-restart workflow. See ScriptingDevelopment.md. |
| S5 - Entity workflow and team-owned bindings | Script component/Inspector authoring, managed entity identity/logging and the existing ECS/PlayMode adapter; subsystem owners extend the agreed API | Entity workflow and Animator parameter writes implemented; see ScriptingEntities.md and AnimatorParameterPreview.md. Transform/physics/input/audio/resource bindings remain owner-specific follow-up work. |

## Decisions that fit this engine

1. **One runtime owned by Core.** Editor and Game use the same host; neither
   executable duplicates Mono initialization. Root-runtime teardown is final for
   that process. Play/Stop, scene replacement and future assembly reload must not
   tear it down and try to initialize Mono again.
2. **Keep Mono behind a native interface.** Only Scripting/ScriptRuntime.cpp
   includes Mono headers. ResourceManager, ECS components, renderer headers and
   gameplay callers do not acquire Mono SDK dependencies or raw Mono objects.
3. **Reuse existing configuration and logging.** Scripting settings live in
   EngineConfig, not a competing settings service. Native host diagnostics use
   Crucible's logger; no extra logging dependency is necessary.
4. **Keep deployment separate from source assets.** Mono's native DLLs and managed
   framework are application dependencies next to Editor/Game, not indexed game
   assets. S2 builds an explicit managed target and deploys DLLs beside each app;
   no DLL asset loader or source/GUID mapping is invented before ownership is defined.
5. **Preserve current builds.** Mono is an opt-in CMake feature. The normal
   run.bat generation flow remains unchanged; its default build needs no Mono SDK.
   A developer's SDK location stays in the ignored local CMake cache.
6. **Separate instance lifetime from engine binding.** S2/S3 can execute standalone
   managed code without reading World or defining a second entity model. Future
   gameplay integration must reuse the current ECS, scene/PlayMode lifecycle and
   safe structural-change rules rather than inventing parallel versions.
7. **No speculative hot reload.** S4 must account for outstanding managed objects,
   threads, delegates, native callbacks and domain references. Restart is an
   acceptable initial policy. Filewatch, if added later, stays editor/tools-only
   under the restriction in the supplied draft.

Mono is the selected runtime, matching the supplied plan. The official Windows
distribution selected for S1 is Mono 6.12.0.206 x64/SGen. This is the legacy Mono
product and framework profile, not the modern .NET runtime. Changing to .NET
hosting later would be a separate agreed architecture change.

## S5 handoff

S2 provides Core::GetScriptRuntime, assembly/object handles, checked class discovery,
construction, exact typed invocation and explicit GC-root release. It builds a
standalone example offline using Mono's 4.5 profile and C# 7.2 compiler. An optional
configured entry demonstrates one-shot user code during Core initialization;
it is not a frame lifecycle. See ScriptingAssemblies.md.

S3 now owns instances through separate native handles. Classes supply synchronous
public void OnCreate(), OnUpdate(float), and OnDestroy(). Opt-in configured
instances follow Core's lifetime/frame loop, independent of scene and PlayMode
state. Failed updates stop only that instance and release its managed root after
one cleanup attempt. See ScriptingLifecycle.md for the contract and verification.

S4 now builds project scripts under Assets/Scripts automatically, supports
explicit additional managed libraries/references, and deploys them using the same
Editor/Game dependency graph. Tools/NewScript.ps1 creates a lifecycle class;
Tools/BuildScripts.ps1 builds/deploys without rebuilding the native applications.
Compiler errors preserve the previous successful library outputs and block
dependent deployment. See ScriptingDevelopment.md for workflows and verification.

Build-restart is the supported reload policy. Script DLLs load from in-memory
copies, so deployment can replace them while an application runs; the new code is
used after a restart. Any future domain reload must account for
S2 raw objects, S3 cached methods/classes and GC handles, plus managed threads,
delegates and native callbacks. Stopping S3 instances alone is insufficient.
Preserve the single root runtime. The S5 EntityScriptSystem now connects component
instances to the current World and PlayMode; standalone S3 instances keep their
original policy. See ScriptingEntities.md for lifecycle and extension rules.

Native binding registration belongs at the host's documented post-JIT,
pre-user-assembly initialization point. S5 registers Log and Animator parameter
internal calls there. Animator uses an SDK-free animation-system adapter, validates
entity generation/session/assignment and queues writes before pose evaluation.
Subsystem owners can extend that pattern with their own lifetime-checked adapters.
Transform, physics, input, audio and resource APIs remain unimplemented.

Sources: [Mono Windows distribution](https://www.mono-project.com/download/stable/)
and [Mono embedding lifecycle/API](https://www.mono-project.com/docs/advanced/embedding/).
