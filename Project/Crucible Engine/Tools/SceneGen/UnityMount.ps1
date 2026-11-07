# UnityMount.ps1 - converts Unity's golem-weapon mounting chain into engine transforms.
#
# In Unity a Golem1 weapon part is placed at the specialist's socket object, then
#     socket object -> [Socket.weaponAttachments pose] -> part root -> "Weapon Visual Rig" -> <Rig> (golem2 rig)
# The engine mounts parts on the GLB's Socket_* node instead (Mount_* frame). For the specialist parts Unity's
# Model_Holder (+90 Y) and FBX offset (-90 X) put the Mount_* node exactly at the socket object's origin, so
#     socket object = Mount frame * A^-1,   A = Ry(90) * Rx(-90)       (Unity space)
# Unity space is the engine's mirrored in X: position (-x, y, z), rotation (x, -y, -z, w).
# Dot-source it; Get-UnityWeaponModelPose / Get-UnityAttachmentPose return @{ Pos = V3; Rot = Q } (engine).

function UQ([double]$x, [double]$y, [double]$z, [double]$w) { @{ x = $x; y = $y; z = $z; w = $w } }
function UV([double]$x, [double]$y, [double]$z) { @{ x = $x; y = $y; z = $z } }
function UQMul($a, $b) {
    UQ ($a.w*$b.x + $a.x*$b.w + $a.y*$b.z - $a.z*$b.y) ($a.w*$b.y - $a.x*$b.z + $a.y*$b.w + $a.z*$b.x) `
       ($a.w*$b.z + $a.x*$b.y - $a.y*$b.x + $a.z*$b.w) ($a.w*$b.w - $a.x*$b.x - $a.y*$b.y - $a.z*$b.z)
}
function UQInv($q) { UQ (-$q.x) (-$q.y) (-$q.z) $q.w }
function UQRotate($q, $v) {
    $p = UQMul (UQMul $q (UQ $v.x $v.y $v.z 0)) (UQInv $q)
    UV $p.x $p.y $p.z
}
function UAxis([double]$deg, [string]$axis) {
    $h = [math]::PI * $deg / 360.0; $s = [math]::Sin($h); $c = [math]::Cos($h)
    switch ($axis) { 'x' { UQ $s 0 0 $c } 'y' { UQ 0 $s 0 $c } default { UQ 0 0 $s $c } }
}
# Unity's Quaternion.Euler: Z first, then X, then Y.
function UEuler([double]$x, [double]$y, [double]$z) { UQMul (UQMul (UAxis $y 'y') (UAxis $x 'x')) (UAxis $z 'z') }
# A transform is @{ P = UV; R = UQ; S = scalar } (uniform scale).
function UT($p, $r, [double]$s = 1.0) { @{ P = $p; R = $r; S = $s } }
function UTMul($a, $b) {   # a then b in a's space (parent * child)
    $bp = UQRotate $a.R (UV ($b.P.x * $a.S) ($b.P.y * $a.S) ($b.P.z * $a.S))
    UT (UV ($a.P.x + $bp.x) ($a.P.y + $bp.y) ($a.P.z + $bp.z)) (UQMul $a.R $b.R) ($a.S * $b.S)
}
function UTInv($t) {
    $ri = UQInv $t.R; $si = 1.0 / $t.S
    $p = UQRotate $ri (UV (-$t.P.x * $si) (-$t.P.y * $si) (-$t.P.z * $si))
    UT $p $ri $si
}
function ToEngine($t) {   # mirror X
    @{ Pos = (V3 (-$t.P.x) $t.P.y $t.P.z); Rot = (Q $t.R.x (-$t.R.y) (-$t.R.z) $t.R.w); Scale = $t.S }
}

# A: the Mount frame relative to Unity's socket object (Model_Holder (0,-0.7071,0,-0.7071) * Rx(-90)).
$script:UnityA = UT (UV 0 0 0) (UQMul (UQ 0 -0.7071068 0 -0.7071068) (UQ -0.7071068 0 0 0.7071067))

# The Model child of a golem weapon part (modelRoot = its rig node, rebased): A^-1 * VisualRig * Rig.
function Get-UnityWeaponModelPose($visualRig, $rig) {
    ToEngine (UTMul (UTMul (UTInv $script:UnityA) $visualRig) $rig)
}
# The part root under a mount, for a socket with a Unity weapon attachment: A^-1 * Att * A.
function Get-UnityAttachmentPose($posX, $posY, $posZ, $eulerX, $eulerY, $eulerZ) {
    $att = UT (UV $posX $posY $posZ) (UEuler $eulerX $eulerY $eulerZ)
    ToEngine (UTMul (UTMul (UTInv $script:UnityA) $att) $script:UnityA)
}
