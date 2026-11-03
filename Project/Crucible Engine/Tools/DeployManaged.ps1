param(
    [Parameter(Mandatory=$true)][string]$FilesManifest,
    [Parameter(Mandatory=$true)][string]$Destination,
    [Parameter(Mandatory=$true)][string]$ApplicationPath,
    [Parameter(Mandatory=$true)][string]$MonoSdk
)
$ErrorActionPreference = 'Stop'
$appPath = [IO.Path]::GetFullPath($ApplicationPath)
$appName = [IO.Path]::GetFileNameWithoutExtension($appPath)
# The app loads script DLLs from an in-memory copy (ScriptRuntime::LoadAssembly), so the
# files are not locked and can be replaced while it runs. It keeps using the copy it
# loaded until it restarts (no hot reload yet), so just say so.
$running = @(Get-Process -Name $appName -ErrorAction SilentlyContinue | Where-Object {
    !$_.Path -or [IO.Path]::GetFullPath($_.Path) -eq $appPath })
if ($running.Count) { Write-Output "$appName is running (PID $($running[0].Id)): deploying anyway; restart it to use the new scripts." }
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
        # Copy beside it, then swap: an app reading the DLL at that moment never sees half a file.
        $partial = "$target.partial"
        try {
            Copy-Item -LiteralPath $files[$name] -Destination $partial -Force
            Move-Item -LiteralPath $partial -Destination $target -Force
        } catch {
            if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -ErrorAction SilentlyContinue }
            throw "Could not replace $name ($($_.Exception.Message)). If $appName was built before scripts were loaded from memory, it still locks them: close it, rebuild it once, and retry."
        }
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
