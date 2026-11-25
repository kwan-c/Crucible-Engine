# S4 - Scripting development workflow

S4 builds on the existing S1-S3 Mono host. Project C# files now compile into
`Crucible.Scripts.dll`, with compiler diagnostics, explicit dependency handling
and managed-only build/deployment targets. It uses the same Mono 6.12.0.206 SDK,
framework 4.5 profile and C# 7.2 compiler; no new package or global installation
is required. The [S5 entity workflow](ScriptingEntities.md) now adds component-based
creation/assignment and PlayMode execution. Subsystem APIs remain team-owned.

## Create and run a script

For scripts attached to entities, use **Inspector > Add Component**, search a script
name and select the existing C# class or **Create C# script "Name"**; see
[ScriptingEntities.md](ScriptingEntities.md). The
default command-line template below retains standalone S3 behavior. Pass `-Entity`
to NewScript.ps1 for the EntityScript template and assign it through a component.

First configure and build the Mono-enabled Editor/Game as described in
[ScriptingRuntime.md](ScriptingRuntime.md). Then, from the engine root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\NewScript.ps1 -Name MyScript
```

This creates `Assets/Scripts/MyScript.cs` with the S3 callbacks and prints
the configuration entry. Existing files are never overwritten. You can also
create `.cs` files manually anywhere under `Assets/Scripts` (subfolders are
fine); CMake discovers additions/removals recursively at build time. No CMake
source-list edit is needed for project scripts. They appear under the Managed
solution folder after regeneration. `ManagedScripts/AssemblyInfo.cs` keeps an
otherwise empty project buildable. `.cs` files elsewhere in `Assets` are not
compiled by this target.

Add the printed entry to the existing `editor.scripting.instances` array in
`Assets/Config/EngineConfig.json`, retaining your other settings:

```json
"editor": {
  "scripting": {
    "instances": [
      {
        "assembly": "Managed/Crucible.Scripts.dll",
        "namespace": "Crucible.Scripts",
        "class": "MyScript",
        "enabled": true
      }
    ]
  }
}
```

Save your scene, then run (the Editor/Game can stay open):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\BuildScripts.ps1
```

Restart the Editor to load the new scripts. The native log reports creation of `MyScript`; its
OnUpdate is called each Core frame. The generated methods are empty: add your
standalone C# logic. `System.Console.WriteLine` writes to process stdout, not the
Editor Console panel. See [ScriptingLifecycle.md](ScriptingLifecycle.md) for the
full contract. Instances follow Core lifetime and continue across Edit/Play/Pause,
Stop and scene changes. These standalone classes do not receive an entity owner.
EntityScript subclasses execute through ScriptComponent, not scripting.instances.

The helper uses CMake from the existing build's cache, so an unrelated CMake on
PATH does not replace the project's configured version. It checks source/build
identity and that Mono is enabled. It does not retarget Visual Studio, download
tools, edit shared configuration, launch/close an application or enable scripts.

## Build commands and Visual Studio

Normal Editor/Game builds include managed compilation and deployment. The Managed
solution folder also contains these targets:

| Target | Purpose |
|---|---|
| CrucibleManagedExamples | Existing Counter and LifecycleCounter example DLL |
| CrucibleProjectScripts | All project `.cs` files under Assets/Scripts |
| CrucibleManagedScripts | Compile all registered managed libraries only |
| EditorManagedScripts | Compile all managed libraries, deploy to Editor/Managed |
| GameManagedScripts | Compile all managed libraries, deploy to Game/Managed |

Building a managed-only target does not compile/link Engine, Editor or Game or
stage Mono itself. Run a normal application build once to stage native/runtime
dependencies. The deployment targets repair missing managed DLLs/symbols on an
incremental build. Source files and reference DLLs are timestamp dependencies;
changes rebuild consumers. Builds with Mono OFF create no managed targets and
require neither the SDK nor these PowerShell tools.

Useful helper variants:

```powershell
# Compile while applications remain open; do not deploy into them.
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\BuildScripts.ps1 -Application None
# Deploy only to the application you closed.
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\BuildScripts.ps1 -Application Editor
# Use another already-configured build tree or configuration.
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\BuildScripts.ps1 -Application Game -Configuration Release -BuildDirectory .\build
```

Debug defines DEBUG and TRACE and disables optimization; other configurations
define TRACE and enable optimization. Every configuration emits `.dll.mdb`
symbols. These artifacts support future debugger work; managed breakpoints and
debugger attachment have not been implemented or verified.

## Diagnostics and recovery

The compiler reports source filename, line/column and CS diagnostic code through
the normal build output. Visual Studio can use that diagnostic format to locate
source. Detailed logs are under the selected build tree:

```text
ManagedScripts/<configuration>/logs/<assembly-name>.log
ManagedScripts/logs/BuildScripts-<configuration>.log
```

Response files hold exact compiler inputs in
`ManagedScripts/<configuration>/args`. They handle spaces in paths and keep long
source/reference lists off the command line. Compiler outputs are first written
under `work/<assembly-name>` with their original assembly filename. Only a
successful DLL/symbol pair replaces that library's previous build outputs.

A compiler error in `Assets/Scripts` (Crucible.Scripts) does not stop the engine
build, in any configuration. Its errors appear as build warnings, the last good
DLL is deployed, and Editor/Game still build. With no earlier DLL (a clean build),
a placeholder with no scripts is built from `AssemblyInfo.cs` and dated in the
past. The next build retries, and the Editor compiles on open and shows the errors
in its Console. Do not treat running after such a build as testing the new script,
and check for these warnings before packaging: a package would contain the last
good scripts, or none.

A compiler error in any other managed library (Crucible.ScriptCore, the examples)
fails the build and prevents dependent deployment. Previously deployed assemblies
remain unchanged; they are the previous successful version, not the failed edit.

Deployment validates all input files, duplicate output filenames, framework
shadowing and its owned-file list before copying. Missing/colliding dependencies
fail with their paths. After a successful copy it removes only stale filenames
recorded in `Managed/Crucible.ManagedFiles.txt`; unrelated DLLs are preserved.
Keep that generated manifest with the app output. An interrupted/locked copy
reports failure; close the app and retry deployment to repair it. Deployment is
not a transaction across multiple applications.

Runtime failures use the existing S2/S3 LastError/native logger. Missing framework
files require a normal application rebuild; missing project dependencies require
correcting the managed references and redeploying. Correct missing class,
namespace, method or lifecycle signatures in source/config, rebuild and restart.
Configured startup failure fails Core initialization and cleans up the host.
An update exception faults only that instance and attempts cleanup once.

## Additional managed libraries and dependencies

For an explicitly owned C# library, add a declaration in
`ManagedScripts/CMakeLists.txt` before the aggregate CrucibleManagedScripts target:

```cmake
crucible_add_managed_library(MyUtilities
    ASSEMBLY Team.Utilities
    SOURCES Utilities/MathHelpers.cs)

crucible_add_managed_library(MyFeatureScripts
    ASSEMBLY Team.FeatureScripts
    SOURCES Feature/FeatureScript.cs
    REFERENCES MyUtilities "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/ApprovedLibrary.dll")
```

Declare referenced managed targets first. REFERENCES accepts those targets or
explicit DLL paths, adds compiler/rebuild dependencies, and stages the declared
DLLs beside the script assemblies in each app's Managed folder. List every
non-framework transitive dependency explicitly; this is not a NuGet resolver or
automatic dependency scanner. External symbol files are not automatically copied.
System and System.Core are framework references provided by the selected SDK.
To reference a library from the default project scripts, declare it before
CrucibleProjectScripts and add REFERENCES to that existing declaration.
Other compatible framework references may be added to the helper's compiler
options by their subsystem owner; do not deploy copies over the Mono profile.

Use unique assembly names/filenames, the selected compatible framework, and
approved libraries only. Replacing an existing reference rebuilds its consumers.
The deployment check rejects filename collisions and DLLs shadowing framework
files. It does not verify every external binary's framework compatibility; the
existing runtime rejects direct modern .NET dependencies where detected and
reports unresolved types through checked managed reflection. Native P/Invoke
dependencies need separate approved native deployment; they are not inferred.

## Reload policy and S5 extension rules

In the Editor, **saving a .cs file is enough**: the Editor compiles and reloads
the scripts by itself (see "Editor hot reload" below). Builds from Visual Studio
or BuildScripts.ps1 still work; pick their DLL up with Scripts > Reload Scripts or
a restart (the only option for the Game). ScriptRuntime loads
each script DLL (and its sibling dependencies such as Crucible.ScriptCore) from an
in-memory copy, so Mono never locks the files: deployment may replace them while
the Editor/Game runs. It prints a note when the application is running. Each DLL is
copied beside its target and then swapped in, so an application that loads it at
that moment never reads a partial file. The running application keeps the copy it
loaded until it reloads or restarts. Deployment never terminates an application or discards
work. Compilation without deployment remains available through `-Application None`.
An Editor built before in-memory loading still locks its DLLs; close it, rebuild it
once, and the restriction is gone.

Play/Stop is not reload. Script DLLs and every script object live in a separate
Mono domain ("Crucible.Scripts"); the root domain only holds the framework and is
never torn down (root teardown is final and must never be used to restart Mono).
`ScriptRuntime::ReloadScripts()` - the Editor's **Scripts > Reload Scripts** -
replaces the script domain:

1. Refused while entity scripts exist (Play): their live state would be lost.
2. OnDestroy on every remaining standalone instance.
3. Every GC handle and every cached Mono pointer (assemblies, objects, instances,
   validated classes, field defaults) is released; handles callers still hold
   become stale and calls with them fail.
4. `mono_domain_unload` (not `mono_domain_try_unload`, which aborts the process
   when called from native code in Mono 6.8). Success is checked by looking the
   domain up by id; Mono aborts threads a script started.
5. A fresh script domain; `ReloadCount()` increments. The next LoadAssembly reads
   the DLLs from disk again; configured standalone instances are recreated.

Static C# fields reset. Authored field values are kept: they live in
ScriptComponent, not in C#. The Inspector re-reads class lists and field defaults
when `ReloadCount()` changes; EntityScriptSystem forgets its (stale) records. A
broken DLL is reported by the next LoadAssembly and scripting stays usable;
deploy a fixed build and reload again. There is no reload during Play.

### Editor hot reload

`Editor/src/Scripting/ScriptHotReload` (ticked by the main menu every frame)
checks `Assets/Scripts` twice a second. When a .cs file is added, removed or
saved and then left alone for 0.4 s, `ScriptCompiler` runs the Mono SDK's
`mono.exe mcs.exe` as a hidden background process over every .cs under
Assets/Scripts plus ManagedScripts/AssemblyInfo.cs, with the same options as
ManagedLibrary.cmake (minus `-sdk:4.5`, which only produced warning CS8001 with the
vendored SDK). The SDK path comes from `CRUCIBLE_MONO_ROOT` (Editor/CMakeLists.txt
defines `CRUCIBLE_MONO_SDK`); without it, `Extern/mono` is used. The Editor keeps
running while it compiles.

- Success: Crucible.Scripts.dll and .mdb are swapped into the Editor's Managed/
  folder (copy beside, then rename), then ReloadScripts runs - at once in Edit
  mode, or when Play stops.
- Errors and warnings go to the Console as `Assets/Scripts/X.cs(line,col): CSxxxx:
  message`; the previous scripts stay loaded and the same sources are not
  recompiled until they change.
- At startup it compiles once if a source is newer than the deployed DLL.
- The menu bar shows "Compiling scripts...", "Script errors: N" (tooltip: the first
  error) or "reload after Stop"; nothing while up to date.
- Scripts menu: **Recompile Scripts** (compile now), **Reload Scripts** (reload the
  DLL already in Managed/), **Auto Recompile on Save** (on by default).

Verified with the real ScriptRuntime, ScriptCompiler and ScriptHotReload against
real Mono and mcs: first compile at startup, save -> new code running in about
0.8 s, compile error with file/line/code and old scripts kept (no recompile loop),
fix -> reload, save during Play -> reload after Stop, new file -> new class listed,
Auto Recompile off + manual Recompile. The Windows process code was compile-checked
with MinGW; run it once in the Editor to confirm.

Verified against real Mono with a harness that drives the real ScriptRuntime:
new classes and changed field defaults appear after a reload, stale handles are
rejected, refusal during entity scripts, OnUpdate exception reporting, broken-DLL
recovery, and 60 reloads alternating two builds with each one running the code
just deployed and memory flat (+88 KB over the last 50 reloads).

OnUpdate is called through `mono_method_get_unmanaged_thunk` (a direct native
entry point) instead of `mono_runtime_invoke`; OnCreate/OnDestroy and other
calls still use mono_runtime_invoke.

S5 subsystem owners should reuse Core::GetScriptRuntime and the documented
post-JIT/pre-user-assembly registration point. Keep Mono headers/types inside the
host, preserve initialization-thread and reentry rules, and defer native structural
mutations to the engine's existing safe points. Entity/ECS integration must define
its own approved PlayMode and scene lifecycle using the existing engine services.
It must not introduce a second ECS, runtime host or automatic game-runtime compiler.

## Verification

Completed verification:

- Normal Mono-enabled Debug/x64 Editor and Game builds pass. There is one existing
  ViewTargets.cpp C4457 warning. Both applications start and close normally with
  exit code 0; smoke logs contain no engine or Vulkan validation errors.
- 23 workflow checks cover script generation, refusal to overwrite a source,
  invalid names, automatic source discovery/removal, compile-only isolation,
  execution of two successive compiled versions, source/line/CS-code diagnostics,
  compiler failure preserving build/deployed DLLs, correction and rebuild.
  Debug and Release managed compilation and compiler settings are checked.
- 25 dependency/deployment checks cover target references, consumer rebuilds,
  sibling staging, missing inputs/references, duplicate names/identities, framework
  shadowing, owned stale-file pruning, preservation of unrelated DLLs and refusal
  of unsafe owned filenames. A real native host executes the dependent library,
  reports its missing base-class dependency without aborting, and executes again
  after restoration and process restart.
- With Editor running, compile-only succeeds, deployment fails with its PID, and
  hashes of its managed DLLs/symbols/ownership manifest remain unchanged. Normal
  close permits deployment. Removing a deployed symbol file is repaired by the
  next managed-only deployment.
- An isolated native project generates with Mono OFF and creates no managed
  targets. The wrapper rejects that build, a missing cache, an unrelated source
  tree and malformed caches with useful diagnostics. The active build remains ON.
- Existing 172-check lifecycle, 78-check assembly invocation, 10-check assembly
  example and 410-check Core/modular-animation suites still pass, including scene
  replacement, materials, sockets and Play/Pause/Stop.

Temporary fixtures and logs live in ignored `build/setup-tools/scripting-s4-*`;
no permanent test project or test scene is added. Shared engine configuration and
scene/source assets are unchanged. Native Release packaging, managed debugger
attachment and long-duration stability were not verified. Hot reload and
subsystem-specific S5 bindings are not implemented.
