param(
    [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Configuration = 'Debug',
    [ValidateSet('Editor','Game','Both','None')][string]$Application = 'Both',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build'),
    [string]$CMakePath = ''
)
$ErrorActionPreference = 'Stop'
$buildPath = [IO.Path]::GetFullPath($BuildDirectory)
$cachePath = Join-Path $buildPath 'CMakeCache.txt'
if (!(Test-Path -LiteralPath $cachePath)) { throw 'Generate the native project with Mono enabled first. See Docs/ScriptingRuntime.md.' }
$cache = @{}
Get-Content -LiteralPath $cachePath | ForEach-Object {
    if ($_ -match '^([^#/:][^:]*):[^=]+=(.*)$') { $cache[$matches[1]] = $matches[2] }
}
$sourcePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\','/')
if (!$cache['CMAKE_HOME_DIRECTORY']) { throw 'Invalid CMake cache: source directory is missing. Regenerate the native project first.' }
if ([IO.Path]::GetFullPath($cache['CMAKE_HOME_DIRECTORY']).TrimEnd('\','/') -ne $sourcePath) {
    throw 'The selected CMake build belongs to another source directory.'
}
if ($cache['CRUCIBLE_ENABLE_MONO'] -notmatch '^(ON|TRUE|YES|1)$') {
    throw 'This build has Mono disabled. Configure CRUCIBLE_ENABLE_MONO=ON and CRUCIBLE_MONO_ROOT first.'
}
if (!$CMakePath) { $CMakePath = $cache['CMAKE_COMMAND'] }
if (!$CMakePath -or !(Test-Path -LiteralPath $CMakePath -PathType Leaf)) { throw 'Pass -CMakePath pointing to the working CMake executable.' }
$targets = switch ($Application) {
    'Editor' { @('EditorManagedScripts') }
    'Game' { @('GameManagedScripts') }
    'Both' { @('EditorManagedScripts','GameManagedScripts') }
    'None' { @('CrucibleManagedScripts') }
}
$logs = Join-Path $buildPath 'ManagedScripts/logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$log = Join-Path $logs ("BuildScripts-$Configuration.log")
$buildArguments = @('--build',$buildPath,'--config',$Configuration,'--target') + $targets + @('--parallel','2')
Write-Output 'Managed scripts use close-build-restart. No running application is closed or reloaded automatically.'
# Stream native diagnostics, then preserve the build exit code and complete log.
$ErrorActionPreference = 'Continue'
& $CMakePath @buildArguments 2>&1 | Tee-Object -FilePath $log
$buildExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($buildExit -ne 0) { throw "C# build/deployment failed ($buildExit). See $log. Fix the reported error and retry; no app was restarted." }
Write-Output "Managed build succeeded. Log: $log"
if ($Application -ne 'None') { Write-Output 'Restart the application to execute the new assemblies.' }
