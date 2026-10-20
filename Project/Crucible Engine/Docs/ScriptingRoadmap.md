# Crucible C# scripting roadmap

This roadmap adapts the supplied tech-lead draft to the current engine. It is a
separate S1-S5 roadmap; the animation roadmap M1-M7 is already complete.

The assigned scope is the managed runtime and its execution infrastructure.
Subsystem owners retain responsibility for their future engine API bindings.
S5 is deferred until the team approves that interface. No scripting ECS component,
gameplay framework, physics/Animator/input bindings, or visual scripting editor
is part of S1-S4.

## What already exists

- Engine.dll is shared by the Editor and Game. Core owns their application
  lifecycle, layers, resources, scene manager, ECS systems and PlayMode.
- Both executables use EngineConfigLoader, including shared settings and per-app
  overrides. Existing logging can report native and managed-host failures.
- AssetManager already recognizes .cs and .lua as Script assets, but recognition
  is not compilation or execution. Assets/Scripts/scripttest.lua is empty.
- There was no Mono SDK, C# runtime host, managed project or script-instance
  manager. Commented ManagedScripts entries were placeholders, not working code.
- The existing GUID/resource pipeline, scene snapshots, PlayMode, renderer,
  animation controllers and editor are retained.

## Milestones

| Milestone | Crucible-specific scope | Completion evidence / next outcome |
|---|---|---|
| S1 - Native runtime host | Optional Windows x64 Mono SDK integration; Core-owned process-lifetime host; shared configuration/logging; portable Editor/Game deployment; managed execution diagnostic | Implemented. See ScriptingRuntime.md for setup and verification. |
| S2 - Assembly management | Build/consume a small managed C# DLL using the chosen framework profile; load assemblies through the host; discover classes, construct objects and invoke methods without gameplay APIs | Implemented. The standalone C# example executes through Core; see ScriptingAssemblies.md for usage and verification. |
| S3 - Managed instance lifecycle | Manage instances using S2's GC handles and exception handling; establish standalone creation/update/disposal conventions and failure policy; release references before runtime teardown | Pending. Deterministic instance lifecycle works without adding another ECS or ScriptComponent. |
| S4 - Development infrastructure | Complete C# build/deployment workflow, compiler diagnostics, dependency resolution and failure recovery; define a safe reload policy and document extension rules | Pending. Teammates can build and debug the scripting foundation reproducibly. Reload is explicit or restart-based until domain/thread/reference constraints are understood. |
| S5 - Team-owned engine bindings | Agree on the interop contract, then connect subsystem-owned entity/transform/physics/input/animation/audio/resource APIs and an approved gameplay/ECS adapter | Deferred. Requires the tech lead/team to approve scope and API ownership. |

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

## S3 handoff

S2 provides Core::GetScriptRuntime, assembly/object handles, checked class discovery,
construction, exact typed invocation and explicit GC-root release. It builds a
standalone example offline using Mono's 4.5 profile and C# 7.2 compiler. An optional
configured entry demonstrates one-shot user code during Core initialization;
it is not a frame lifecycle. See ScriptingAssemblies.md.

Build S3 instance management on these handles rather than exposing the private root
domain. Define creation/update/disposal conventions, when failed instances stop
receiving callbacks, and how Core shuts them down before Mono cleanup. Do not
create another ECS or ScriptComponent. The S2 one-shot entry remains optional;
neither it nor the host's BCL check establishes an automatic script-class lifecycle.

Future native binding registration belongs at the host's documented post-JIT,
pre-user-assembly initialization point. S1 registers no internal calls and
provides no subsystem APIs. Owners will add approved adapters there in S5.

Sources: [Mono Windows distribution](https://www.mono-project.com/download/stable/)
and [Mono embedding lifecycle/API](https://www.mono-project.com/docs/advanced/embedding/).
