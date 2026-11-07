# GlbBounds.ps1 - prints the bounding box of a GLB's meshes in the frame of one of its nodes (e.g. a
# Mount_* node), i.e. where the geometry ends up relative to the socket it is mounted on.
#     pwsh -File Tools/SceneGen/GlbBounds.ps1 "Assets/Models/Weapon model/ARM_Blaster.glb" Mount_Arm
#     pwsh -File Tools/SceneGen/GlbBounds.ps1 "Assets/Models/golem2_v3.glb" SwordArmRig SwordArm_Neutral   # in that clip's first pose
param([Parameter(Mandatory)][string]$Glb, [Parameter(Mandatory)][string]$FrameNode, [string]$Clip)
$b = [IO.File]::ReadAllBytes((Resolve-Path $Glb))
$jsonLength = [BitConverter]::ToUInt32($b, 12)
$o = [Text.Encoding]::UTF8.GetString($b, 20, $jsonLength) | ConvertFrom-Json
if ($Clip) {
    # Pose the nodes at the clip's first key (translation / rotation / scale channels).
    $anim = $o.animations | Where-Object { $_.name -eq $Clip } | Select-Object -First 1
    if (-not $anim) { throw "No clip named $Clip" }
    $bin = 20 + $jsonLength + 8
    foreach ($ch in $anim.channels) {
        $acc = $o.accessors[$anim.samplers[$ch.sampler].output]
        $view = $o.bufferViews[$acc.bufferView]
        $at = $bin + [int]$view.byteOffset + [int]$acc.byteOffset
        $n = if ($ch.target.path -eq 'rotation') { 4 } else { 3 }
        $v = @(); for ($k = 0; $k -lt $n; $k++) { $v += [BitConverter]::ToSingle($b, $at + 4 * $k) }
        $node = $o.nodes[$ch.target.node]
        $node | Add-Member -NotePropertyName $ch.target.path -NotePropertyValue $v -Force
    }
}

function Mat($n) {   # column-vector 4x4 from TRS (glTF order T * R * S)
    if ($n.matrix) { $m = $n.matrix; return ,@(@($m[0],$m[4],$m[8],$m[12]), @($m[1],$m[5],$m[9],$m[13]), @($m[2],$m[6],$m[10],$m[14]), @(0,0,0,1)) }
    $t = if ($n.translation) { $n.translation } else { @(0,0,0) }
    $q = if ($n.rotation) { $n.rotation } else { @(0,0,0,1) }
    $s = if ($n.scale) { $n.scale } else { @(1,1,1) }
    $x,$y,$z,$w = $q
    $r = @(@((1-2*($y*$y+$z*$z)), (2*($x*$y-$z*$w)), (2*($x*$z+$y*$w))),
           @((2*($x*$y+$z*$w)), (1-2*($x*$x+$z*$z)), (2*($y*$z-$x*$w))),
           @((2*($x*$z-$y*$w)), (2*($y*$z+$x*$w)), (1-2*($x*$x+$y*$y))))
    return ,@(@(($r[0][0]*$s[0]),($r[0][1]*$s[1]),($r[0][2]*$s[2]),$t[0]),
              @(($r[1][0]*$s[0]),($r[1][1]*$s[1]),($r[1][2]*$s[2]),$t[1]),
              @(($r[2][0]*$s[0]),($r[2][1]*$s[1]),($r[2][2]*$s[2]),$t[2]), @(0,0,0,1))
}
function Mul($a, $c) { $r = @(); for ($i=0;$i -lt 4;$i++){ $row=@(); for($j=0;$j -lt 4;$j++){ $v=0; for($k=0;$k -lt 4;$k++){ $v += $a[$i][$k]*$c[$k][$j] }; $row += $v }; $r += ,$row }; return ,$r }
function Ident { ,@(@(1,0,0,0),@(0,1,0,0),@(0,0,1,0),@(0,0,0,1)) }

$parent = @{}
for ($i = 0; $i -lt $o.nodes.Count; $i++) { foreach ($c in @($o.nodes[$i].children)) { if ($null -ne $c) { $parent[[int]$c] = $i } } }
$frame = -1; for ($i = 0; $i -lt $o.nodes.Count; $i++) { if ($o.nodes[$i].name -eq $FrameNode) { $frame = $i } }
if ($frame -lt 0) { throw "No node named $FrameNode" }
# Global of node i relative to the frame node (only for descendants of the frame).
function Rel($i) { if ($i -eq $frame) { return Ident }; if (-not $parent.ContainsKey($i)) { return $null }; $p = Rel $parent[$i]; if ($null -eq $p) { return $null }; return ,(Mul $p (Mat $o.nodes[$i])) }

$min = @([double]::MaxValue,[double]::MaxValue,[double]::MaxValue); $max = @([double]::MinValue,[double]::MinValue,[double]::MinValue)
for ($i = 0; $i -lt $o.nodes.Count; $i++) {
    $n = $o.nodes[$i]; if ($null -eq $n.mesh) { continue }
    $m = Rel $i; if ($null -eq $m) { continue }
    foreach ($p in $o.meshes[$n.mesh].primitives) {
        $acc = $o.accessors[$p.attributes.POSITION]
        foreach ($cx in $acc.min[0],$acc.max[0]) { foreach ($cy in $acc.min[1],$acc.max[1]) { foreach ($cz in $acc.min[2],$acc.max[2]) {
            for ($k = 0; $k -lt 3; $k++) {
                $v = $m[$k][0]*$cx + $m[$k][1]*$cy + $m[$k][2]*$cz + $m[$k][3]
                if ($v -lt $min[$k]) { $min[$k] = $v }; if ($v -gt $max[$k]) { $max[$k] = $v }
            } } } }
    }
}
"{0} in {1} frame: min=({2:N2}, {3:N2}, {4:N2}) max=({5:N2}, {6:N2}, {7:N2})" -f (Split-Path $Glb -Leaf), $FrameNode, $min[0],$min[1],$min[2],$max[0],$max[1],$max[2]
"centre=({0:N2}, {1:N2}, {2:N2}) size=({3:N2}, {4:N2}, {5:N2})" -f (($min[0]+$max[0])/2),(($min[1]+$max[1])/2),(($min[2]+$max[2])/2),($max[0]-$min[0]),($max[1]-$min[1]),($max[2]-$min[2])
