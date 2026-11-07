# MakePlayerShowcase.ps1 - writes Assets/Scenes/Game/TestFolder/Player Showcase.scene: the Unity player alone
# (FullPlayer_mech_specialist behaviour, animation and Combat HUD) on a small test course, with one of every
# part on a rack in front of the spawn to try swapping. Run:
#     pwsh -NoProfile -File "Tools/SceneGen/MakePlayerShowcase.ps1"
# Re-running overwrites the scene (and the player's animation controllers).
. "$PSScriptRoot\SceneGen.Common.ps1"
. "$PSScriptRoot\SceneGen.Player.ps1"

$engineRoot = Resolve-Path "$PSScriptRoot\..\.."
$assets = Join-Path $engineRoot 'Assets'
Initialize-PlayerAssets $assets

$S = New-EntityList 'SHOW'

[void](Add-Entity $S 'Directional Light' $null ([ordered]@{
    Transform = (Xf (V3 0 50 0) (V3 1 1 1) (Q -0.3535534 0.3535534 0.1464466 0.8535534))
    Light = (Light 1 0.96 0.9 1.0)
}))
[void](Add-Entity $S 'Ground' $null ([ordered]@{
    Transform = (Xf (V3 0 -0.25 0) (V3 120 0.5 120)); MeshRenderer = (Mesh $Builtin.Cube 0.42 0.44 0.46); Collider = (BoxCollider)
}))

# Test course (north is -Z, the way the player faces at the start):
#   runway ahead - 5 s of walking starts the automatic run; slalom pillars west - dodging;
#   walls and a ramp east - camera, landing and the Jump pose; blocks - jumping.
[void](Add-Entity $S 'Runway' $null ([ordered]@{ Transform = (Xf (V3 0 0.01 -55) (V3 6 0.02 80)); MeshRenderer = (Mesh $Builtin.Cube 0.3 0.32 0.36) }))
for ($i = 0; $i -lt 8; $i++) {
    [void](Add-Entity $S ("Runway Marker " + ($i + 1)) $null ([ordered]@{ Transform = (Xf (V3 0 0.03 (-20 - $i * 10)) (V3 6 0.03 0.4)); MeshRenderer = (Mesh $Builtin.Cube 0.95 0.75 0.2) }))
}
$n = 0
foreach ($p in @(@(-16, -10), @(-21, -17), @(-16, -24), @(-21, -31), @(-16, -38))) {
    $n++; [void](Add-Entity $S "Slalom Pillar $n" $null ([ordered]@{ Transform = (Xf (V3 $p[0] 2 $p[1]) (V3 1.6 4 1.6)); MeshRenderer = (Mesh $Builtin.Cube 0.75 0.3 0.25); Collider = (BoxCollider) }))
}
[void](Add-Entity $S 'Wall North' $null ([ordered]@{ Transform = (Xf (V3 20 2 -18) (V3 12 4 1)); MeshRenderer = (Mesh $Builtin.Cube 0.35 0.5 0.7); Collider = (BoxCollider) }))
[void](Add-Entity $S 'Wall South' $null ([ordered]@{ Transform = (Xf (V3 20 2 -6) (V3 12 4 1)); MeshRenderer = (Mesh $Builtin.Cube 0.35 0.5 0.7); Collider = (BoxCollider) }))
[void](Add-Entity $S 'Ramp' $null ([ordered]@{ Transform = (Xf (V3 20 1.15 8) (V3 7 0.4 12) (QPitch -11)); MeshRenderer = (Mesh $Builtin.Cube 0.55 0.55 0.4); Collider = (BoxCollider) }))
[void](Add-Entity $S 'Ramp Top' $null ([ordered]@{ Transform = (Xf (V3 20 1.15 18) (V3 7 2.3 8)); MeshRenderer = (Mesh $Builtin.Cube 0.55 0.55 0.4); Collider = (BoxCollider) }))
for ($i = 0; $i -lt 3; $i++) {
    [void](Add-Entity $S ("Jump Block " + ($i + 1)) $null ([ordered]@{ Transform = (Xf (V3 (-10 - $i * 4) (0.4 + $i * 0.4) 10) (V3 3 (0.8 + $i * 0.8) 3)); MeshRenderer = (Mesh $Builtin.Cube 0.4 0.6 0.4); Collider = (BoxCollider) }))
}

# The player (Unity's starting loadout: Golem Head, Golem Torso, Fast Legs, Sword in the left arm).
[void](Add-PlayerRig $S (V3 0 1.8 0))

# Parts rack: one of each part, 3 m apart on a strip 8 m to the right of the spawn.
[void](Add-Entity $S 'Parts Rack' $null ([ordered]@{ Transform = (Xf (V3 9 0.01 -3) (V3 4 0.02 30)); MeshRenderer = (Mesh $Builtin.Cube 0.36 0.3 0.26) }))
$rack = @('sword', 'mace', 'throwing_axe', 'small_crossbow', 'large_crossbow', 'cannon', 'cannon', 'golem_head', 'fast_legs', 'heavy_legs')
for ($i = 0; $i -lt $rack.Count; $i++) {
    [void](Add-Part $S ("Pickup " + $rack[$i] + " " + ($i + 1)) $null $rack[$i] (V3 9 0 (10 - $i * 3)) 90)
}

[void](Add-UnityHud $S $PlayerControls)

$scenePath = Join-Path $assets 'Scenes\Game\TestFolder\Player Showcase.scene'
Write-JsonFile $scenePath ([ordered]@{ version = 1; name = 'Player Showcase'; entities = $S.Entities })
"Wrote $($S.Entities.Count) entities to $scenePath"
