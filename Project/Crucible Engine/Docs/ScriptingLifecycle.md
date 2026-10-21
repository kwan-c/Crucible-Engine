# S3 - Managed instance lifecycle

S3 adds owned, standalone managed instances to the existing Mono host. It uses
S2's assembly loading and GC handles, with cached callbacks and a consistent
failure/cleanup policy. No ScriptComponent, entity API or gameplay binding is added.

## Try it in Editor or Game

Use the Mono-enabled setup in [ScriptingRuntime.md](ScriptingRuntime.md), then build
Editor/Game normally. `Crucible.Examples.dll` now contains both the S2 `Counter`
and S3 `LifecycleCounter` examples. Both are ordinary C# classes with no engine
API dependencies. No extra SDK, managed framework or third-party library is added.

To enable the lifecycle example in Editor, merge this into the existing `editor`
object of `Assets/Config/EngineConfig.json`, keeping its other settings:

```json
"editor": {
  "scripting": {
    "instances": [
      {
        "assembly": "Managed/Crucible.Examples.dll",
        "namespace": "Crucible.Examples",
        "class": "LifecycleCounter",
        "enabled": true
      }
    ]
  }
}
```

Launch Editor, leave it running briefly, then close it normally. The example
writes `LifecycleCounter: created.` to process stdout and, on cleanup,
`LifecycleCounter: destroyed after ... updates (...s).` The native logger also
records the instance class/handle and its final successful update count.
Managed Console output is not yet routed through the Editor Console panel.

The default instance list is empty. This implementation leaves the shared config
asset unchanged. Use top-level `scripting/instances` for both applications, or
`game/scripting/instances` for Game. An app-specific array replaces the entire
shared list; `[]` clears it. Each entry needs a nonempty assembly and class;
namespace is optional, and enabled defaults to true. Global `scripting.enabled`
false skips both the runtime and configured instances.

The existing S2 startup method remains a separate optional one-shot invocation.
It runs before engine initialization; S3 configured instances are created after
Core's systems have initialized. A configured-creation failure fails Core::Init
and rolls back this configuration batch before normal Core cleanup.

## Callback contract

A lifecycle class must be a public, top-level, nongeneric reference class with a
public parameterless constructor and these public instance methods:

```csharp
public void OnCreate() { /* initialize this instance */ }
public void OnUpdate(float deltaTime) { /* standalone managed work */ }
public void OnDestroy() { /* release this instance's resources */ }
```

All three callbacks are required. Inherited implementations work. Parameter and
return types must match exactly; `double` is not accepted in place of `float`.
Validation happens before running the constructor. Method metadata is cached
until destruction/runtime teardown. Callbacks are synchronous: Task-returning
methods and `async void` callbacks are rejected. S3 does not manage background
tasks, managed thread lifetimes or asynchronous cleanup.

`OnCreate` runs once after successful construction, including for an initially
disabled instance. Disabled instances retain their object and skip OnUpdate.
Re-enabling resumes updates without repeating OnCreate. Explicit destruction,
bulk destruction and shutdown attempt OnDestroy once and release the GC root.
Bulk cleanup uses reverse creation order. Releasing a GC root does not guarantee
immediate collection or replace any IDisposable work the callback should perform.

## Core lifetime and frame policy

These are **standalone Core-lifetime instances**. Core calls UpdateInstances once
per frame, using its existing clamped delta time, independently of Edit/Play/Pause/
Stop and scene replacement. This policy makes the foundation usable without an
ECS/gameplay adapter. Scene changes and Stop do not recreate these instances or
reset their managed fields. Use SetInstanceEnabled to suspend an individual one.

This does not define gameplay script behavior. An approved future S5 adapter
will own entity/component instances and connect their creation, updates and
destruction to existing ECS and PlayMode rules. It must reuse this host rather
than introducing another ECS or restarting Mono on Play/Stop.

Core destroys managed instances before destroying world entities/systems/layers.
The runtime stays alive until the existing final host-shutdown point. Shutdown
also disposes any remaining instances, then frees S2 object roots and cleans up
Mono once. No domain unload/reload is introduced.

## Native API

Public headers remain free of Mono SDK types:

```cpp
auto& scripts = core.GetScriptRuntime();
ScriptAssemblyHandle library;
ScriptInstanceHandle instance;
if (!scripts.LoadAssembly("Managed/Crucible.Examples.dll", library)) return;
if (!scripts.CreateInstance(library, "Crucible.Examples", "LifecycleCounter", instance)) return;

// Core ticks its owned instances. A standalone host outside Core calls
// UpdateInstances(deltaTime) itself; do not tick Core's host a second time.
scripts.SetInstanceEnabled(instance, false);
ScriptInstanceInfo info;
if (scripts.GetInstanceInfo(instance, info)) {
    // state, updateCount, elapsedSeconds, lastError, class and assembly identity
}
scripts.DestroyInstance(instance);
```

- CreateInstance owns a new managed object; it does not expose an S2 object handle.
  Copying its native handle aliases that same instance. Handles are host-local,
  transient and never serialized.
- SetInstanceEnabled changes update eligibility. Faulted instances must be
  destroyed/recreated; enabling does not restart a failed script.
- GetInstanceInfo and ListInstances return native snapshots. Update count and
  elapsed time include successful updates only. List order follows creation IDs.
- UpdateInstances requires finite, nonnegative delta time. Zero is valid and
  still invokes enabled callbacks. Invalid time rejects the whole tick.
- DestroyInstance clears a valid handle even if managed OnDestroy throws. Its
  aliases become stale. An empty handle is an idempotent release on a running host.
  DestroyAllInstances removes all records, including fault diagnostics.
- CreateConfiguredInstances creates a fresh batch each time it is explicitly
  called. Failure disposes only that batch in reverse order and preserves earlier
  instances. Assemblies remain cached until Mono shutdown, as in S2.
- All operations use the initialization thread. Reentry into the managed host
  during managed execution, including constructors and lifecycle callbacks, is
  refused. This also covers Initialize/Shutdown and attempts to initialize a
  different host during cleanup. Future native adapters must defer lifecycle
  mutations until a safe point after the managed call returns. No worker-thread
  attachment/dispatch API is added.

The S2 CreateObject/Invoke/DestroyObject APIs keep their existing semantics:
DestroyObject releases a raw object's root and does not call OnDestroy. Use the
instance APIs when lifecycle callbacks and automatic host cleanup are desired.

## Failure policy

- Constructor failure returns no instance and invokes no lifecycle callbacks.
- OnCreate failure attempts OnDestroy for partial initialization, releases the
  object, returns false and leaves no instance record/output handle.
- OnUpdate failure marks that instance Faulted, attempts OnDestroy immediately,
  and releases its object. It receives no further updates. Its native diagnostic
  record remains queryable until explicit/bulk destruction. Other enabled
  instances continue receiving updates in the same frame.
- OnDestroy failure is logged and reported, but the root is still released and
  cleanup continues for other instances. It is never retried automatically.
- If fault cleanup also throws, diagnostics retain both the original callback
  error and cleanup error, including available managed exception text/stack.

UpdateInstances returns false for new failures in that tick and exposes the first
through LastError. Faulted records retain their own lastError even after another
successful host operation clears the host error. Errors use the existing native
logger. These are trusted in-process scripts; fatal CLR/native failures and
unbounded/blocking script execution are not recoverable through this API.

## Verification

Completed Debug/x64 checks:

- Editor/Game builds with Mono enabled and disabled. The no-Mono Editor opens
  Modular Animation Preview and closes normally; lifecycle/config API guards
  report unavailable without adding a runtime dependency.
- Final Mono-enabled Editor and Game open and close normally with exit code 0.
  The Editor smoke opens Modular Animation Preview; logs contain one runtime
  initialization and shutdown per application and no engine/validation errors.
  The final runtime implementation also compiles with CRUCIBLE_WITH_MONO=0.
- 172 lifecycle checks using actual compiled C#: independent instances, disabled
  updates, zero/invalid delta time, cached inherited/overloaded callbacks, strict
  and async-signature rejection before constructors, constructor/creation/update/
  destruction failures, fault isolation, stale handles, wrong-thread calls,
  constructor/callback/teardown reentry rejection, config rollback and reverse
  cleanup order.
- Forced managed collections and weak references verify roots are released after
  ordinary, partial-creation and fault cleanup. A probe-local native callback
  exercises reentry; no binding registration is added to the production host.
- 21 config checks cover instance-list load/save, invalid shapes/types, unknown
  settings, preserved overrides and isolated Editor/Game list replacement/clearing.
- The real-engine modular-animation regression passes 410 checks over 30 frames
  with one enabled and one disabled LifecycleCounter. It covers scenes, materials,
  sockets and Play/Pause/Stop while asserting callback counts; normal and failed
  Core startup both clean up. S2's 10-check example and 78-check invocation suites
  still pass.

Verification sources/logs are ignored under `build/setup-tools/scripting-s3-*`;
no permanent test target or fixture asset is added. Release packaging, long-run
stability, debugger integration, hot reload and gameplay bindings remain unverified
or deferred. [S4](ScriptingRoadmap.md) is the next milestone.
