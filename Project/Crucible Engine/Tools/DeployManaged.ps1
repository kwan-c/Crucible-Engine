param(
    [Parameter(Mandatory=$true)][string]$FilesManifest,
    [Parameter(Mandatory=$true)][string]$Destination,
    [Parameter(Mandatory=$true)][string]$ApplicationPath,
    [Parameter(Mandatory=$true)][string]$MonoSdk
)
$ErrorActionPreference = 'Stop'
$appPath = [IO.Path]::GetFullPath($ApplicationPath)
$appName = [IO.Path]::GetFileNameWithoutExtension($appPath)
foreach ($process in @(Get-Process -Name $appName -ErrorAction SilentlyContinue)) {
    if (!$process.Path -or [IO.Path]::GetFullPath($process.Path) -eq $appPath) {
        throw "Save your work and close $appName before deployment (PID $($process.Id)). Managed assemblies require an application restart. No files were changed."
    }
}
$destinationPath = [IO.Path]::GetFullPath($Destination)
$ownedManifest = Join-Path $destinationPath 'Crucible.ManagedFiles.txt'
$files = @{}
foreach ($source in @(Get-Content -LiteralPath $FilesManifest | Where-Object { $_.Trim() })) {
    $source = [IO.Path]::GetFullPath($source)
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Managed artifact missing: $source. Rebuild the managed targets." }
    $name = [IO.Path]::GetFileName($source)
    if ($name -notmatch '\.dll(\.mdb)?$') { throw "Unsupported managed artifact: $name" }
    if ($files.ContainsKey($name) -and $files[$name] -ne $source) { throw "Managed deployment collision: $name comes from both $($files[$name]) and $source" }
    if (Test-Path -LiteralPath (Join-Path $MonoSdk "lib/mono/4.5/$name")) { throw "Managed dependency shadows the deployed Mono framework: $name" }
    $files[$name] = $source
}
if (!$files.Count) { throw 'Managed deployment manifest is empty.' }
$previous = @()
if (Test-Path -LiteralPath $ownedManifest) { $previous = @(Get-Content -LiteralPath $ownedManifest | Where-Object { $_.Trim() }) }
foreach ($name in $previous) {
    if ([IO.Path]::GetFileName($name) -ne $name -or $name -notmatch '^[^<>:"/\\|?*]+\.dll(\.mdb)?$') {
        throw "Invalid owned managed filename in $ownedManifest. Resolve this manifest before retrying."
    }
}
New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
foreach ($name in @($files.Keys | Sort-Object)) {
    $target = Join-Path $destinationPath $name
    if (!(Test-Path -LiteralPath $target) -or (Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $files[$name]).Hash) {
        Copy-Item -LiteralPath $files[$name] -Destination $target
    }
}
# Remove only filenames recorded by this tool; unrelated hand-deployed files
# are never enumerated for deletion. Filename validation keeps every path local.
foreach ($name in $previous) {
    if (!$files.ContainsKey($name)) {
        $stale = Join-Path $destinationPath $name
        if (Test-Path -LiteralPath $stale -PathType Leaf) { Remove-Item -LiteralPath $stale }
    }
}
@($files.Keys | Sort-Object) | Set-Content -LiteralPath $ownedManifest -Encoding UTF8
Write-Output "Deployed $($files.Count) managed artifacts to $destinationPath. Restart $appName to load changes."
