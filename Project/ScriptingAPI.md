# Crucible Engine: Scripting API Reference

> **Written by:** Claude (Anthropic's AI assistant), at the request of Team MediumWell
> **Created:** 28 November 2026
> **Last updated:** 28 November 2026 (update this line when you change the file)
> **Generated from:** `Crucible Engine/ManagedScripts/Runtime/**/*.cs` as of the created date

A class-by-class reference for the C# scripting API (`Crucible.ScriptCore.dll`), laid out like
Unity's Scripting Reference: what each class is for, its properties and methods, and a short
example. It is for gameplay programmers and designers writing scripts under `Assets/Scripts`.

- For **how to set up, compile and run scripts**, the scene format and the gotchas, read
  [EngineGuide.md](EngineGuide.md). This file only covers the API.
- **The source is the truth.** If this page and `ManagedScripts/Runtime` disagree, the source is
  right; please fix this page in the same change.
- When you add or change a public API, add it here too, and bump **Last updated**.

All classes are in the `Crucible` namespace family that scripts already see; game scripts live in
`namespace Crucible.Scripts`.

---

## Contents

**Core**
[EntityScript](#entityscript) ·
[Entity](#entity) ·
[Transform](#transform) ·
[Log](#log) ·
[Time](#time) ·
[Application](#application) ·
[EditorButton](#editorbutton-attribute)

**Math**
[Vector2](#vector2) ·
[Vector3](#vector3) ·
[Quaternion](#quaternion) ·
[Color](#color)

**Input**
[Input](#input) ·
[KeyCode](#keycode) ·
[InputSettings](#inputsettings)

**Physics**
[Physics](#physics) ·
[RaycastHit](#raycasthit) ·
[Collision](#collision) ·
[Collider](#collider) ·
[LayerMask](#layermask) ·
[RigidBody](#rigidbody) ·
[CharacterController](#charactercontroller) ·
[Ragdoll](#ragdoll)

**Player and camera**
[PlayerController](#playercontroller) ·
[CameraOrbit](#cameraorbit) ·
[Camera](#camera) ·
[Ray](#ray)

**Rendering**
[MeshRenderer](#meshrenderer) ·
[MaterialParams](#materialparams) ·
[Light](#light) ·
[ParticleEmitter](#particleemitter) ·
[Graphics](#graphics) ·
[QualitySettings](#qualitysettings) ·
[Screen](#screen)

**Audio**
[AudioSource](#audiosource) ·
[Audio](#audio)

**Animation**
[Animator](#animator)

**UI and text**
[UIText](#uitext) ·
[UIImage](#uiimage) ·
[UIButton](#uibutton) ·
[UISlider](#uislider) ·
[UIRect](#uirect) ·
[UICanvas](#uicanvas) ·
[UI](#ui) ·
[WorldText](#worldtext)

**Scenes and assets**
[Scene](#scene) ·
[Prefab](#prefab) ·
[Asset handles](#asset-handles-audioclip-texture-font-material-model-mesh)

**AI and navigation**
[FlowFieldAgent](#flowfieldagent) ·
[FlowFieldSurface](#flowfieldsurface) ·
[Navigation](#navigation) ·
[Terrain](#terrain) ·
[StateGraphAgent](#stategraphagent) ·
[Behaviour graph nodes](#behaviour-graph-nodes)

**Networking**
[Network](#network) ·
[Network callbacks](#network-callbacks-on-entityscript) ·
[NetVars](#netvars) ·
[NetData](#netdata) ·
[NetWriter / NetReader](#netwriter--netreader) ·
[NetMessage](#netmessage) ·
[NetScript and NetFields](#netscript-and-netfields) ·
[NetGameState](#netgamestate) ·
[NetEffect](#neteffect) ·
[NetInput, PlayerActions, ActionMap](#netinput-playeractions-actionmap)

---

## How to read this page

- **Component handles.** Classes like `RigidBody`, `Light` or `UIText` are handles to a component
  on an entity. `GetComponent<T>()` returns `null` if the entity has none. The shortcut
  properties on `Entity` / `EntityScript` (`Transform`, `RigidBody`, `Animator`...) never return
  null; on a missing component their calls just do nothing.
- Every component handle has an **`Entity`** property (the entity it is on). It is not repeated in
  each table below.
- *(get)* means read-only. Everything else can be read and set.
- Positions are metres, angles are degrees, colours are 0..1 sRGB, unless a row says otherwise.
- The world is **right-handed, Y up, forward is -Z**.

---

# Core

## EntityScript

`public abstract class EntityScript`

The base class of every gameplay script (Unity's `MonoBehaviour`). Add it to an entity through a
Script component. An entity can have several scripts; they run in Inspector order.

Public, non-static, non-readonly fields of type `bool`, `int`, `float`, `double`, `string`,
`Entity`, `Prefab`, `AudioClip`, `Texture`, `Font`, `Material` or `Model` show in the Inspector.
Other field types are skipped without a warning.

### Properties

| Property | Type | Description |
|---|---|---|
| `Entity` | `Entity` | *(get)* The entity this script is on. |
| `Transform` | `Transform` | *(get)* Shortcut for `Entity.Transform`. |
| `RigidBody` | `RigidBody` | *(get)* Shortcut for `Entity.RigidBody`. |
| `Animator` | `Animator` | *(get)* Shortcut for `Entity.Animator`. |
| `AudioSource` | `AudioSource` | *(get)* Shortcut for `Entity.AudioSource`. |
| `CharacterController` | `CharacterController` | *(get)* Shortcut for `Entity.CharacterController`. |
| `Enabled` | `bool` | This script on or off (Unity's `enabled`). While off it gets no `OnUpdate` or callbacks but keeps its fields. Applies from the next frame. Only affects this script. |

### Methods

| Method | Description |
|---|---|
| `T GetComponent<T>()` | Same as `Entity.GetComponent<T>()`. |
| `T[] GetComponents<T>()` | Same as `Entity.GetComponents<T>()`. |
| `bool HasComponent<T>()` | Same as `Entity.HasComponent<T>()`. |
| `static void Destroy(Entity entity, float delay = 0f)` | Destroys an entity at the end of the frame, or after `delay` seconds. Same as `Entity.Destroy`. |
| `static bool DontDestroyOnLoad(Entity entity)` | Keeps a root entity alive through scene loads. Same as `Scene.DontDestroyOnLoad`. |

### Callbacks (override these)

| Callback | When it runs |
|---|---|
| `OnCreate()` | Once when Play starts, or when the entity is spawned. Inspector fields are already applied. |
| `OnUpdate(float deltaTime)` | Every frame while Playing. `deltaTime` is seconds since the last frame. |
| `OnDestroy()` | When Play stops, the scene changes or the entity is destroyed. |
| `OnCollisionEnter(Collision collision)` / `OnCollisionExit(Collision collision)` | A solid collider started / stopped touching this entity. |
| `OnTriggerEnter(Entity other)` / `OnTriggerExit(Entity other)` | Something entered / left a trigger collider. |
| `OnButtonClicked(Entity button)` | A UI button was clicked. Every script hears every click; check which button. |
| `OnSliderChanged(Entity slider, float value)` | A UI slider moved. |
| `OnUIAction(string actionId)` | A button whose On Click is **Custom** was clicked. |

Network callbacks are listed under [Network callbacks](#network-callbacks-on-entityscript).

**Order:** every script's `OnCreate` runs, then `OnUpdate`. Physics callbacks arrive at the start
of the frame after the physics step, before `OnUpdate`, on both entities' scripts.

### Example

```csharp
namespace Crucible.Scripts
{
    public sealed class Spinner : EntityScript
    {
        public float degreesPerSecond = 90f;

        public override void OnUpdate(float deltaTime)
        {
            Transform.Rotation = Transform.Rotation *
                Quaternion.AngleAxis(degreesPerSecond * deltaTime, Vector3.Up);
        }
    }
}
```

---

## Entity

`public sealed class Entity`

A reference to one entity in the scene (Unity's `GameObject`). Two `Entity` values are `==` when
they refer to the same entity (same `Index` and `Generation`).

### Properties

| Property | Type | Description |
|---|---|---|
| `Name` | `string` | The Hierarchy name. |
| `IsAlive` | `bool` | *(get)* False once the entity is destroyed. Check it before using a stored reference. |
| `Index`, `Generation` | `uint` | *(get)* The entity's slot and which use of that slot it is. |
| `Identity` | `EntityIdentity` | *(get)* `Index` + `Generation` as one struct. |
| `Tag` | `string` | The tag (`"Untagged"` by default). Setting a tag the project doesn't define throws `ArgumentException`. A tag set during Play is restored by Stop. |
| `IsActiveSelf` | `bool` | *(get)* Its own active flag (Unity's `activeSelf`). |
| `IsActive` | `bool` | *(get)* Active, counting its parents (Unity's `activeInHierarchy`). |
| `Transform`, `RigidBody`, `Animator`, `AudioSource`, `CharacterController`, `Camera`, `CameraOrbit`, `Light`, `ParticleEmitter`, `Collider`, `MeshRenderer`, `PlayerController`, `WorldText`, `MaterialParams`, `FlowFieldAgent`, `FlowFieldSurface`, `StateGraphAgent`, `Terrain` | component handles | *(get)* Shortcuts to the entity's components. |
| `IsNetworked`, `NetworkId`, `Owner`, `IsMine`, `NetVars`, `NetData` | | Networking. See [Network](#network). |

### Constants

| Constant | Value |
|---|---|
| `Untagged` | `"Untagged"` |

### Methods

| Method | Description |
|---|---|
| `static Entity Find(string name)` | An entity with exactly this Hierarchy name, or null. Searches everything: call it in `OnCreate` and keep the result. |
| `Entity FindChild(string name)` | A child, grandchild... with exactly this name, or null. Use it to reach parts of a spawned prefab. |
| `static Entity FindWithTag(string tag)` | An entity with this tag, or null. |
| `static Entity[] FindAllWithTag(string tag)` | Every entity with this tag. |
| `static bool IsTagDefined(string tag)` | Does the project define this tag? |
| `bool CompareTag(string tag)` | Alive and tag is exactly `tag` (case-sensitive). |
| `T GetComponent<T>()` | The component or script of type `T`, or null. With several scripts, the first in Inspector order that is a `T`. A base class matches derived scripts. **Interfaces throw.** |
| `T[] GetComponents<T>()` | Every component or script of type `T`, in Inspector order. `GetComponents<EntityScript>()` returns all scripts. |
| `bool HasComponent<T>()` | Does it have one? |
| `bool SetEnabled<T>(bool enabled)` | Switch one component on or off (Unity's `.enabled`). `T = EntityScript` switches all its scripts. Transform can't be switched off. False if it has none. |
| `bool IsEnabled<T>()` | Is that component switched on? |
| `void SetActive(bool active)` | Unity's `SetActive`: switches the entity and all its children off or back on. On your own entity this pauses your script. |
| `static void Destroy(Entity entity, float delay = 0f)` | Destroys it and its children at the end of this frame, or after `delay` seconds. Its scripts get `OnDestroy`. Null and dead entities are ignored. |

`T` for the component methods can be any component handle on this page, any UI type, or a script
class.

### Example

```csharp
public Entity door;          // set in the Inspector

public override void OnCreate()
{
    Entity light = door.FindChild("Lamp");
    if (light != null) light.Light.Color = Color.Red;

    Health hp = door.GetComponent<Health>();   // another script on the door
}
```

---

## Transform

`public sealed class Transform`

Position, rotation and scale. `Position`, `Rotation` and `Scale` are **local** (relative to the
parent, as in the Inspector). The `World*` properties are in world space.

### Properties

| Property | Type | Description |
|---|---|---|
| `Position` | `Vector3` | Local position (Unity's `localPosition`). |
| `Rotation` | `Quaternion` | Local rotation. |
| `Scale` | `Vector3` | Local scale. |
| `WorldPosition` | `Vector3` | World position (Unity's `position`). Setting it moves the entity there whatever its parent. |
| `WorldRotation` | `Quaternion` | World rotation. |
| `Forward` | `Vector3` | *(get)* World direction of local **-Z**. |
| `Right` | `Vector3` | *(get)* World direction of local +X. |
| `Up` | `Vector3` | *(get)* World direction of local +Y. |
| `Parent` | `Entity` | The parent, or null for a root. Setting it is `SetParent(value, true)`. |
| `ChildCount` | `int` | *(get)* Number of direct children. |

### Methods

| Method | Description |
|---|---|
| `void LookAt(Vector3 target)` | Turn so `Forward` points at `target`. |
| `void LookAt(Vector3 target, Vector3 worldUp)` | Same, with a chosen up direction. |
| `void LookAt(Entity target)` | Look at another entity's position. |
| `bool SetParent(Entity parent, bool worldPositionStays = true)` | Make this the last child of `parent` (null = root). `true` keeps its world position; `false` keeps its local values. False (logged) if refused, e.g. it would make a loop. |
| `Entity GetChild(int index)` | The child at `index`. |

### Example

```csharp
// Move forward at 5 m/s in world space
Transform.WorldPosition = Transform.WorldPosition + Transform.Forward * (5f * deltaTime);
```

---

## Log

`public static class Log`

Writes to the Editor Console. `Console.WriteLine` does **not** show in the Editor.

| Method | Description |
|---|---|
| `static void Info(string message)` | A normal message. |
| `static void Warning(string message)` | A warning (yellow). |
| `static void Error(string message)` | An error (red). |

```csharp
Log.Info("Player picked up " + part.Name);
```

---

## Time

`public static class Time`

| Property | Type | Description |
|---|---|---|
| `DeltaTime` | `float` | *(get)* Seconds since the last frame (the same as `OnUpdate`'s argument). |
| `ElapsedTime` | `float` | *(get)* Seconds of Play, pause excluded (Unity's `Time.time`). |
| `ElapsedTimeAsDouble` | `double` | *(get)* The same, as a double (for long sessions). |
| `FrameCount` | `int` | *(get)* Frames since Play started. |

---

## Application

`public static class Application`

| Property | Type | Description |
|---|---|---|
| `IsPlaying` | `bool` | *(get)* False only while an `[EditorButton]` method runs outside Play. Guard physics, audio and navigation with it. |

---

## EditorButton (attribute)

`[EditorButton]` on a public, parameterless `void` method shows it as a button in the Inspector
(Unity's `[ContextMenu]`). Clicking it outside Play makes a temporary copy of the script with the
Inspector's values and calls only that method (no `OnCreate` / `OnUpdate` / `OnDestroy`).

```csharp
[EditorButton]
public void Generate()
{
    if (!Application.IsPlaying) Log.Info("Previewing in Edit mode");
    // ...build something
}
```

Changes it makes are real and saved with the scene. See EngineGuide section 12.

---

# Math

## Vector2

`public struct Vector2`. Fields `X`, `Y`.

| Member | Description |
|---|---|
| `Vector2(float x, float y)` | Constructor. |
| `Zero`, `One`, `Up` | `(0,0)`, `(1,1)`, `(0,1)`. |
| `Length` | *(get)* Length. |
| `Normalized` | *(get)* The same direction, length 1. |
| `static float Dot(Vector2 a, Vector2 b)` | Dot product. |
| Operators | `a + b`, `a - b`, `v * float`. |

## Vector3

`public struct Vector3`. Fields `X`, `Y`, `Z`.

| Member | Description |
|---|---|
| `Vector3(float x, float y, float z)` | Constructor. |
| `Zero`, `One`, `Up` | `(0,0,0)`, `(1,1,1)`, `(0,1,0)`. |
| `Length` | *(get)* Length. |
| `Normalized` | *(get)* The same direction, length 1. |
| `static float Dot(Vector3 a, Vector3 b)` | Dot product. |
| `static Vector3 Cross(Vector3 a, Vector3 b)` | Cross product. |
| Operators | `a + b`, `a - b`, `v * float`. |

**Not in ScriptCore:** unary minus (write `v * -1f`), `Distance`, `Lerp`, `MoveTowards`,
`ProjectOnPlane`, `ClampMagnitude`, `Down`/`Forward` constants. Use
`Assets/Scripts/Player/MechMath.cs` or write `(a - b).Length`.

```csharp
float distance = (target.Transform.WorldPosition - Transform.WorldPosition).Length;
```

## Quaternion

`public struct Quaternion`. Fields `X`, `Y`, `Z`, `W`.

### Static members

| Member | Description |
|---|---|
| `Identity` | No rotation. |
| `Euler(float x, float y, float z)` / `Euler(Vector3 degrees)` | From Euler angles in degrees. |
| `AngleAxis(float degrees, Vector3 axis)` | A rotation of `degrees` around `axis`. |
| `LookRotation(Vector3 forward)` / `LookRotation(Vector3 forward, Vector3 up)` | A rotation whose forward (-Z) points along `forward`. |
| `FromToRotation(Vector3 from, Vector3 to)` | The rotation that turns `from` onto `to`. |
| `Slerp(a, b, t)` / `Lerp(a, b, t)` | Blend between two rotations. |
| `RotateTowards(from, to, float maxDegrees)` | Turn toward `to` by at most `maxDegrees`. |
| `Angle(a, b)` | Degrees between two rotations. |
| `Dot(a, b)` | Dot product. |
| `Approximately(a, b, float degrees = 0.01f)` | Within `degrees` of each other. |

### Instance members

| Member | Description |
|---|---|
| `EulerAngles` | The rotation as Euler angles (degrees). |
| `Inverse` | *(get)* The opposite rotation. |
| `Normalized`, `Length`, `LengthSquared` | *(get)* |
| `Forward`, `Back`, `Right`, `Up` | *(get)* The rotated direction vectors. |
| `void ToAngleAxis(out float degrees, out Vector3 axis)` | Split into angle and axis. |
| Operators | `q * q` (combine), `q * v` (rotate a vector), `==`, `!=`. |

```csharp
// Turn smoothly toward a target, 180 degrees per second at most
Quaternion want = Quaternion.LookRotation(target - Transform.WorldPosition, Vector3.Up);
Transform.WorldRotation = Quaternion.RotateTowards(Transform.WorldRotation, want, 180f * deltaTime);
```

## Color

`public struct Color`. Fields `R`, `G`, `B`, `A` (0..1, sRGB).

| Member | Description |
|---|---|
| `Color(float r, float g, float b, float a = 1f)` | Constructor. |
| `White`, `Black`, `Red`, `Green`, `Blue`, `Yellow`, `Cyan`, `Magenta`, `Gray`, `Clear` | Named colours. |
| `static Color FromBytes(int r, int g, int b, int a = 255)` | From 0..255 values. |
| `static Color Lerp(Color a, Color b, float t)` | Blend. |
| Operators | `c * float`, `a * b`, `a + b`, `==`, `!=`. |

`Color` is **not** an Inspector field type: set colours in code.

---

# Input

## Input

`public static class Input`

Keyboard, mouse and gamepad state for the current frame.

### Methods

| Method | Description |
|---|---|
| `static bool IsDown(KeyCode key)` | Held this frame (Unity's `GetKey`). |
| `static bool IsPressed(KeyCode key)` | Went down this frame (Unity's `GetKeyDown`). |
| `static bool IsReleased(KeyCode key)` | Went up this frame (Unity's `GetKeyUp`). |
| `static void SetMouseLocked(bool locked)` | Ask to hide and capture the cursor (`true`) or free it. A menu canvas or the Editor can hold it free; the request applies once they let go. |

### Properties

| Property | Type | Description |
|---|---|---|
| `MousePosition` | `Vector2` | *(get)* Window pixels, top-left origin. |
| `MouseDelta` | `Vector2` | *(get)* Pixels moved this frame. **Don't multiply by deltaTime.** |
| `MouseScroll` | `Vector2` | *(get)* Scroll wheel this frame. |
| `IsMouseLocked` | `bool` | *(get)* Is the cursor captured? Gate gameplay input on it. |
| `IsPointerOverUI` | `bool` | *(get)* Is the mouse over UI that blocks clicks? Check before firing. |
| `IsGamepadConnected` | `bool` | *(get)* |
| `GamepadName` | `string` | *(get)* |
| `GamepadLeftStick`, `GamepadRightStick` | `Vector2` | *(get)* -1..1 per axis. |
| `GamepadLeftTrigger`, `GamepadRightTrigger` | `float` | *(get)* 0..1. |

```csharp
if (Input.IsMouseLocked && !Input.IsPointerOverUI && Input.IsPressed(KeyCode.MouseLeft))
    Fire();
```

## KeyCode

`public enum KeyCode`. Used by `Input.IsDown / IsPressed / IsReleased`.

| Group | Values |
|---|---|
| Letters | `A` .. `Z` |
| Number row | `Num0` .. `Num9` |
| Function | `F1` .. `F12` |
| Navigation | `Left`, `Right`, `Up`, `Down`, `Home`, `End`, `PageUp`, `PageDown`, `Insert`, `Delete` |
| Editing | `Space`, `Enter`, `Tab`, `Backspace`, `Escape` |
| Punctuation | `Apostrophe`, `Comma`, `Minus`, `Period`, `Slash`, `Semicolon`, `Equal`, `LeftBracket`, `Backslash`, `RightBracket`, `GraveAccent` |
| Modifiers | `LeftShift`, `LeftControl`, `LeftAlt`, `LeftSuper`, `RightShift`, `RightControl`, `RightAlt`, `RightSuper` |
| Locks | `CapsLock`, `NumLock`, `ScrollLock`, `PrintScreen`, `Pause` |
| Keypad | `KP0` .. `KP9`, `KPDecimal`, `KPDivide`, `KPMultiply`, `KPSubtract`, `KPAdd`, `KPEnter`, `KPEqual` |
| Mouse | `MouseLeft`, `MouseRight`, `MouseMiddle`, `MouseButton4` .. `MouseButton8` |
| Gamepad (Xbox names) | `GamepadA`, `GamepadB`, `GamepadX`, `GamepadY`, `GamepadLB`, `GamepadRB`, `GamepadBack`, `GamepadStart`, `GamepadGuide`, `GamepadLeftStick`, `GamepadRightStick`, `GamepadDpadUp/Right/Down/Left`, `GamepadLT`, `GamepadRT` (triggers count as held past halfway) |

## InputSettings

`public static class InputSettings`

The player's rebindable controls, saved in their settings.

| Member | Description |
|---|---|
| `ForwardKey`, `BackKey`, `LeftKey`, `RightKey`, `JumpKey` | `KeyCode` for each move. |
| `JumpButton` | Gamepad jump button. |
| `MouseSensitivity`, `StickSensitivity` | `float` |
| `InvertY` | `bool` |
| `static void ResetKeys()` | Back to the default keys. |
| `static bool Save()` | Write the settings to disk. |

---

# Physics

## Physics

`public static class Physics`

Queries against the physics world. `ignore` is an entity to see through (usually the shooter).
`layerMask` is built with [LayerMask](#layermask).

### Methods

| Method | Description |
|---|---|
| `static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance = ∞, Entity ignore = null)` | First collider along a ray. |
| `static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance = ∞, Entity ignore = null)` | The same, without the hit details. |
| `static bool Raycast(Ray ray, out RaycastHit hit, float maxDistance = ∞, Entity ignore = null)` | Using a `Ray`. |
| `static bool Raycast(Ray ray, float maxDistance = ∞, Entity ignore = null)` | |
| `static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance, int layerMask, Entity ignore = null)` | With a layer mask. |
| `static bool SphereCast(Vector3 origin, float radius, Vector3 direction, out RaycastHit hit, float maxDistance = ∞, int layerMask = LayerMask.All, Entity ignore = null)` | Sweep a ball. **Unlike Unity**, a body overlapping at the start is reported at distance 0. |
| `static bool CapsuleCast(Vector3 point1, Vector3 point2, float radius, Vector3 direction, out RaycastHit hit, float maxDistance = ∞, int layerMask = LayerMask.All, Entity ignore = null)` | Sweep a capsule. Starting overlaps are hit at distance 0. |
| `static bool CheckSphere(Vector3 position, float radius, int layerMask = LayerMask.All, Entity ignore = null)` | Is anything inside this sphere? |
| `static int OverlapSphereNonAlloc(Vector3 position, float radius, Entity[] results, int layerMask = LayerMask.All, Entity ignore = null)` | Fills `results` with entities inside the sphere; returns the count. |
| `static int OverlapBoxNonAlloc(Vector3 center, Vector3 halfExtents, Entity[] results, Quaternion orientation, int layerMask = LayerMask.All, Entity ignore = null)` | The same, for a box. |
| `static int OverlapCapsuleNonAlloc(Vector3 point0, Vector3 point1, float radius, Entity[] results, int layerMask = LayerMask.All, Entity ignore = null)` | The same, for a capsule. |
| `static int RaycastNonAlloc(Vector3 origin, Vector3 direction, RaycastHit[] results, float maxDistance = ∞, int layerMask = LayerMask.All, Entity ignore = null)` | Every hit along a ray, nearest first. |
| `static int Explode(Vector3 position, float radius, float force, float upwardsModifier = 0f)` | Push every dynamic body within `radius` away. Returns how many entities it reached. Damage is up to you. |

For the `NonAlloc` versions, make the array once (a field), reuse it, and only read the first
`count` entries. Entities destroyed this frame are left out.

### Example

```csharp
private readonly Entity[] _hits = new Entity[16];

void Splash(Vector3 centre)
{
    int count = Physics.OverlapSphereNonAlloc(centre, 4f, _hits, LayerMask.GetMask(PhysicsLayer.Enemy));
    for (int i = 0; i < count; i++)
        Log.Info("Hit " + _hits[i].Name);
}
```

## RaycastHit

`public struct RaycastHit`

| Field | Type | Description |
|---|---|---|
| `Entity` | `Entity` | What was hit. |
| `Point` | `Vector3` | World point of the hit. |
| `Normal` | `Vector3` | Surface normal there. |
| `Distance` | `float` | Metres from the origin. |

## Collision

`public struct Collision`. Passed to `OnCollisionEnter` / `OnCollisionExit`.

| Field | Type | Description |
|---|---|---|
| `Other` | `Entity` | The other entity. Check `Other.IsAlive`. |
| `Point` | `Vector3` | Contact point. |
| `Normal` | `Vector3` | Points from this entity toward the other. |
| `ImpactSpeed` | `float` | How hard they hit. |

On `OnCollisionExit` only `Other` is set.

## Collider

`public sealed class Collider`

| Property | Type | Description |
|---|---|---|
| `Shape` | `ColliderShape` | `Box` (reads Size), `Sphere` (Radius), `Capsule` (Radius, Height), `HeightField` (the Terrain on the same entity; static only). |
| `Size` | `Vector3` | Box size. |
| `Center` | `Vector3` | Offset from the entity. |
| `Radius`, `Height` | `float` | Sphere / capsule size. Height is the total, ends included. |
| `IsTrigger` | `bool` | Blocks nothing; sends `OnTriggerEnter/Exit` instead of collisions. |
| `Layer` | `PhysicsLayer` | `NonMoving`, `Moving`, `Player`, `Enemy`, `Projectile`, `Corpse`. |
| `Friction`, `Bounciness` | `float` | |
| `WorldSize`, `WorldRadius`, `WorldHeight` | | *(get)* Sizes after the Transform's scale. |

## LayerMask

`public static class LayerMask`

| Member | Description |
|---|---|
| `const int All` | Every layer (except `Corpse`, which `All` skips). |
| `static int GetMask(params PhysicsLayer[] layers)` | A mask of the given layers. |

```csharp
int mask = LayerMask.GetMask(PhysicsLayer.NonMoving, PhysicsLayer.Enemy);
```

## RigidBody

`public sealed class RigidBody`

| Member | Description |
|---|---|
| `Velocity` | `Vector3` m/s. |
| `Mass`, `Drag`, `AngularDrag` | `float` |
| `UseGravity`, `IsKinematic` | `bool` |
| `FreezeRotation` | `bool`. Lock all rotation axes (Unity's `freezeRotation`). |
| `void AddForce(Vector3 v)` | Continuous force. |
| `void AddTorque(Vector3 v)` | Continuous torque. |
| `void ApplyImpulse(Vector3 v)` | One-off kick. |
| `bool AddExplosionForce(float force, Vector3 position, float radius, float upwardsModifier = 0f)` | Unity's `AddExplosionForce`, as an impulse. False if nothing was pushed. |

## CharacterController

`public sealed class CharacterController`

A capsule that walks, steps and slides, with the engine's gravity.

| Member | Description |
|---|---|
| `void SimpleMove(Vector3 velocity)` | Walk at this velocity (m/s). The Y part is ignored. **Keeps walking until changed**, and **don't multiply by deltaTime**. `SimpleMove(Vector3.Zero)` stops. |
| `void Jump(float height)` | Jump to about `height` metres. |
| `IsGrounded` | *(get)* `bool` |
| `Velocity` | *(get)* `Vector3`. What it actually moved last step. |
| `Center` | *(get)* `Vector3`. The capsule's offset from the entity. |
| `Height`, `Radius`, `SlopeLimit`, `StepOffset` | `float` |

Setting `Transform.Position` teleports it. Don't use it together with a `PlayerController` on the
same entity (both would move it).

```csharp
Vector3 move = Transform.Forward * (Input.IsDown(KeyCode.W) ? 6f : 0f);
CharacterController.SimpleMove(move);
if (Input.IsPressed(KeyCode.Space) && CharacterController.IsGrounded)
    CharacterController.Jump(1.5f);
```

## Ragdoll

`public static class Ragdoll`

| Member | Description |
|---|---|
| `static bool Spawn(Entity model, Vector3 velocity, Entity owner = null)` | Turn a model (MeshRenderer with a model + AnimatedModel) into a ragdoll at the end of this frame, every piece starting at `velocity`. `owner` is destroyed with the ragdoll. False (and logged) if it can't. |
| `static int Count` | *(get)* Ragdolls alive. |

---

# Player and camera

## PlayerController

`public sealed class PlayerController`

The built-in WASD controller. Remove it if your own script moves the character.

| Property | Type |
|---|---|
| `MoveSpeed` | `float` |
| `JumpHeight` | `float` |
| `FaceCamera` | `bool` |

## CameraOrbit

`public sealed class CameraOrbit`

On the camera pivot (a child of the player). The engine turns it with the mouse and stick.

| Property | Type | Description |
|---|---|---|
| `Sensitivity` | `float` | Mouse. |
| `StickSpeed` | `float` | Gamepad, degrees per second. |
| `MinPitch`, `MaxPitch` | `float` | Pitch limits. |
| `Yaw`, `Pitch` | `float` | Current angles. Set them to aim the camera from a script. |

## Camera

`public sealed class Camera`

| Member | Description |
|---|---|
| `static Camera Main` | *(get)* The enabled camera with the highest Priority, or null. Looked up each call: cache it. |
| `static Vector2 ScreenSize` | *(get)* Game screen size in pixels. |
| `FieldOfView`, `NearClip`, `FarClip` | `float` |
| `Priority` | `int` |
| `Enabled` | `bool` |
| `bool ScreenPointToRay(Vector2 windowPosition, out Ray ray)` | A ray through a window position (pass `Input.MousePosition`). False if not over the game screen. |
| `bool ViewportPointToRay(Vector2 viewportPoint, out Ray ray)` | A ray through 0..1 screen coordinates; `(0.5, 0.5)` is the centre. |
| `bool WorldToScreenPoint(Vector3 worldPosition, out Vector2 screenPosition)` | Where a point appears on screen (top-left origin). False if behind the camera. |
| `bool WorldToScreenPoint(Vector3 worldPosition, out Vector2 screenPosition, out float depth)` | The same, plus depth. |

```csharp
Ray ray;
if (Camera.Main.ViewportPointToRay(new Vector2(0.5f, 0.5f), out ray))
{
    RaycastHit hit;
    if (Physics.Raycast(ray, out hit, 100f, Entity))
        Log.Info("Aiming at " + hit.Entity.Name);
}
```

## Ray

`public struct Ray`

| Member | Description |
|---|---|
| `Ray(Vector3 origin, Vector3 direction)` | Constructor. |
| `Origin`, `Direction` | `Vector3` fields. |
| `Vector3 GetPoint(float distance)` | The point `distance` along the ray. |

---

# Rendering

## MeshRenderer

`public sealed class MeshRenderer`

| Member | Description |
|---|---|
| `Visible` | `bool` |
| `Model` | `Model`. The model drawn. **Setting it clears the plain mesh, the slot overrides and the part root**; an AnimatedModel starts over. |
| `Mesh` | `Mesh`. The built-in mesh drawn when there's no model (`Mesh.Cube`...). Setting it clears Model. |
| `Material` | `Material` for the whole object; null = default / the model's own. |
| `MaterialCount` | *(get)* `int`. The model's material slots. |
| `Material GetMaterial(int slot)` | |
| `bool SetMaterial(int slot, Material material)` | Override one slot; null goes back to the default. |

## MaterialParams

`public sealed class MaterialParams`

Per-object material values (add the component in the Inspector). Objects sharing a material can
still differ here.

| Member | Description |
|---|---|
| `Tint` | `Color` multiplied into the base colour (red when hit, team colours). White = none. Pure black reads as "no tint"; use `(0.01, 0.01, 0.01)` for near-black. |
| `EmissiveScale` | `float`. Glow: 0 off, 1 as the material says, more = brighter. |
| `Custom` | `Vector3`. The raw values behind `Tint`. |
| `void ClearTint()` | Remove the tint. |

```csharp
Entity.MaterialParams.Tint = Color.Red;   // flash red
```

## Light

`public sealed class Light`

| Property | Type | Description |
|---|---|---|
| `Type` | `LightType` | `Directional`, `Point`, `Spot`. |
| `Color` | `Color` | |
| `Intensity`, `Range` | `float` | |
| `SpotAngle`, `InnerSpotAngle` | `float` | Degrees (spot lights). |
| `Enabled`, `CastShadows` | `bool` | |

## ParticleEmitter

`public sealed class ParticleEmitter`

| Method | Description |
|---|---|
| `void Play()` / `void Stop()` | Start / stop emitting. |
| `void Restart()` | A fresh run this frame (the burst fires again). |
| `void Emit(int count)` | Spawn extra particles now (sparks on a hit). |

| Property | Description |
|---|---|
| `IsEmitting` | *(get)* Still emitting. |
| `IsAlive` | *(get)* Emitting or with particles still alive. |
| `ParticleCount` | *(get)* |
| `Loop`, `Duration`, `Rate`, `BurstCount`, `MaxParticles`, `Spread`, `WorldSpace` | Emission. |
| `LifetimeMin/Max`, `SpeedMin/Max`, `SizeMin/Max`, `EndSizeScale`, `GravityScale`, `Drag` | Particles. |
| `StartColor`, `EndColor`, `Brightness` | Colour. |
| `EmitLight`, `LightColor`, `LightIntensity`, `LightRange` | Light from the particles. |

## Graphics

`public static class Graphics`

| Property | Description |
|---|---|
| `PendingShaderBuilds` | *(get)* Shaders still compiling in the background. |
| `ShadersReady` | *(get)* None pending. Handy for a loading screen. |

## QualitySettings

`public static class QualitySettings`

| Member | Description |
|---|---|
| `Level` | `QualityLevel`: `Low`, `Medium`, `High`, `Ultra`, `Custom` (read-only result). Setting a preset changes every setting below. |
| `AntiAliasing` | `Off`, `FXAA`, `MSAA2x`, `MSAA4x`. |
| `Shadows` | `ShadowQuality`: `Off`, `Low`, `Medium`, `High`, `Ultra`. |
| `AmbientOcclusion`, `Bloom`, `VolumetricFog`, `Vignette`, `FilmGrain`, `ChromaticAberration` | `bool` |
| `VSync` | `bool` (not part of presets). |
| `FrameRateLimit` | `int`: `FrameRateDefault` (-1), `FrameRateUnlimited` (0), or 15..1000. |
| `Brightness` | `float` 0.5..1.5. |
| `FieldOfViewOffset` | `float` -10..+30 degrees added to every camera. |
| `static bool Save()` / `static void Revert()` / `static void ResetToDefaults()` | Save, undo unsaved changes, or reset. |

## Screen

`public static class Screen`

| Member | Description |
|---|---|
| `Width`, `Height` | *(get)* Current size. |
| `Mode` | `WindowMode`: `Windowed`, `Maximized`, `Fullscreen`. |
| `FullScreen` | `bool`. Ignored in the editor. |
| `IsEditor` | *(get)* Running inside the Editor. |
| `Resolutions` | *(get)* `Resolution[]` (`Width`, `Height`, `RefreshRate`) the monitor offers. |
| `bool SetResolution(int width, int height, WindowMode mode, int refreshRate = 0)` | Unity's `SetResolution`. Saved for next launch. False in the editor. |
| `bool SetBorderlessFullscreen()` | |
| `AspectRatioLock`, `WindowedWidth/Height`, `FullscreenWidth/Height`, `FullscreenRefreshRate`, `IsExclusiveFullscreen` | Window details. |

---

# Audio

## AudioSource

`public sealed class AudioSource`

| Member | Description |
|---|---|
| `void Play()` / `void Stop()` | Play or stop `Clip`. |
| `void PlayOneShot()` | Play `Clip` once more on top (gunshots). |
| `bool PlayOneShot(AudioClip clip, float volumeScale = 1f)` | Play another clip once without changing `Clip`. Up to 8 a frame. |
| `IsPlaying` | *(get)* Is the `Play()` sound playing? (Not one-shots; updates next frame.) |
| `Clip` | `AudioClip` |
| `Volume`, `Pitch` | `float` |
| `Loop` | `bool` |
| `Bus` | `AudioBus`: `Master`, `Music`, `SFX`. Applies from the next Play. |
| `SpatialBlend` | `float` 0 = 2D, 1 = 3D. |
| `MinDistance`, `MaxDistance` | `float` |

```csharp
public AudioClip reloadClick;
...
AudioSource.PlayOneShot(reloadClick, 0.75f);
```

## Audio

`public static class Audio`

| Member | Description |
|---|---|
| `static Entity PlayClipAtPoint(AudioClip clip, Vector3 position, float volume = 1f, AudioBus bus = AudioBus.SFX)` | Play a clip once in 3D. Returns the temporary entity (move it to make the sound follow something), or null. |
| `MasterVolume`, `MusicVolume`, `SfxVolume` | `float` |
| `static float GetBusVolume(AudioBus bus)` / `static void SetBusVolume(AudioBus bus, float volume)` | |
| `static bool SaveSettings()` | Write the player's settings to `UserSettings.json`. |

---

# Animation

## Animator

`public sealed class Animator`

Sets parameters on the entity's AnimatedModel controller (`.animcontroller`).

| Member | Description |
|---|---|
| `bool SetBool(string name, bool value)` | |
| `bool SetFloat(string name, float value)` | |
| `bool SetInteger(string name, int value)` | |
| `bool SetTrigger(string name)` / `bool ResetTrigger(string name)` | |
| `Speed` | `float` playback speed: 1 normal, 0 freezes the pose (hit stop). Applies immediately. |

Each setter returns false if there's no controller or no parameter by that name.

```csharp
Animator.SetBool("Moving", speed > 0.1f);
Animator.SetFloat("Speed", speed);
if (fired) Animator.SetTrigger("Fire");
```

---

# UI and text

Screen UI is a canvas entity with child entities. Positions are canvas pixels, **+Y is down**.

## UIText

| Property | Type |
|---|---|
| `Text` | `string` |
| `Color` | `Color` |
| `FontSize` | `float` |
| `Font` | `Font` (null = default; a .ttf/.otf must be compiled in the Font Importer) |

## UIImage

| Property | Type | Description |
|---|---|---|
| `Color` | `Color` | |
| `Texture` | `Texture` | |
| `HorizontalFill` | `float` | 0..1, how much shows from the left. Crops without stretching the texture (a bar). |

## UIButton

| Property | Type | Description |
|---|---|---|
| `WasClicked` | `bool` | *(get)* Clicked this frame. |
| `Interactable` | `bool` | |
| `State` | `UIState` | *(get)* `Normal`, `Hovered`, `Pressed`, `Disabled`. |

## UISlider

| Property | Type | Description |
|---|---|---|
| `Value`, `MinValue`, `MaxValue` | `float` | |
| `WholeNumbers` | `bool` | |
| `Interactable` | `bool` | |
| `State` | `UIState` | *(get)* |
| `WasChanged` | `bool` | *(get)* Moved this frame. |

## UIRect

| Member | Description |
|---|---|
| `Position` | `Vector2`. The pivot's offset from the anchors. |
| `Size` | `Vector2` |
| `Rotation` | `float` |
| `bool GetScreenRect(out Vector2 min, out Vector2 max)` | Where it was drawn last frame, in screen pixels. |

## UICanvas

| Property | Type | Description |
|---|---|---|
| `Enabled` | `bool` | |
| `RenderMode` | `CanvasRenderMode` | `ScreenSpace` or `WorldSpace`. |
| `SortOrder` | `int` | Higher draws on top. |
| `PixelsPerUnit`, `Pivot`, `Billboard`, `AlwaysOnTop`, `DoubleSided`, `Intensity`, `Fade`, `FadeStart`, `FadeEnd` | | World-space options. |

## UI

`public static class UI`

| Member | Description |
|---|---|
| `MenuOpen` | `bool`. Is the Escape menu open? Setting it opens/closes it next frame. |
| `static bool WasActionTriggered(string actionId)` | Was a **Custom** button with this Action Id clicked this frame? |

### Example: a health bar

```csharp
public Entity healthFill;   // a UIImage
public float fullWidth = 360f;

void ShowHealth(float ratio)
{
    UIRect rect = healthFill.GetComponent<UIRect>();
    rect.Size = new Vector2(fullWidth * ratio, rect.Size.Y);
}

public override void OnButtonClicked(Entity button)
{
    if (button.Name == "Quit") Scene.Load("Scenes/Game/Main Menu Scene.scene");
}
```

## WorldText

`public sealed class WorldText`

3D text in the world. Sizes are **metres**.

| Group | Properties |
|---|---|
| Content | `Text`, `Color`, `FontSize` (the em; 0.5 ≈ 35 cm letters), `Font`, `RichText`, `Bold`, `Underline` |
| Box | `Size`, `Pivot`, `HorizontalAlign` (`Left/Center/Right`), `VerticalAlign` (`Top/Middle/Bottom`), `Overflow` (`Overflow/Truncate/Ellipsis`), `Wrap`, `LineSpacing`, `CharacterSpacing`, `BestFit`, `MinFontSize`, `MaxFontSize` |
| Facing | `Billboard` (`Off`, `FaceCamera`, `Upright`), `AlwaysOnTop`, `DoubleSided` |
| Look | `Intensity`, `Fade`, `FadeStart`, `FadeEnd`, `Outline`, `OutlineColor`, `OutlineWidth`, `Glow`, `GlowColor`, `GlowWidth`, `GlowSoftness`, `Shadow`, `ShadowColor`, `ShadowOffset`, `ShadowSoftness` |

```csharp
Entity.WorldText.Text = "+" + damage;
Entity.WorldText.Billboard = Billboard.FaceCamera;
```

---

# Scenes and assets

## Scene

`public static class Scene`

| Member | Description |
|---|---|
| `static bool Load(string path)` | Switch scene at the start of the next frame. `path` is inside Assets, e.g. `"Scenes/Level2.scene"`. |
| `static bool LoadImmediate(string path)` | Same, skipping the loading screen. |
| `static bool Reload()` | Reload the current scene. |
| `Name` | *(get)* The current scene's name. |
| `static bool DontDestroyOnLoad(Entity entity)` / `static bool IsDontDestroyOnLoad(Entity entity)` | Keep a root entity across loads. Reloading its own scene brings a second copy: guard with a static `Instance`. |
| `static int BeginPreload(string path)` | Start loading a scene's assets for a loading screen. Returns how many were queued, or -1. |
| `static float StepPreload(float budgetMs = 8f)` | Load for about `budgetMs`; returns progress 0..1. Call every frame until 1, then `Load`. |
| `PreloadProgress` / `static void CancelPreload()` | |
| `ClaimLoads`, `HasHeldLoad`, `HeldLoadSerial`, `BeginPreloadHeld`, `ReleaseHeldLoad` | Used by the loading-screen script to hold a load until it is ready. |

```csharp
// Loading screen
public override void OnCreate() { Scene.BeginPreload(next); }
public override void OnUpdate(float dt)
{
    bar.HorizontalFill = Scene.StepPreload();
    if (bar.HorizontalFill >= 1f) Scene.Load(next);
}
```

## Prefab

`public sealed class Prefab`. Assign it in the Inspector with a `public Prefab` field.

| Member | Description |
|---|---|
| `Name` | *(get)* |
| `Entity Instantiate()` | Spawn where it was saved. |
| `Entity Instantiate(Vector3 position, Quaternion rotation)` | Spawn at a place. |
| `Entity Instantiate(Entity parent)` | Spawn as a child, keeping its saved transform as the local offset. |

Returns the root entity, or null. **The new entity's scripts don't exist until later this frame**,
so `GetComponent<MyScript>()` on it right away returns null.

```csharp
public Prefab bullet;
...
Entity b = bullet.Instantiate(muzzle.Transform.WorldPosition, muzzle.Transform.WorldRotation);
```

## Asset handles (AudioClip, Texture, Font, Material, Model, Mesh)

Small handle classes for assets set in the Inspector. Each has `Name` and `==` / `!=`.

| Class | Notes |
|---|---|
| `AudioClip` | A `.wav` / `.ogg`. |
| `Texture` | An image. |
| `Font` | A `.ttf` / `.otf` (compiled in the Font Importer) or `.cfont`. |
| `Material` | A `.mat`. |
| `Model` | A `.glb`. |
| `Mesh` | Built-ins only: `Mesh.Cube`, `Sphere`, `Capsule`, `Cylinder`, `Cone`, `Pyramid`, `Plane`, `Disc`. Not an Inspector field type. |

---

# AI and navigation

## FlowFieldAgent

`public sealed class FlowFieldAgent`

Moves an enemy over a `FlowFieldSurface` (Unity's `NavMeshAgent`). The agent moves its own entity.

### Methods

| Method | Description |
|---|---|
| `void MoveTo(Vector3 point, float stoppingDistance = -1f, float slowingDistance = -1f)` | Walk straight to a point. Negative = use the settings. |
| `void Arrive(Entity target)` | Chase an entity and stop at `StoppingDistance`. Follows the flow field when the straight line is blocked. Null stands still. |
| `void FollowField()` | Follow the flow field toward the players. |
| `void Stop()` | Stand still. |
| `void FaceTarget(Entity target)` | Keep facing this entity whatever it's doing (strafing). Null = face the way it walks. Stop doesn't clear it. |

### Properties

| Property | Description |
|---|---|
| `HasArrived` | *(get)* Within StoppingDistance (+0.5 m). False until the frame after a new order. |
| `RemainingDistance`, `Velocity` | *(get)* |
| `Speed`, `Acceleration`, `AngularSpeed`, `BaseOffset` | Movement. |
| `StoppingDistance`, `SlowingDistance` | Arrival. |
| `NeighbourRadius`, `AlignmentWeight`, `Radius`, `AvoidanceDistance`, `AvoidanceWeight` | Crowd behaviour. |
| `AvoidancePriority` | 0..99, **lower is more important** (as Unity). |

```csharp
public Entity player;
public override void OnUpdate(float dt)
{
    Entity.FlowFieldAgent.Arrive(player);
    if (Entity.FlowFieldAgent.HasArrived) Attack();
}
```

## FlowFieldSurface

| Property | Description |
|---|---|
| `FloodRadius` | How far the field spreads. |
| `RebuildDistance` | How far a player moves before the field rebuilds. |
| `DynamicUpdateInterval` | Seconds between rechecks of `DynamicObstacle` entities. |

What blocks enemies (tags `StaticObstacle`, `DynamicObstacle`, `Walkable`) is explained in
EngineGuide section 5, "AI".

## Navigation

`public static class Navigation`

| Member | Description |
|---|---|
| `static bool IsWalkable(Vector3 position)` | Could an enemy stand here? False while nothing is baked. |
| `static bool IsPathClear(Vector3 from, Vector3 to)` | Could an enemy walk straight between them? |
| `static bool Rebake()` | Bake again next frame (after changing the ground in Play). |
| `IsBaked` | *(get)* |
| `BakeCount` | *(get)* Changes when a new bake is in. Read it before `Rebake()`. |

## Terrain

`public sealed class Terrain`

Generated ground. Needs a MeshRenderer (no mesh) and a `HeightField` Collider on the same entity.

| Member | Description |
|---|---|
| `bool Build(int cols, int rows, float cellSize, float[] heights, byte[] regions)` | Replace the ground. `heights`: `(cols+1)*(rows+1)` corners, row-major (`z*(cols+1)+x`). `regions`: `cols*rows` `TerrainRegion` values (`Ground`, `Cliff`, `Road`, `Tunnel`). Call `Navigation.Rebake()` after. |
| `bool Build(..., TerrainVolume volume)` | The same, with a cave volume replacing the cells under it. |
| `IsBuilt` | *(get)* |
| `bool GroundHeight(float x, float z, out float height)` | World height of the ground at (x, z). |

`TerrainVolume(Vector3 origin, float voxelSize, int countX, int countY, int countZ)` holds a
`Density` array (above 0 rock, below 0 air); `Index(x, y, z)` gives the array index.

## StateGraphAgent

`public sealed class StateGraphAgent`

Runs a behaviour/state graph on an enemy.

| Member | Description |
|---|---|
| `CurrentState` | *(get)* `string` |
| `bool SetVariableValue<T>(string name, T value)` | Set a graph variable. `T`: `Entity`, `Vector3`, `float`, `int`, `bool`. |
| `bool GetVariableValue<T>(string name, out T value)` | Read one. |

```csharp
Entity.StateGraphAgent.SetVariableValue("Target", player);
```

## Behaviour graph nodes

Custom nodes for the behaviour graph window live in `ManagedScripts/Runtime/AI` (templates in
`ManagedScripts/Templates`). They work like Unity Behavior.

| Type | Description |
|---|---|
| `Node` | Base. Override `Status OnStart()`, `Status OnUpdate()`, `void OnEnd()`. `Status` is `Running`, `Success`, `Failure`. Has `Self` (the enemy), `DeltaTime`, `IsRunning`, `CurrentStatus`. |
| `Action` | A leaf node. |
| `Modifier` | One child (`Child`). |
| `Composite` | Many children (`Children`). |
| `Condition` | Override `bool IsTrue()`. Used on state-graph arrows. |
| `BlackboardVariable<T>` | A field that holds its own value or links to a graph variable: `BlackboardVariable<Entity>.Link("Target")`. Read `.Value`. |
| `[NodeDescription(name:, description:, story:, category:, id:)]` | Required on every node. Never change `id` once a graph uses the node. |
| `BehaviorGraphAsset` | A field that picks another `.behaviorgraph`. |

Built-in nodes: `WaitAction`, `LogAction`, `MoveToAction`, `ArriveAction`, `FollowFieldAction`,
`StopAction`, `RunSubgraphNode`, `OnStartNode`, `SequenceComposite`, `TryInOrderComposite`.
Built-in conditions: `BlackboardBoolCondition`, `TargetWithinCondition`, `TargetBeyondCondition`,
`TimeInStateCondition`, `StateFinishedCondition`.

```csharp
[NodeDescription(name: "Wait", description: "Waits a number of seconds.",
                 story: "Wait [Seconds] s", category: "Action/Delay", id: "my-wait-node")]
public sealed class MyWait : Action
{
    public BlackboardVariable<float> Seconds = new BlackboardVariable<float>(1f);
    float _left;
    protected override Status OnStart()  { _left = Seconds.Value; return Status.Running; }
    protected override Status OnUpdate() { _left -= DeltaTime; return _left <= 0f ? Status.Success : Status.Running; }
}
```

---

# Networking

Multiplayer is **host-authoritative**: the host (or a single-player game) decides what happens;
clients ask with commands and are told with events and network variables. The same code runs
offline, where this machine is the authority. See also `Crucible Engine/ReadMe_ForNetworking.md`
and `Crucible Engine/Docs/NetworkGameplayTest.md`.

## Network

`public static class Network`

### Session state

| Member | Description |
|---|---|
| `IsOffline`, `IsHost`, `IsClient`, `IsOnline` | *(get)* Which kind of machine this is. |
| `IsAuthority` | *(get)* This machine decides game state (host or offline). **Guard every change to shared state with it.** |
| `IsReady` | *(get)* In the game (always true on host/offline; on a client once the world arrived). |
| `LocalPeerId` | *(get)* 0 host/offline, 1..3 a client, -1 not accepted yet. |
| `LocalPlayerName`, `PlayerCount`, `Peers` | *(get)* |
| `IsPeerConnected(int)`, `IsPeerReady(int)`, `GetPeerName(int)`, `GetPing(int)`, `Ping` | Per-player info. |
| `HostPeerId` (0), `MaxPlayers` (4), `Everyone` (-1), `TickRate` (60) | Constants. |
| `static bool HasAuthority(Entity e)`, `static bool RequireAuthority(string what)` | Authority checks. `RequireAuthority` logs once on a client. |

### Hosting and joining

| Member | Description |
|---|---|
| `static bool StartHost(int port = 0, string playerName = null)` | Host from this machine. Result arrives as `OnNetworkStarted` / `OnNetworkStopped`. |
| `static bool Join(string address, int port = 0, string playerName = null)` | Join by IP. |
| `static bool Join(LobbyInfo lobby, string playerName = null)` | Join a listed session. |
| `static bool Leave(string reason = null)` | Leave (the host ends it for everyone). |
| `static bool StartBrowsing()` / `StopBrowsing()` / `Lobbies` / `LastBrowseError` | Lobby browser. |
| `static bool SetLobby(bool open, string status = null)` | Host: allow joins or not, and the status text. |
| `static bool Kick(int peer, string reason = null)` | Host: remove a client. |
| `LastEndCode`, `LastEndReason`, `static string DescribeEnd(bool wasJoined)` | Why the last session ended (`EndCode` constants). |
| `LobbyServerState`, `LobbyDiscovery`, `CanUseLanDiscovery`, `UseLanDiscovery()` and others | Connection-server / LAN discovery details. |

### Objects

| Member | Description |
|---|---|
| `static Entity Spawn(Prefab prefab, Vector3 position, Quaternion rotation, int owner = HostPeerId)` / `Spawn(Prefab prefab, int owner = HostPeerId)` | Authority: create a network object on every machine. The prefab needs a Network Object component. |
| `static bool Despawn(Entity entity, float delay = 0f)` | Authority: remove it everywhere. |
| `static bool SetOwner(Entity entity, int peer)` | Host: give an object to a player. |
| `static Entity FindEntity(uint networkId)` | The local entity for a NetworkId. |
| `static Entity ObjectOf(Entity entity)` | The network object an entity belongs to. **Use on raycast hits.** |
| `static Entity GetPlayerEntity(int peer)`, `LocalPlayerEntity`, `static bool IsLocalPlayer(Entity e)` | Player characters. |
| `static bool KeepAcrossScenes(Entity e)`, `KeepPlayersAcrossScenes`, `IsKeptAcrossScenes(Entity e)` | Keep objects through `LoadScene`. |

On `Entity`: `IsNetworked`, `NetworkId`, `Owner` (PeerId), `IsMine` (belongs to this machine's
player: `if (!Entity.IsMine) return;`), `NetVars`, `NetData`.

### Messages

| Member | Description |
|---|---|
| `static bool SendCommand(ushort kind, NetWriter data = null, Entity target = null)` | Ask the authority to do something. Arrives in `OnNetworkCommand`. |
| `static bool SendEvent(ushort kind, NetWriter data = null, Entity target = null, int toPeer = Everyone)` | Authority: tell every machine something happened. Arrives in `OnNetworkEvent`. |
| `static bool SendEventExcept(int exceptPeer, ushort kind, NetWriter data = null, Entity target = null)` | Everyone but one player. |
| `static bool SendEventUnreliable(ushort kind, NetWriter data = null, Entity target = null)` | Cheap, may be lost (footsteps, sparks). |

### Scenes, time and input

| Member | Description |
|---|---|
| `static bool LoadScene(string path)` / `LoadScene(string path, int seed)` | Host: move the whole session to a scene. (`Scene.Load` during a session ends it.) |
| `SceneSeed`, `HasSceneSeed` | The same seed on every machine, for generated levels. |
| `Time` (`double`), `Tick`, `HasSharedTime` | The shared clock. Store end ticks in network variables for countdowns. |
| `static void SetLocalInput(Vector3 move, float yawDegrees, float pitchDegrees, uint actions)` | Give this machine's input this frame. Call every frame from one script on every machine. |
| `static void SetLocalActions(uint actions, float pitchDegrees = 0f)` | Only actions and pitch (when PlayerController moves the player). |
| `static NetInput GetInput(int peer)`, `LocalInput` | A player's input this frame. |
| `ViewTick`, `MaxRewindSeconds`, `static bool RaycastAt(double viewTick, Vector3 origin, Vector3 direction, out RaycastHit hit, ...)` | Lag-compensated hit tests on the host. |

### Example: a door any player can open

```csharp
public sealed class NetDoor : EntityScript
{
    const ushort OpenCmd = 1;
    const int OpenSlot = 0;
    bool playerNearby;                 // set from OnTriggerEnter / OnTriggerExit
    void PlayOpenAnimation() { Animator.SetTrigger("Open"); }

    public override void OnUpdate(float dt)
    {
        if (playerNearby && Input.IsPressed(KeyCode.F))
            Network.SendCommand(OpenCmd, null, Entity);        // ask the host
    }

    public override void OnNetworkCommand(int sender, ushort kind, NetReader data)
    {
        if (kind == OpenCmd) Entity.NetVars.SetBool(OpenSlot, true);   // host decides
    }

    public override void OnNetVarChanged(int slot)
    {
        if (slot == OpenSlot && Entity.NetVars.GetBool(OpenSlot)) PlayOpenAnimation();  // everyone sees it
    }
}
```

## Network callbacks (on EntityScript)

| Callback | When |
|---|---|
| `OnNetworkStarted()` | This machine is in a session (`IsReady` from here on). Never in offline games. |
| `OnNetworkStopped(string reason)` | The session ended or a join failed. |
| `OnPeerJoined(int peer)` / `OnPeerReady(int peer)` / `OnPeerLeft(int peer)` | Another player joined / finished loading / left. Host: give them what they need in `OnPeerReady`. |
| `OnNetworkCommand(int sender, ushort kind, NetReader data)` | Authority only. `sender` can't be faked; **`data` can: validate it.** |
| `OnNetworkEvent(ushort kind, NetReader data)` | Every machine. Check `data.Ok`. |
| `OnNetVarChanged(int slot)` | One of this entity's NetVars changed (the frame after). Read values in `OnCreate` too. |
| `OnNetDataChanged()` | This entity's NetData changed. |
| `OnOwnershipChanged(int previousOwner, int newOwner)` | The object changed owner. |
| `OnNetworkSceneLoading()` | The session is moving scene (only DontDestroyOnLoad scripts hear it). |
| `OnAllPeersReady()` | Host: every client has loaded the new scene. Start the round here. |

## NetVars

`public sealed class NetVars`. `Entity.NetVars`: 64 numbered slots (`SlotCount`) the authority
sets and every player sees.

| Method | Description |
|---|---|
| `GetInt`, `GetFloat`, `GetBool`, `GetVector3`, `GetEntity` `(int slot)` | Read. |
| `SetInt`, `SetFloat`, `SetBool`, `SetVector3`, `SetEntity` `(int slot, value)` | Write (authority). A Vector3 uses 3 slots. |

## NetData

`public sealed class NetData`. `Entity.NetData`: up to 256 bytes (`MaxBytes`) of state the
authority sets, for lists and anything that doesn't fit numbered slots.

| Member | Description |
|---|---|
| `NetReader Read()` | Read the bytes. |
| `bool Set(NetWriter writer)` / `bool Set(byte[] bytes)` / `bool Clear()` | Write (authority). |
| `Bytes`, `Length` | *(get)* |

## NetWriter / NetReader

Pack and unpack message data. Writers chain.

| NetWriter | NetReader |
|---|---|
| `WriteByte`, `WriteBool`, `WriteUShort`, `WriteShort`, `WriteUInt`, `WriteInt`, `WriteFloat`, `WriteString`, `WriteVector3`, `WriteQuaternion`, `WriteEntity` | `ReadByte`, `ReadBool`, `ReadUShort`, `ReadShort`, `ReadUInt`, `ReadInt`, `ReadFloat`, `ReadString`, `ReadVector3`, `ReadQuaternion`, `ReadEntity` |
| `Clear()`, `Length`, `Overflowed`, `MaxBytes` (16000), `MaxStringBytes` (1000) | `Ok` (false after a bad read), `Remaining`, `AtEnd` |

`WriteEntity` only works for network objects; anything else reads back as null.

```csharp
Network.SendEvent(HitEvent, new NetWriter().WriteEntity(target).WriteFloat(damage));
...
public override void OnNetworkEvent(ushort kind, NetReader data)
{
    if (kind != HitEvent) return;
    Entity target = data.ReadEntity(); float damage = data.ReadFloat();
    if (!data.Ok || target == null) return;
    ShowDamageNumber(target, damage);
}
```

## NetMessage

`public struct NetMessage`. Named message kinds, so scripts don't have to agree on numbers.

| Member | Description |
|---|---|
| `static NetMessage Named(string name)` | A kind made from the name (the same on every machine). |
| `static NetMessage Fixed(string name, ushort kind)` | A chosen number. |
| `Name`, `Kind`, `bool Is(ushort kind)` | |
| `SendCommand(...)`, `SendEvent(...)`, `SendEventExcept(...)`, `SendEventUnreliable(...)` | Same as the `Network` methods, with this kind. |
| `static void OnCommand(NetMessage m, NetCommandHandler handler, EntityScript owner = null)` | Register a command handler (in `NetRouter`). |
| `static void OnEvent(NetMessage m, NetEventHandler handler, EntityScript owner = null)` | Register an event handler. |

```csharp
static readonly NetMessage Fire = NetMessage.Named("Fire");
...
if (kind == Fire.Kind) { ... }
```

## NetScript and NetFields

`public abstract class NetScript : EntityScript`. A script whose `NetField` fields are synced
automatically through the entity's NetVars.

| Field type | Value | Slots |
|---|---|---|
| `NetInt(int slot, int initial = 0)` | `int` | 1 |
| `NetFloat(int slot, float initial = 0f)` | `float` | 1 |
| `NetBool(int slot, bool initial = false)` | `bool` | 1 |
| `NetVector3(int slot[, Vector3 initial])` | `Vector3` | 3 |
| `NetEntityRef(int slot)` | `Entity` | 1 |
| `NetTimer(int slot)` | countdown: `Start(seconds)`, `Stop()`, `Remaining`, `IsRunning`, `JustFinished()` | 1 |

Each has `.Value` and a `Changed` event `(old, new)`, which fires on every machine the frame after
a change.

Rules: the fields are found before `OnCreate`; every field needs its own slots, and all scripts on
one entity share its 64 slots; a `NetScript` that overrides `OnNetVarChanged` **must call
`base.OnNetVarChanged(slot)`**; the object's Network Object must replicate NetVariables.

```csharp
public sealed class NetHealth : NetScript
{
    readonly NetFloat _hp = new NetFloat(0, 100f);

    public override void OnCreate() { _hp.Changed += (oldV, newV) => UpdateBar(newV); }

    public void TakeDamage(float amount)
    {
        if (!Network.RequireAuthority("TakeDamage")) return;
        _hp.Value = _hp.Value - amount;
    }
}
```

## NetGameState

`public abstract class NetGameState : NetScript`. One match-wide state object per game.

| Member | Description |
|---|---|
| `static NetGameState Current` | The one in the scene. |
| `Phase`, `Wave` | `int`, with `PhaseChanged` / `WaveChanged` events. |
| `MatchTimer` | `NetTimer` |
| `GetScore(int peer)`, `SetScore(int peer, int value)`, `AddScore(int peer, int amount)` | Scores (authority sets). `ScoreChanged` event. |
| `static bool RequestScore(int amount)` | Any machine: ask for points for this player. |
| `protected virtual bool AllowScoreRequest(int peer, int amount)` | Override to vet client requests. |
| `FirstGameSlot` (32) | Your own NetFields start here. |

## NetEffect

`public sealed class NetEffect`. Fire-and-forget effects played on every machine.

| Member | Description |
|---|---|
| `static NetEffect Register(string name, NetEffectHandler handler, bool unreliable = false)` | Every machine registers it with the same name. Handler: `(Entity target, Vector3 position, NetReader extra)`. |
| `static NetEffect Find(string name)` | |
| `bool Play(Entity target = null, Vector3 position = default, NetWriter extra = null)` | Plays here now and on every other machine. |

## NetInput, PlayerActions, ActionMap

| Type | Members |
|---|---|
| `NetInput` (struct) | `Move`, `Yaw`, `Pitch`, `Held`, `Pressed`, `IsFresh`, `IsDown(int action)`, `WasPressed(int action)`, `None`, `MaxActions` (32). |
| `PlayerActions` | `static NetInput Of(Entity entity)`: the input of whoever owns the entity. |
| `ActionMap` | `Bind(int action, KeyCode key)` (chains), `Unbind`, `Clear`, `uint Sample()` (the held bits), `uint Send(float pitchDegrees = 0f)` / `Send(Vector3 move, float yaw, float pitch)` (sample and call `Network.SetLocal*`). |

```csharp
const int Shoot = 0, Jump = 1;
readonly ActionMap _keys = new ActionMap().Bind(Shoot, KeyCode.MouseLeft).Bind(Jump, KeyCode.Space);

public override void OnUpdate(float dt)
{
    _keys.Send();                                   // every machine
    NetInput input = PlayerActions.Of(Entity);       // this character's owner
    if (Network.IsAuthority && input.WasPressed(Shoot)) FireFor(Entity);
}
```

---

## Keeping this page up to date

1. When you add or change a public member under `ManagedScripts/Runtime`, edit its table here.
2. Give new members a one-line description and, if it's not obvious, a short example.
3. Bump **Last updated** at the top.
4. If the change is a gotcha or a workflow change, update [EngineGuide.md](EngineGuide.md) too.
