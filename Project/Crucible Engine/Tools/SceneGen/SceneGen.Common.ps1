# SceneGen.Common.ps1 - helpers for writing Crucible .scene / .cprefab / .meta files from PowerShell.
# Dot-source it:  . "$PSScriptRoot\SceneGen.Common.ps1"
# See Docs/EngineGuide.md ("Writing scenes by hand or by script") for the format these produce.
$ErrorActionPreference = 'Stop'

# --- built-in assets (ResourceManager::BuiltinId: FNV-1a of "Crucible/Builtin/<name>", LSB set) -----
$script:Builtin = @{
    Cube = [uint64]3578055278504480029; Capsule = [uint64]5067099792209742713; Cone = [uint64]15775344583901223439
    Cylinder = [uint64]2856787514755437467; Disc = [uint64]5933818420240141953; Plane = [uint64]3649096549467294993
    Pyramid = [uint64]1398714408298369051; Sphere = [uint64]8362592118289502741; DefaultMaterial = [uint64]4225546827863954955
}
$script:InvalidIndex = [uint64]4294967295   # "no node" / "whole model" / rest pose

# --- GUIDs and .meta files ------------------------------------------------------------------------
function Get-StableGuid([string]$key) {
    # Deterministic 64-bit id with the low bit set (engine instance GUIDs are always odd).
    $mod = [System.Numerics.BigInteger]::Pow(2, 64)
    $h = [System.Numerics.BigInteger]::Parse('14695981039346656037')
    foreach ($c in [Text.Encoding]::UTF8.GetBytes("Crucible/MediumWell/" + $key)) {
        $h = (($h -bxor [System.Numerics.BigInteger]$c) * [System.Numerics.BigInteger]::Parse('1099511628211')) % $mod
    }
    return [uint64]($h -bor 1)
}

# Reads the GUID of an asset (Assets-relative path); creates its .meta when missing.
# The engine keeps a GUID it finds in a .meta and only rewrites the hash line.
function Get-AssetGuid([string]$assetsRoot, [string]$relativePath) {
    $full = Join-Path $assetsRoot $relativePath
    if (-not (Test-Path -LiteralPath $full)) { throw "Asset not found: $full" }
    $meta = "$full.meta"
    if (Test-Path -LiteralPath $meta) {
        $line = Get-Content -LiteralPath $meta | Where-Object { $_ -like 'guid=*' } | Select-Object -First 1
        if ($line) { return [Convert]::ToUInt64($line.Substring(5), 16) }
    }
    $guid = Get-StableGuid $relativePath.Replace('\', '/')
    $text = "guid={0:X16}`nhash=0`npath={1}`n" -f $guid, $relativePath.Replace('\', '/')
    [IO.File]::WriteAllText($meta, $text, (New-Object Text.UTF8Encoding($false)))
    return $guid
}

function Write-JsonFile([string]$path, $object) {
    $dir = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
    $json = $object | ConvertTo-Json -Depth 40
    [IO.File]::WriteAllText($path, $json, (New-Object Text.UTF8Encoding($false)))
}

# --- values ------------------------------------------------------------------------------------------
function V2($x, $y) { [ordered]@{ x = [double]$x; y = [double]$y } }
function V3($x, $y, $z) { [ordered]@{ x = [double]$x; y = [double]$y; z = [double]$z } }
function V4($x, $y, $z, $w) { [ordered]@{ x = [double]$x; y = [double]$y; z = [double]$z; w = [double]$w } }
function Q($x, $y, $z, $w) { V4 $x $y $z $w }
function QYaw($deg) { $r = [math]::PI * $deg / 360.0; Q 0 ([math]::Sin($r)) 0 ([math]::Cos($r)) }
function QPitch($deg) { $r = [math]::PI * $deg / 360.0; Q ([math]::Sin($r)) 0 0 ([math]::Cos($r)) }
function Xf($pos, $scale = (V3 1 1 1), $rot = (Q 0 0 0 1)) { [ordered]@{ positions = $pos; rotation = $rot; scale = $scale } }

# --- entity list builder -------------------------------------------------------------------------------
# New-EntityList "ZOO" -> an object; Add-Entity $list name parentId components -> id.
# Scenes reference parents by id string; prefabs (-Prefab) by list index (-1 = root).
function New-EntityList([string]$prefix, [switch]$Prefab) {
    $p = 0; foreach ($c in [Text.Encoding]::ASCII.GetBytes($prefix.PadRight(4).Substring(0, 4))) { $p = ($p -shl 8) -bor $c }
    [pscustomobject]@{ Prefix = $p; Counter = 0; Entities = (New-Object System.Collections.ArrayList); Siblings = @{}; IsPrefab = [bool]$Prefab; Index = @{} }
}

function Add-Entity($list, [string]$name, $parent, [System.Collections.Specialized.OrderedDictionary]$components) {
    $list.Counter++
    $id = '{0:X8}{1:X8}' -f $list.Prefix, (0x10000 + 2 * $list.Counter + 1)   # odd, unique per list
    $e = [ordered]@{ id = $id }
    if ($list.IsPrefab) {
        $e.parent = if ($parent) { $list.Index[$parent] } else { -1 }
    } elseif ($parent) {
        $e.parent = $parent
        if (-not $list.Siblings.ContainsKey($parent)) { $list.Siblings[$parent] = 0 }
        $e.siblingIndex = $list.Siblings[$parent]
        $list.Siblings[$parent]++
    }
    $c = [ordered]@{}
    if (-not $components.Contains('Transform')) { $c.Transform = Xf (V3 0 0 0) }
    foreach ($k in $components.Keys) { $c[$k] = $components[$k] }
    $c.EntityName = [ordered]@{ name = $name }
    $e.components = $c
    $list.Index[$id] = $list.Entities.Count
    [void]$list.Entities.Add($e)
    return $id
}

function IdToDecimal([string]$hexId) { [Convert]::ToUInt64($hexId, 16) }

# --- components ------------------------------------------------------------------------------------------
function Mesh($meshId, $r = 0.5, $g = 0.5, $b = 0.5) {
    [ordered]@{ visible = $true; modelAsset = 0; modelRoot = $script:InvalidIndex; materialOverrides = @(); colour = (V4 $r $g $b 1); meshAsset = $meshId; materialAsset = 0 }
}
function ModelRenderer($modelGuid, $modelRoot = $script:InvalidIndex) {
    [ordered]@{ visible = $true; modelAsset = $modelGuid; modelRoot = [uint64]$modelRoot; materialOverrides = @(); colour = (V4 0.5 0.5 0.5 0.5); meshAsset = 0; materialAsset = 0 }
}
function Anim($controllerGuid = 0, $clip = 0, $loop = $true, $speed = 1.0) {
    [ordered]@{ controllerAsset = [uint64]$controllerGuid; clip = [uint64]$clip; speed = [double]$speed; loop = $loop; previewInEditor = $false; playOnStart = $true }
}
function Socket($modelGuid, $node, $alias = '') { [ordered]@{ enabled = $true; socketName = $alias; modelAsset = $modelGuid; node = [uint64]$node } }
function BoxCollider($size = (V3 1 1 1), $trigger = $false, $layer = 'NonMoving', $center = (V3 0 0 0)) {
    [ordered]@{ type = 'Box'; size = $size; radius = 0.5; height = 2.0; center = $center; friction = 0.2; bounciness = 0.0; layer = $layer; isTrigger = $trigger }
}
function SphereCollider($radius, $trigger = $false, $layer = 'NonMoving') {
    [ordered]@{ type = 'Sphere'; size = (V3 1 1 1); radius = [double]$radius; height = 2.0; center = (V3 0 0 0); friction = 0.2; bounciness = 0.0; layer = $layer; isTrigger = $trigger }
}
function CapsuleCollider($radius, $height, $layer = 'Moving', $center = (V3 0 0 0)) {
    [ordered]@{ type = 'Capsule'; size = (V3 1 1 1); radius = [double]$radius; height = [double]$height; center = $center; friction = 0.2; bounciness = 0.0; layer = $layer; isTrigger = $false }
}
function KinematicBody() {
    [ordered]@{ velocity = (V3 0 0 0); mass = 1.0; useGravity = $false; drag = 0.0; angularDrag = 0.05; isKinematic = $true
        constraints = [ordered]@{ freezePositionX = $false; freezePositionY = $false; freezePositionZ = $false; freezeRotationX = $false; freezeRotationY = $false; freezeRotationZ = $false } }
}
function Light($r, $g, $b, $intensity = 1.0) {
    [ordered]@{ type = 'Directional'; colour = (V3 $r $g $b); intensity = [double]$intensity; range = 10.0; innerConeDeg = 20.0; outerConeDeg = 30.0; enabled = $true; castShadows = $true }
}
function AudioSourceComp($clipGuid = 0, $output = 'SFX') {
    [ordered]@{ clip = [uint64]$clipGuid; loadType = 'DecompressOnLoad'; output = $output; playOnAwake = $false; loop = $false; volume = 1.0; pitch = 1.0
        spatialBlend = 0.0; minDistance = 1.0; maxDistance = 30.0; rolloffMode = 'Linear' }
}

# Script with Inspector values. Field helpers: FBool/FFloat/FInt/FString/FEntity/FAsset (type = Prefab, AudioClip, Model...).
function Script([string]$class, $fields = @()) {
    $list = @(); foreach ($f in $fields) { $f.declaringType = "Crucible.Scripts.$class"; $list += $f }
    [ordered]@{ enabled = $true; assembly = 'Managed/Crucible.Scripts.dll'; namespace = 'Crucible.Scripts'; class = $class; fields = $list }
}
function FieldBase($name, $type) {
    [ordered]@{ declaringType = ''; name = $name; type = $type; boolean = $false; integer = 0; single = 0.0; real = 0.0; text = ''; stringIsNull = $false; entityId = [uint64]0; assetId = [uint64]0 }
}
# Type names must be the engine's (ScriptComponentReflection.cpp): Bool, Int, Float. A saved field is
# only applied when its type matches the C# field, so 'Boolean' / 'Int32' / 'Single' were ignored.
function FBool($name, $v) { $f = FieldBase $name 'Bool'; $f.boolean = [bool]$v; $f }
function FFloat($name, $v) { $f = FieldBase $name 'Float'; $f.single = [double]$v; $f }
function FInt($name, $v) { $f = FieldBase $name 'Int'; $f.integer = [int]$v; $f }
function FString($name, $v) { $f = FieldBase $name 'String'; $f.text = [string]$v; $f }
function FEntity($name, [string]$entityHexId) { $f = FieldBase $name 'Entity'; $f.entityId = IdToDecimal $entityHexId; $f }
function FAsset($name, $type, $guid) { $f = FieldBase $name $type; $f.assetId = [uint64]$guid; $f }

function WText($text, $size, $col, $box = (V2 10 3), $billboard = 'Upright') {
    [ordered]@{
        text = $text; colour = $col; fontAsset = 0; fontSize = [double]$size; size = $box; pivot = (V2 0.5 0.5); billboard = $billboard
        alwaysOnTop = $false; doubleSided = $true; intensity = 1.0; fade = $false; fadeStart = 20.0; fadeEnd = 30.0
        horizontalAlign = 'Center'; verticalAlign = 'Middle'; wrap = $true; overflow = 'Overflow'; lineSpacing = 1.0; characterSpacing = 0.0
        richText = $true; bold = $false; underline = $false; bestFit = $false; minFontSize = 0.1; maxFontSize = 1.0
        outline = $false; outlineColour = (V4 0 0 0 1); outlineWidth = 0.02; glow = $false; glowColour = (V4 1 0.85 0.4 0.8); glowWidth = 0.03; glowSoftness = 0.5
        shadow = $false; shadowColour = (V4 0 0 0 0.6); shadowOffset = (V2 0.02 0.02); shadowSoftness = 0.0
    }
}

# --- screen UI ------------------------------------------------------------------------------------------
# Anchors/pivots: (0,0) top-left, (1,1) bottom-right; positions +y DOWN; reference pixels of the canvas page.
function Rect($amin, $amax, $pivot, $pos, $size) { [ordered]@{ anchorMin = $amin; anchorMax = $amax; pivot = $pivot; position = $pos; size = $size; rotation = 0.0; raycastTarget = $false } }
function TL($x, $y, $w, $h) { Rect (V2 0 0) (V2 0 0) (V2 0 0) (V2 $x $y) (V2 $w $h) }
function Img($r, $g, $b, $a) { [ordered]@{ colour = (V4 $r $g $b $a); textureAsset = 0 } }
function Txt($text, $size, $col, $h = 'Left', $v = 'Middle', $bold = $false, $shadow = $true) {
    [ordered]@{
        text = $text; colour = $col; fontAsset = 0; fontSize = [double]$size; horizontalAlign = $h; verticalAlign = $v
        wrap = $false; overflow = 'Overflow'; lineSpacing = 1.0; characterSpacing = 0.0; richText = $true; bold = $bold; underline = $false
        bestFit = $false; minFontSize = 10.0; maxFontSize = 40.0
        outline = $false; outlineColour = (V4 0 0 0 1); outlineWidth = 2.0
        glow = $false; glowColour = (V4 1 0.85 0.4 0.8); glowWidth = 3.0; glowSoftness = 0.5
        shadow = $shadow; shadowColour = (V4 0 0 0 0.6); shadowOffset = (V2 2 2); shadowSoftness = 0.0
    }
}
function Canvas($sortOrder = 0) {
    [ordered]@{ enabled = $true; renderMode = 'ScreenSpace'; referenceResolution = (V2 1920 1080); toggleWithEscape = $false; showsCursor = $false
        scaleWithScreenSize = $true; sortOrder = $sortOrder; pixelsPerUnit = 100.0; pivot = (V2 0.5 0.5); billboard = 'Off'; alwaysOnTop = $false
        doubleSided = $true; intensity = 1.0; fade = $false; fadeStart = 20.0; fadeEnd = 30.0 }
}

