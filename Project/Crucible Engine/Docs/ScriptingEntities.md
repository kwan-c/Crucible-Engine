# S5 - Entity script components

The S5 entity workflow attaches C# instances to the existing ECS using one
ScriptComponent per entity. It adds Inspector creation/selection, authored scene
data, PlayMode scheduling, owner identity and managed logging. The animation-only
follow-up adds Animator parameter writes and example graphs; see
[AnimatorParameterPreview.md](AnimatorParameterPreview.md). Transform, physics,
input, audio and resource APIs remain team-owned extension work.

## Create a script from an entity

1. Use the Mono-enabled Editor build from [ScriptingRuntime.md](ScriptingRuntime.md).
2. Select an entity. In Inspector open **Add Component** and type the script name.
3. For a new name select **Create C# script "Name"**, then **Create and assign**.
   For an existing compiled class select its **(C#)** result to attach it directly.
   Creation defaults to the Crucible.Scripts namespace; the modal allows changing it.
4. The Editor creates `Assets/Scripts/<ClassName>.cs` and assigns its class
   to this component. Open that source file in your C# editor to add logic.
5. Save the scene. Compile the scripts (the Editor can stay open) using the
   Visual Studio EditorManagedScripts target or:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\BuildScripts.ps1
   ```

6. Restart Editor, reopen the saved scene, and press Play. The component heading
   is the script name with a `.cs` suffix. Public fields appear below its enabled checkbox; the
   template writes creation and destruction messages to the Editor Console.

For existing compiled scripts, use **Add Component**. It lists public,
concrete, nongeneric classes with a public parameterless constructor that derive
from Crucible.EntityScript. New/changed code requires a script build, then Scripts > Reload Scripts.
There is no class picker, refresh button, creation button, Advanced settings or
routine execution-counter display inside an attached script component.

Add Component searches those same compiled classes in Crucible.Scripts.dll,
alongside native components. It hides the raw ScriptComponent entry. Results
include namespaces to distinguish equally named classes. A source file with the
searched name that has not appeared in the assembly produces a build/restart hint
instead of an offer to create a duplicate. Attachment/creation is disabled during
Play/Pause. There is currently one script per entity: to replace an assigned class,
remove the existing component first. An unassigned
ScriptComponent can be filled through Add Component without adding a duplicate.

Creation from Add Component adds the component and assignment as one undo step;
Cancel adds neither a component nor a source file. Source files stay on disk after
Undo. The Asset Browser does not create scripts; C# project sources live under
Assets/Scripts and can be opened from the Asset Browser.

## Try the logging example

1. Build the Mono-enabled Editor and reopen it.
2. Select an entity without an assigned script. In Add Component search
   **ScriptLogProbe** and select **Crucible.Scripts.ScriptLogProbe (C#)**.
3. Open the Editor Console and press Play. Expect OnCreate, then a first-frame
   OnUpdate message. A further OnUpdate message appears at 5, 10, 15, ... seconds
   of accumulated Play time with the default settings. Pause/disable suspends the timer.
   The component exposes Log Messages (bool), Message Interval Seconds (float) and
   Message (string). Edit them in Edit Mode to save initial values, or during
   Play/Pause to change only the current instance. Turning Log Messages off mutes
   periodic messages; lifecycle messages still appear.
4. Stop: OnDestroy prints the number of periodic messages sent. Play again:
   a fresh instance starts its timer at zero.

The example logs OnUpdate's first call and each five-second interval rather than
every frame, so the Console stays readable. These are Crucible's three current
entity-script callbacks; Unity's additional callbacks are not implied. See
Assets/Scripts/ScriptLogProbe.cs. The example is ordinary project source
and is not automatically attached to any existing scene.

Creation writes a new source file exclusively; it never overwrites an existing
file. Invalid/reserved names or namespaces, missing templates and unavailable source folders
produce an error in the modal. Source creation requires a source checkout: the
Editor checks for ManagedScripts beside the active Assets folder and writes the
new file to Assets/Scripts. The modal retains
its original target and refuses assignment if selection, component or PlayMode
state has changed. Cancel creates nothing.

Component add/remove, class assignment and enabled edits use the existing undo
and dirty-scene workflow. Undoing assignment leaves the source file on disk.
Public field edits use that same scene/undo workflow in Edit Mode. Replace a
script through Remove Component and Add Component; authored fields are specific
to the attached class and are cleared on a new assignment.

## Public fields

Supported fields are public mutable instance bool, int, float, double, string
and `Entity` fields, including inherited fields. Private/protected fields,
static/const/readonly fields, [NonSerialized] fields, properties, enums, arrays
and other custom/object types are excluded. This does not yet implement private [SerializeField] or arbitrary
Unity serialization. Rebuild and use Scripts > Reload Scripts after editing a script's declaration.

For example, adding public bool canMove = true; exposes a Can Move checkbox.
Default values come from C# field initializers. Inspecting a class with supported
fields constructs a temporary object once per class per process, without binding
an entity or calling lifecycle callbacks. Keep entity-dependent work in OnCreate,
not constructors. Classes without supported fields need no inspection instance.
Constructor failures are caught and shown as a compact Inspector diagnostic.

Edit Mode stores only overrides, keyed by declaring type, name and field type.
These travel through scene/prefab/clone/undo/Play snapshots via the existing
EntityIO/RTTR path. They are applied after construction and owner binding and
before OnCreate. Removed, renamed or type-changed fields keep their old serialized
overrides but cannot write another field; new fields use their C# initializer.
Inherited fields hiding the same name have distinct identities and labels.

In Play/Pause the Inspector reads and edits the managed instance directly; it
does not author a new override or dirty the scene. Pause retains edits; Stop
discards them and restores Edit values. Disabled existing instances retain fields.
An initially disabled script has no instance yet, so its fields stay read-only
during that session until an instance starts. Script-enabled is independent of
a user-defined public bool such as Log Messages. Non-finite numeric writes and
invalid UTF-8 strings are rejected without partially applying a field batch.

### Referencing other entities

Declare `public Entity door;` and drag an entity from the Hierarchy onto the
field in the Inspector (X clears it). This is Unity's `public GameObject door;`.

- The field saves the target's scene id, not its Entity handle, so the link
  survives saving, reloading and play-mode Stop.
- When the script starts, the id is turned into a live `Entity`, with
  `Transform`, `RigidBody`, `Animator`, `Name` and `IsAlive`. An unassigned
  field, or a target that is no longer in the scene, is `null` (a missing one
  also logs a warning).
- If the target is destroyed during Play the reference stays non-null, but
  `IsAlive` turns false and component calls do nothing. Check `IsAlive` if
  that can happen.
- `EntityScript.Entity` (the script's own entity) is the same type, so
  `Entity.Transform` and `door.Transform` work the same way.
- `Entity.Find("Door")` returns an entity by its Hierarchy name, or null. Names
  need not be unique (any match may be returned), and it searches every entity,
  so call it in OnCreate and keep the result.
- `GetComponent<T>()` / `HasComponent<T>()` on an `Entity` (or on the script
  itself, for its own entity). T is `Transform`, `RigidBody`, `Animator`, or a
  script class: `door.GetComponent<Door>()` returns the Door script so you can
  read its fields and call its methods. `GetComponent<EntityScript>()` returns
  whatever script is there. Null if the entity has no such component/script.
  Any other T throws.
- Not yet: more component types.

### Switching things on and off

- `entity.SetEnabled<T>(bool)` / `IsEnabled<T>()` (Unity's `.enabled`) for any
  type `GetComponent` accepts; a script class means the entity's script. Uses
  World's per-component on/off (data kept; queries skip it, so it isn't drawn,
  simulated, heard or updated). Transform can't be switched off. Returns false if
  the entity has no such component.
- `EntityScript.Enabled`: this script on / off (no OnUpdate or UI callbacks
  while off; switch it back on from another script).
- `entity.SetActive(bool)`, `IsActive` (it and every parent - Unity's
  activeInHierarchy), `IsActiveSelf`. The engine has no entity-level active, so
  SetActive(false) switches off every component that is on, on the entity and
  all its children, and remembers which; SetActive(true) turns exactly those back
  on (anything switched off separately stays off; a child deactivated on its own
  stays inactive). On your own entity it pauses your script too. The records are
  Play-only and are cleared on Stop / scene change.

C++ side: `Bindings/EnableBindings.cpp` (World's `TrySetComponentEnabled` /
`IsComponentEnabled` by ComponentId).

### Spawning and destroying

Declare `public Prefab bulletPrefab;` and drag a `.cprefab` from the Asset
Browser onto it (saved as the prefab's asset guid, so renames keep the link).

- `prefab.Instantiate()`, `Instantiate(position, rotation)` (as a root) and
  `Instantiate(parent)` (keeps the prefab's saved transform as the local offset)
  build the whole prefab at once and return its root `Entity`, or null. You can
  set up the copy straight away (`copy.GetComponent<Bullet>().speed = 40f`);
  scripts on it get OnCreate on the next frame. Loaded prefabs are cached and
  reloaded only when the file changes.
- `entity.FindChild("Trail")` searches the entity's children, grandchildren
  and so on by Hierarchy name - the way to reach parts of a spawned prefab.
- `Entity.Destroy(e)` (or `Destroy(e)` inside a script) destroys an entity and
  its children at the end of the frame; `Destroy(e, 3f)` after 3 seconds of Play
  time. Until then the entity works normally. Scripts in the subtree get
  OnDestroy first, children before parents. Stop discards pending destroys.
- Everything spawned during Play is removed on Stop (the scene snapshot is
  restored).
- `public Entity` fields (and physics joint connected bodies) between parts of
  a prefab point at the copy's own parts in every copy - placed in the editor
  or spawned with `Instantiate`. The prefab file stores them by part (entry id);
  references to entities outside the prefab are cleared when it is saved (with
  a warning), like Unity. Prefabs saved before this change need saving again.
  See `Scene/EntityRefs.h`.

### Transform: world space and hierarchy

- `Position` / `Rotation` / `Scale` are LOCAL (relative to the parent).
  `WorldPosition` and `WorldRotation` are world space (Unity's
  `transform.position` / `rotation`), worked out from the current local values
  (`World::TryGetWorldMatrix`), so they are right even after a move earlier in
  the same frame. Setting them converts into the parent's space.
- `Forward` (local **-Z**, the engine's convention for cameras and lights),
  `Right` (+X), `Up` (+Y), all world space. `LookAt(position)`,
  `LookAt(position, worldUp)`, `LookAt(entity)`.
- `Parent` (get/set), `SetParent(parent, worldPositionStays = true)` (returns
  false and logs why if refused), `ChildCount`, `GetChild(i)`.

C++ side: `Bindings/HierarchyBindings.cpp` (local values stay in
`ComponentBindings.cpp`).

### Camera

- `Camera.Main`: the enabled Camera with the highest `Priority` (first found on
  a tie) - the same choice the renderer makes. `GetComponent<Camera>()` for a
  specific one. Settings: `FieldOfView` (vertical, degrees), `NearClip`,
  `FarClip`, `Priority` (raise it to switch cameras), `Enabled`, plus `Entity`.
- `Camera.ScreenSize`: the game screen in pixels (Game view / window).
- `ScreenPointToRay(Input.MousePosition, out Ray ray)`: takes WINDOW pixels and
  maps them onto the Game view; false if the mouse is not over it.
  `ViewportPointToRay(new Vector2(0.5f, 0.5f), out ray)` aims through the
  screen centre (locked cursor). `Physics.Raycast(ray, out hit, ...)` takes the
  `Ray` directly.
- `WorldToScreenPoint(world, out Vector2 pixels[, out float depth])`:
  game-screen pixels, origin top-left (the UI's space); false behind the camera.
- Screen maths uses the camera's current world matrix and FOV, so it works for
  any camera and is right even after a move earlier in the frame.
- `CameraOrbit` (the player's third-person pivot, turned by the mouse/stick):
  `Sensitivity`, `StickSpeed`, `MinPitch`, `MaxPitch`, `Yaw`, `Pitch` - for aim
  tuning or recoil. Usually on the camera's parent.

C++ side: `Bindings/CameraBindings.cpp`. Graphics/, Player/ and the components
are unchanged; Core passes the Renderer (`SetRendererBinding`).

### Light

- `GetComponent<Light>()` (or `Entity.Light`): `Type` (`LightType.Directional`,
  `Point`, `Spot`), `Color`, `Intensity`, `Range`, `SpotAngle` (outer cone),
  `InnerSpotAngle`, `Enabled`, `CastShadows` (first enabled directional light
  only), plus `Entity`.
- The renderer reads every Light each frame, so changes show the same frame.
  A light shines along `Transform.Forward` (local -Z): aim a spotlight with
  `Transform.LookAt(target)`.
- `Color` (new, shared with UI later): `R, G, B, A` 0..1, `Color.Red` etc.,
  `FromBytes`, `Lerp`, `*`, `+`. Lights ignore alpha and accept channels above 1.

C++ side: `Bindings/LightBindings.cpp`. The Light component is unchanged.

### Particles

- `GetComponent<ParticleEmitter>()` (or `Entity.ParticleEmitter`):
  - `Play()` (a stopped emitter starts a fresh run, burst included), `Stop()`
    (no new particles; live ones finish), `Restart()` (a fresh run this frame,
    even while playing), `Emit(count)` (extra particles this frame, playing or
    not; `MaxParticles` still caps), `IsEmitting`, `ParticleCount` (alive as of
    last frame), `IsAlive` (emitting or particles alive - false once a finished
    effect has faded, so it is safe to `Destroy`).
  - Spawning: `Loop`, `Duration`, `Rate`, `BurstCount`, `MaxParticles`,
    `Spread`, `WorldSpace`.
  - Each particle: `LifetimeMin/Max`, `SpeedMin/Max`, `SizeMin/Max`,
    `EndSizeScale`, `GravityScale`, `Drag`.
  - Look: `StartColor`, `EndColor` (`Color`, alpha = opacity), `Brightness`.
  - Light: `EmitLight`, `LightColor`, `LightIntensity`, `LightRange`.
  - Shape, texture, flipbook and blend stay Inspector-only.
- Particles fire around local **+Y** (not Forward): rotate the entity to aim.
- `Restart` and `Emit` are requests to `ParticleSystem` (v1.1:
  `ParticleSystem::Restart`, `Emit`, `GetParticleCount`), applied at its next
  update - PreRender, after scripts, so the same frame. A request for an entity
  without a valid emitter that frame is dropped.

C++ side: `Bindings/ParticleBindings.cpp`, and `Graphics/ParticleSystem.*`
(the request list and per-emitter count; Core passes the system with
`SetParticleSystemBinding`). The component is unchanged.

### UI

Get them with `GetComponent<T>()` on the UI entity (`ManagedScripts/Runtime/UI.cs`):

- `UIText`: `Text`, `Color`, `FontSize`.
- `UIImage`: `Color` (alpha 0 hides it).
- `UISlider`: `Value` (setting it clamps / rounds; doesn't count as a change),
  `MinValue`, `MaxValue`, `WholeNumbers`, `Interactable`, `State`, `WasChanged`.
- `UIButton`: `WasClicked`, `Interactable`, `State` (`UIState`: Normal,
  Hovered, Pressed, Disabled).
- `UIRect`: `Position`, `Size` (reference pixels, **+y down**), `Rotation`,
  `GetScreenRect(out min, out max)` (last frame's box in screen pixels).
- `UICanvas`: `Enabled` (show / hide a whole HUD or menu).
- Static `UI`: `MenuOpen` (the Escape menu; applied next frame),
  `WasActionTriggered("Id")` for buttons whose On Click is Custom.

- `UIImage.Texture` and `UIText.Font` take `public Texture` / `public Font`
  fields (drag an image / a .ttf, .otf or .cfont onto them; saved as asset guids
  like `AudioClip`). Null = a solid colour / the default font. UISystem looks the
  asset up every frame, so a swap shows immediately. A .ttf / .otf must have been
  compiled in the Font Importer.

Two ways to react to the player:

- Callbacks: override `OnButtonClicked(Entity button)`,
  `OnSliderChanged(Entity slider, float value)` or `OnUIAction(string actionId)`
  on `EntityScript`. Every running script hears every UI event once, in the frame
  it happens, before that frame's `OnUpdate`; compare the entity with your own
  fields (`if (button == startButton)`). Only scripts that override one of them
  are called (checked once per class). An exception faults the script like a
  failing `OnUpdate`.
- Polling: `WasClicked` / `WasChanged` / `WasActionTriggered` are true
for exactly the frame the player did it. UISystem sends `UIEvents` on the
engine bus at the end of PreUpdate (before scripts); `UIBindings.cpp`
subscribes (Core passes `EventBuses` via `SetEventsBinding`) and records them,
and `EntityScriptSystem` hands them to the callbacks (step 5b, via
`ScriptRuntime::DispatchUIEvent` -> `EntityScript.DispatchUIEvent`) and clears
them at the end of its Update. Colours are sRGB 0-1 with alpha.

C++ side: `Bindings/UIBindings.cpp`. `UI/` is unchanged.

### World text and world-space canvases

- `GetComponent<WorldText>()` (or `Entity.WorldText`): `Text`, `Color`,
  `FontSize`, `Font`; placement `Size`, `Pivot`, `Billboard` (`Off` /
  `FaceCamera` / `Upright`), `AlwaysOnTop`, `DoubleSided`, `Intensity` (> 1
  glows), `Fade` / `FadeStart` / `FadeEnd`; layout `HorizontalAlign`,
  `VerticalAlign`, `Overflow`, `Wrap`, `LineSpacing`, `CharacterSpacing`,
  `RichText`, `Bold`, `Underline`, `BestFit` / `MinFontSize` / `MaxFontSize`;
  effects `Outline` / `OutlineColor` / `OutlineWidth`, `Glow` / `GlowColor` /
  `GlowWidth` / `GlowSoftness`, `Shadow` / `ShadowColor` / `ShadowOffset` /
  `ShadowSoftness`. World units (metres) before the Transform's scale.
  `SetEnabled<WorldText>` / `HasComponent` work too.
- The components were not changed: WorldTextSystem and the world-canvas pass
  read them every frame, and the layout cache only lays text out again when the
  text or a layout setting changes. Setting the same `Text` every frame is free
  (and doesn't stamp the component as changed).
- World-space `UICanvas`: `RenderMode` (read-only), `SortOrder`, and the
  world-only `PixelsPerUnit`, `Pivot`, `Billboard`, `AlwaysOnTop`,
  `DoubleSided`, `Intensity`, `Fade` / `FadeStart` / `FadeEnd`. Its
  `UIText` / `UIImage` children work through the usual UI classes.
- Damage numbers: a prefab with a WorldText, `Instantiate` at the hit,
  set `Text`, move it, `Destroy(e, 1f)`.
- Buttons on a world-space canvas can't be clicked (UISystem only takes the
  mouse on screen-space canvases) - an engine feature, not a binding.
- The C# UIText still has only Text / Color / FontSize / Font; its alignment and
  effect settings could be added the same way (same field names).

C++ side: `Bindings/UIBindings.cpp` (generic GetText / SetText, GetEnum /
SetEnum, GetInt / SetInt templates). Test: `Assets/Scripts/WorldTextTest.cs`.

### Scene loading

- `Scene.Load("Scenes/Level2.scene")` (a path inside Assets), `Scene.Reload()`,
  `Scene.Name`. Uses `SceneManager::RequestLoad`, like the UI's Load Scene
  button: the switch happens at the start of the next frame; old scripts get
  OnDestroy, new ones start. Stop still returns to the scene open at Play.
- `Time.ElapsedTime` keeps counting across a scene switch; pending
  `Destroy`s and one-shot sounds are dropped.

C++ side: `Bindings/SceneBindings.cpp`.

### Collisions and triggers

Override on `EntityScript` (Unity's names):

- `OnCollisionEnter(Collision c)`: `c.Other`, `c.Point`, `c.Normal` (from this
  entity toward Other), `c.ImpactSpeed` (m/s, for damage / volume).
- `OnCollisionExit(Collision c)`: only `c.Other` is set.
- `OnTriggerEnter(Entity other)` / `OnTriggerExit(Entity other)`: a Collider
  with Is Trigger ticked; called on the trigger's script AND the entering one's.

From Hugo's `Physics/PhysicsEvents.h`. Physics steps after scripts, so the
callbacks arrive at the start of the next frame, before OnUpdate (like Unity,
after the physics step). Both entities' scripts are called. The other entity
may have been destroyed since: check `other.IsAlive`. There is no
OnCollisionStay. Only scripts that override one are called; a throwing callback
faults the script like OnUpdate.

C++ side: listeners in `Bindings/PhysicsBindings.cpp`, `EntityScriptSystem`
step 5b, `ScriptRuntime::DispatchPhysicsEvent`.

### Collider and MeshRenderer

- `GetComponent<Collider>()` (or `Entity.Collider`): `Shape`
  (`ColliderShape.Box` / `Sphere` / `Capsule`), `Size` (Box, full size),
  `Radius` (Sphere / Capsule), `Height` (Capsule, total), `Center`, `Friction`,
  `Bounciness`, `Layer` (`PhysicsLayer.NonMoving` / `Moving` / `Player` /
  `Enemy` / `Projectile`), `IsTrigger`; read-only `WorldSize` / `WorldRadius` /
  `WorldHeight` (after the Transform's scale, what physics uses).
- Sizes are before scale, as in the Inspector. Size axes, Radius and Height
  must be above 0; Friction and Bounciness not negative. Bad values are refused.
- Every change rebuilds the physics body in that frame's step (a moving body
  keeps its velocity); setting the same value again does nothing. Fine once in
  a while, not every frame on many bodies.
- `SetEnabled<Collider>(false)` removes the body (no collisions at all).
- `GetComponent<MeshRenderer>()` (or `Entity.MeshRenderer`): `Visible` hides
  only the mesh - collider, scripts and children carry on. Drawn the same frame.
  `SetEnabled<MeshRenderer>(false)` works too. Materials and models: see below.

C++ side: `Bindings/ColliderBindings.cpp`. Collider (Hugo's
`Physics/PhysicsComponents.h`) is unchanged. Test:
`Assets/Scripts/ColliderTest.cs`.

### Materials, models and glow (MeshRenderer, MaterialParams)

- New field types `Material` (a .mat) and `Model` (a model file): public
  fields picked in the Inspector, saved as asset guids, with `.Name`.
- `MeshRenderer.Material`: the material for the whole object (null = the
  default material, or a model's own imported ones). `MeshRenderer.Model`: the
  model (setting it clears the plain mesh, part root and slot overrides, as the
  Inspector does; an AnimatedModel starts over). `MaterialCount`,
  `GetMaterial(slot)` / `SetMaterial(slot, material)` override one model slot
  (wins over Material; null = back to the model's own; false for a slot the
  model doesn't have). Drawn the same frame.
- Only the material the object USES changes; the .mat and other objects using
  it are untouched. A model's imported materials can't be picked - make a .mat.
- `GetComponent<MaterialParams>()` (add the component in the Inspector):
  `EmissiveScale` (glow multiplier, 0 = off, 1 = as the material) and `Custom`
  (three spare shader numbers). Per object, without breaking batching.
- Built-in meshes, code only: `MeshRenderer.Mesh = Mesh.Sphere` (`Mesh.Cube`,
  `Sphere`, `Capsule`, `Cylinder`, `Cone`, `Pyramid`, `Plane`, `Disc`; `.Name`).
  Setting a mesh clears the Model (a model always wins), via
  `ResourceManager::AssignMesh`, as the Inspector's Mesh picker. Their ids are
  `ResourceManager::BuiltinId(name)` - fixed hashes of the renderer's primitive
  names, so renaming one breaks saved references. A `null` Mesh.X means the
  renderer has no primitive by that name.
- Not done: a `public Mesh` field picked in the Inspector (would need a field
  type validated through the ResourceManager, not the AssetManager); a
  per-object colour tint - needs a shader change (`MeshRenderer::colour` is
  not drawn).

C++ side: `Bindings/MeshRendererBindings.cpp` (uses the ResourceManager from
`ScriptEngineConnection::resources` for slot counts). Test:
`Assets/Scripts/MeshRendererTest.cs`.

### Keybinds (InputSettings) and PlayerController

- The mech's keys are the PLAYER'S settings, not the scene's: `UserSettings.json`
  (next to Editor.exe / the game .exe, never in git), under `"input/keys"`,
  saved by key name:
  `{ "forward": "W", "back": "S", "left": "A", "right": "D", "jump": "Space", "jumpButton": "GamepadA" }`.
  Every scene and every mech uses them. A missing or misspelled key keeps its
  default; delete the file to reset everything. The gamepad's left stick always
  walks.
- Static `InputSettings`: `ForwardKey`, `BackKey`, `LeftKey`, `RightKey`,
  `JumpKey`, `JumpButton` (any `KeyCode`, mouse / gamepad buttons too;
  `KeyCode.Unknown` = unbound), `ResetKeys()`, plus `MouseSensitivity`,
  `StickSensitivity` (0.1 .. 10) and `InvertY`. A change applies at once;
  `InputSettings.Save()` writes the file (`Audio.SaveSettings()` writes the
  same file). Like the volumes, changes are NOT undone by Stop and are lost on
  exit unless saved.
- `GetComponent<PlayerController>()` (or `Entity.PlayerController`):
  `MoveSpeed`, `JumpHeight` (0 = no jumping), `FaceCamera` - designer values,
  saved in the scene. `SetEnabled<PlayerController>(false)` stops the mech.
- Changed in Hugo's files (tell Hugo): `Configuration/UserSettings.h/.cpp`
  v1.1 (the key fields, load / save by name), `ECS/ECSPlayerControllerSystem.cpp`
  v1.7 (reads them instead of hardcoded WASD / Space / A). The PlayerController
  component itself is unchanged.

C++ side: `Bindings/InputSettingsBindings.cpp` (uses `GetUserSettings()` from
`AudioBindings.cpp`), `Bindings/PlayerControllerBindings.cpp`. Test:
`Assets/Scripts/KeybindTest.cs`.

### Window mode and resolution (Screen)

- Static `Screen` (Unity's): `Mode` (`WindowMode.Windowed` / `Maximized` /
  `Fullscreen`), `FullScreen` (on / off; off goes back to maximized or
  windowed, whichever it was), read-only `Width` / `Height` (the game screen,
  same as `Camera.ScreenSize`), `AspectRatioLock` (the game locks the window to
  the project's shape; 0 = free), `IsEditor`.
- Resolution: `Screen.Resolutions` (the monitor's fullscreen sizes, smallest
  first, `Resolution` = Width / Height / RefreshRate),
  `Screen.SetResolution(w, h, mode, refreshRate = 0)`:
  - `Fullscreen` = EXCLUSIVE: the monitor switches to w x h (must be in
    Resolutions; refresh 0 = the highest). The screen blinks on the switch and
    alt-tab minimizes the game so the desktop gets its resolution back.
  - `Windowed` / `Maximized` = the window's size (Maximized uses it when
    restored). Kept inside the monitor's work area; with the aspect lock the
    width is kept and the height follows.
  - `SetBorderlessFullscreen()` = fullscreen at the monitor's own resolution
    (the default, quick alt-tab). `Mode = Fullscreen` uses whichever was chosen
    last. Read back: `WindowedWidth` / `Height`, `FullscreenWidth` / `Height` /
    `RefreshRate` (0 = borderless), `IsExclusiveFullscreen`.
- Everything is saved to `UserSettings.json` at once (Core, the F11 path):
  `"game": { "graphics": { "fullScreen", "maximized", "windowWidth",
  "windowHeight", "fullscreenWidth", "fullscreenHeight", "fullscreenRefreshRate" } }`.
  A saved resolution the monitor no longer offers falls back to borderless. The
  editor's section now remembers its windowed size too (it overrides
  EngineConfig.json's windowWidth / windowHeight on the next launch).
- Not done: Windows' strict exclusive mode (`VK_EXT_full_screen_exclusive`, a
  renderer change) and a render scale (draw smaller, upscale).
- IGNORED IN THE EDITOR, as in Unity: the window there is the editor. The
  first ignored change logs once. Reading works in both apps. Test in the game.

C++ side: `Bindings/ScreenBindings.cpp` (Core through
`ScriptRuntime::SetCoreBinding`); `WindowManager::SetWindowedSize` /
`SetFullscreenResolution` / `GetResolutions`; `Core` wrappers;
`EngineConfigLoader::SaveWindowState(app, SavedWindowState)`. Test:
`Assets/Scripts/ScreenTest.cs`.

### Movement, raycasts and time

- `GetComponent<CharacterController>()` (or `CharacterController`): Unity's
  CharacterController. `SimpleMove(velocity)` walks at m/s until changed (do not
  multiply by deltaTime; `SimpleMove(Vector3.Zero)` stops), `Jump(height)` in
  metres, `IsGrounded`, `Velocity`, and `Height` / `Radius` / `SlopeLimit` /
  `StepOffset`. Physics (PostUpdate) uses requests in the same frame. Don't put
  a script-driven controller on an entity that also has a PlayerController -
  both would call SimpleMove.
- `Physics.Raycast(origin, direction, out RaycastHit hit, maxDistance, ignore)`
  returns the first collider hit: `hit.Entity`, `Point`, `Normal`, `Distance`.
  `ignore` (usually the shooter) is skipped inside the physics query (ECSPhysicsSystem::Raycast's EntityFilter), so a ray may start inside it. Rays
  hit Colliders and CharacterControllers (their hidden capsule), not triggers. Play only.
- `Time.DeltaTime`, `Time.ElapsedTime` (Playing seconds since Play; Pause
  excluded; `ElapsedTimeAsDouble` too) and `Time.FrameCount`. Reset on Stop,
  not on `Scene.Load`.

C++ side: `Bindings/PhysicsBindings.cpp`, `Bindings/TimeBindings.cpp` (play
clock in `EntityContext`, advanced by `EntityScriptSystem`). Physics/ and
ECSPhysicsSystem are unchanged.

### Audio

`GetComponent<AudioSource>()` (or `Entity.AudioSource`) gives Unity's
AudioSource for an entity with an AudioSource component. `public AudioClip`
fields take an audio file dragged from the Asset Browser (saved as its guid).

- `Play()`, `Stop()`, `PlayOneShot()`, `PlayOneShot(clip)`, `IsPlaying`.
- `Clip`, `Volume` (0..1), `Pitch`, `Loop`, `Bus` (`AudioBus.Master` / `Music` / `SFX`, Unity's "Output"), `SpatialBlend` (0 = 2D, 1 = 3D),
  `MinDistance`, `MaxDistance`.
- Requests are carried out by ECSAudioSystem later in the same frame.
  `IsPlaying` is updated once per frame, so it is still false right after
  `Play()`. Volume and Pitch change a sound already playing; the other settings (Bus included)
  apply from the next Play: a sound already playing stays on its old bus (moving a live voice would need an Audio/ change).
- `PlayOneShot(clip, volumeScale = 1)` plays another clip once on top, WITHOUT
  changing `Clip` (Unity's), e.g. a reload click from the gun's source. Different
  clips in one frame each play (up to 8 a frame; returns false past that).
  `volumeScale` multiplies `Volume`; changing `Volume` while it plays resets it to
  the source volume. Uses `AudioSource::PlayOneShot(clip, scale)` (Audio/
  AudioComponents.h v1.3) and ECSAudioSystem v1.6.
- Static `Audio` class:
  - `Audio.MasterVolume`, `MusicVolume`, `SfxVolume`, or
    `GetBusVolume(AudioBus)` / `SetBusVolume(AudioBus, v)` (0..1). These are the
    player's settings (Core's UserSettings, which ECSAudioSystem reads every
    frame), heard next frame. `Audio.SaveSettings()` writes UserSettings.json.
    They are not scene data, so Stop does not undo them.
  - `Audio.PlayClipAtPoint(clip, position, volume = 1, bus = AudioBus.SFX)` plays a clip once in 3D
    on that bus (SFX unless given), Unity-style: a temporary "One Shot Audio" entity with a Play
    On Awake AudioSource, destroyed once the sound ends (or after 2 s if it
    never starts). Returns that entity, or null. Pause and Stop apply as for
    any AudioSource.
- Bindings only - `Audio/`, `ECSAudioSystem` and `UserSettings` are unchanged.
  Not yet: per-shot clips for one source (needs an AudioSource change).

C++ side: `Bindings/AudioBindings.cpp`; one-shot tracking in
`Bindings/EntityContext.cpp`, ticked by `EntityScriptSystem`.

C++ side: `Bindings/PrefabBindings.cpp` (Instantiate, Destroy),
`EntityScriptSystem::ApplyDestroys`, `EntityContext` (destroy queue).

#### OnCreate order

When Play starts, every script is created and given its field values first;
only then does each get OnCreate (like Unity's Awake then Start). So
`GetComponent<Door>()` in OnCreate finds Door even if Door's own OnCreate has
not run yet - don't rely on anything Door sets up in its OnCreate until the
first OnUpdate. A script whose OnCreate never ran gets no OnUpdate or OnDestroy.

C++ side: `ScriptRuntime.cpp` (`IsEntityReference`, `ReadFields`, `WriteFields`,
`StartEntityInstance`), `EntityScriptSystem.cpp` (create / start / update
passes, `LookupScript`), `Bindings/EntityContext.*` (scene id <-> entity,
script lookup hook) and `Bindings/EntityBindings.cpp`.

The command-line equivalent for entity source creation is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\NewScript.ps1 -Name MyScript -Entity
```

Without `-Entity`, NewScript retains S4's standalone class template. Standalone
S3 `scripting.instances` entries remain available for application-lifetime work;
entity scripts belong on ScriptComponent, not in that list.

## Managed API

Project scripts reference the new `Crucible.ScriptCore.dll` automatically. Its
sources are under `ManagedScripts/Runtime`; it is built/deployed alongside
Crucible.Scripts and the existing examples using the S4 dependency helper.

```csharp
using Crucible;

namespace Crucible.Scripts
{
    public sealed class MyScript : EntityScript
    {
        private float elapsed;

        public override void OnCreate()
        {
            elapsed = 0;
            Log.Info("Started on entity " + Entity);
        }

        public override void OnUpdate(float deltaTime)
        {
            elapsed += deltaTime;
        }

        public override void OnDestroy()
        {
            Log.Info("Stopped after " + elapsed + " seconds");
        }
    }
}
```

Callbacks are synchronous public instance void methods; inherit defaults or use
the exact override signatures. Async void and Task-returning callbacks are not
supported. Entity is bound after construction and before OnCreate. Constructors
must not expect an owner. Entity.Index and Entity.Generation are read-only runtime
identity values, not GUIDs, liveness tests or cross-scene references. Do not retain
them as saved scene data. No entity lookup, transform access or destruction API
is exposed yet.

Log.Info, Log.Warning and Log.Error use the existing native user-facing logger,
so messages reach the Editor Console and other configured sinks. Null and invalid
UTF-16 messages are handled safely. System.Console.WriteLine retains its usual
process-stdout behavior. Managed exceptions use the S3 native error/stack reporting.

## Lifecycle policy

| Trigger | Entity script behavior |
|---|---|
| Edit | No construction or callbacks |
| Play | Each enabled assigned component gets a fresh bound object, OnCreate once, then OnUpdate each frame |
| Initially disabled | No object until first enabled during Play |
| Disable | Existing instance/fields retained; updates stop |
| Re-enable | Same instance resumes; OnCreate does not repeat |
| Pause | Existing instances retained without updates; new instances wait for Resume |
| Resume | Retained instances continue; new enabled assignments may start |
| Stop | Instances disposed; existing PlayMode restores the authored scene snapshot |
| Next Play | Fresh instances and fields; Mono itself remains alive |
| Remove component/entity | Managed cleanup at the next script-system safe point |
| New/load scene during Play | Old instances disposed; new scene's enabled components start |
| New/load scene during Pause | Old instances disposed; new construction waits for Resume |
| Engine exit | Entity instances disposed before GPU shutdown and world/resource destruction |

Instance fields reset on a new Play. Static managed state and loaded assemblies
persist for the process lifetime; restarting the application resets those. Releasing
the host's GC handle does not force collection if user code retains the object.

OnDestroy is attempted once. Failed construction produces no lifecycle callbacks.
Failed OnCreate gets one partial-cleanup attempt and leaves a diagnostic, not a
live object. Failed OnUpdate faults only that instance, attempts OnDestroy and
releases its root. Other scripts continue. Failed creation/update does not retry
every frame or on a disable/re-enable toggle. Correct configuration and start a
new Play session; changes to compiled code also require a build and Scripts > Reload Scripts.

Class assignment controls are disabled during Play/Pause; enabled can still be
toggled. Programmatic retargeting releases the old object and defers any new
construction until Playing. Failed creation is retried after a definition change
or a fresh session. Errors remain available in the status tooltip without a
repeating log every frame. The three live status rows keep a fixed height.

World hooks only queue cleanup; they never invoke C# inside ECS hook dispatch.
OnDestroy after removal/scene replacement can therefore run after its owner is
gone. Owner identity remains available, but future native bindings must reject
stale owners. No teardown ordering between different entities is promised.

## Native integration and serialization

ScriptComponent stores enabled, assembly path, namespace, class name and public
field overrides.
The existing component registry/X-macro, RTTR, EntityIO, prefab, clone, undo and
PlayMode snapshot paths handle those values. Runtime handles, errors, counters
and live managed objects stay in EntityScriptSystem/ScriptRuntime. Authored public
field values are scene data; transient changes to a running instance are not.
Copying a component does not copy or share its managed instance.

EntityScriptSystem is registered once in Core. It uses the current World,
SceneManager, ECS scheduler and PlayMode session revision. It runs on the main
thread as an exclusive Update-stage system; it stays scheduled in Edit/Pause for
cleanup while enforcing Play-only managed updates. It copies component definitions
out of the query before running C#, keeping managed execution outside active ECS
queries. Removal hooks and scene listeners are lifetime guarded.

ScriptRuntime's entity creation verifies the ScriptCore base class, binds owner
identity before OnCreate and owns the object's GC handle. Entity instances are
excluded from S3 UpdateInstances, and only the adapter calls UpdateEntityInstance.
This preserves configured standalone instances' original application-lifetime
policy and prevents double updates in Edit/Play/Pause. Fault handling reuses S3.

With Mono disabled, ScriptComponent remains authorable/serializable. Creation of
an enabled assigned instance reports Mono unavailable once per attempted
definition/session; the component and normal engine features remain usable.

## Subsystem-owner handoff

The currently exposed managed API is EntityScript, EntityIdentity, Log and an
entity-bound Animator parameter facade. Animator supports SetBool, SetFloat,
SetInteger, SetTrigger and ResetTrigger; it queues typed writes through the
existing animation system without moving entities. It is bound before OnCreate,
and calls return false for stale/missing targets, invalid parameter types or names,
worker threads and calls outside a Play session. See AnimatorParameterPreview.md
for the contract and example scene. These APIs retain the engine's ECS and
subsystem ownership.

For each further API, agree on an SDK-free native adapter owned by that subsystem.
Add checked Mono marshalling/registration inside ScriptRuntime.cpp, where the
current Log internal call is registered after JIT initialization and before user
assemblies execute. Keep Mono headers and object pointers inside that file.
Resolve owner/world context for each call and validate entity generation plus
scene/session lifetime; a bare index is insufficient. Native structural changes
must use the existing command-buffer/safe-point rules. Declare scheduler access,
callback lifetime and error behavior as part of each adapter.

Do not reenter public host lifecycle APIs from managed callbacks. Do not restart
Mono on Play/Stop. Replacing script DLLs on disk is safe (they load from memory)
but has no effect until the application restarts. Background
task/thread tracking, additional serialized field types, multiple script
slots per entity, debugger attachment and hot reload are separate work.

## Verification

Completed Debug/x64 verification:

- Full Editor and Game builds pass with Mono enabled and disabled. The no-Mono
  Editor opens Modular Animation Preview and closes normally; nine no-Mono API/
  component checks confirm stable unavailable diagnostics and retained authoring data.
- 149 real managed/native entity lifecycle checks cover class filtering, rejection
  of unbound standalone entity scripts, full-width owner values bound before
  OnCreate, user-facing log levels/null/invalid UTF-16, independent instances,
  serialization/cloning, Edit/Play/Pause/Resume/Stop, enable changes, source
  definition correction, snapshot restoration, removal/re-addition, destruction,
  scene changes, callback failure isolation, teardown errors and stale host handles.
- Actual rendered Inspector Add Component/Create/Assign/Undo/Redo/Save actions
  pass 28 checks. The created source is compiled through S4, then a fresh process
  reopens its saved scene, selects the compiled class and verifies exact Play/
  Pause/Resume/Stop callback counts across another 71 checks. Creation/destruction
  messages reach the native user-facing log sink used by the Console.
- 24 source-authoring checks cover names/namespaces, project availability,
  exclusive duplicate-file protection, reserved-name diagnostics and template
  expansion. Generated classes named Log, EntityScript and Entity, plus escaped
  C# keywords, compile against ScriptCore using qualified framework references.
  The command-line entity template and its reserved-name rejection are checked too.
- Closing a real Core loop with an active component calls OnDestroy exactly once
  and clears entity instances while the host remains alive for final cleanup.
- Recompiled existing suites pass: 172 standalone lifecycle checks, 78 assembly
  invocation checks, 10 example checks and 410 Core/modular-animation checks,
  including materials, sockets, scene replacement and PlayMode behavior.

Temporary fixtures/logs are ignored under build/setup-tools/scripting-s5-*.
The original UI test source was removed and its project DLL rebuilt. The later
Add Component workflow includes the reusable ScriptLogProbe example above; no
shared scene/config is changed. Native Release packaging,
managed debugger attachment and long-duration stability are unverified. The
component workflow and Animator parameter writes are implemented; other
subsystem-specific S5 bindings remain pending.

The Add Component C# search update is also verified in the rendered Inspector:
case-insensitive existing-class search/attachment, disabled duplicate attachment,
creation using the searched name, Cancel without side effects, one-step Undo/Redo,
scene saving and a rebuilt/restarted process running the saved assignment.
ScriptLogProbe produces OnCreate/first-OnUpdate logs, exactly one five- and
ten-second message, holds its timer in Pause and starts fresh after Stop/Play.
Editor and Game Debug builds pass. These fixtures/logs use the ignored
build/setup-tools/scripting-search-* prefix; their generated verification source
is removed before the final assemblies are rebuilt. ScriptLogProbe.cs remains
as the user-facing example.

The named-component/public-field update passes 87 native checks for initializers,
field visibility, inheritance/hiding, Unicode/null strings, transactional input
validation, overrides before OnCreate, scene round-trip, live edits and independent
runtime state. Rendered Inspector checks cover a public checkbox, Undo/Redo,
scene saving, Play/Pause edits, Stop restoration and stable height (88 checks),
plus reopening the saved field in a fresh process (20 checks). Existing assembly
(78), lifecycle (172) and dependency-loading (9) checks still pass. Debug Editor
and Game builds pass. Fixtures use ignored build/setup-tools/scripting-fields-*;
no shared scene/config is changed.
