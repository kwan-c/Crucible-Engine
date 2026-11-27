# MainMenuManager — Implementation & Limitations Report

**Scene:** `Main Menu Scene V2`
**Script:** `Project/Crucible Engine/Assets/Scripts/MainMenuManager.cs`
**Target assembly:** `Crucible.Scripts.dll` (every `.cs` under `Assets/Scripts/` is compiled there automatically).

---

## 1. What was implemented (all possible steps)

| # | Requirement | Status | How it is done |
|---|-------------|--------|----------------|
| 1 | On scene open, set the `Camera` XYZ to a public `Vector3` named `camPosition1` | ✅ Done | `public Vector3 camPosition1`; `cameraEntity.Transform.WorldPosition = camPosition1;` in `OnCreate()`. |
| 2 | `camPosition2` public `Vector3` | ✅ Done | `public Vector3 camPosition2`. |
| 3 | Public move-speed variable | ✅ Done | `public float moveSpeed` (units/sec for movement, degrees/sec for rotation). |
| 4 | Catch the `Start` Button's action id (`"Start"`) | ✅ Done | `public override void OnUIAction(string actionId)` — the Start Button has `On Click = "Custom"`, `Action Id = "Start"`, which the engine dispatches to every running script. |
| 5 | Disable `logoModel`'s MeshRenderer component | ✅ Done | `logoModel.SetEnabled<MeshRenderer>(false);` |
| 6 | Move the Camera to `camPosition2` over time, scaled by `moveSpeed` | ✅ Done | Frame-by-frame `MoveTowards` in `OnUpdate(float deltaTime)` (the engine's `Vector3` has no built-in `Lerp`/`MoveTowards`, so a small helper implements it with `Vector3` arithmetic). |
| 7 | On reaching `camPosition2`, cast a ray from the mouse; on left-click hitting `table1`'s Collider, move the Camera to `table1`'s `CamPos` child and rotate −90° on X | ✅ Done | `Input.IsPressed(KeyCode.MouseLeft)` + `Camera.ScreenPointToRay(Input.MousePosition, out Ray)` + `Physics.Raycast(ray, out hit)`, then compare `hit.Entity == table1`; move to `table1.FindChild("CamPos").WorldPosition` and rotate toward `Quaternion.Euler(-90, 0, 0)`. |

The scene entities are resolved by Hierarchy name in `OnCreate` with `Entity.Find("Camera")`, `Entity.Find("logoModel")`, `Entity.Find("table1")` and `table1.FindChild("CamPos")` (this is the engine's recommended pattern — resolve once in `OnCreate`, not every frame).

---

## 2. What was skipped and why (animation steps are not scriptable from C#)

The following steps **cannot be executed** with the engine's current C# scripting API and were skipped:

1. Play `logoModel`'s `"Logo_Start"` animation once, without loop.
2. Reset `logoModel`'s pose to rest afterwards.
3. Play `"Logo_Explode"` once, without loop, when `Start` is pressed.
4. Wait for the `"Logo_Explode"` animation to finish before disabling the MeshRenderer.

### Why

The only animation surface exposed to C# is the `Crucible.Animator` class (`ManagedScripts/Runtime/Animator.cs`), and it only contains controller **parameter** setters:

```csharp
SetBool / SetFloat / SetInteger / SetTrigger / ResetTrigger
```

These calls flow through `AnimatorBindings.cpp → IScriptAnimatorBinding::SetParameter → AnimatedModelSystem::SetParameter`. That implementation (`Engine/src/Animation/AnimatedModelSystem.cpp`, line 17) rejects the call unless the entity's `AnimatedModel.controllerAsset` is **non-zero** (i.e. it must have an AnimatorController). There is:

- **no** C# method to play a clip by name,
- **no** C# method to set the `AnimatedModel.clip`, `loop`, `speed`, or `playOnStart` fields,
- **no** C# method to query whether a clip is playing / finished,
- **no** C# method to reset a pose to rest.

The `logoModel` entity in this scene has an `AnimatedModel` component configured as:

```json
"AnimatedModel": {
    "controllerAsset": 0,          // 0 = direct single-clip playback, no controller
    "clip": 4294967295,            // InvalidModelIndex = no clip assigned
    "speed": 1.0,
    "loop": false,
    "playOnStart": true
}
```

So `Animator.SetTrigger("Logo_Start")` / `SetTrigger("Logo_Explode")` would simply return `false` and do nothing, because there is no controller (`controllerAsset == 0`) and no clip is assigned. There is no other C# path to drive a named clip or read playback state.

### Resulting behaviour

Because "wait for the animation to finish" is also impossible to detect, the script disables `logoModel`'s MeshRenderer **immediately** when the `Start` action is caught, then begins the Camera move to `camPosition2`. Everything else works as specified.

---

## 3. How to use the script

1. In the editor, open the **Main Menu Scene V2** scene.
2. Add the `MainMenuManager` script component to any entity in that scene (a dedicated empty entity is fine).
3. In the Inspector set:
   - `camPosition1` (Camera start position),
   - `camPosition2` (Camera destination after `Start`),
   - `moveSpeed` (movement/rotation speed).
4. Enter Play mode. Clicking the **Start** button disables the logo model and drives the Camera to `camPosition2`; from there, a left-click on **table1** flies the Camera to `table1/CamPos` while pitching −90° on X.

No C++ changes were made and none are required.
