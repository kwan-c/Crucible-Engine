# Milestone 7: Modular animations and weapons

Milestone 7 completes the seven-milestone animation roadmap. A locomotion entity
and its equipped weapons use separate AnimatedModel/AnimatorInstance values.
They share immutable compiled resources and GPU meshes/materials, with independent
clocks, transitions, parameters, poses and palettes. No combined locomotion/fire
clips, combined exports, or ECS entity for every bone are required.

## Try it in the editor

Build Editor in Debug/x64. Open **Assets/Scenes/Modular Animation Preview.scene**
in the asset browser, or launch from `build/Editor/Debug`:

```powershell
.\Editor.exe --scene "Scenes/Modular Animation Preview.scene"
```

The two mechs use the existing locomotion controller. Each has a child weapon
with its own controller: Cannon Demo or Crossbow Arm Demo. The weapons cycle
Neutral -> Fire -> Reload -> Neutral at different speeds. Each has a small
child sphere named by its SocketAttachment alias, Muzzle, following a weapon node.
The scene uses existing orange/greenish material assets without modifying them.

1. Select either mech. MeshRenderer's **Model part root** is node 10, Armature.
   AnimatedModel still provides the locomotion controller and playback controls.
2. Expand its child weapon. MeshRenderer draws only its weapon subtree; its
   normal global/slot material controls remain available. AnimatedModel owns a
   different controller, so pausing this weapon leaves the mech moving.
3. In the weapon's Runtime Parameters, switch **AutoFire** off. Let its current
   action finish, then press the **Fire** trigger to play another action. AutoFire
   and Fire are example asset parameters, not engine-defined gameplay names.
4. Inspect SocketAttachment: its parent model node is Body, index 3. Changing
   the weapon's Transform adjusts its mount alignment in that animated node's
   coordinates. SocketAttachment.socketName is an optional gameplay alias.
5. Expand the weapon's Muzzle child. It follows a node of the weapon's own pose,
   demonstrating nested attachments rather than just following the mech entity.
6. Try Play/Pause/Stop, save/load, duplicate a mech subtree, or move its mount with
   the Scene view gizmo. **Detach socket (keep world pose)** unparents and removes
   the binding, preserving a representable world TRS; Undo/Redo restores both.

The standalone Cannon/CrossbowArm files contain no clips. This preview therefore
selects independently animated subtrees from the supplied golem2_v3 resource:
mech root 10, cannon root 13 and crossbow arm root 18. Those indices and clip
choices exist only in scene/controller configuration. Separate self-contained
animated weapon resources work through exactly the same component path.

## Author a mount

Create a normal entity, parent it beneath the model owner in the Hierarchy,
then add **SocketAttachment**. Choose **Parent model node** in its Inspector.
The picker lists indices and names inside the parent's selected part. It pins
the binding to that model GUID; selecting a different model requires explicitly
choosing/rebinding its node. Duplicate or empty node names are unambiguous by index.

An empty socket entity can hold weapon children, lights or markers. Alternatively,
put SocketAttachment directly on the weapon entity, with MeshRenderer and its own
AnimatedModel. The weapon's own model need not match its parent's model. Use the
ordinary Transform for alignment; there is no second hidden offset transform.

For a file containing several rigs, **Model part root** draws only that subtree
and rebases the selected root to the entity origin. Choose a rig or mount root
above the animated mesh/joints. Selecting the moving mesh itself intentionally
cancels that node's own rigid motion. Whole model, the default, preserves the
original authored scene coordinates and remains compatible with older scenes.
Joint palettes still evaluate against the complete resource, including joints
outside the rendered subtree. Material slots retain their original indices.

## Transform and scheduling contract

```text
AnimatedModelSystem: independently evaluate every model pose
  -> SocketAttachmentSystem: prepare shared CPU model/rest-pose references
  -> HierarchyTransformSystem: parent world * socket node * child local TRS
  -> RenderExtractSystem: ordinary model-part proxies and material assignments
  -> existing renderer: main, shadows, outlines, depth/normal, bounds and picking
```

For whole models the node frame is `pose.global[node]`. For a selected part:

```text
nodeFrame = inverse(pose.global[modelRoot]) * pose.global[node]
attachedWorld = parentEntityWorld * nodeFrame * attachedLocalTRS
meshWorld = entityWorld * inverse(pose.global[modelRoot]) * pose.global[meshNode]
skinPalette = inverse(pose.global[meshNode]) * jointGlobal * inverseBind
```

The same evaluated pose and rebasing helper feed sockets and rendering. This is
hierarchy integration, not a renderer-only position adjustment: ordinary
descendants and other WorldTransform consumers see the same mounted transform.
Nested animated weapons/muzzle markers resolve top-down in the same frame.
Active mounts recompute each frame; removal/disable also invalidates their old
frame even if Transform was not edited. Fresh world/parent-frame queries and the
Scene view gizmo include sockets, so manipulation and compatible keep-world
reparenting use the same coordinates.

An owner without AnimatedModel uses a shared cached CPU rest pose. An owner with
animation requires its current valid pose; failure never silently substitutes
rest pose. Missing parent/model, model GUID mismatch, invalid node, a node outside
the selected part, or an unrepresentable pose/root hides the mounted subtree.
The Inspector explains unresolved bindings. Fixing the binding restores it.
Disabling SocketAttachment explicitly restores ordinary scene parenting.

## Gameplay API

`Animation/ModelSockets.h` provides AttachModelSocket, DetachModelSocket,
FindModelSocket and ModelSocketWorld. Structural attach/detach operations belong
outside ECS query iteration, like World::SetParent. Do not keep component pointers
across those calls because adding/removing a component may relocate entities.

```cpp
// Configuration supplies nodeIndex, alignment and aliases; no engine name policy.
if (AttachModelSocket(world, resources, weapon, mech, nodeIndex, alignment,
                      "PrimaryWeapon")) {
    // Pose resolution occurs in PreRender; the owner may have just spawned.
}

// After combat accepts an attack, use the equipped weapon's existing controller.
if (auto* animation = world.Write<AnimatedModel>(weapon))
    animation->controller.SetTrigger(configuredFireParameter);

const Entity marker = FindModelSocket(world, weapon, "Muzzle");
glm::mat4 muzzleWorld;
if (ModelSocketWorld(world, marker, muzzleWorld)) {
    // Use the marker origin/orientation for effects or a projectile spawn.
}

DetachModelSocket(world, weapon, true); // rejects unavailable/sheared world poses
```

FindModelSocket searches direct children and returns null for empty, missing or
ambiguous aliases. Socket/world queries use the current evaluated pose, not a
prediction of a future frame. Read them after hierarchy propagation for the
current rendered frame. Declared systems using fresh world queries must declare
reads of Transform, HierarchyNode, SocketAttachment, MeshRenderer and AnimatedModel;
WorldTransform readers should run after HierarchyTransformSystem.

Generic keep-world reparenting supports compatible model bindings. Unparenting
an enabled mount requires DetachModelSocket; DetachChildren refuses active mounts
before changing anything. Removing a component or disabling it means ordinary
parenting and may change the entity's world position. AttachModelSocket checks
model/node/subtree validity and cycles before mutating; a newly spawned owner can
resolve its pose on the next PreRender. Runtime resource caches are not serialized.

## Verification and limits

- Debug Editor/Game build passed with the installed VS 2022 toolchain.
- Temporary native real-engine/Vulkan checks covered independent firing, exact
  same-frame/nested socket transforms, compatible reparenting, disable/removal,
  invalid bindings, static fallback, materials/bindings in scene roundtrips,
  Play/Pause/Stop, owner destruction and proxy cleanup.
- Actual Inspector mouse input exercised detach and Undo/Redo. GPU viewport
  readbacks showed the modular scene with distinct materials, animation, shadows
  and weapon outlines. Temporary capture hooks were restored before final build.
- Final native verification passed 281 socket/lifecycle checks, 28 Inspector and
  Hierarchy input checks (including Ctrl+D of a mounted subtree), and 61 existing
  controller/material regression checks. The normal Editor executable opened this
  scene and closed cleanly after a short run; this was not a long-duration soak.
- Probes and capture files live under ignored build/setup-tools; no permanent
  test target, synthetic model, or third-party change was introduced.

The final build command was the local CMake 3.30.3 executable with
`--build build --config Debug --target Editor Game -- /m:1 /v:minimal /nologo`.
Temporary probes were compiled with the installed VS 2022 toolchain through
`build/setup-tools/milestone7-integration-build.cmd`, `milestone7-ui-build.cmd`
and the existing `milestone5-integration-build.cmd`, then run from the Editor
output directory against its Engine.dll. Logs and readbacks remain under
`build/setup-tools` for inspection.

This completes the animation feature scope, not a combat/weapon inventory system.
Combat acceptance, aiming, input bindings, projectiles and equip policy belong to
gameplay. Sockets inherit the node's full transform including scale. They do not
retarget unrelated skeletons, provide IK, make physics bodies follow animation,
or solve skin-surface attachments. Use exported nodes for deliberate attachment
points. Singular roots and world transforms requiring shear cannot be represented
by every TRS operation and are rejected safely. Resource hot reload/eviction and
the existing supported glTF feature limits are unchanged. VR tracking and mocap
remain outside the completed seven milestones.
