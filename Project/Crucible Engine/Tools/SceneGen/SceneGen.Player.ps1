# SceneGen.Player.ps1 - the player mech, its parts and its HUD, shared by every scene generator.
# Dot-source after SceneGen.Common.ps1:
#     . "$PSScriptRoot\SceneGen.Common.ps1"; . "$PSScriptRoot\SceneGen.Player.ps1"
#     Initialize-PlayerAssets $assets          # GUIDs, writes the player's animation controllers
#     $player = Add-PlayerRig $list (V3 0 1.8 0)
#     [void](Add-Part $list 'Sword pickup' $null 'sword' (V3 4 0 -6))   # ground point; the part rests on it
#     [void](Add-UnityHud $list $controlsText)
#
# It ports Unity's FullPlayer_mech_specialist (the model, sockets, MechLegAnimator / MechWeaponAnimator /
# MechMovementAudio presentation and the starting loadout) and the Combat HUD prefabs (layout, sprites,
# icons). Scripts that drive it: Assets/Scripts/Player/*.cs.

. "$PSScriptRoot\UnityMount.ps1"   # Unity weapon mounting chain -> engine transforms

$script:PartScale = 0.55   # the mech visual's scale; parts on the ground use it too

function Initialize-PlayerAssets([string]$assets) {
    $script:PM = @{}
    $files = @{
        Legs = 'Models\Golem model\LEGS_Biped.glb'; Torso = 'Models\Golem model\TORSO_Specialist.glb'; Head = 'Models\Golem model\HEAD_Specialist.glb'
        Cannon = 'Models\Weapon model\SHOULDER_Cannon.glb'
    }
    foreach ($k in $files.Keys) { $script:PM[$k] = Get-AssetGuid $assets $files[$k] }
    $script:PA = @{}
    foreach ($k in 'Cannon_attack', 'Crossbow_attack', 'CrossbowArm_attack', 'Golem1Legs_Move', 'Mace_attack', 'StoneSword_attack', 'ThrowingAxe_attack') {
        $script:PA[$k] = Get-AssetGuid $assets "Audio\SFX\$k.wav"
    }
    $script:PT = @{}
    foreach ($k in 'HpBar', 'HpBarBack', 'HpBarWhite', 'HpBarGlow', 'WeaponsHUDBG', 'WeaponsHUDGlow', 'Cursor', 'CursorLockon', 'CursorCamLockon') { $script:PT[$k] = Get-AssetGuid $assets "Textures\HUD\$k.png" }
    foreach ($k in 'SwordIcon', 'MacecIcon', 'CrossbowArmIcon', 'ThrowingAxeIcon', 'CannonIcon', 'GiantCrossbowIcon') { $script:PT[$k] = Get-AssetGuid $assets "Textures\HUD\PartIcons\$k.png" }
    foreach ($k in 'HealIcon', 'JumpIcon', 'DamageAmpIcon') { $script:PT[$k] = Get-AssetGuid $assets "Textures\HUD\AbilityIcons\$k.png" }

    # Node indices in the specialist GLBs (EngineGuide.md, "Models, sockets, part roots and animation").
    $script:PN = @{ LegsSocketHip = 2; TorsoMountHip = 31; TorsoSocketHead = 2; TorsoArmL = 9; TorsoArmR = 20; TorsoShoulderL = 16; TorsoShoulderR = 27
                    HeadMount = 7; CannonMount = 9 }

    # --- Unity's Biped_Specialist.controller (driven by MechLegAnimator) ---------------------------------
    # LEGS_Biped clips: 0 Dash, 1 Idle, 2 Jog, 3 JumpAir. Unity states: Neutral = Idle; Locomotion = Idle->Jog
    # blend at speed 2 x clamp(speed / 6, 0.35, 2); Boost = Dash frames 0-16; BoostStop = Dash 18-30;
    # Jump = JumpAir. The engine has no blend trees or speed parameters, so Locomotion is the Jog clip in four
    # speed bands, and BoostStop enters Dash at 18/30. MechPlayer writes Integer Pose and Float Speed (m/s).
    function C($p, $mode, $value) { $c = [ordered]@{ parameter = $p; mode = $mode }; if ($null -ne $value) { $c.value = $value }; $c }
    function AnyTo($to, $conds, $offset = 0.0) { [ordered]@{ from = 'Any'; to = $to; duration = 0.08; offset = [double]$offset; conditions = @($conds) } }
    $legs = [ordered]@{
        version = 1; entryState = 0
        parameters = @([ordered]@{ name = 'Pose'; type = 'Integer'; default = 0 }, [ordered]@{ name = 'Speed'; type = 'Float'; default = 0.0 })
        states = @(
            [ordered]@{ name = 'Neutral'; clip = 1; speed = 1.0; loop = $true },
            [ordered]@{ name = 'Locomotion Slow'; clip = 2; speed = 1.0; loop = $true },    # ~3 m/s
            [ordered]@{ name = 'Locomotion'; clip = 2; speed = 2.0; loop = $true },         # ~6 m/s
            [ordered]@{ name = 'Locomotion Fast'; clip = 2; speed = 3.3; loop = $true },    # ~10 m/s
            [ordered]@{ name = 'Locomotion Run'; clip = 2; speed = 4.0; loop = $true },     # Unity's clamp (12+ m/s)
            [ordered]@{ name = 'Boost'; clip = 0; speed = 1.0; loop = $false },
            [ordered]@{ name = 'BoostStop'; clip = 0; speed = 1.0; loop = $false },
            [ordered]@{ name = 'Jump'; clip = 3; speed = 1.0; loop = $true })
        transitions = @(
            (AnyTo 5 @((C 0 'Equals' 2))),
            (AnyTo 6 @((C 0 'Equals' 3)) 0.6),
            (AnyTo 7 @((C 0 'Equals' 4))),
            (AnyTo 0 @((C 0 'Equals' 0))),
            (AnyTo 1 @((C 0 'Equals' 1), (C 1 'Less' 4.5))),
            (AnyTo 2 @((C 0 'Equals' 1), (C 1 'Greater' 4.5), (C 1 'Less' 7.5))),
            (AnyTo 3 @((C 0 'Equals' 1), (C 1 'Greater' 7.5), (C 1 'Less' 11.5))),
            (AnyTo 4 @((C 0 'Equals' 1), (C 1 'Greater' 11.5))))
    }
    Write-JsonFile (Join-Path $assets 'Animation\Player\Mech Legs.animcontroller') $legs
    $script:PC = @{ Legs = (Get-AssetGuid $assets 'Animation\Player\Mech Legs.animcontroller') }


    # --- Golem1 weapon rigs (golem2_v3.glb) and their Unity weapon animation sets ------------------------
    $script:PM.Golem2 = Get-AssetGuid $assets 'Models\golem2_v3.glb'
    $script:PM.Bolt = Get-AssetGuid $assets 'Models\CrossbowBolt.glb'
    $script:PM.Axe = Get-AssetGuid $assets 'Models\Golem1 Weapons\ThrowingAxe.glb'
    $script:PN.Rig = @{ CanonRig = 14; CrossBowArmRig = 18; CrossBowRig = 23; MaceArmRig = 27; SwordArmRig = 31; ThrowingAxeRig = 37 }
    # golem2_v3 clips (= Unity's Anim/Weapons .anim files): Neutral loops; Fire plays at its authored speed;
    # Reload (MechWeaponAnimator) is stretched over the rest of the attack interval: speed = clip / (cooldown - fire).
    # Cooldowns from MechPartCatalogue: crossbow arm max(1 / 0.6, 1.2) = 1.67 s -> reload 0.208 / 1.46 = 0.143.
    $weaponSets = @{
        'Sword'          = @{ Neutral = 16; Fire = 15 }
        'Mace'           = @{ Neutral = 14; Fire = 13 }
        'Throwing Axe'   = @{ Neutral = 18; Fire = 17 }
        'Crossbow Arm'   = @{ Neutral = 9; Fire = 8; Reload = 10; ReloadSpeed = 0.143 }
        'Large Crossbow' = @{ Neutral = 12; Fire = 11 }   # Unity's Crossbow_Reload is not in the GLB
    }
    # MechWeaponAnimator + MechWeaponIdle as a controller: Hold = Neutral's first frame (speed 0, every weapon
    # most of the time); Idle = Neutral playing (only the weapon MechWeaponIdle picks, Bool "Idle");
    # Fire (Trigger "Fire") at its authored speed, then Reload if the set has one, then back to Hold.
    # Blends 0.04 s (MechWeaponAnimationSet.blendDuration).
    function Write-WeaponController([string]$name, $neutral, $fire, $fireSpeed, $reload, $reloadSpeed) {
        $states = @(
            [ordered]@{ name = 'Hold'; clip = [uint64]$neutral; speed = 0.0; loop = $true },
            [ordered]@{ name = 'Idle'; clip = [uint64]$neutral; speed = 1.0; loop = $true },
            [ordered]@{ name = 'Fire'; clip = $fire; speed = [double]$fireSpeed; loop = $false })
        $params = @([ordered]@{ name = 'Fire'; type = 'Trigger'; default = $false }, [ordered]@{ name = 'Idle'; type = 'Bool'; default = $false })
        $edges = @(
            [ordered]@{ from = 'Any'; to = 2; duration = 0.04; canTransitionToSelf = $true; conditions = @((C 0 'If')) },
            [ordered]@{ from = 0; to = 1; duration = 0.04; conditions = @((C 1 'If')) },
            [ordered]@{ from = 1; to = 0; duration = 0.04; conditions = @((C 1 'IfNot')) })
        if ($null -ne $reload) {
            $states += [ordered]@{ name = 'Reload'; clip = $reload; speed = [double]$reloadSpeed; loop = $false }
            $edges += [ordered]@{ from = 2; to = 3; duration = 0.04; hasExitTime = $true; exitTime = 1.0; conditions = @() }
            $edges += [ordered]@{ from = 3; to = 0; duration = 0.04; hasExitTime = $true; exitTime = 1.0; conditions = @() }
        } else {
            $edges += [ordered]@{ from = 2; to = 0; duration = 0.04; hasExitTime = $true; exitTime = 1.0; conditions = @() }
        }
        $rel = "Animation\Player\Mech $name.animcontroller"
        Write-JsonFile (Join-Path $assets $rel) ([ordered]@{ version = 1; entryState = 0; parameters = $params; states = $states; transitions = $edges })
        Get-AssetGuid $assets $rel
    }
    foreach ($name in $weaponSets.Keys) {
        $set = $weaponSets[$name]
        $script:PC[$name] = Write-WeaponController $name $set.Neutral $set.Fire 1.0 $set.Reload $set.ReloadSpeed
    }
    # Cannon_Specialist: Fire = SHOULDER_Cannon clip 2 (1.6 s); its cooldown is the 3 s reload, so Unity plays it
    # at its authored speed. Its Neutral is a separate Unity .anim (not in the GLB): the rest pose here.
    $script:PC.Cannon = Write-WeaponController 'Cannon' 4294967295 2 1.0 $null 0

    # --- projectiles (Unity's Prefabs/Combat/Projectiles + the cannon's sphere) ---------------------------
    # The visual is half size in Unity, like every part; the bolt's tip is the model's -Z (the engine's
    # forward); the axe faces +Z, so it turns round, and spins 720 deg/s about X (Unity's angularVelocity).
    function Write-Projectile([string]$name, $visual, $spin = 0.0) {
        $p = New-EntityList 'PROJ' -Prefab
        $root = Add-Entity $p $name $null ([ordered]@{ ScriptComponent = (Script 'MechProjectile' @((FFloat 'spinDegreesPerSecond' $spin))) })
        [void](Add-Entity $p 'Visual' $root $visual)
        $rel = "Prefabs\Player\$name.cprefab"
        Write-JsonFile (Join-Path $assets $rel) ([ordered]@{ version = 3; name = $name; entities = $p.Entities })
        Get-AssetGuid $assets $rel
    }
    $v = 0.5 * $script:PartScale / 0.5   # Unity's 0.5, relative to the mech's scale
    $script:PP = @{
        Bolt   = (Write-Projectile 'Projectile Bolt' ([ordered]@{ Transform = (Xf (V3 0 0 0) (V3 $v $v $v)); MeshRenderer = (ModelRenderer $PM.Bolt) }))
        Axe    = (Write-Projectile 'Projectile Axe' ([ordered]@{ Transform = (Xf (V3 0 0 0) (V3 $v $v $v) (QYaw 180)); MeshRenderer = (ModelRenderer $PM.Axe) }) 720.0)
        Cannon = (Write-Projectile 'Projectile Cannon' ([ordered]@{ Transform = (Xf (V3 0 0 0) (V3 0.66 0.66 0.66)); MeshRenderer = (Mesh $Builtin.Sphere 1 0.5 0.15) }))
    }

    # A golem weapon part: the rig (rebased) posed by Unity's chain; muzzle on a rig node; socket attachments.
    function GolemWeapon($rig, $set, $sound, $projectile, $lift, $labelH, $visualRig, $rigLocal, $muzzle = $null, $attachments = $null) {
        $poses = @()
        if ($attachments) {
            foreach ($socket in $attachments.Keys) {
                $a = $attachments[$socket]; $t = Get-UnityAttachmentPose $a[0] $a[1] $a[2] $a[3] $a[4] $a[5]
                # Invariant culture: MechPart parses these with '.' decimals.
                $poses += [string]::Format([cultureinfo]::InvariantCulture, '{0}:{1:R},{2:R},{3:R},{4:R},{5:R},{6:R},{7:R}',
                    $socket, $t.Pos.x, $t.Pos.y, $t.Pos.z, $t.Rot.x, $t.Rot.y, $t.Rot.z, $t.Rot.w)
            }
        }
        @{ Glb = $PM.Golem2; Root = $PN.Rig[$rig]; Controller = $PC[$set]; MountFrame = $true; Lift = $lift; LabelH = $labelH; Sound = $sound
           Projectile = $projectile; ModelPose = (Get-UnityWeaponModelPose $visualRig $rigLocal); Muzzle = $muzzle
           SocketPoses = ($poses -join ';') }
    }

    # --- what each part looks like, in the frame of the socket it mounts on ------------------------------
    # The specialist Mount_* frame is Z up with the weapon pointing along -Y; an arm part hangs below
    # (GlbBounds.ps1 measures it). Parts with no GLB yet are placeholder primitives in that frame.
    # Lift = height of the root above the ground so the part rests on it. Sound = Unity's attackSound.
    function Piece($mesh, $pos, $scale, $col) { @{ Mesh = $mesh; Pos = $pos; Scale = $scale; Colour = $col } }
    $wood = @(0.45, 0.3, 0.18); $darkWood = @(0.3, 0.2, 0.12); $steel = @(0.78, 0.8, 0.84); $iron = @(0.35, 0.35, 0.38); $bronze = @(0.6, 0.45, 0.2)
    $script:Looks = @{
        cannon         = @{ Glb = $PM.Cannon; Root = $PN.CannonMount; Controller = $PC.Cannon; MountFrame = $true; Lift = 0.15; LabelH = 3.5; Sound = 'Cannon_attack'
                            Projectile = 'Cannon'; Muzzle = @{ Node = 3; Pos = (V3 0 0 0) } }   # Marker_Muzzle
        golem_head     = @{ Glb = $PM.Head; Root = $PN.HeadMount; Clip = 0; MountFrame = $true; Lift = 0.25; LabelH = 2.5 }   # HeadSpecialist_Idle
        golem_legs     = @{ Glb = $PM.Legs; Root = $InvalidIndex; MountFrame = $false; Lift = 0.0; LabelH = 6.5 }
        fast_legs      = @{ Glb = $PM.Legs; Root = $InvalidIndex; MountFrame = $false; Lift = 0.0; LabelH = 6.5 }
        heavy_legs     = @{ Glb = $PM.Legs; Root = $InvalidIndex; MountFrame = $false; Lift = 0.0; LabelH = 6.5 }
        # Golem1 weapons: their animated rigs in golem2_v3.glb (Unity's Anim/Weapons clips are the same
        # bones and lengths). Model = the rig, posed on the socket by Unity's chain (UnityMount.ps1).
        sword          = (GolemWeapon 'SwordArmRig' 'Sword' 'StoneSword_attack' $null 0.76 1.2 `
                            (UT (UV -0.21105832 -3.560719 2.6860807) (UQ -0.51563185 -0.55239975 0.4513548 0.4746126)) (UT (UV 2.6789262 0 0) (UQ 0 0 0 1)))
        mace           = (GolemWeapon 'MaceArmRig' 'Mace' 'Mace_attack' $null 1.82 1.5 `
                            (UT (UV 0.48144925 -1.6021278 0.17955756) (UQ 0.062794715 -0.70539147 0.030046728 0.70539135)) (UT (UV 2.8563287 2.6732314 0) (UQ 0 0 0 1) 0.7088742))
        throwing_axe   = (GolemWeapon 'ThrowingAxeRig' 'Throwing Axe' 'ThrowingAxe_attack' 'Axe' 2.16 1.1 `
                            (UT (UV -2.4103804 -3.5550857 0.92847836) (UQ -0.000000011979292 -0.98835 0.000000003848606 0.15219812)) (UT (UV -2.6407213 2.6878393 0) (UQ 0 0 0 1) 0.75524205) `
                            @{ Node = 33; Pos = (V3 0 0 0) })   # Barrel: where the loaded axe sits
        small_crossbow = (GolemWeapon 'CrossBowArmRig' 'Crossbow Arm' 'CrossbowArm_attack' 'Bolt' 1.26 0.5 `
                            (UT (UV 0.39409956 -4.2521887 2.7717547) (UQ -0.04507756 -0.70566857 -0.045077566 0.70566845)) (UT (UV -2.764666 3.1298048 -0.24413073) (UQ -0.6992517 0 0 0.7148756)) `
                            @{ Node = 16; Pos = (V3 -0.040878534 1.7930981 0.16400842) } `
                            @{ ArmL = @(-1.1528591, 0.66340816, -0.007088081, 0.0000066548937, -0.0000070149827, 6.044073); ArmR = @(-1.1528591, 0.66340816, -0.007088081, 0.0000066548937, -0.0000070149827, 6.044073) })
        large_crossbow = (GolemWeapon 'CrossBowRig' 'Large Crossbow' 'Crossbow_attack' 'Bolt' -0.6 3.8 `
                            (UT (UV 0.855424 -4.283654 -2.8880484) (UQ 0.054690953 -0.7049887 -0.054690767 0.7049885)) (UT (UV 2.2358465 6.6864476 1.2233881) (UQ -0.6946785 0 0 0.7193205)) `
                            @{ Node = 21; Pos = (V3 -0.008011181 4.3538947 0.119458154) } `
                            @{ ShoulderL = @(0.29793274, -0.8212483, 0.8583476, 351.13354, 0.3116413, 357.97903); ShoulderR = @(0.2979325, -0.82124853, -0.94435114, 351.13354, 0.3116413, 357.97903) })
    }
}

function QMul($a, $b) {   # Hamilton product, components x y z w (a * b applies b first)
    Q ($a.w*$b.x + $a.x*$b.w + $a.y*$b.z - $a.z*$b.y) ($a.w*$b.y - $a.x*$b.z + $a.y*$b.w + $a.z*$b.x) `
      ($a.w*$b.z + $a.x*$b.y - $a.y*$b.x + $a.z*$b.w) ($a.w*$b.w - $a.x*$b.x - $a.y*$b.y - $a.z*$b.z)
}

# A MechPart: root (pickup trigger, script, attack voice) with Model children and a Label.
# $groundPoint = $null: mounted (identity local transform under a mount). Otherwise it rests there, upright.
function Add-Part($list, [string]$name, $parent, [string]$partId, $groundPoint, $yaw = 90) {
    $look = $script:Looks[$partId]
    if (-not $look) { throw "No look for part '$partId' in SceneGen.Player.ps1" }
    $s = $script:PartScale
    if ($groundPoint) {
        $rot = QYaw $yaw
        if ($look.MountFrame) { $rot = QMul $rot (QPitch -90) }
        $t = Xf (V3 $groundPoint.x ($groundPoint.y + $look.Lift) $groundPoint.z) (V3 $s $s $s) $rot
    } else { $t = Xf (V3 0 0 0) }
    $fields = @((FString 'partId' $partId), (FFloat 'groundScale' $s), (FFloat 'mountedScale' 1.0), (FFloat 'groundLift' $look.Lift), (FBool 'mountFrame' $look.MountFrame))
    $c = [ordered]@{ Transform = $t; Collider = (SphereCollider (2.2 / $s) $true) }
    if ($look.Projectile) { $fields += (FAsset 'projectile' 'Prefab' $script:PP[$look.Projectile]) }
    # WeaponVisualSettings.firingShake / firingShakeDuration (arm weapons 0.02 / 0.12, MechPart's default).
    $shake = @{ large_crossbow = @(0.04, 0.18); cannon = @(0.075, 0.26) }[$partId]
    if ($shake) { $fields += (FFloat 'firingShake' $shake[0]); $fields += (FFloat 'firingShakeDuration' $shake[1]) }
    if ($look.SocketPoses) { $fields += (FString 'socketPoses' $look.SocketPoses) }
    if ($look.Sound) {
        $fields += (FAsset 'attackSound' 'AudioClip' $script:PA[$look.Sound])
        $voice = AudioSourceComp $script:PA[$look.Sound]
        $voice.spatialBlend = 0.35; $voice.minDistance = 4.0; $voice.maxDistance = 40.0   # MechWeaponAnimator's voice
        $c.AudioSource = $voice
    }
    $c.ScriptComponent = (Script 'MechPart' $fields)
    $e = Add-Entity $list $name $parent $c
    if ($look.Glb) {
        $m = [ordered]@{}
        if ($look.ModelPose) { $mp = $look.ModelPose; $m.Transform = (Xf $mp.Pos (V3 $mp.Scale $mp.Scale $mp.Scale) $mp.Rot) }
        $m.MeshRenderer = (ModelRenderer $look.Glb $look.Root)
        if ($look.Controller) { $m.AnimatedModel = (Anim $look.Controller 0 $true) }
        elseif ($null -ne $look.Clip) { $m.AnimatedModel = (Anim 0 $look.Clip $true) }
        $model = Add-Entity $list 'Model' $e $m
        # Muzzle: follows a node of the (animated) weapon model; MechPart.MuzzlePosition reads it.
        if ($look.Muzzle) {
            [void](Add-Entity $list 'Muzzle' $model ([ordered]@{ Transform = (Xf $look.Muzzle.Pos); SocketAttachment = (Socket $look.Glb $look.Muzzle.Node 'Muzzle') }))
        }
    }
    $n = 0
    foreach ($piece in $look.Pieces) {
        $n++
        [void](Add-Entity $list "Model $n" $e ([ordered]@{ Transform = (Xf $piece.Pos $piece.Scale); MeshRenderer = (Mesh $piece.Mesh $piece.Colour[0] $piece.Colour[1] $piece.Colour[2]) }))
    }
    # Label above the part: "up" is +Z in a mount frame, +Y otherwise; scaled back to world size.
    $up = if ($look.MountFrame) { V3 0 0 $look.LabelH } else { V3 0 $look.LabelH 0 }
    $inv = 1 / $s
    $labelRot = if ($look.MountFrame) { QPitch 90 } else { Q 0 0 0 1 }
    [void](Add-Entity $list 'Label' $e ([ordered]@{ Transform = (Xf $up (V3 $inv $inv $inv) $labelRot); WorldText = (WText '' 0.32 (V4 1 1 1 1) (V2 4 1.2)) }))
    return $e
}

# The player: capsule controller + camera rig + the specialist model with sockets and Unity's starting loadout
# (Golem Head and the Sword in the left arm on mounts; Golem Torso / Fast Legs from MechPlayer's start* fields).
function Add-PlayerRig($list, $position) {
    $s = $script:PartScale
    $player = Add-Entity $list 'Player' $null ([ordered]@{
        Transform = (Xf $position)
        CharacterController = [ordered]@{ height = 3.4; radius = 0.9; center = (V3 0 0 0); slopeLimit = 45.0; stepOffset = 0.4; pushStrength = 100.0; layer = 'Player' }
        ScriptComponent = (Script 'MechPlayer')
    })
    $pivot = Add-Entity $list 'Camera Pivot' $player ([ordered]@{
        Transform = (Xf (V3 0 1.8 0))
        CameraOrbit = [ordered]@{ sensitivity = 0.1; stickSpeed = 180.0; minPitch = -18.0; maxPitch = 30.0; yaw = 0.0; pitch = 12.0 }
    })
    [void](Add-Entity $list 'Player Camera' $pivot ([ordered]@{
        Transform = (Xf (V3 1.4 0.6 9))
        AudioListener = [ordered]@{}
        Camera = [ordered]@{ enabled = $true; fovY = 60.0; nearZ = 0.1; farZ = 1000.0; priority = 1 }
    }))
    # Feet at the capsule's bottom, turned to face the engine's -Z (glTF faces +Z), scaled to the capsule.
    $visual = Add-Entity $list 'Mech Visual' $player ([ordered]@{ Transform = (Xf (V3 0 -1.7 0) (V3 $s $s $s) (QYaw 180)) })
    $legsVoice = AudioSourceComp $script:PA.Golem1Legs_Move
    $legsVoice.loop = $true; $legsVoice.volume = 0.3; $legsVoice.spatialBlend = 0.35; $legsVoice.minDistance = 4.0   # MechMovementAudio
    $legs = Add-Entity $list 'Legs' $visual ([ordered]@{ MeshRenderer = (ModelRenderer $script:PM.Legs); AnimatedModel = (Anim $script:PC.Legs 1 $true); AudioSource = $legsVoice })
    # Mount_Hip as the part root: the torso is drawn with that node on the legs' Socket_Hip.
    $torso = Add-Entity $list 'Torso' $legs ([ordered]@{
        SocketAttachment = (Socket $script:PM.Legs $script:PN.LegsSocketHip 'Hip')
        MeshRenderer = (ModelRenderer $script:PM.Torso $script:PN.TorsoMountHip)
        AnimatedModel = (Anim 0 2 $true)   # TorsoSpecialist_Idle (Unity's Torso_Specialist.controller)
    })
    $mountHead = Add-Entity $list 'Mount Head' $torso ([ordered]@{ SocketAttachment = (Socket $script:PM.Torso $script:PN.TorsoSocketHead 'Head') })
    [void](Add-Entity $list 'Mount ShoulderL' $torso ([ordered]@{ SocketAttachment = (Socket $script:PM.Torso $script:PN.TorsoShoulderL 'ShoulderL') }))
    [void](Add-Entity $list 'Mount ShoulderR' $torso ([ordered]@{ SocketAttachment = (Socket $script:PM.Torso $script:PN.TorsoShoulderR 'ShoulderR') }))
    $mountAL = Add-Entity $list 'Mount ArmL' $torso ([ordered]@{ SocketAttachment = (Socket $script:PM.Torso $script:PN.TorsoArmL 'ArmL') })
    [void](Add-Entity $list 'Mount ArmR' $torso ([ordered]@{ SocketAttachment = (Socket $script:PM.Torso $script:PN.TorsoArmR 'ArmR') }))
    [void](Add-Part $list 'Part Head' $mountHead 'golem_head' $null)
    [void](Add-Part $list 'Part Sword' $mountAL 'sword' $null)
    return $player
}

# Unity rect (y up) -> engine rect (y down): anchors and pivot flip in y, the position's y is negated, size stays.
function URect($aMinX, $aMinY, $aMaxX, $aMaxY, $pX, $pY, $x, $y, $w, $h) {
    Rect (V2 $aMinX (1 - $aMaxY)) (V2 $aMaxX (1 - $aMinY)) (V2 $pX (1 - $pY)) (V2 $x (-$y)) (V2 $w $h)
}
function Sprite($guid, $r = 1, $g = 1, $b = 1, $a = 1) { [ordered]@{ colour = (V4 $r $g $b $a); textureAsset = [uint64]$guid } }

# Unity's Mech Cursor Canvas (1920 x 1080): Player Health HUD, CombatHUD (WeaponsHUD / AbilitiesHUD pods),
# Cursor; plus the PlayerDamageVignette edges, a controls line and the ` movement debug readout.
function Add-UnityHud($list, [string]$controlsText) {
    $T = $script:PT
    $cream = V4 0.94 0.93 0.86 1
    $hud = Add-Entity $list 'Player HUD' $null ([ordered]@{
        UICanvas = (Canvas)
        ScriptComponent = (Script 'MechHud' @(
            (FAsset 'iconSword' 'Texture' $T.SwordIcon), (FAsset 'iconMace' 'Texture' $T.MacecIcon), (FAsset 'iconThrowingAxe' 'Texture' $T.ThrowingAxeIcon),
            (FAsset 'iconCrossbowArm' 'Texture' $T.CrossbowArmIcon), (FAsset 'iconLargeCrossbow' 'Texture' $T.GiantCrossbowIcon), (FAsset 'iconCannon' 'Texture' $T.CannonIcon),
            (FAsset 'abilityIconHead' 'Texture' $T.HealIcon), (FAsset 'abilityIconTorso' 'Texture' $T.DamageAmpIcon), (FAsset 'abilityIconLegs' 'Texture' $T.JumpIcon),
            (FAsset 'cursorFree' 'Texture' $T.Cursor), (FAsset 'cursorLockOn' 'Texture' $T.CursorLockon), (FAsset 'cursorCamLock' 'Texture' $T.CursorCamLockon)))
    })
    foreach ($edge in @(@('Top', (V2 0 0), (V2 1 0), (V2 0 0), (V2 0 90)), @('Bottom', (V2 0 1), (V2 1 1), (V2 0 1), (V2 0 90)),
                        @('Left', (V2 0 0), (V2 0 1), (V2 0 0), (V2 90 0)), @('Right', (V2 1 0), (V2 1 1), (V2 1 0), (V2 90 0)))) {
        [void](Add-Entity $list ('Vignette ' + $edge[0]) $hud ([ordered]@{ UIRect = (Rect $edge[1] $edge[2] $edge[3] (V2 0 0) $edge[4]); UIImage = (Img 0.65 0.015 0.015 0) }))
    }

    # Cursor.prefab: the 700 x 700 reticle at the screen centre (free aim).
    [void](Add-Entity $list 'Cursor' $hud ([ordered]@{ UIRect = (URect 0.5 0.5 0.5 0.5 0.5 0.5 0 0 700 700); UIImage = (Sprite $T.Cursor 1 1 1 0.3529412) }))

    # Player Health HUD.prefab: bottom-left at (43, 47), 360 x 44.8; glow behind, then back, white, front.
    $health = Add-Entity $list 'Health HUD' $hud ([ordered]@{ UIRect = (URect 0 0 0 0 0 0 43 47 360 44.81328) })
    [void](Add-Entity $list 'Glow' $health ([ordered]@{ UIRect = (URect 0 0 0 0 0 0.5 -25.394196 22.40664 410.7884 116.51453); UIImage = (Sprite $T.HpBarGlow 1 1 1 0.3) }))
    [void](Add-Entity $list 'Bar Back' $health ([ordered]@{ UIRect = (URect 0 0 0 0 0 0 0 0 360 44.81328); UIImage = (Sprite $T.HpBarBack) }))
    [void](Add-Entity $list 'Bar White' $health ([ordered]@{ UIRect = (URect 0 0 0 0 0 0 0 0 360 44.81328); UIImage = (Sprite $T.HpBarWhite) }))
    [void](Add-Entity $list 'Bar Front' $health ([ordered]@{ UIRect = (URect 0 0 0 0 0 0 0 0 360 44.81328); UIImage = (Sprite $T.HpBar 0.3 0.95 0.5 1) }))

    # CombatHUD.prefab WeaponsHUD: top-right anchored at (-24, -875), 368 x 178; one 180 x 86 pod per corner cell.
    $weapons = Add-Entity $list 'Weapons HUD' $hud ([ordered]@{ UIRect = (URect 1 1 1 1 1 1 -24 -875 368 178) })
    $cells = @(@('Q', 0, 1, 0, 1), @('E', 1, 1, 1, 1), @('LMB', 0, 0, 0, 0), @('RMB', 1, 0, 1, 0))   # key, anchor x/y, pivot x/y (Unity)
    foreach ($cell in $cells) {
        $pod = Add-Entity $list ('Weapon ' + $cell[0]) $weapons ([ordered]@{ UIRect = (URect $cell[1] $cell[2] $cell[1] $cell[2] $cell[3] $cell[4] 0 0 180 86) })
        # WeaponsHUDPodPrefab (its stretched children resolved to the 180 x 86 pod).
        $bg = Add-Entity $list 'Background' $pod ([ordered]@{ UIRect = (TL 0 0 180 86); UIImage = (Sprite $T.WeaponsHUDBG 0.05 0.07 0.1 1) })
        $icon = Add-Entity $list 'Icon' $bg ([ordered]@{ UIRect = (TL 8 20 52 58); UIImage = (Img 1 1 1 0.15) })
        [void](Add-Entity $list 'Sweep Line' $icon ([ordered]@{ UIRect = (Rect (V2 0 0) (V2 1 0) (V2 0.5 0.5) (V2 0 58) (V2 0 2)); UIImage = (Img 1 0.93 0.6 0) }))
        [void](Add-Entity $list 'Ammo' $bg ([ordered]@{ UIRect = (TL 66 20 108 58); UIText = (Txt 'N/A' 20 $cream 'Center' 'Middle') }))
        [void](Add-Entity $list 'Glow' $pod ([ordered]@{ UIRect = (TL 60.2 18.55 117.2 63.1); UIImage = (Sprite $T.WeaponsHUDGlow 0.5943396 0.76558256 1 0.32941177) }))
        [void](Add-Entity $list 'Name' $pod ([ordered]@{ UIRect = (TL 2.1 2 168 18); UIText = (Txt 'WEAPON' 13 $cream 'Left' 'Middle' $true $false) }))
        [void](Add-Entity $list 'Key' $pod ([ordered]@{ UIRect = (TL 3.5 2 168 18); UIText = (Txt $cell[0] 13 $cream 'Right' 'Middle' $true $false) }))
    }

    # CombatHUD.prefab AbilitiesHUD: top-right anchored at (-404, -913), 310 x 110; pods 90 x 90, 20 apart,
    # bottom-right aligned (MechAbilitiesHud.Layout), with the key chip 55 above each pod's centre.
    $abilities = Add-Entity $list 'Abilities HUD' $hud ([ordered]@{ UIRect = (URect 1 1 1 1 1 1 -404 -913 310 110) })
    $keys = @('Tab', 'Ctrl', 'Spc')
    for ($i = 0; $i -lt 3; $i++) {
        $pod = Add-Entity $list ('Ability ' + $keys[$i]) $abilities ([ordered]@{ UIRect = (URect 1 0 1 0 1 0 (-(2 - $i) * 110) 0 90 90) })
        $bg = Add-Entity $list 'Background' $pod ([ordered]@{ UIRect = (TL 0 0 90 90); UIImage = (Img 0.05 0.07 0.1 0.85) })
        $icon = Add-Entity $list 'Icon' $bg ([ordered]@{ UIRect = (TL 5 5 80 80); UIImage = (Img 1 1 1 0.15) })
        [void](Add-Entity $list 'Sweep Line' $icon ([ordered]@{ UIRect = (Rect (V2 0 0) (V2 1 0) (V2 0.5 0.5) (V2 0 80) (V2 0 2)); UIImage = (Img 1 0.93 0.6 0) }))
        [void](Add-Entity $list 'Cooldown' $pod ([ordered]@{ UIRect = (TL 5 5 80 80); UIText = (Txt '' 50 (V4 1 1 1 0) 'Center' 'Middle' $true) }))
        $chip = Add-Entity $list 'Key Chip' $pod ([ordered]@{ UIRect = (TL 20 -20 50 20); UIImage = (Img 0.18823531 0.19215688 0.20392159 0.9490196) })
        [void](Add-Entity $list 'Key' $chip ([ordered]@{ UIRect = (TL 0 0 50 20); UIText = (Txt $keys[$i] 14 (V4 1 1 1 1) 'Center' 'Middle' $true $false) }))
    }

    [void](Add-Entity $list 'Controls' $hud ([ordered]@{ UIRect = (TL 24 20 1100 70); UIText = (Txt $controlsText 18 (V4 1 1 1 0.85) 'Left' 'Top') }))
    [void](Add-Entity $list 'Movement Debug' $hud ([ordered]@{ UIRect = (TL 24 110 700 200); UIText = (Txt '' 18 (V4 0.6 1 0.85 1) 'Left' 'Top') }))
    return $hud
}

$script:PlayerControls = "WASD move  |  Shift dodge  |  Space jump  |  walk 5 s to auto-run`n" +
                         "LMB / RMB arms  |  Q / E shoulders  |  R reload  |  F lock-on (flick to switch)  |  Tab / Ctrl abilities`n" +
                         "Near parts: 1 left / only slot, 2 right slot, V cycle  |  K damage  |  H heal  |  `` debug  |  Esc free mouse"
