# MakeZooScene.ps1 - writes the Zoo test scene (a port of Gam3333's Assets/Scenes/ZooScene.unity) plus the
# animation controllers and enemy prefabs it uses. Run from anywhere:
#     pwsh -File "Tools/SceneGen/MakeZooScene.ps1"
# Re-running overwrites those generated files (not the models/audio). Missing .meta files are created with
# stable GUIDs, so the scene's asset references stay valid when the editor indexes them.
#
# Unity is left-handed and this engine right-handed: every Unity position is mirrored as (x, y, -z), so the
# player (Unity z = -81) still walks "forward" (engine -Z) into the four areas.
. "$PSScriptRoot\SceneGen.Common.ps1"
. "$PSScriptRoot\SceneGen.Player.ps1"   # the player, its parts and HUD (shared with MakePlayerShowcase.ps1)

$engineRoot = Resolve-Path "$PSScriptRoot\..\.."
$assets = Join-Path $engineRoot 'Assets'
Initialize-PlayerAssets $assets

# --- Zoo assets -----------------------------------------------------------------------------------------
$M = @{ Fodder = (Get-AssetGuid $assets 'Models\Enemy model\ENEMY_Fodder.glb'); Tent = (Get-AssetGuid $assets 'Models\Zoo Props\tent.glb') }
$A = @{}
foreach ($k in 'Cannon_attack', 'Crossbow_attack', 'CrossbowArm_attack', 'Golem1Legs_Move', 'Mace_attack', 'StoneSword_attack', 'ThrowingAxe_attack') {
    $A[$k] = Get-AssetGuid $assets "Audio\SFX\$k.wav"
}
function Cond($p, $mode) { [ordered]@{ parameter = $p; mode = $mode } }

# ENEMY_Fodder clips: 5 HitReact_Front 8 Idle 9 Jog 14 Walk.
$fodderController = [ordered]@{
    version = 1; entryState = 0
    parameters = @([ordered]@{ name = 'Moving'; type = 'Bool'; default = $false }, [ordered]@{ name = 'Fast'; type = 'Bool'; default = $false },
                   [ordered]@{ name = 'Hit'; type = 'Trigger'; default = $false })
    states = @([ordered]@{ name = 'Idle'; clip = 8; speed = 1.0; loop = $true }, [ordered]@{ name = 'Walk'; clip = 14; speed = 1.0; loop = $true },
               [ordered]@{ name = 'Jog'; clip = 9; speed = 1.0; loop = $true }, [ordered]@{ name = 'Hit'; clip = 5; speed = 1.5; loop = $false })
    transitions = @(
        [ordered]@{ from = 'Any'; to = 3; duration = 0.05; conditions = @((Cond 2 'If')) },
        [ordered]@{ from = 3; to = 0; duration = 0.15; hasExitTime = $true; exitTime = 1.0; conditions = @() },
        [ordered]@{ from = 0; to = 1; duration = 0.2; conditions = @((Cond 0 'If'), (Cond 1 'IfNot')) },
        [ordered]@{ from = 0; to = 2; duration = 0.2; conditions = @((Cond 0 'If'), (Cond 1 'If')) },
        [ordered]@{ from = 1; to = 0; duration = 0.2; conditions = @((Cond 0 'IfNot')) },
        [ordered]@{ from = 2; to = 0; duration = 0.2; conditions = @((Cond 0 'IfNot')) })
}
Write-JsonFile (Join-Path $assets 'Animation\Zoo\Zoo Fodder.animcontroller') $fodderController
$fodderCtrl = Get-AssetGuid $assets 'Animation\Zoo\Zoo Fodder.animcontroller'

# --- enemy prefabs (Unity: Enemy_Fodder 80 hp cost 2, elites 200-400 hp cost 10) ------------------------
function Write-EnemyPrefab([string]$name, [double]$visualScale, [double]$health, [double]$speed, [bool]$fast, [double]$damage) {
    $p = New-EntityList 'ZENM' -Prefab
    $root = Add-Entity $p $name $null ([ordered]@{
        RigidBody = (KinematicBody)
        Collider = (CapsuleCollider (0.6 * $visualScale / 0.8) (2.0 * $visualScale / 0.8) 'Moving' (V3 0 (1.0 * $visualScale / 0.8) 0))
        FlowFieldAgent = [ordered]@{ speed = $speed; acceleration = 20.0; angularSpeed = 360.0; baseOffset = 0.0; startWith = 'Nothing'
            stoppingDistance = 2.2; slowingDistance = 5.0; neighbourRadius = 6.0; separationRadius = 1.8 * $visualScale / 0.8; separationWeight = 1.5
            alignmentWeight = 0.4; radius = 0.6 * $visualScale / 0.8; avoidanceDistance = 3.0; avoidanceWeight = 2.2 }
        ScriptComponent = (Script 'ZooEnemy' @((FFloat 'maximumHealth' $health), (FBool 'fast' $fast), (FFloat 'attackDamage' $damage)))
    })
    # glTF models face +Z; the engine's forward is -Z, so the visual turns 180 degrees.
    [void](Add-Entity $p 'Visual' $root ([ordered]@{
        Transform = (Xf (V3 0 0 0) (V3 $visualScale $visualScale $visualScale) (QYaw 180))
        MeshRenderer = (ModelRenderer $M.Fodder)
        AnimatedModel = (Anim $fodderCtrl 8 $true)
    }))
    $rel = "Prefabs\Zoo\$name.cprefab"
    Write-JsonFile (Join-Path $assets $rel) ([ordered]@{ version = 3; name = $name; entities = $p.Entities })
    return (Get-AssetGuid $assets $rel)
}
$smallPrefab = Write-EnemyPrefab 'Zoo Small Enemy' 0.8 100 4.5 $false 10
$elitePrefab = Write-EnemyPrefab 'Zoo Elite Enemy' 1.3 320 6.0 $true 25

# --- scene ----------------------------------------------------------------------------------------------
$S = New-EntityList 'ZOO '
$black = V4 0.02 0.02 0.02 1
$white = V4 1 1 1 1
function U([double]$x, [double]$y, [double]$z) { V3 $x $y (-$z) }   # Unity position -> engine

[void](Add-Entity $S 'Directional Light' $null ([ordered]@{
    Transform = (Xf (V3 0 30 0) (V3 1 1 1) (Q -0.40821788 0.23456968 0.10938163 0.8754261))   # Unity's rotation, mirrored
    Light = (Light 1 0.96 0.9 1.0)
}))

# Unity's 200 m Plane; also the flow field the enemies walk on (Unity's NavGrid).
[void](Add-Entity $S 'Ground' $null ([ordered]@{
    Transform = (Xf (V3 0 -0.25 0) (V3 200 0.5 200))
    MeshRenderer = (Mesh $Builtin.Cube 0.45 0.47 0.45)
    Collider = (BoxCollider)
    FlowFieldSurface = [ordered]@{ cellSize = 2.0; bodyRadiusPadding = 0.6; stepHeight = 0.3; shoulderHeight = 1.6; floodRadius = 120.0; rebuildDistance = 3.0; wallAdjacencyCost = 4.0 }
}))

# The four areas. Each Unity area is a 20 x 5 TMP text box with three cubes under it: the platform and a
# low step in front and behind (anchor (0.5,0)-(0.5,0.5) puts them 1.25 below the box centre).
function Add-Area([string]$name, [double]$ux, [double]$uz, [double]$width, [double]$textY, [string]$text) {
    $areaY = 12.5
    $root = Add-Entity $S $name $null ([ordered]@{ Transform = (Xf (U $ux $areaY $uz)) })
    $cubes = @(@('Platform', -10.2, 0.0, 14.466773), @('Step Front', -11.410002, -8.69, 5.1618896), @('Step Back', -11.36, 8.64, 5.1618896))
    foreach ($c in $cubes) {
        [void](Add-Entity $S $c[0] $root ([ordered]@{
            Transform = (Xf (V3 0 (-1.25 + $c[1]) (-$c[2])) (V3 $width 2.5998 $c[3]))
            MeshRenderer = (Mesh $Builtin.Cube 0.62 0.62 0.6)
            Collider = (BoxCollider)
        }))
    }
    [void](Add-Entity $S 'Text' $root ([ordered]@{ Transform = (Xf (V3 0 $textY 0)); WorldText = (WText $text 1.2 $black (V2 20 6)) }))
    return $root
}
$movement = Add-Area 'Movement' -19.52 -46.34 16.568327 -2.5 "WASD to Move`n`nLShift to Dodge`n`nSpace to jump"
$partsArea = Add-Area 'parts' 13.1 -46.34 16.568327 -3.28 "Swap parts on the ground with F, cycle with V"
$enemyArea = Add-Area 'Enemy' -4.1 -11.3 16.568327 0.5 "Enemies spawn from spawners.`n`nQ, E, LMB and RMB to attack from your`nequipped weapons`n`nF to toggle lock-on."
$soundsArea = Add-Area 'Sounds' 42.2 -7.1 45.7 -2.5 "Sounds`n`nPress F to play"
$platformTop = 12.5 - 1.25 - 10.2 + 2.5998 / 2   # 2.35

# --- the Zoo pads (Unity's SoundCube / Toggle prefabs: a 1 x 1.62 x 1 post on a 3 x 0.1 x 3 plate) ------
function Add-Pad([string]$name, $parent, $localPos, [System.Collections.Specialized.OrderedDictionary]$extra) {
    $c = [ordered]@{
        Transform = (Xf $localPos (V3 1 1.6242 1))
        MeshRenderer = (Mesh $Builtin.Cube 0.75 0.35 0.2)
        Collider = (BoxCollider (V3 3 3 3) $true)
    }
    foreach ($k in $extra.Keys) { $c[$k] = $extra[$k] }
    $pad = Add-Entity $S $name $parent $c
    [void](Add-Entity $S 'Plate' $pad ([ordered]@{ Transform = (Xf (V3 0 -0.496 0) (V3 3 0.1 3)); MeshRenderer = (Mesh $Builtin.Cube 0.75 0.35 0.2); Collider = (BoxCollider) }))
    [void](Add-Entity $S 'Label' $pad ([ordered]@{ Transform = (Xf (V3 -0.04 1.1143948 -0.12) (V3 1 0.6156877 1)); WorldText = (WText 'Press F' 0.4 $black (V2 10 5)) }))
    return $pad
}

# Sound cubes: RectTransform children of Sounds, so (anchoredX, anchoredY, -localZ) from its centre.
$sounds = @(@('SoundCube (1)', -19.82, -5.1, 'Cannon_attack'), @('SoundCube (2)', -13.82, -5.1, 'Crossbow_attack'),
            @('SoundCube (3)', -7.92, -5.1, 'CrossbowArm_attack'), @('SoundCube (4)', -2.22, -5.1, 'Golem1Legs_Move'),
            @('SoundCube (5)', -19.82, 1.52, 'Mace_attack'), @('SoundCube (6)', -13.82, 1.52, 'StoneSword_attack'),
            @('SoundCube (7)', -7.92, 1.52, 'ThrowingAxe_attack'))
foreach ($cube in $sounds) {
    [void](Add-Pad $cube[0] $soundsArea (V3 $cube[1] -9.3 (-$cube[2])) ([ordered]@{
        AudioSource = (AudioSourceComp $A[$cube[3]])
        ScriptComponent = (Script 'ZooSoundPlayer' @((FAsset 'clip' 'AudioClip' $A[$cube[3]])))
    }))
}

# Enemy area: the two wave groups (switched by the toggles), the spawner tents and ZooEnemyWaves.
$smallGroup = Add-Entity $S 'Small Enemies' $enemyArea ([ordered]@{ Transform = (Xf (V3 -0.75 -8.59 -14.98)) })
$eliteGroup = Add-Entity $S 'Elite Enemies' $enemyArea ([ordered]@{ Transform = (Xf (V3 -0.97 -8.59 -14.98)) })
[void](Add-Pad 'Toggle' $enemyArea (V3 -4.23 -9.29 5.3200006) ([ordered]@{ ScriptComponent = (Script 'ZooToggle' @((FEntity 'toggleObject' $eliteGroup))) }))
[void](Add-Pad 'Toggle (1)' $enemyArea (V3 2.89 -9.29 5.3200006) ([ordered]@{ ScriptComponent = (Script 'ZooToggle' @((FEntity 'toggleObject' $smallGroup))) }))
# Tents: EnemySpawnerModel_* place the tent 39.33 m beyond the spawner (Unity z), on the ground.
$maceTent = Add-Entity $S 'EnemySpawner_Mace' $null ([ordered]@{ Transform = (Xf (U (-4.1 + 3.27 - 0.32) 0 (-11.3 + 13.7 - 39.33)) (V3 3 3 3)); MeshRenderer = (ModelRenderer $M.Tent) })
$crossbowTent = Add-Entity $S 'EnemySpawner_Crossbow' $null ([ordered]@{ Transform = (Xf (U (-4.1 - 4.39 - 0.32) 0 (-11.3 + 14.11 - 39.33)) (V3 3 3 3)); MeshRenderer = (ModelRenderer $M.Tent) })
$smallSpawn = Add-Entity $S 'Small Spawn Point' $maceTent ([ordered]@{ Transform = (Xf (V3 0 0.3 -1.5)) })
$eliteSpawn = Add-Entity $S 'Elite Spawn Point' $crossbowTent ([ordered]@{ Transform = (Xf (V3 0 0.3 -1.5)) })
$enemyRoot = $S.Entities[$S.Index[$enemyArea]]
$enemyRoot.components.ScriptComponent = (Script 'ZooEnemyWaves' @(
    (FEntity 'smallEnemies' $smallGroup), (FEntity 'eliteEnemies' $eliteGroup),
    (FEntity 'smallSpawnPoint' $smallSpawn), (FEntity 'eliteSpawnPoint' $eliteSpawn),
    (FAsset 'smallEnemyPrefab' 'Prefab' $smallPrefab), (FAsset 'eliteEnemyPrefab' 'Prefab' $elitePrefab)))

# Back to Menu pad (Toggle (2)), near the start.
[void](Add-Pad 'Toggle (2)' $null (U -3.2399998 0.85 -69) ([ordered]@{ ScriptComponent = (Script 'ZooBackToMain') }))

# --- part pickups: Unity's parts area (children of 'parts'), minus the Blaster (no stats yet) ------------
$pickups = @(
    @('shoulder_crossbow', -2.439745, 1.8899994, 'large_crossbow'),
    @('shoulder_cannon_specialist', -2.4691238, -5.119999, 'cannon'),
    @('shoulder_cannon_specialist (1)', -2.4691238, -3.119999, 'cannon'),
    @('BipedHeavy_Module', 4.76, 3.5999985, 'heavy_legs'),
    @('BipedFast_Module', 4.76, -1.5951805, 'fast_legs'),
    @('arm_sword', -5.285001, -1.8799973, 'sword'),
    @('arm_throwingaxe', 0.81799984, -1.2799988, 'throwing_axe'),
    @('arm_crossbow', -5.932, 3.1500015, 'small_crossbow'),
    @('arm_mace', -5.045001, -3.619999, 'mace'),
    @('head_base', 1.7183266, -2.674385, 'golem_head'))
foreach ($p in $pickups) {
    [void](Add-Part $S $p[0] $null $p[3] (U (13.1 + $p[1]) $platformTop (-46.34 + $p[2])))
}

# --- player and HUD (SceneGen.Player.ps1: Unity's FullPlayer_mech_specialist and Combat HUD) -----------------
[void](Add-PlayerRig $S (U -2.224197 3 -81.38426))
[void](Add-UnityHud $S ($PlayerControls -replace 'walk 5 s to auto-run', 'walk 5 s to auto-run  |  F on a pad: use it'))

$scenePath = Join-Path $assets 'Scenes\Game\TestFolder\Zoo Scene.scene'
Write-JsonFile $scenePath ([ordered]@{ version = 1; name = 'Zoo Scene'; entities = $S.Entities })
"Wrote $($S.Entities.Count) entities to $scenePath"
