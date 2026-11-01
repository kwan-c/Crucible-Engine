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

A compiler error fails the build and prevents dependent deployment. Previously
deployed assemblies remain unchanged; they are the previous successful version,
not the failed edit. Fix the reported source/reference error and rebuild. Do not
interpret launching an old binary after a failed build as testing the new script.

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

The supported reload policy is **save, build/deploy, restart**. ScriptRuntime loads
each script DLL (and its sibling dependencies such as Crucible.ScriptCore) from an
in-memory copy, so Mono never locks the files: deployment may replace them while
the Editor/Game runs. It prints a note when the application is running. Each DLL is
copied beside its target and then swapped in, so an application that loads it at
that moment never reads a partial file. The running application keeps the copy it
loaded until it restarts. Deployment never terminates an application or discards
work. Compilation without deployment remains available through `-Application None`.
An Editor built before in-memory loading still locks its DLLs; close it, rebuild it
once, and the restriction is gone.

Play/Stop is not reload. Mono caches loaded assembly identity, metadata, methods,
objects and dependencies until final shutdown. Same-path loading returns the
cached assembly; changing a DLL on disk cannot update existing objects. Root
runtime teardown is final and must never be used to restart Mono within a process.

S4 adds no file watcher or hot reload. A future reload design must cover raw S2
objects, S3 instances/cached methods, GC handles, delegates, managed threads,
native callbacks and domain references before unloading anything. Cleanup of S3
instances alone is insufficient. Restart remains the supported recovery after a
failed dependency load.

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
