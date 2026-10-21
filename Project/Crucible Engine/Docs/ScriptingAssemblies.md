# S2 - C# assembly loading and invocation

S2 adds standalone C# execution to the Core-owned Mono host from
[S1](ScriptingRuntime.md). Both Editor and Game can build, load and invoke the
same managed DLL. This page describes the S2 raw-object API. S3 now adds
[managed instance callbacks](ScriptingLifecycle.md); entity components and engine
API bindings remain deferred to S5.

## Build and try the example

Use the SDK/setup and Mono-enabled CMake configuration in ScriptingRuntime.md.
Build Editor or Game normally; their dependencies build `CrucibleManagedExamples`
and deploy `Managed/Crucible.Examples.dll` and its `.dll.mdb` beside each app.
In Visual Studio, select Debug/x64, build and run the Editor startup project.

The source is `ManagedScripts/Examples/Counter.cs`. It contains ordinary C#
logic, with no Crucible references: a stateful counter, a `Run()` entry, a static
multiply method and a string echo method. The matching Mono SDK's `mono.exe`
runs `mcs.exe` offline, targeting its 4.5 framework profile, C# 7.2, AnyCPU and a
library output. No dotnet SDK, NuGet restore or global Mono installation is used.
The `.mdb` is deployed as a symbol artifact; managed debugger attachment has
not been implemented or verified.

User code does not run automatically with the default settings. To run the example
once during Editor startup, merge this into the existing `editor` object in
`Assets/Config/EngineConfig.json` (retain its other settings):

```json
"editor": {
  "scripting": {
    "startupAssembly": "Managed/Crucible.Examples.dll",
    "startupNamespace": "Crucible.Examples",
    "startupClass": "Counter",
    "startupMethod": "Run",
    "startupStatic": false
  }
}
```

On launch, look for:

```text
[Scripting] Loaded managed assembly Crucible.Examples from ...
[Scripting] Startup Counter.Run returned: C# DLL executed: Counter = 7
```

The message verifies that Core loaded the compiled file, constructed a managed
object and invoked its method. This example has no visible scene effect. Close
the app, edit `Counter.cs`, rebuild and restart to try a change. Remove the
startup keys to stop invoking it. The shared config asset is left unchanged by
this implementation.

Use a top-level `scripting` section for both apps, or `game/scripting` for Game.
These settings use the existing shared/per-app config loading rules. Known
scripting keys round-trip through the shared configuration save; the existing
override settings editor preserves manually authored scripting overrides.

All startup strings default to empty. A configured entry needs assembly, class
and method; namespace can be empty. The method must take no arguments and use a
supported return type. `startupStatic=false` requires a public parameterless
constructor, invokes the method and releases the temporary object's GC root.
`true` invokes a static method. Failure makes `Core::Init` fail with a diagnostic;
an empty entry or `enabled=false` skips user code. An explicitly configured entry
in a build without Mono reports unavailable instead of silently succeeding.

The entry runs immediately after host initialization, before engine systems and
assets are initialized. Keep it standalone. It is not an Awake/Start/Update
callback and does not respond to Play/Pause/Stop.

## Native API

`Core::GetScriptRuntime()` provides the host after initialization. The public
headers expose native values and handles, never Mono headers or pointers:

```cpp
auto& scripts = core.GetScriptRuntime();
ScriptAssemblyHandle library;
ScriptObjectHandle counter;
ScriptValue result;

if (!scripts.LoadAssembly("Managed/Crucible.Examples.dll", library)) {
    // Report scripts.LastError(); do not continue using a failed output handle.
    return;
}
std::vector<ScriptClassInfo> classes;
if (!scripts.DiscoverClasses(library, classes)) return;
if (!scripts.CreateObject(library, "Crucible.Examples", "Counter", counter)) return;

if (scripts.Invoke(counter, "Add", {int32_t(9)}, result)) {
    const int32_t total = std::get<int32_t>(result); // 9
    (void)total;
}
scripts.DestroyObject(counter); // release even after an invocation failure

scripts.InvokeStatic(library, "Crucible.Examples", "Counter", "Multiply",
                     {int32_t(6), int32_t(7)}, result); // Int32 42 on success
```

Always check return values in production callers. `LastError()` and the existing
engine logger describe failures. Successful managed operations clear the prior
error. Failed loads/creations clear their output handles; failed invocations
clear their result to `std::monostate`. Class discovery publishes a complete
result only on success.

| C++ ScriptValue alternative | Managed type |
|---|---|
| `bool` | `System.Boolean` |
| `int32_t` | `System.Int32` |
| `float` | `System.Single` |
| `double` | `System.Double` |
| `std::string` | `System.String`, encoded as UTF-8 on the native side |
| `nullptr` | null `System.String` |
| `std::monostate` | void return only |

Overloads require exact types; there are no implicit numeric conversions.
String conversion preserves embedded NUL and Unicode, and rejects invalid UTF-8
arguments or invalid UTF-16 results. Null, empty string and void remain distinct.
Unsupported signatures are rejected before executing the method.

Discovery exposes public, top-level, nongeneric reference classes, including
abstract/static classes for static invocation. Creation requires a concrete class
with a public parameterless constructor. Calls support public instance/static
methods and inherited methods, choosing a matching declaration on the nearest
class. This is a small invocation contract, not a full C# overload binder.
Generic methods, nested/generic classes, structs, enums, arrays, ref/out,
arbitrary object parameters/results, Int64 and async return types are outside S2.

## Ownership and failure handling

- Call the API on the host's initialization thread. There is no worker-thread
  attachment API. Accessors/diagnostics are not a concurrent scripting interface.
- Relative UTF-8 DLL paths resolve beside the executable; absolute paths work too.
  The renderer's existing working-directory requirements still apply to the app.
- Assemblies/images belong to Mono until final shutdown. Canonical paths are
  cached; a conflicting assembly name from another file is rejected. Replacing
  an already loaded file requires an app restart. There is no unload/reload API.
- Dependencies use Mono's framework/sibling probing. Keep custom dependency DLLs
  beside the main DLL. A dependency loaded implicitly can later receive an explicit
  native assembly handle. Missing types are checked through managed reflection
  before using legacy native metadata accessors. Public-member validation is
  conservative: an unresolved public signature can reject a class even if the
  requested method would not use that signature. Discovery fails as a whole when
  exported types cannot be resolved. Restart after correcting failed dependencies.
- Direct modern framework references such as `System.Private.CoreLib` or
  `System.Runtime` version 5+ are rejected with a rebuild hint. This check is not
  a universal compatibility verifier. Build against the bundled Mono profile.
- Every created object has a strong GC handle until `DestroyObject` or shutdown.
  Temporary call arguments/results are rooted and pinned while native code uses
  their addresses. Handle copies refer to the same owned object; destroying one
  invalidates its aliases. Handles are host-local and must never be serialized.
- DestroyObject releases a GC reference. It does not synchronously collect the
  object or invoke Dispose/OnDestroy. S3's separate instance API adds lifecycle
  callbacks and ownership. Remaining roots are released before final JIT
  cleanup. Core still retains one root runtime across scene and PlayMode changes.
- Constructor and method exceptions return false, with the managed type and
  available exception text/stack. A throwing exception formatter has a safe
  fallback. Ordinary invocation failures leave the host usable. This executes
  trusted project code in-process; it is not a sandbox or recovery from process
  termination, native crashes or resource exhaustion.

## Verification

Completed Debug/x64 checks:

- Editor and Game builds with Mono enabled and disabled; no-Mono API/startup
  guards return safely.
- Actual compiled example execution and a 78-check invocation/GC/error suite:
  independent object state, exact overloads, values/strings/null, collection
  pressure, constructors, inheritance, exceptions, stale handles and shutdown.
- Separate dependency/loading checks: sibling resolution, missing base/field/
  parameter types, duplicate identity, invalid DLL and modern framework rejection.
- Configured instance/static startup entry, incomplete settings and error
  preservation; 12 configuration checks for load/save and invalid types. A failed
  C# entry makes Core initialization fail and releases the already-started runtime.
- The existing real-engine modular-animation probe passes 311 checks with a C#
  startup entry, including scene reload, materials, sockets, Play/Pause/Stop and
  final cleanup.
- Normal Editor and Game startup/shutdown with the default empty user entry.
- An incremental Editor build repairs a removed deployed example DLL; its hash
  matches compiler output and the 10-check example execution probe still passes.

Builds and verification fixtures/logs are ignored under
`build/setup-tools/scripting-s2-*`; no permanent test target is added. Verification
does not claim long-duration stability, Release packaging, debugger support or
hot reload.

S3 is complete; see [ScriptingLifecycle.md](ScriptingLifecycle.md). Next is S4
development infrastructure. Engine bindings remain deferred to the approved S5
interface and subsystem owners.
