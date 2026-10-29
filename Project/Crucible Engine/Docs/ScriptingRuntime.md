# S1 - Mono runtime hosting

S1 embeds a managed runtime in the existing Engine.dll/Core lifecycle. This page
documents that foundation. [S2](ScriptingAssemblies.md) now adds C# DLL loading,
discovery, objects and invocation. [S3](ScriptingLifecycle.md) now provides standalone
instance lifecycle callbacks. [S4](ScriptingDevelopment.md) adds project script
creation, build/deployment tooling and diagnostics. [S5](ScriptingEntities.md) adds
entity script components, owner identity and logging; subsystem APIs remain
team-owned extension work in ScriptingRoadmap.md.

## Distribution and compatibility

The selected package is the official **Mono 6.12.0.206 x64 Windows SDK**, using
SGen and its CLR v4 / .NET Framework-compatible managed profile. Its native
runtime identifies itself as `6.12.0 (Visual Studio built mono)`; the package
patch version is pinned by the installer filename and checksum:

```text
mono-6.12.0.206-x64-0.msi
SHA256 4125F57D97CFA88257915EDC969E913DE198CD8E22396A29849037479A0AC368
```

The downloaded MSI's Authenticode signature was verified as valid, signed by
Microsoft Corporation. The SDK was extracted locally, not installed system-wide.
Mono/.NET library approval is taken from the supplied task; no new dependencies
beyond Mono were added. The approval spreadsheet itself was not supplied here.

This is legacy Mono. Its managed framework is not CoreCLR or the modern .NET
shared framework. The existing installed dotnet SDK is not a substitute for this
embedding SDK. S2 targets the bundled Mono 4.5 framework/compiler profile.
No modern .NET compatibility or managed debugging/hot-reload claim is made by S1.

Official references: [download/version and project status](https://www.mono-project.com/download/stable/),
[embedding configuration and shutdown](https://www.mono-project.com/docs/advanced/embedding/).

## Build setup

From the Crucible Engine root, prepare a local SDK:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\SetupMono.ps1
```

The execution-policy setting applies only to that PowerShell process. The helper
downloads the pinned signed MSI to ignored build/setup-tools, verifies SHA256 and
signature before extraction, and uses Windows Installer's administrative-image
mode. It does not install Mono globally or change PATH. It also fetches the
matching upstream LICENSE and PATENTS.TXT notices for runtime deployment.

The resulting SDK root is:

```text
build/setup-tools/mono-sdk-6.12.0.206/Mono
```

Configure the existing build using the project's working CMake executable
(verification used local CMake 3.30.3 and VS 2022/v143):

```powershell
$monoSdk = (Resolve-Path .\build\setup-tools\mono-sdk-6.12.0.206\Mono).Path
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCRUCIBLE_ENABLE_MONO=ON "-DCRUCIBLE_MONO_ROOT=$monoSdk"
cmake --build build --config Debug --target Editor Game -- /m:1 /v:minimal /nologo
```

Use the CMake version already working for the project; this does not change
run.bat or retarget Visual Studio. CRUCIBLE_MONO_ROOT can also point at another
matching installed/extracted SDK; MONO_ROOT is accepted when the CMake path is
empty. Headers, import library and framework must come from the same x64 SDK.
Commit source/build instructions, not the SDK, MSI, output folders or local cache.

**Without Mono:** CRUCIBLE_ENABLE_MONO defaults to OFF. Both executables retain
their normal features and ScriptRuntime reports Unavailable, with a clear log
message. There is no Mono link/header/deployment requirement in that build.
Explicitly configuring ON with an incomplete SDK fails during CMake configuration.

## Deployment

EditorMonoRuntime and GameMonoRuntime stage dependencies beside their executable
on incremental builds, including when a deployed file was removed. Both use the
same SDK and framework; no machine-specific SDK path is compiled into Engine.dll.

```text
Editor.exe or Mekanomachia.exe
Engine.dll and the engine's existing dependencies
mono-2.0-sgen.dll
MonoPosixHelper.dll
libmono-btls-shared.dll
Mono/
  lib/mono/4.5/        complete managed profile, including Facades
  etc/mono/           runtime config, machine.config and framework settings
  LICENSE
  PATENTS.TXT
```

Only native bin/*.dll files and the profile/configuration tree are deployed;
the SDK include files, import libraries and unrelated reference profiles are not.
The profile is copied intact to retain its framework dependency layout. S1 does
not implement framework trimming. The normal Windows/UCRT/MSVC runtime
requirements of these native binaries and the existing engine still apply.

No machine-wide Mono or dotnet runtime is needed to execute the staged host.
The host explicitly sets Mono's lib/etc directories. Missing framework/config
files are reported before JIT initialization and fail Core::Init. Missing native
DLLs are Windows loader errors before Core can run; rebuild the application to
repair deployment. Non-Windows and non-x64 Mono-enabled builds are not supported
by this initial packaging implementation.

## Runtime configuration

EngineConfig has a ScriptingConfig. Optional JSON keys in the existing
Assets/Config/EngineConfig.json are:

```json
"scripting": {
  "enabled": true,
  "runtimeDirectory": "Mono",
  "verifyManagedExecution": true
}
```

These are the defaults; the shared asset file was not changed. Like the other
sections, editor/game JSON sections can override scripting settings. Save retains
them through EngineConfigLoader and preserves unknown keys/app overrides.
No new settings panel is introduced in S1.

Relative runtimeDirectory paths resolve against the running executable's
directory, not its current working directory or the Assets root. Absolute paths
are also supported. Setting enabled=false skips Mono initialization in an enabled
build. It does not remove native link dependencies; use the CMake OFF option for
an application that has no Mono dependency at all.

## Ownership and lifecycle

- Core creates its ScriptRuntime at Init and stops initialization if the enabled
  host fails. Both Editor and Game share this path. GetScriptRuntime is valid
  after Init has created the host, like the existing Core resource accessors.
- The native public header has no Mono headers/types. Only ScriptRuntime.cpp
  touches the SDK. The host owns one root domain initialized with CLR v4 profile.
- One host may initialize Mono in a process. A second active host is rejected.
  Repeated Initialize on the running owning host is idempotent; it does not
  reconfigure an already-running runtime.
- Lifecycle calls belong to the initialization thread. Wrong-thread cleanup is
  refused, leaving the owner to perform final cleanup. Worker-thread managed
  execution/attachment is not an S1 API.
- Native Mono diagnostics use Crucible's existing logger, with a nonthrowing C
  callback. Startup validation/errors are accessible through LastError. States
  distinguish NotStarted, Unavailable, Disabled, Running, Stopped and Failed.
- With verification enabled, startup resolves and invokes System.Math.Abs(Int32)
  through Mono's managed invocation API. It requires a boxed Int32 result of
  314159 for -314159 and logs that result. A managed exception or wrong result
  fails initialization. ManagedExecutionVerified exposes successful verification.
- Core keeps the root runtime alive across scenes and Play/Pause/Stop. During
  final cleanup it releases entities, systems and layers first, then shuts Mono
  down once. Destruction/repeated Shutdown is safe. Final Mono cleanup cannot be
  followed by initialization in that same process; the guard reports an error.
- S3 releases managed instances/callbacks before final host shutdown. S4 must
  manage reload separately; no domain unload or hot-reload API exists in S1.

The post-JIT, pre-user-assembly point in ScriptRuntime.cpp is reserved for future
subsystem-owned binding registration. S1 adds no internal calls, engine wrappers,
ECS integration, gameplay base class or managed assembly abstraction.

## Verification

The following are completed checks, not assumptions from compilation:

- Editor and Game Debug/x64 build with CRUCIBLE_ENABLE_MONO both OFF and ON.
- The no-Mono Editor opens the existing Modular Animation Preview scene and
  shuts down normally.
- An ignored native host probe actually invokes managed BCL code through the
  deployed Mono JIT. It tests unavailable/disabled states, duplicate host rejection,
  idempotent initialization, wrong-thread shutdown rejection, final cleanup and
  refusal to restart. Missing deployment fails before JIT and can be corrected
  before the first successful initialization.
- Scripting configuration load/save/type validation and preservation of unknown
  settings/app overrides are exercised.
- A missing deployed etc/mono/config is diagnosed; rebuilding repairs that file.
- The existing real-engine modular-animation probe passes 311 checks with added
  checks that Mono remains running/verified throughout its 30 frames, including
  scene reload, Play/Pause/Stop, serialization, sockets and renderer cleanup.
  The log records one runtime initialization and one final shutdown.
- Core initialization with an invalid runtime path fails cleanly and its partial
  startup cleanup completes.
- The normal Editor executable opens Modular Animation Preview, and the normal
  Game executable opens its existing Fog_Lighting_Effects startup scene and enters
  Play. Both log successful managed execution and final shutdown, then exit with
  code 0 when closed normally from their intended output working directories.

These S1 checks used the BCL method above. S2 subsequently verifies user-authored
C# DLLs and managed exceptions; see ScriptingAssemblies.md. No long-duration
stability, Release distribution, managed debugger or hot reload is claimed.

Ignored probes/build/run logs are under build/setup-tools/scripting-s1-*.
No permanent test target or third-party source edit was added.

Main changed/added files are Engine/MonoRuntime.cmake, Engine/DeployMono.cmake,
the Engine/Editor/Game CMakeLists, Scripting/ScriptRuntime.h/.cpp,
Scripting/ScriptingConfig.h, Core.h/.cpp, Configuration/EngineConfig.h,
Configuration/EngineConfigLoader.cpp, Tools/SetupMono.ps1 and the two scripting
documents. The shared config asset, existing script samples and teammates'
subsystem implementations were not changed.
