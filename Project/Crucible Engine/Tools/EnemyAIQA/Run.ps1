param(
    [string]$Probe = "EnemyAIProbe",
    [string]$Scene = "Game/TestFolder/Zoo Scene",
    [string]$Label = "Zoo",
    [int]$TimeoutSeconds = 180
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$qa = Join-Path $root "Build/EnemyAIQA"
$game = Join-Path $qa "Game"
$assets = Join-Path $qa "Assets"
$utf8 = New-Object Text.UTF8Encoding($false)
$sourcePath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "$Probe.cs")).Path
New-Item -ItemType Directory -Path $qa,$game,$assets -Force | Out-Null
# Keep generated config, scene metadata, caches and the instrumented DLL in an isolated game.
foreach ($folder in @("Assets","Resources")) {
    & robocopy (Join-Path $root $folder) (Join-Path $qa $folder) /E /XO /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Copy failed for $folder" }
}
& robocopy (Join-Path $root "Build/Game/Debug") $game /E /XO /XD Cache /XF *.pdb *.log *.txt /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copy failed for the Game runtime" }
$configFile = Join-Path $assets "Config/EngineConfig.json"
$config = Get-Content -Raw -LiteralPath (Join-Path $root "Assets/Config/EngineConfig.json") | ConvertFrom-Json
$config.game.project.startupScene = "Scenes/$Scene.scene"
$config.game.graphics.fullScreen = $false
$config.game.graphics.maximized = $false
$config.graphics.windowWidth = 960
$config.graphics.windowHeight = 540
$config.graphics.backgroundFPS = 60
[IO.File]::WriteAllText($configFile, ($config | ConvertTo-Json -Depth 12), $utf8)
$relative = "Scenes/$Scene.scene"
$contents = [IO.File]::ReadAllText((Join-Path $root "Assets/$relative"))
$fixture = '{"id":"AB3485F80CDEA109","components":{"EntityName":{"name":"Enemy QA"},"ScriptComponent":{"enabled":true,"assembly":"Managed/Crucible.Scripts.dll","namespace":"Crucible.Scripts","class":"' + $Probe + '","fields":[]}}}'
if ($Probe -eq "EnemyAIProbe") {
    $fixture += ',{"id":"AB3485F80CDEA110","components":{"EntityName":{"name":"QA obstruction"},"Transform":{"positions":{"x":-85,"y":2,"z":-85},"scale":{"x":1,"y":1,"z":1}},"Collider":{"type":"Box","size":{"x":3,"y":4,"z":0.7},"layer":"NonMoving","isTrigger":false}}}'
    $fixture += ',{"id":"AB3485F80CDEA111","components":{"EntityName":{"name":"QA second player"},"Transform":{"positions":{"x":85,"y":2,"z":-80},"scale":{"x":1,"y":1,"z":1}},"PlayerController":{},"CharacterController":{"radius":0.4,"height":1.8},"ScriptComponent":{"enabled":true,"assembly":"Managed/Crucible.Scripts.dll","namespace":"Crucible.Scripts","class":"CombatHealth","fields":[]}}}'
}
if ($Probe -eq "WeaponPortSmoke") {
    $cannon = Get-Content -Raw -LiteralPath (Join-Path $root 'Assets/Prefabs/Golem Cannon.cprefab') | ConvertFrom-Json
    $cannonWeapon = $cannon.entities | Where-Object { $_.components.EntityName.name -eq 'Weapon Model' }
    if (!$cannonWeapon.components.SocketAttachment.enabled) { throw 'The reference Cannon weapon has no active socket' }
    $cannonWeapon.components.SocketAttachment.socketName = 'QA mount reference'
    $mountReference = @{
        id = 'AB3485F80CDEA119'
        components = @{
            EntityName = @{ name = 'QA Cannon socket' }
            Transform = @{}
            SocketAttachment = $cannonWeapon.components.SocketAttachment
        }
    }
    $fixture += ',' + ($mountReference | ConvertTo-Json -Depth 8 -Compress)
    foreach ($letter in @('A','B')) {
        $target = [ordered]@{
            id = "AB3485F80CDEA11$letter"
            components = @{
                EntityName = @{name="QA target $letter"}
                Transform = @{positions=@{x=-70;y=2;z=-70};scale=@{x=1;y=1;z=1}}
                Collider = @{type="Box";size=@{x=1;y=1.6;z=1};center=@{x=0;y=.25;z=0};layer="Enemy"}
                ScriptComponent = @{enabled=$true;assembly="Managed/Crucible.Scripts.dll";namespace="Crucible.Scripts";class="CombatHealth";fields=@()}
            }
        }
        $fixture += ',' + ($target | ConvertTo-Json -Depth 8 -Compress)
    }
}
# Splice into the entities array without rounding 64-bit engine asset identifiers.
$at = $contents.IndexOf('[', $contents.IndexOf('"entities"')) + 1
[IO.File]::WriteAllText((Join-Path $assets $relative),$contents.Insert($at, $fixture + ','),$utf8)
$rsp = Join-Path $qa "compile.rsp"
$argsFile = @("-noconfig", "-sdk:4.5", "-langversion:7.2", "-target:library", "-debug", "-r:System.dll", "-r:System.Core.dll",
    ('-r:"' + $game + '/Managed/Crucible.ScriptCore.dll"'), ('-out:"' + $game + '/Managed/Crucible.Scripts.dll"'),
    ('"' + $root + '/ManagedScripts/AssemblyInfo.cs"'))
$argsFile += Get-ChildItem "$root/Assets/Scripts" -Recurse -Filter *.cs | ForEach-Object { '"' + $_.FullName + '"' }
$argsFile += '"' + $sourcePath + '"'
[IO.File]::WriteAllLines($rsp,[string[]]$argsFile,$utf8)
& "$root/Extern/mono/bin/mono.exe" "$root/Extern/mono/lib/mono/4.5/mcs.exe" "@$rsp"
if ($LASTEXITCODE) { throw "Probe compilation failed" }
$env:CRUCIBLE_ASSET_ROOT = $assets
$log = Join-Path $qa ($Label + ".log")
$env:CRUCIBLE_QA_RESULT = Join-Path $qa ($Label + "-" + [guid]::NewGuid().ToString("N") + ".result")
$process = Start-Process -FilePath "$game/Mekanomachia.exe" -WorkingDirectory $game -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $qa ($Label + ".stderr"))
try {
    $end = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 700
        $done = Select-String -LiteralPath $log -Pattern 'END failures=(\d+)' | Select-Object -Last 1
    } until ($done -or (Test-Path -LiteralPath $env:CRUCIBLE_QA_RESULT) -or $process.HasExited -or [DateTime]::UtcNow -gt $end)
    if (Test-Path -LiteralPath $env:CRUCIBLE_QA_RESULT) {
        $failures = [IO.File]::ReadAllText($env:CRUCIBLE_QA_RESULT)
        Write-Output "$Label completed with $failures failures. $log"
        if ($failures -ne "0") { throw "Gameplay checks failed. See $log" }
    } else {
        if (!$done) { throw "Game exited or timed out before completion. See $log" }
        Write-Output $done.Line
        if ($done.Matches[0].Groups[1].Value -ne '0') { throw "Gameplay checks failed. See $log" }
    }
} finally {
    if (!$process.HasExited) {
        [void]$process.CloseMainWindow()
        if (!$process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
    }
}
if (Select-String -LiteralPath $log -Pattern '\[ERROR\]' -Quiet) { throw "The Game logged an engine error. See $log" }
