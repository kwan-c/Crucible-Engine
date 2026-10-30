# Copies only the parts of the pinned Mono 6.12 x64 SDK that Crucible needs into
# Extern/mono, so the SDK can be committed and teammates need no download.
# Run once from the engine root, then commit Extern/mono:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\VendorMono.ps1
param(
    [string]$Destination = (Join-Path $PSScriptRoot '../Extern/mono'),
    # The full SDK has paths over 200 characters deep, which exceed Windows' 260-character
    # limit inside a deep project folder, so it is unpacked in a short folder first.
    [string]$WorkDirectory = (Join-Path $env:SystemDrive 'cmono-tmp')
)
$ErrorActionPreference = 'Stop'
$dest = [IO.Path]::GetFullPath($Destination)

if (Test-Path -LiteralPath (Join-Path $dest 'configure.ac')) {
    throw "$dest contains the Mono SOURCE code, not the SDK. Delete that folder, then run this again."
}
if (Test-Path -LiteralPath (Join-Path $dest 'bin/mono-2.0-sgen.dll')) {
    Write-Output "Mono SDK is already vendored at $dest"
    exit 0
}
if ((Test-Path -LiteralPath $dest) -and (Get-ChildItem -LiteralPath $dest -Force | Select-Object -First 1)) {
    throw "$dest exists and is not empty. Remove it, then run this again."
}

# Downloads, verifies and unpacks the official SDK in the short work folder.
$work = [IO.Path]::GetFullPath($WorkDirectory)
New-Item -ItemType Directory -Force -Path $work | Out-Null
$msiName = 'mono-6.12.0.206-x64-0.msi'
$cachedMsi = Join-Path $PSScriptRoot "../build/setup-tools/$msiName"
if ((Test-Path -LiteralPath $cachedMsi) -and !(Test-Path -LiteralPath (Join-Path $work $msiName))) {
    Copy-Item -LiteralPath $cachedMsi -Destination $work   # reuse an earlier download
}
& (Join-Path $PSScriptRoot 'SetupMono.ps1') -ToolsDirectory $work | Out-Host
$sdk = Join-Path $work 'mono-sdk-6.12.0.206/Mono'

foreach ($dir in 'bin', 'include', 'lib/mono', 'etc') {
    New-Item -ItemType Directory -Force -Path (Join-Path $dest $dir) | Out-Null
}
Copy-Item -Path (Join-Path $sdk 'bin/*.dll') -Destination (Join-Path $dest 'bin')       # runtime DLLs (deployed with the game)
Copy-Item -LiteralPath (Join-Path $sdk 'bin/mono.exe') -Destination (Join-Path $dest 'bin') # runs the C# compiler
Copy-Item -LiteralPath (Join-Path $sdk 'lib/mono-2.0-sgen.lib') -Destination (Join-Path $dest 'lib')
Copy-Item -LiteralPath (Join-Path $sdk 'include/mono-2.0') -Destination (Join-Path $dest 'include') -Recurse
Copy-Item -LiteralPath (Join-Path $sdk 'lib/mono/4.5') -Destination (Join-Path $dest 'lib/mono') -Recurse # framework + mcs.exe
Copy-Item -LiteralPath (Join-Path $sdk 'etc/mono') -Destination (Join-Path $dest 'etc') -Recurse
foreach ($notice in 'LICENSE', 'PATENTS.TXT') { Copy-Item -LiteralPath (Join-Path $sdk $notice) -Destination $dest }

Remove-Item -LiteralPath $work -Recurse -Force   # temporary full SDK and installer
$mb = [math]::Round(((Get-ChildItem -LiteralPath $dest -Recurse -File | Measure-Object Length -Sum).Sum) / 1MB)
Write-Output "Vendored Mono 6.12.0.206 SDK into $dest ($mb MB). Commit Extern/mono, then run rebuild.bat."
