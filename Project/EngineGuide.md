# Crucible Engine: gameplay and scene guide

A working reference for writing gameplay in Crucible and building scenes, for people and for AI
assistants. **Check here first before searching the engine for a function.** If you add a
scripting API, a component or a gotcha, update this file in the same change.

The deeper subsystem docs are in this folder: scripting
([ScriptingDevelopment](<Crucible Engine/Docs/ScriptingDevelopment.md>), [ScriptingEntities](<Crucible Engine/Docs/ScriptingEntities.md>),
[ScriptingLifecycle](<Crucible Engine/Docs/ScriptingLifecycle.md>)), models ([ModelImport](<Crucible Engine/Docs/ModelImport.md>)), animation
([AnimationUsage](<Crucible Engine/Docs/AnimationUsage.md>), [AnimatorController](<Crucible Engine/Docs/AnimatorController.md>),
[ModularAnimation](<Crucible Engine/Docs/ModularAnimation.md>)). This guide summarises what you need from them. Where
the two disagree, the source code under `ManagedScripts/Runtime` is the truth.

Contents:

1. [Where things live](#1-where-things-live)
2. [Compile and run](#2-compile-and-run)
3. [Writing a script](#3-writing-a-script)
4. [Unity to Crucible cheat sheet](#4-unity-to-crucible-cheat-sheet)
5. [Scripting API reference](#5-scripting-api-reference)
6. [Coordinates and conventions](#6-coordinates-and-conventions)
7. [Assets, GUIDs and .meta files](#7-assets-guids-and-meta-files)
8. [Scene and prefab file format](#8-scene-and-prefab-file-format)
9. [Models, sockets, part roots and animation](#9-models-sockets-part-roots-and-animation)
10. [Recipes](#10-recipes)
11. [Scene generation tools](#11-scene-generation-tools)
12. [Gotchas](#12-gotchas)
13. [Ported from Unity (Gam3333)](#13-ported-from-unity-gam3333)

---

## 1. Where things live

| Path (from `Project/Crucible Engine`) | What |
|---|---|
| `Assets/Scripts/**/*.cs` | Game scripts. Every `.cs` here compiles into `Crucible.Scripts.dll`; subfolders are fine. |
| `ManagedScripts/Runtime/*.cs` | The scripting API (`Crucible.ScriptCore.dll`): Entity, Transform, Input... Read these for exact signatures. |
| `ManagedScripts/Templates/*.cs.txt` | Templates for a new EntityScript and for behaviour-graph Action/Condition/Composite nodes. |
| `Assets/Scenes/**/*.scene` | Scenes (JSON). Loaded by path relative to `Assets`, e.g. `Scenes/Game/Main Menu Scene.scene`. |
| `Assets/Prefabs/**/*.cprefab` | Prefabs (JSON, almost the same format as scenes). |
| `Assets/Models/**/*.glb` | Models. GLB only; the editor compiles them when they appear. |
| `Assets/Audio/**/*.wav` | Audio clips. |
| `Assets/Animation/**/*.animcontroller` | Animator controllers (JSON state machines). |
| `Assets/Materials/*.mat`, `Assets/Textures`, `Assets/Fonts` | Materials, textures, fonts. |
| `Assets/Config/EngineConfig.json` | Engine settings. |
| `Engine/src/...` | C++ engine. Script bindings: `Engine/src/Scripting/Bindings/*Bindings.cpp`. Components: `Engine/src/ECS/Components/Components.h`, `Engine/src/Physics/PhysicsComponents.h`, `Engine/src/Player/PlayerComponents.h`, `Engine/src/UI/UIComponents.h`. |
| `Tools/SceneGen/` | PowerShell helpers that write scenes, prefabs and `.meta` files ([section 11](#11-scene-generation-tools)). |

## 2. Compile and run

- **Editor (normal workflow):** save a `.cs` file under `Assets/Scripts` and the Editor recompiles
  and reloads the scripts by itself, about 0.4 s after the last save. Errors appear in the Console
  as `Assets/Scripts/X.cs(line,col): CSxxxx: message`, and the old scripts stay loaded until you
  fix them. A save during Play reloads when Play stops. The **Scripts** menu has Recompile,
  Reload and Auto Recompile.
- **The build must have Mono enabled** (`CRUCIBLE_ENABLE_MONO=ON`, `CRUCIBLE_MONO_ROOT`); without
  it there is no C# at all. `Tools/BuildScripts.ps1` refuses a Mono-off build.
- **Offline compile check** (no Editor, no Mono build needed). It uses the vendored compiler with
  the same language level. Run from the engine root:

  ```powershell
  $out = "$env:TEMP\crucible-check"; New-Item -ItemType Directory -Force $out | Out-Null
  $core = Get-ChildItem ManagedScripts\Runtime -Recurse -Filter *.cs | % FullName
  & Extern\mono\bin\mono.exe Extern\mono\lib\mono\4.5\mcs.exe -nologo -target:library -langversion:7.2 "-out:$out\core.dll" @core
  $mine = Get-ChildItem Assets\Scripts -Recurse -Filter *.cs | % FullName
  & Extern\mono\bin\mono.exe Extern\mono\lib\mono\4.5\mcs.exe -nologo -target:library -langversion:7.2 -warn:4 "-r:$out\core.dll" "-out:$out\scripts.dll" @mine
  ```

  No output means it compiled. This only proves that the code compiles; run the scene to test it.
- Open a scene from the Asset Browser, or launch with `Editor.exe --scene "Scenes/....scene"`.

## 3. Writing a script

```csharp
namespace Crucible.Scripts
{
    public sealed class Door : EntityScript          // public, concrete, non-generic, parameterless ctor
    {
        public float openHeight = 3f;                 // public fields = Inspector fields (supported types below)
        public Entity button;                         // reference to another entity (set in the Inspector)
        public AudioClip openSound;                   // assets: Prefab, AudioClip, Texture, Font, Material, Model

        public override void OnCreate() { }                      // Play starts (fields already applied)
        public override void OnUpdate(float deltaTime) { }       // every Playing frame
        public override void OnDestroy() { }                     // Stop, scene change, entity destroyed
        public override void OnTriggerEnter(Entity other) { }    // also OnTriggerExit
        public override void OnCollisionEnter(Collision c) { }   // also OnCollisionExit (only Other is set)
        public override void OnButtonClicked(Entity button) { }  // UI events, every script hears every one
        public override void OnSliderChanged(Entity slider, float value) { }
        public override void OnUIAction(string actionId) { }     // buttons whose On Click is Custom
    }
}
```

Rules:

- **One script per entity** (one ScriptComponent). To give an entity several behaviours, compose
  plain C# classes inside one script (see `MechPlayer` + `MechHealth` + `MechWeapon`), or put
  scripts on child entities.
- **Inspector field types:** `bool`, `int`, `float`, `double`, `string`, `Entity`, `Prefab`,
  `AudioClip`, `Texture`, `Font`, `Material`, `Model`. **Anything else (Color, Vector3, arrays,
  lists, enums, structs) is silently skipped**: it keeps its C# default and does not show in the
  Inspector or save in the scene. Fields must be public, non-static and non-readonly.
- **Language:** C# 7.2 on the .NET 4.5 profile (Mono `mcs`). No switch expressions, no `??=`,
  no ranges, no records, no nullable reference types, no `System.ValueTuple` tuples. `out var`,
  expression-bodied members, pattern `is T x` and local functions are fine. Not every `System.*`
  API exists in the 4.5 profile.
- **Order:** OnCreate runs for every script, then OnUpdate. A script found with `GetComponent<T>()`
  during OnCreate may not have run its own OnCreate yet. Do cross-script setup on the first
  OnUpdate (MechPlayer equips its parts there).
- **Static fields reset** on every script reload (each save in the Editor). Do not keep
  important state in statics across reloads.
- **Logging:** `Log.Info / Log.Warning / Log.Error` go to the Editor Console. `Console.WriteLine`
  does not.
- Physics callbacks arrive at the start of the frame after the physics step, before OnUpdate, on
  **both** entities' scripts. Check `other.IsAlive` first.

## 4. Unity to Crucible cheat sheet

| Unity | Crucible |
|---|---|
| `MonoBehaviour`, `Start`/`Awake`, `Update`, `OnDestroy` | `EntityScript`, `OnCreate`, `OnUpdate(dt)`, `OnDestroy` |
| `gameObject`, `transform` | `Entity`, `Transform` |
| `transform.position` / `localPosition` | `Transform.WorldPosition` / `Transform.Position` (local) |
| `transform.forward` | `Transform.Forward` (this is local **-Z**) |
| `GetComponent<T>()` | `GetComponent<T>()`: concrete component or script types only, no interfaces or base classes |
| `GameObject.Find(name)` / `transform.Find(name)` | `Entity.Find(name)` / `Entity.FindChild(name)` (searches all descendants) |
| `Instantiate(prefab, pos, rot)` | `prefab.Instantiate(pos, rot)` with a `public Prefab prefab;` field |
| `Destroy(go, t)` | `Destroy(entity, t)` |
| `SetActive`, `activeSelf`, `activeInHierarchy` | `Entity.SetActive`, `IsActiveSelf`, `IsActive` |
| `component.enabled = x` | `Entity.SetEnabled<T>(x)` / `IsEnabled<T>()` |
| `Input.GetKey / GetKeyDown / GetKeyUp` | `Input.IsDown / IsPressed / IsReleased(KeyCode.X)` |
| `Input.GetAxis("Mouse X")` | `Input.MouseDelta` (pixels, do not multiply by dt) |
| `Cursor.lockState` | `Input.IsMouseLocked` (read-only: the UI system owns the cursor) |
| `CharacterController.Move(v*dt)` | `CharacterController.SimpleMove(velocity)` (no dt; keeps moving until changed) plus `Jump(height)` |
| `Physics.Raycast` | `Physics.Raycast(ray or origin+dir, out hit, maxDistance, ignore)` |
| `Camera.main`, `ViewportPointToRay` | `Camera.Main`, `cam.ViewportPointToRay(new Vector2(.5f,.5f), out ray)` |
| `AudioSource.PlayOneShot(clip, vol)` | the same; `Audio.PlayClipAtPoint(clip, pos)` |
| `Animator.SetBool/SetFloat/SetTrigger` | `Animator.SetBool/SetFloat/SetInteger/SetTrigger` (needs an AnimatedModel with a controller) |
| `SceneManager.LoadScene(name)` | `Scene.Load("Scenes/Folder/Name.scene")` |
| `NavMeshAgent.SetDestination` | `FlowFieldAgent.MoveTo(point)` / `Arrive(entity)` / `FollowField()`; the ground needs a FlowFieldSurface |
| `Terrain`, or a generated mesh plus `MeshCollider` | a `Terrain` component, a `MeshRenderer` and a `Collider` of type `HeightField`; a script calls `Terrain.Build(...)` then `Navigation.Rebake()` |
| `Physics.OverlapSphereNonAlloc`, `OverlapBoxNonAlloc`, `OverlapCapsuleNonAlloc`, `RaycastNonAlloc`, `CapsuleCast` | the same names; results are `Entity[]` instead of `Collider[]` |
| uGUI `Text`, `Image`, `Slider`, `Button`, `RectTransform`, `Canvas` | `UIText`, `UIImage`, `UISlider`, `UIButton`, `UIRect`, `UICanvas` |
| TextMeshPro in world space | `WorldText` (sizes in metres) |
| `Mathf`, `Vector3.MoveTowards`, `ProjectOnPlane`, `ClampMagnitude` | not in ScriptCore; use `Assets/Scripts/Player/MechMath.cs` |
| ScriptableObject data | plain static C# tables (see `MechPartCatalogue`) or JSON assets |
| `[ContextMenu]` on a method | `[EditorButton]` on a public, parameterless `void` method: an Inspector button that runs it outside Play |
| `Application.isPlaying` | `Application.IsPlaying` (false only inside an `[EditorButton]` call) |

## 5. Scripting API reference

All of these are in `ManagedScripts/Runtime`. Each component class is a handle: `GetComponent<T>()`
returns null if the entity does not have one. The shortcut properties (`Entity.Transform`,
`.Animator`, ...) never return null; on a missing component their calls just fail quietly.

**EntityScript** (base class): `Entity`, `Transform`, `RigidBody`, `Animator`, `AudioSource`,
`CharacterController`, `GetComponent<T>()`, `HasComponent<T>()`, `Destroy(entity, delay)`,
`DontDestroyOnLoad(entity)`, `Enabled` (this script on/off), and the callbacks in section 3.

**Entity:** `Name`, `IsAlive`, `Index`/`Generation` (`==` compares these), `Transform`,
`RigidBody`, `Animator`, `AudioSource`, `CharacterController`, `Camera`, `CameraOrbit`, `Light`,
`ParticleEmitter`, `Collider`, `MeshRenderer`, `PlayerController`, `WorldText`, `MaterialParams`,
`FlowFieldAgent`, `FlowFieldSurface`, `StateGraphAgent`, `Terrain`; static `Find(name)`, `FindChild(name)`,
static `Destroy(e, delay)`, `GetComponent<T>()` / `HasComponent<T>()` (T: any component above, a
UI type or a script class), `SetEnabled<T>(bool)` / `IsEnabled<T>()`, `SetActive(bool)`,
`IsActiveSelf`, `IsActive`.

**Transform:** `Position`, `Rotation`, `Scale` (local); `WorldPosition`, `WorldRotation`;
`Forward` (-Z), `Right` (+X), `Up` (+Y); `LookAt(point | entity)`; `Parent` (get/set);
`SetParent(entity or null, worldPositionStays = true)`; `ChildCount`, `GetChild(i)`.

**Math:** `Vector2` and `Vector3` (fields `X Y Z`; `+ - *scalar`, `Length`, `Normalized`, `Dot`,
`Cross`, `Zero`/`One`/`Up`; **no** unary minus, Distance, Lerp or MoveTowards). `Quaternion`
(`Identity`, `Euler`, `AngleAxis`, `LookRotation(forward, up)`, `FromToRotation`, `Slerp`, `Lerp`,
`RotateTowards`, `Angle`, `Inverse`, `Forward/Right/Up`, `q * q`, `q * v`). `Color` (`R G B A`
0..1 sRGB, `new Color(r,g,b,a=1)`, named colours, `Lerp`, `FromBytes`).

**Input:** `IsDown/IsPressed/IsReleased(KeyCode)`: keys, mouse buttons (`MouseLeft`, `MouseRight`,
`MouseMiddle`) and gamepad buttons (`GamepadA`...). `MousePosition` (window pixels, top-left
origin), `MouseDelta`, `MouseScroll`, `IsMouseLocked`, `IsPointerOverUI`,
`GamepadLeftStick/RightStick/LeftTrigger/RightTrigger`, `IsGamepadConnected`. KeyCode names:
`A..Z`, `Num0..Num9`, `F1..F12`, `Space`, `Enter`, `Tab`, `Escape`, `LeftShift`, `LeftControl`,
`LeftAlt`, `GraveAccent` (`` ` ``), `Left/Right/Up/Down`, `KP0..`. The player's rebindable keys
are in **InputSettings**: `ForwardKey`, `BackKey`, `LeftKey`, `RightKey`, `JumpKey`, `JumpButton`,
`MouseSensitivity`, `StickSensitivity`, `InvertY`, `Save()`.

**Time:** `DeltaTime`, `ElapsedTime` (Play time, pause excluded; use this as Unity's `Time.time`),
`ElapsedTimeAsDouble`, `FrameCount`.

**Physics:**
- `Physics.Raycast(...)`: four overloads, with `(Ray | origin, direction)`, optional
  `out RaycastHit` (`Entity`, `Point`, `Normal`, `Distance`), `maxDistance` and `Entity ignore`.
- `Physics.SphereCast`, `CheckSphere`, `CapsuleCast` (first hit), and the non-allocating
  `OverlapSphereNonAlloc`, `OverlapBoxNonAlloc`, `OverlapCapsuleNonAlloc` and `RaycastNonAlloc`
  (nearest first): pass your own results array, keep it between calls, and use the returned
  count. Entities destroyed this frame are left out.
- `Collider`: `Shape` (Box, Sphere, Capsule, HeightField: the Terrain on the same entity, static only), `Size`, `Center`, `Radius`, `Height`, `IsTrigger`,
  `Layer` (NonMoving, Moving, Player, Enemy, Projectile), `Friction`, `Bounciness`, and
  `World*` sizes.
- `RigidBody`: `Velocity`, `Mass`, `UseGravity`, `Drag`, `IsKinematic`, `AddForce`, `AddTorque`,
  `ApplyImpulse`.
- `CharacterController`: `SimpleMove(velocity)`, `Jump(height)`, `IsGrounded`, `Velocity` (actual
  last step), `Height`, `Radius`, `SlopeLimit`, `StepOffset`. Gravity is the engine's. Setting
  `Transform.Position` teleports it.

**Player rig:** `PlayerController` (the built-in WASD controller: `MoveSpeed`, `JumpHeight`,
`FaceCamera`) and `CameraOrbit` (on the camera pivot, a child of the player: `Sensitivity`,
`StickSpeed`, `MinPitch`, `MaxPitch`, `Yaw`, `Pitch`). The engine turns the pivot with the mouse
whether or not the player has a PlayerController. **Remove PlayerController if a script moves the
character**, or both will SimpleMove it.

**Camera:** static `Main` (highest-priority enabled camera), `ScreenSize`; `FieldOfView`,
`NearClip`, `FarClip`, `Priority`, `Enabled`; `ScreenPointToRay(Input.MousePosition, out ray)`,
`ViewportPointToRay(v01, out ray)`, `WorldToScreenPoint(pos, out screen[, out depth])`. A `Ray` has
`Origin` and `Direction`.

**Audio:** `AudioSource`: `Play`, `Stop`, `PlayOneShot()`, `PlayOneShot(clip, volumeScale)`,
`IsPlaying`, `Clip`, `Volume`, `Pitch`, `Loop`, `Bus`, `SpatialBlend`, `Min/MaxDistance`. Static
`Audio`: `PlayClipAtPoint(clip, pos, volume, bus)`, `Master/Music/SfxVolume`,
`Get/SetBusVolume`, `SaveSettings`. `AudioClip.Name`.

**Screen UI** (a canvas entity and its children; see the [UI format](#screen-ui-components)):
`UIText` (`Text`, `Color`, `FontSize`, `Font`), `UIImage` (`Color`, `Texture`), `UIRect`
(`Position`, `Size`, `Rotation`, `GetScreenRect`), `UIButton` (`WasClicked`, `Interactable`,
`State`), `UISlider` (`Value`, `Min/MaxValue`, `WholeNumbers`, `WasChanged`), `UICanvas`
(`Enabled`, `SortOrder`, world-space options). Static `UI`: `MenuOpen`,
`WasActionTriggered(id)`. Hide part of a HUD with `Entity.SetActive(false)` on a group entity.

**WorldText** (3D text, sizes in metres): `Text`, `Color`, `FontSize` (the em; 0.5 gives letters
about 35 cm tall), `Size` (box), `Pivot`, `Billboard` (Off, FaceCamera, Upright), `AlwaysOnTop`,
alignment, wrap, outline, glow, shadow, fade.

**Rendering:** `MeshRenderer`: `Visible`, `Model` (**setting it clears the part root and the slot
overrides**), `Mesh`, `Material`, `GetMaterial/SetMaterial(slot)`, `MaterialCount`.
`MaterialParams`: `Tint`, `EmissiveScale`, `Custom`, `ClearTint`. `Light`: `Type`, `Color`,
`Intensity`, `Range`, `SpotAngle`, `CastShadows`, `Enabled`. `ParticleEmitter`: `Play`, `Stop`,
`Restart`, `Emit(n)` and every emitter setting.

**Animation:** `Animator.SetBool/SetFloat/SetInteger/SetTrigger/ResetTrigger(name, value)`. It
returns false when the entity has no AnimatedModel controller with that parameter.

**Prefab / Scene:** `prefab.Instantiate()`, `Instantiate(pos, rot)`, `Instantiate(parent)`,
`prefab.Name`; `Scene.Load(path)`, `Scene.Reload()`, `Scene.Name`, `Scene.DontDestroyOnLoad(e)`.

**AI:**
- `FlowFieldAgent`: `MoveTo(point)`, `Arrive(entity)` (chase and stop at StoppingDistance),
  `FollowField()`, `Stop()`, `HasArrived`, `RemainingDistance`, `Velocity`, and every setting.
  The agent moves its own entity.
- `FlowFieldSurface`: `FloodRadius`, `RebuildDistance`. What counts as too steep comes from the terrain
  generator (`SubLevelGenerator.maxWalkableSlope`, which also sets the player's `SlopeLimit`).
- `Navigation`: `IsWalkable(pos)`, `IsPathClear(from, to)`, `Rebake()` (after changing the ground
  mid-Play; it bakes on the next frame), `IsBaked`, and `BakeCount` (read it before
  `Rebake()`: when it changes, the new grid is in).
- What blocks enemies: only colliders on entities tagged `StaticObstacle` or `DynamicObstacle`
  (the tag on the entity that has the collider). Untagged colliders are ignored by navigation:
  enemies still bump into them but don't path round them. `StaticObstacle` is stamped once at
  bake (moving one needs a rebake); `DynamicObstacle` is re-checked every
  `dynamicUpdateInterval` (Nav Surface, 0.25 s) while Playing, and its blocked cells follow it.
  Nav Surface > **Auto-tag obstacles** (Edit mode) tags untagged colliders in the height band
  (Dynamic if they have a RigidBody) and fixes StaticObstacles with a RigidBody; one Ctrl+Z undoes it. The generated terrain's cliffs come
  from the generator itself, in Edit and Play; scenes without terrain have a flat floor at the
  Nav Surface's height.
- `StateGraphAgent`: `CurrentState`, `Set/GetVariableValue<T>`. Behaviour-graph nodes live in
  `ManagedScripts/Runtime/AI`; templates are in `ManagedScripts/Templates`.

**Terrain** (generated ground): `Build(cols, rows, cellSize, float[] heights, byte[] regions)`,
`IsBuilt`, `GroundHeight(x, z, out height)` (world space, on the exact triangle). Heights are
`(cols + 1) * (rows + 1)` corners, row-major (`z * (cols + 1) + x`); regions are `cols * rows`
`TerrainRegion` values (Ground, Cliff, Road), which set each cell's colour from the component.
The grid is centred on the entity and is never saved. C++ side: `Engine/src/Terrain`,
`ECS/ECSTerrainSystem`, and the HeightField case in `Physics/PhysicsShapes.cpp`.

**Editor buttons:** `[EditorButton]` on a public `void` method with no parameters shows it as a
button under the script's fields in the Inspector (Unity's `[ContextMenu]`). A click builds a
temporary copy of the script with the Inspector's field values and calls only that method: no
OnCreate, OnUpdate or OnDestroy, and only outside Play. Entity calls work during it; physics,
navigation and audio do nothing, so guard them with `Application.IsPlaying`. See
`SubLevelGenerator.Generate`. C++ side: `ScriptRuntime::RunEditorButton`,
`EntityContext::EditorCallScope`.

**Other:** `Log.Info/Warning/Error`; `Application.IsPlaying`; `Screen` (`Width`, `Height`, `Mode`, `FullScreen`,
`SetResolution`, `Resolutions`, `IsEditor`).

## 6. Coordinates and conventions

- **Right-handed, Y up, forward is -Z** (cameras, lights, `Transform.Forward`,
  `Quaternion.LookRotation`). Right is +X.
- **glTF/GLB models face +Z**, so a model entity usually needs a 180 degree yaw
  (`QYaw 180` in SceneGen) under the gameplay entity that faces -Z.
- **Porting Unity positions** (Unity is left-handed): mirror Z, so `(x, y, z)` becomes
  `(x, y, -z)`. Rotations become `(-qx, -qy, qz, qw)`. Unity's forward (+Z) then maps to the
  engine's forward (-Z), and left and right stay where they were.
- **Screen UI:** reference pixels of the canvas page (for example 1920 x 1080), **+y is down**.
  Anchors and pivots are 0..1 of the parent with **(0,0) at the top-left**, so bottom-left is
  anchor (0,1). The position is the pivot's offset from the anchors. Unity's bottom-left
  `(43, 47)` becomes anchor (0,1), pivot (0,1), position (43, -47).
- **Unity RectTransform world objects** (TMP containers used as scene groups): a child's position
  is the parent rect's pivot, plus the anchor offset, plus `anchoredPosition`.
- Colours are 0..1 sRGB floats.

## 7. Assets, GUIDs and .meta files

Every asset has a `<file>.meta` beside it:

```text
guid=9E46D8D20AE68001
hash=A243BAD7B714075F
path=Models/Cannon.glb
```

- Scenes reference assets by **GUID as a decimal integer**
  (`"meshAsset": 11405041502999969793` is `0x9E46D8D20AE68001`).
- **GUIDs are odd** (low bit set). The engine keeps any GUID it finds in a `.meta` and only
  rewrites the `hash` line (`hash=0` is fine), so you can write `.meta` files yourself to fix an
  asset's GUID before the editor sees it (`Get-AssetGuid` in SceneGen does this). If a file has no
  `.meta`, the editor generates a random GUID on its next scan, and anything that pointed at the
  old one breaks. **Commit the `.meta` files.**
- The `path=` line is only a hint.
- **Built-in assets** (no file): the meshes Cube `3578055278504480029`, Capsule
  `5067099792209742713`, Plane `3649096549467294993`, Sphere `8362592118289502741`, Cylinder
  `2856787514755437467`, Cone `15775344583901223439`, Disc `5933818420240141953`, Pyramid
  `1398714408298369051`, and Default Material `4225546827863954955`. These are FNV-1a of
  `"Crucible/Builtin/<Name>"` with the low bit set.
- **Models:** GLB 2.0 only, with **embedded** buffers and images. `.fbx/.obj/.gltf` are not
  imported. **Any `.fbx` left under `Assets` fails the whole build** (the `CompiledModels` step
  runs `CrucibleModelCompiler --compile-tree Assets Resources`), so keep FBX sources outside
  `Assets`. A GLB that references an external texture fails too. Kenney packs do this; embed
  the image first (the `embed_glb_image` approach in section 11). Supported: skins, rigid nodes,
  several meshes and materials, LINEAR/STEP animation. Animation clip indices are the order of
  the GLB's `animations` array.
- **Audio:** `.wav` and `.ogg` clips. `AudioSource.output` / `AudioBus` is `Master`, `Music` or `SFX`.

## 8. Scene and prefab file format

A scene is JSON. The editor writes it with 4-space indentation, but any valid JSON loads:

```json
{
  "version": 1,
  "name": "Zoo Scene",
  "entities": [
    {
      "id": "5A4F4F2000010003",
      "parent": "5A4F4F2000010001",
      "siblingIndex": 0,
      "components": {
        "Transform": { "positions": {"x":0,"y":0,"z":0}, "rotation": {"x":0,"y":0,"z":0,"w":1}, "scale": {"x":1,"y":1,"z":1} },
        "EntityName": { "name": "Player" }
      }
    }
  ]
}
```

- `id`: 16 hex digits, unique in the file. `parent` and `siblingIndex` are omitted on roots.
  Note that the Transform key is **`positions`** (plural).
- **Prefabs** (`.cprefab`): `"version": 3`, and **`parent` is the list index of the parent entity
  (`-1` for the root)**, not an id.
- An Entity script field stores the target's id as a decimal (`"entityId"`). Asset fields store a
  GUID (`"assetId"`).

Component blocks (copy them as they are; every key shown is expected):

```jsonc
"MeshRenderer": { "visible": true, "modelAsset": 0, "modelRoot": 4294967295, "materialOverrides": [],
                  "colour": {"x":0.5,"y":0.5,"z":0.5,"w":1}, "meshAsset": 3578055278504480029, "materialAsset": 0 },
// a model: modelAsset = GLB guid, meshAsset 0; modelRoot = node index of a part root, 4294967295 = whole model
"Collider": { "type": "Box", "size": {"x":1,"y":1,"z":1}, "radius": 0.5, "height": 2.0, "center": {"x":0,"y":0,"z":0},
              "friction": 0.2, "bounciness": 0.0, "layer": "NonMoving", "isTrigger": false },
// type Box|Sphere|Capsule; layer NonMoving|Moving|Player|Enemy|Projectile; size and radius scale with the Transform
"RigidBody": { "velocity": {...}, "mass": 1.0, "useGravity": true, "drag": 0.0, "angularDrag": 0.05, "isKinematic": false,
               "constraints": { "freezePositionX": false, ..., "freezeRotationZ": false } },
"CharacterController": { "height": 2.0, "radius": 0.5, "center": {...}, "slopeLimit": 45.0, "stepOffset": 0.3, "pushStrength": 100.0, "layer": "Player" },
"PlayerController": { "moveSpeed": 6.0, "jumpHeight": 1.5, "faceCamera": true },
"CameraOrbit": { "sensitivity": 0.1, "stickSpeed": 180.0, "minPitch": -30.0, "maxPitch": 70.0, "yaw": 0.0, "pitch": 0.0 },
"Camera": { "enabled": true, "fovY": 60.0, "nearZ": 0.1, "farZ": 1000.0, "priority": 1 },
"AudioListener": {},
"Light": { "type": "Directional", "colour": {"x":1,"y":1,"z":1}, "intensity": 1.0, "range": 10.0,
           "innerConeDeg": 20.0, "outerConeDeg": 30.0, "enabled": true, "castShadows": true },
"AudioSource": { "clip": <guid>, "loadType": "DecompressOnLoad", "output": "SFX", "playOnAwake": false, "loop": false,
                 "volume": 1.0, "pitch": 1.0, "spatialBlend": 0.0, "minDistance": 1.0, "maxDistance": 30.0, "rolloffMode": "Linear" },
"AnimatedModel": { "controllerAsset": <guid or 0>, "clip": 0, "speed": 1.0, "loop": true, "previewInEditor": false, "playOnStart": true },
"SocketAttachment": { "enabled": true, "socketName": "ArmL", "modelAsset": <parent's model guid>, "node": 9 },
"FlowFieldSurface": { "cellSize": 2.0, "bodyRadiusPadding": 0.6, "stepHeight": 0.3, "shoulderHeight": 1.6,
                      "floodRadius": 100.0, "rebuildDistance": 3.0, "wallAdjacencyCost": 4.0 },
"FlowFieldAgent": { "speed": 6.0, "acceleration": 30.0, "angularSpeed": 240.0, "baseOffset": 0.0, "startWith": "Nothing",
                    "stoppingDistance": 2.0, "slowingDistance": 5.0, "neighbourRadius": 10.0, "separationRadius": 1.5,
                    "separationWeight": 1.5, "alignmentWeight": 0.4, "radius": 0.6, "avoidanceDistance": 3.0, "avoidanceWeight": 2.2 },
"ScriptComponent": { "enabled": true, "assembly": "Managed/Crucible.Scripts.dll", "namespace": "Crucible.Scripts",
                     "class": "ZooToggle", "fields": [
    { "declaringType": "Crucible.Scripts.ZooToggle", "name": "toggleObject", "type": "Entity",
      "boolean": false, "integer": 0, "single": 0.0, "real": 0.0, "text": "", "stringIsNull": false,
      "entityId": 6507999012386734093, "assetId": 0 } ] }
// "fields": [] = C# defaults. type Boolean|Int32|Single|Double|String|Entity|Prefab|AudioClip|Texture|Font|Material|Model
```

The full WorldText block (all keys) is in `Tools/SceneGen/SceneGen.Common.ps1` (`WText`).

### Screen UI components

A canvas entity with children:

```jsonc
"UICanvas": { "enabled": true, "renderMode": "ScreenSpace", "referenceResolution": {"x":1920,"y":1080}, "toggleWithEscape": false,
              "showsCursor": false, "scaleWithScreenSize": true, "sortOrder": 0, "pixelsPerUnit": 100.0, "pivot": {"x":0.5,"y":0.5},
              "billboard": "Off", "alwaysOnTop": false, "doubleSided": true, "intensity": 1.0, "fade": false, "fadeStart": 20.0, "fadeEnd": 30.0 },
"UIRect":  { "anchorMin": {"x":0,"y":1}, "anchorMax": {"x":0,"y":1}, "pivot": {"x":0,"y":1}, "position": {"x":43,"y":-47},
             "size": {"x":360,"y":45}, "rotation": 0.0, "raycastTarget": false },
"UIImage": { "colour": {"x":1,"y":1,"z":1,"w":1}, "textureAsset": 0 },
"UIText":  { "text": "HEALTH", "colour": {...}, "fontAsset": 0, "fontSize": 20.0, "horizontalAlign": "Left", "verticalAlign": "Middle", ... }
```

A group entity with only a UIRect is a layout container. Later siblings draw on top. For a
world-space panel, use `"renderMode": "WorldSpace"` and place it with the entity's Transform.

## 9. Models, sockets, part roots and animation

- **Draw a model:** `MeshRenderer.modelAsset` = the GLB's GUID (meshAsset 0). Add `AnimatedModel`
  to animate it: either a direct `clip` index (controllerAsset 0) or a controller.
- **Part root (`modelRoot`):** draws only the subtree under that node, **rebased so that node sits
  at the entity's origin**. Use the GLB's `Mount_*` node (`Mount_Hip`, `Mount_Head`, `Mount_Arm`,
  `Mount_Shoulder`) so that a part lands correctly on a socket.
- **Mount frames are Z-up.** The specialist `Mount_*` and `Socket_*` nodes carry Blender's frame,
  so in a mount frame +Z is up and the weapon points along -Y (an arm part hangs below the
  origin). A mount-rooted part lying on the ground therefore needs a -90 degree pitch about X to
  stand upright in the engine's Y-up world (`MechPart.mountFrame`).
- **Sockets:** a child entity with `SocketAttachment { modelAsset: <parent's model GUID>, node:
  <node index in the parent's model> }` follows that animated node. Its own Transform is an extra
  offset in the node's frame. Put a part directly on it, or use an empty mount entity with parts
  as children. The node must be inside the parent's selected part root.
- **Node indices:** the order of the GLB's `nodes` array. To list them:

  ```powershell
  $b=[IO.File]::ReadAllBytes("Assets\Models\Golem model\TORSO_Specialist.glb"); $n=[BitConverter]::ToUInt32($b,12)
  $o=[Text.Encoding]::UTF8.GetString($b,20,$n)|ConvertFrom-Json; for($i=0;$i -lt $o.nodes.Count;$i++){ "$i $($o.nodes[$i].name)" }
  ($o.animations | % name) -join ', '    # clip indices in order
  ```

  The specialist set (Assets/Models/Golem model, Weapon model, Enemy model):

  | Model | Useful nodes | Clips |
  |---|---|---|
  | LEGS_Biped | 2 Socket_Hip | 0 Dash, 1 Idle, 2 Jog, 3 JumpAir, 4 JumpLand, 5 JumpStart, 6 PowerUp, 7-10 Strafe, 11 Walk |
  | TORSO_Specialist | 31 Mount_Hip, 2 Socket_Head, 9 Socket_Arm.L, 20 Socket_Arm.R, 16 Socket_Shoulder.L, 27 Socket_Shoulder.R | 0-1 CannonFire L/R, 2 Idle, 3 PowerUp |
  | HEAD_Specialist | 7 Mount_Head | 0 Idle, 1 PowerUp |
  | ARM_Blaster | 8 Mount_Arm, 2 Marker_Muzzle | 0 Fire |
  | SHOULDER_Cannon | 9 Mount_Shoulder, 3 Marker_Muzzle | 0 ChargeLoop, 1 ChargeStart, 2 Fire |
  | ENEMY_Fodder | 16 Socket_Shoulder, 17 Socket_Arm | 8 Idle, 9 Jog, 14 Walk, 4-7 HitReact (Back/Front/Left/Right), 0-3 fire/charge, 10-13 strafes |

  The `*.clips.json` files that come with the FBX exports list the same indices, with frame
  counts, loop flags and markers.
- **Controllers** (`.animcontroller`, JSON): `parameters` (Bool, Float, Integer, Trigger),
  `states` (`clip`, `speed`, `loop`), `transitions` (`from`: a state index or `"Any"`, `to`,
  `duration`, `conditions`: `{parameter index, mode}` where `If`/`IfNot` is for bools and triggers,
  and `Greater`/`Less`/`Equals`/`NotEqual` with a `value` is for numbers; optional `hasExitTime`
  and `exitTime`). Transition order is priority. An Any-State edge never re-enters the current
  state. Examples: `Assets/Animation/Player/Mech Legs.animcontroller` (Integer + Float conditions,
  an entry `offset` to start part-way into a clip) and `Assets/Animation/Zoo/Zoo Fodder.animcontroller`.
- **No blend trees, no speed parameters.** To follow a speed, use several states on the same clip at
  different `speed` values, chosen by Float thresholds (the legs' four Locomotion bands do this). To
  play part of a clip, enter it with `offset` (the legs' BoostStop enters Dash at 18/30).
  Scripts drive them with `entity.Animator.SetBool("Moving", true)`.

## 10. Recipes

**Script-driven third-person character** (see `MechPlayer`):

```text
Player          CharacterController, Script (calls SimpleMove/Jump; NO PlayerController)
|- Camera Pivot CameraOrbit, at shoulder height       <- the engine turns this with the mouse
|  '- Camera    Camera (+ AudioListener), about (1, 0.5, 6): behind and to the right
'- Visual       yaw 180, the model
```

The camera-relative move direction comes from the camera's right vector:
`right = flat(cam.Entity.Transform.Right)` and `forward = Cross(Up, right)`.

**"Stand here and press F" trigger pad** (see `Assets/Scripts/Zoo/ZooPad.cs`): a Collider with
`isTrigger: true`. In OnTriggerEnter/Exit, check `other.GetComponent<MechPlayer>() != null` and
remember the result; in OnUpdate, read `Input.IsPressed(KeyCode.F)`.

**Spawning:** `public Prefab enemyPrefab;` then `enemyPrefab.Instantiate(position, Quaternion.Identity)`.
The prefab's own scripts run OnCreate when it is spawned.

**Something the player's weapons can damage:** in OnCreate, call
`MechCombatTargets.Register(Entity, (amount, from) => TakeDamage(amount))`; in OnDestroy, call
`Unregister(Entity)`. It needs a Collider so that ranged weapons' raycasts hit it.

**A HUD bar:** a UIImage with pivot (0,0); each frame, set `rect.Size = new Vector2(fullWidth * ratio, height)`.
See `MechHud` for the lost-health bar, the cooldown sweeps and dimming.

**Hide or show a HUD block:** call `groupEntity.SetActive(bool)` on its group entity.

**Generated ground** (see `Assets/Scripts/TestScripts/TerrainTest.cs`, `Scenes/Terrain_test.scene`):

```text
Terrain       Terrain, MeshRenderer (no mesh or model), Collider (HeightField, NonMoving), Script
Nav Surface   FlowFieldSurface, scaled to cover the terrain (scale = metres)
```

In OnCreate, fill the heights and regions, call `Terrain.Build`, then `Navigation.Rebake()`.
The engine draws it, collides with it, and enemies path on it once `Navigation.BakeCount` changes.

## 11. Scene generation tools

`Tools/SceneGen/` writes scenes from PowerShell, so a layout can be reviewed as code and
regenerated:

| File | Use |
|---|---|
| `SceneGen.Common.ps1` | Dot-source it. Provides `New-EntityList`, `Add-Entity`; component builders (`Mesh`, `ModelRenderer`, `Anim`, `Socket`, `BoxCollider`, `SphereCollider`, `CapsuleCollider`, `KinematicBody`, `Light`, `AudioSourceComp`, `Script` with `FBool/FFloat/FInt/FString/FEntity/FAsset`, `WText`); UI builders (`Rect`, `TL`, `Img`, `Txt`, `Canvas`); `Get-AssetGuid` (reads a `.meta`, or creates one with a stable GUID); `$Builtin` ids; `QYaw`/`QPitch`. |
| `SceneGen.Player.ps1` | The player, shared by every scene: `Initialize-PlayerAssets` (GUIDs; writes `Animation/Player/Mech Legs` and `Mech Cannon` controllers), `Add-PlayerRig` (capsule, camera rig, specialist model on sockets, Unity starting loadout), `Add-Part` (a pickup or mounted part from the `$Looks` table: GLB or placeholder primitives, attack sound), `Add-UnityHud` (Unity's Combat HUD layout and sprites; `URect` converts Unity rects), `$PlayerControls`. |
| `MakePlayerShowcase.ps1` | Writes `Assets/Scenes/Game/TestFolder/Player Showcase.scene`: test course, the player, a rack with one of every part. |
| `MakeZooScene.ps1` | Writes `Assets/Scenes/Game/TestFolder/Zoo Scene.scene`, the Fodder controller and the two enemy prefabs. |
| `CheckScene.ps1 <file>` | Offline check: parents exist, Entity fields resolve, and every asset GUID has a `.meta` or is a built-in. |
| `GlbBounds.ps1 <glb> <node> [clip]` | Bounding box of a GLB's meshes in one node's frame (e.g. `Mount_Arm`), optionally in a clip's first-frame pose: where a part's geometry sits relative to its socket. |
| `UnityMount.ps1` | Converts Unity's weapon mounting chain (socket object, `weaponAttachments`, `Weapon Visual Rig`, rig) into engine transforms. Unity space is the engine's mirrored in X: position (-x, y, z), rotation (x, -y, -z, w), which is verified against the GLB. The specialist Mount_* node sits at Unity's socket object rotated by Ry(90)·Rx(-90). |

Run them with `pwsh -NoProfile -File Tools/SceneGen/<script>.ps1`. **Regenerating overwrites the
generated files**, including edits made to them in the Editor. Either keep the generator as the
source of truth, or stop using it for that scene once people start editing it by hand.

Embedding an external texture into a GLB: read the JSON and BIN chunks, append the PNG to the BIN
chunk (4-byte aligned), add a bufferView, then replace `images[i].uri` with `bufferView` and
`mimeType`. `Assets/Models/Zoo Props/tent.glb` was converted this way.

## 12. Gotchas

- **One script per entity.** A second behaviour needs a child entity or composition.
- **A spawned prefab's scripts do not exist yet** right after `Prefab.Instantiate` (they are created
  later in the frame, "OnCreate deferred"), so `GetComponent<MyScript>()` returns null. Queue the data
  and let the script read it in its own OnCreate (`MechProjectile.Fire`).
- **Unsupported field types are skipped without a warning** (Color, Vector3, arrays): they keep
  their C# defaults.
- **`GetComponent<IInterface>()` and `GetComponent<AbstractBase>()` throw**: only concrete
  component and script types work. Use a registry, like `MechCombatTargets`.
- **`MeshRenderer.Model = x` resets `modelRoot`** to the whole model. To swap parts that need a
  part root, move entities around (`Transform.SetParent`) instead of swapping models.
- **PlayerController plus your own movement script fight each other.** Remove PlayerController.
- `CharacterController.SimpleMove` is a velocity that persists. Call `SimpleMove(Vector3.Zero)`
  to stop.
- `SetActive(false)` on your own entity pauses your own script.
- **A Terrain shows nothing in Edit mode** until something builds it: the grid is never saved. Use
  an `[EditorButton]` (the Sub Level Generator's **Generate**) to preview it; the preview is gone
  after Play stops or the scene reloads.
- **An `[EditorButton]` method's changes are real**: components it writes are saved with the scene
  and can't be undone, and prefabs it instantiates become scene entities. Keep previews to
  runtime-only data (a Terrain's grid) until a "generated, don't save" mark exists.
- **A HeightField collider needs a built Terrain on the same entity**, and no RigidBody: until
  `Terrain.Build` runs it has no body at all (no error).
- The Editor locks the cursor at Play start. Esc frees it and clicking the Game view captures
  it again. Gate gameplay input on `Input.IsMouseLocked`.
- `Transform.Position` and `Rotation` are **local**. Use the `World*` properties for world space.
- Static state resets on every script reload.
- **PowerShell variable names are not case-sensitive** (`$s` and `$S` are the same variable). This
  matters when writing generators.
- A GLB with external textures or required extensions fails to import. An FBX under `Assets`
  fails the build (`CompiledModels ... exited with code 1`). Run
  `Build\Tools\ModelCompiler\Debug\CrucibleModelCompiler.exe --compile-tree Assets Resources` to see which file.
- In PowerShell, `,` binds tighter than `+`: `@(@(1,2) + $x, @(3))` does not build two rows.
  Write each row out in full.

## 13. Ported from Unity (Gam3333)

| Engine | Unity source | Notes |
|---|---|---|
| `Assets/Scripts/Player/MechPlayer.cs` | MechPlayerInput, MechMovementController, MechStats, EquipManager/PickupDetector, MechCombat | Walking, 5 s auto-run, Quick Boost dash with curve and cooldown, jump, facing, part equip and swap, hits via screen-centre ray, splash and a melee arc. No lock-on, projectile visuals, hit stop or knockback. **Starting loadout** = Unity's specialist: Golem Torso, Golem Head, Fast Legs, Sword in the left arm, other sockets empty (`start*` fields; a MechPart under a mount wins). **Swap** like Unity: near parts, V cycles the selection, 1 = left / only slot, 2 = right slot (F is Unity's camera lock); the old part is unparented and lands at the mech's feet with its ammo kept. Empty sockets have no HUD pod. |
| `.../Player/MechHealth.cs` | CombatHealth | Death stops the mech; H revives (debug). |
| `.../Player/MechLoadout.cs` | PartCatalogue PartData, Part.Consume/TryReload, PartAbilityCooldown | Real stats. The Blaster has none in Unity, so placeholders are used. |
| `.../Player/MechPart.cs` | Part (physical pickup) | |
| `.../Player/MechHud.cs` | MechHealthHud, MechWeaponsHud/WeaponsHudPod, MechAbilitiesHud/AbilitiesHudPod, HudCooldownSweep, PlayerDamageVignette, MechLocomotionDebug | Unity's sprites (`Assets/Textures/HUD`: HP bar set, pod background and glow, cursor, part and ability icons) and the Combat HUD prefab layout. The bars shrink rather than crop (the engine's UIImage has no Filled mode). |
| `MechPlayer` legs and audio | MechLegAnimator, MechMovementAudio | Poses Neutral / Locomotion / Boost / BoostStop / Jump, with Unity's BoostStop timing, sent as Integer `Pose` + Float `Speed`. The legs loop Golem1Legs_Move at volume 0.3, pitch clamp(speed / 6, 0.65, 1.6). |
| `MechPart.PlayAttack` / `SetIdle` | MechWeaponAnimator, MechWeaponIdle (+ MechWeaponAnimationSet) | Controllers `Animation/Player/Mech <Weapon>.animcontroller`. Hold = Neutral's first frame (every weapon, most of the time). Idle = Neutral playing, only for the one weapon MechWeaponIdle picks after 3 s standing still (Bool `Idle`). Fire (Trigger) plays at its authored speed, then Reload stretched over the rest of the cooldown. R plays no animation, as in Unity. The attack sound uses the part's own voice (volume 0.75). The firing camera shake comes from WeaponVisualSettings. |
| Unity values used | FullPlayer_mech_specialist, weapon prefabs | `moveSpeed` 10; MechCombat melee box (5, 3.5, 5.5) centred 2.75 ahead, fallback muzzle (0.65, 0.5, 0.65); MechWeaponIdle delay 3 s; firing shake 0.02 / 0.12 s (arms), 0.04 / 0.18 s (large crossbow), 0.075 / 0.26 s (cannon). Unity metres are scaled by `MechPlayer.unityScale` (1.1: this mech is 0.55 of the specialist model, Unity's parts 0.5). |
| Golem1 weapons (sword, mace, throwing axe, crossbow arm, large crossbow) | Prefabs/Mech/Base/Parts + Anim/Weapons | The animated rigs in `Models/golem2_v3.glb` (CanonRig 14, CrossBowArmRig 18, CrossBowRig 23, MaceArmRig 27, SwordArmRig 31, ThrowingAxeRig 37) as the part's `modelRoot`. Its clips are the same bones and lengths as Unity's `.anim` files. Posed on the socket by Unity's chain (`UnityMount.ps1`), with Unity's per-socket `weaponAttachments` as `MechPart.socketPoses`. Mounted 1:1 with the specialist parts (Unity scales every part by `partScale` 0.5). |
| `.../Player/MechProjectile.cs` | MechProjectile, RangedAttack | A straight shot from the part's `Muzzle` (a SocketAttachment on the model's muzzle node) toward the screen-centre aim point. It flies at ProjectileSpeed up to the maximum range, then stops on the first collider or a target within its radius. Damage falls off with distance travelled, and splash hits everyone in the radius (the direct target takes both). Prefabs: `Prefabs/Player/Projectile Bolt / Axe / Cannon`. Shots aim at the selected enemy's aim point when there is one (AimSolver). No trails, impact VFX or hit stop yet. |
| `.../Player/MechTargetLock.cs` (owned by MechPlayer) | MechTargetLockController | Unity's defaults: lock range 60 m, auto-aim radius 0.18 of the smaller screen side, switch radius 0.025, screen padding 0.04, flick 80 px within 0.2 s (minimum 120 px/s, release 0.1 s, cooldown 0.25 s, half-angle 60°). Automatic aim is always on: the enemy the centre ray hits, then one within the switch radius, else the nearest to the centre; a valid selection is kept. **F** toggles hard lock; a lost target is replaced by the nearest visible one. Candidates are `MechCombatTargets` entries (each with an aim height) instead of an overlap query. The centre-ray check uses the first hit, so unlike Unity an enemy behind a wall is not "under the crosshair". |
| `MechPlayer.UpdateLockCamera` | CameraControlThirdPerson.UpdateCameraOrbit | While hard locked: CameraOrbit yaw eases to the target bearing (response 0.12 s); pitch eases to max(20°, sightline clearance), clamped 5–80°; the pivot rises 1.5 m; mouse and stick look are off, and the body faces the target. The script sets yaw/pitch itself; there is no Cinemachine LookAt or melee close-up framing. |
| `MechHud.RefreshCursor` | LockOnCursor | The `Cursor` image moves to the selected enemy's aim point while it is on screen (else the centre) and swaps sprites: `CursorCamLockon` in hard lock, `CursorLockon` on an automatic target, `Cursor` otherwise. |
| `.../Player/MechCombatTargets.cs` | stands in for `GetComponentInParent<CombatHealth>` | |
| `Assets/Scripts/Zoo/*` | ZooSoundplayer, ZooToggle, ZooBacktoMain, ZooEnemyWaves | `ZooEnemy` is a placeholder chaser until the Behavior Graph enemies are ported. |
| `Scenes/Game/TestFolder/Player Showcase.scene` | FullPlayer_mech_specialist + Mech Cursor Canvas | The player alone: behaviour, animation and HUD, on a test course with a rack of every part. Generated by `Tools/SceneGen/MakePlayerShowcase.ps1`. |
| `Terrain` component, `ECSTerrainSystem`, HeightField collider | SubLevelGenerator's terrain mesh (TerrainMeshBuilder) and MeshCollider, NavTerrain hand-off | Step 1 of the port: the engine side only. Same layout as Unity's TerrainMap (corner heights, per-cell regions, one diagonal per cell). Tested by `Scenes/Terrain_test.scene` (`TerrainTest.cs`). Terrain generation is ported (next row); chunks, roads and decor are not yet. |
| `Assets/Scripts/Level/Generation/` `SubLevelGenerator`, `TerrainGenerator`, `SeededNoise`, `SeededRandom` | SubLevelGenerator (terrain part), TerrainGenerator, SeededNoise, SubLevelGenSettings | Algorithm unchanged; settings are fields on the generator (defaults = Unity's mission presets). `SeededRandom` (Mulberry32) replaces `System.Random`, so a seed gives a different map than in Unity but the same one on every machine. The generator sizes the Nav Surface to the map and gives it and the player one slope limit (Unity's ApplyPlayerSlope). Test: `Scenes/Generator_test.scene`. |
| `Scenes/Game/TestFolder/Zoo Scene.scene` | Scenes/ZooScene.unity | Generated by `Tools/SceneGen/MakeZooScene.ps1`. Uses only the specialist GLBs (`Models/Golem model`, `Models/Weapon model`). Cannon, head and legs use their GLBs; sword, mace, throwing axe and both crossbows are placeholder primitives (built in the mount frame) until they have models. The Blaster is left out: its Unity PartData has no stats. |
