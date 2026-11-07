# CheckScene.ps1 - offline sanity check of a .scene / .cprefab: parents exist, Entity fields point at
# entities of the file, and every asset id is a built-in or a GUID found in some Assets/**/*.meta.
#     pwsh -File Tools/SceneGen/CheckScene.ps1 "Assets/Scenes/Game/TestFolder/Zoo Scene.scene"
param([Parameter(Mandatory)][string]$Path)
. "$PSScriptRoot\SceneGen.Common.ps1"
$assets = Join-Path (Resolve-Path "$PSScriptRoot\..\..") 'Assets'
$known = @{}
foreach ($v in $Builtin.Values) { $known[[uint64]$v] = 'builtin' }
Get-ChildItem $assets -Recurse -Filter *.meta | ForEach-Object {
    $line = Get-Content -LiteralPath $_.FullName -TotalCount 1
    if ($line -like 'guid=*') { $known[[Convert]::ToUInt64($line.Substring(5), 16)] = $_.Name }
}
$doc = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
$ids = @{}
foreach ($e in $doc.entities) { $ids[$e.id] = $e }
$errors = 0
function Fail($msg) { Write-Host "ERROR $msg" -ForegroundColor Red; $script:errors++ }
function CheckAsset($what, $value) { if ($value -and [uint64]$value -ne 0 -and -not $known.ContainsKey([uint64]$value)) { Fail "$what -> unknown asset $value" } }
foreach ($e in $doc.entities) {
    $name = $e.components.EntityName.name
    if ($e.parent -is [string] -and -not $ids.ContainsKey($e.parent)) { Fail "$name : parent $($e.parent) missing" }
    $mr = $e.components.MeshRenderer
    if ($mr) { CheckAsset "$name MeshRenderer.modelAsset" $mr.modelAsset; CheckAsset "$name MeshRenderer.meshAsset" $mr.meshAsset; CheckAsset "$name MeshRenderer.materialAsset" $mr.materialAsset }
    if ($e.components.AnimatedModel) { CheckAsset "$name AnimatedModel.controllerAsset" $e.components.AnimatedModel.controllerAsset }
    if ($e.components.SocketAttachment) { CheckAsset "$name SocketAttachment.modelAsset" $e.components.SocketAttachment.modelAsset }
    if ($e.components.AudioSource) { CheckAsset "$name AudioSource.clip" $e.components.AudioSource.clip }
    $sc = $e.components.ScriptComponent
    if ($sc) {
        foreach ($f in $sc.fields) {
            if ($f.type -eq 'Entity' -and [uint64]$f.entityId -ne 0) {
                $hex = '{0:X16}' -f [uint64]$f.entityId
                if (-not $ids.ContainsKey($hex)) { Fail "$name.$($f.name) -> entity $hex not in this file" }
            }
            if ($f.type -notin 'Entity', 'Boolean', 'Int32', 'Single', 'Double', 'String') { CheckAsset "$name.$($f.name)" $f.assetId }
        }
        $cs = Get-ChildItem (Join-Path $assets 'Scripts') -Recurse -Filter "$($sc.class).cs"
        if (-not $cs) { Write-Host "warn  $name : no Assets/Scripts/**/$($sc.class).cs (class may live in another file)" -ForegroundColor Yellow }
    }
}
"{0}: {1} entities, {2} error(s)" -f (Split-Path -Leaf $Path), $doc.entities.Count, $errors
exit $errors
