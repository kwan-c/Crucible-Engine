# Scripting test checklist

One test script at a time: several use the same keys (F1, B, G, ...). Every script logs to the
Console with its own tag, e.g. `[AudioTest]`. A test passes when every box under it is ticked.

Before you start:

- [ ] Fresh build: Engine, Editor, Game, `CrucibleScriptCore`, `CrucibleProjectScripts`.
- [ ] Editor opens with no `[Script]` errors, and the Console has no `MissingMethodException` on Play
      (that means a C# / C++ binding name mismatch - note which call).
- [ ] Play, Stop, Play again with a test script on an entity: no "Cannot re-enter the managed host" error.

---

## 1. Input - `InputTest`

Setup: on any entity. Press Play.

- [y] Each key prints "pressed" once and "released" once (not every frame).
- [y] Mouse buttons and gamepad buttons log the same way.
- [y] WASD / arrow keys move the entity; the gamepad left stick moves it too.
- [y] Scrolling, the triggers, and plugging / unplugging a gamepad are logged.
- [y] F1 prints the mouse and gamepad state.

## 2. Entity references - `EntityRefTest`

Setup: on entity A; drag entity B onto "Target". Press Play.

- [y] OnCreate logs B's name.
- [y] B bobs up and down.
- [y] T logs both positions and "alive".
- [y] With Target empty: logs that it is unassigned, no errors.
- [y] Save, reload the scene: Target is still B.

## 3. Find / GetComponent - `FindComponentTest`

Setup: `EntityRefTest` on entity A (with a Target). This script on entity C, "Find Name" = A's
Hierarchy name. Press Play.

- [y] OnCreate logs that A was found, its components, and its `EntityRefTest` script.
- [y] B speeds up A's bobbing, N slows it down.
- [y] A wrong name logs "No entity named ...", no errors.

## 4. Spawn / destroy - `SpawnTest`

Setup: on any entity; "Prefab" = a .cprefab. Optional: "Child Name" = a child in the prefab. Press Play.

- [y] G spawns a copy next to the entity; it disappears after "Lifetime" seconds.
- [y] H spawns a copy as a child of the entity (check the Hierarchy).
- [y] K destroys the newest copy at once.
- [y] Each spawn logs the copy and its script; with Child Name set, FindChild finds the child.
- [y] Stop: every spawned copy is gone from the Hierarchy.

## 5. Audio source - `AudioTest`

Setup: entity with an AudioSource that has a clip (Play On Awake off). Optional "Other Clip". Press Play.

- [y] P plays / stops; the log says "Started playing" / "Stopped playing" a frame later.
- [y] O plays overlapping one-shots (press it fast).
- [y] I plays Other Clip, and the log says the source's own Clip is unchanged.
- [y] U plays both clips in the same frame (Other Clip at half volume).
- [y] 1 / 2 change the volume, 3 / 4 the pitch, of a sound already playing.
- [y] K cycles Bus Master -> Music -> SFX (logged; also shown in the start-up line). After K, press P: turning that bus down (AudioBusTest 5-0, or the options menu) quiets it, the other buses do not.
- [y] L toggles Loop; it applies from the next P.

## 6. Audio buses - `AudioBusTest`

Setup: any entity; "Clip" = an audio file. Press Play.

- [y] J plays the clip to the right of the entity; a "One Shot Audio" entity appears in the
      Hierarchy and goes away when it ends.
- [y] H plays it on the Music bus (log says "Music bus"): 7 / 8 change its volume, 9 / 0 do not.
- [y] 5 / 6 Master, 7 / 8 Music, 9 / 0 SFX volume change what you hear.
- [y] V saves; after restarting the editor the volumes are as saved (`UserSettings.json` "audio").
- [y] Put the volumes back to 1 (6 / 8 / 0) and save.

## 7. Movement, raycasts, time - `MoveTest`

Setup: entity with a CharacterController (no Collider) above a floor with a Collider. Press Play.

- [y] I J K L walk along the world axes; it stays on the floor and climbs small steps.
- [y] Space jumps only when grounded.
- [y] R logs the raycast hits down and along +Z (the floor's name and distance).
- [y] Every 2 s it logs ElapsedTime and FrameCount; Pause stops them, Resume continues.

## 8. Transform and scenes - `TransformSceneTest`

Setup: on an entity; "Target" = another entity; "Next Scene" = e.g. `Scenes/Fog_Lighting_Effects.scene`. Press Play.

- [y] T turns to face Target; U then steps 1 m forward (toward it).
- [y] Y turns 45 degrees.
- [y] O logs world position / rotation / directions / parent / children.
- [y] P parents it to Target without it moving on screen; P again un-parents, still in place.
- [y] N loads Next Scene; M reloads the current one. No errors, scripts start again.

## 9. Camera - `CameraTest`

Setup: in a scene with a Camera. Optional "Target", and "Other Camera" (an entity with a Camera). Press Play.

- [y] Left click logs what is under the mouse (or the screen centre with the cursor locked).
- [y] Holding Z zooms in (field of view 30); releasing goes back.
- [y] C switches the Game view to Other Camera and back.
- [y] V kicks the view up (CameraOrbit pitch), if the camera hangs under a CameraOrbit.
- [y] Every second it logs Target's screen position; it changes as Target moves on screen.

## 10. Light - `LightTest`

Setup: entity with a Point or Spot Light. Optional "Target". Press Play.

- [y] F flickers, G pulses white / red, H switches it off / on.
- [y] B cycles Point -> Spot -> Directional.
- [y] 9 / 0 shrink / grow the range (Point / Spot).
- [y] As a Spot light it keeps aiming at Target.

## 11. Particles - `ParticleTest`

Setup: entity with a ParticleEmitter (Burst Count > 0, Loop off). Press Play.

- [y] Q plays, E stops (live particles fade out normally).
- [y] R fires the burst again straight away.
- [y] T emits 30 extra particles, even while stopped.
- [y] X toggles red / original colour; 1 / 2 rate; 3 / 4 gravity; L the light.
- [y] Once everything has faded (Duration up, last particles gone), it logs once that the effect is no longer alive; the Inspector's Playing unticks itself at the end of Duration.
- [y] After that, Q (Play) runs it again with its burst - no R needed. After Stop, Playing in the edit-mode Inspector is ticked as it was saved.

## 12. Screen UI - `UITest`

Setup: on any entity; fill in Text, Button (with a UIImage), Slider, Bar, Canvas, Action Id,
Swap Texture, Swap Font as available. Press Play.

- [ ] Text shows the time and frame count, updating every frame.
- [ ] Clicking Button logs once per click and tints its image.
- [ ] Dragging Slider logs and resizes Bar like a health bar.
- [ ] V hides / shows Canvas; N opens / closes the in-game menu (same as Escape).
- [ ] K makes Button unclickable (clicks do nothing) and back.
- [ ] J swaps the Button's texture, F the Text's font.
- [ ] A Custom button with Action Id logs its action; the OnButtonClicked / OnSliderChanged /
      OnUIAction callbacks log too.

## 13. On / off - `ActiveTest`

Setup: on any entity; "Target" = an entity with a mesh, children, a Light, a ParticleEmitter
and / or a script. Press Play.

- [y] F6 hides Target and all its children; F6 again brings them back.
- [y] F7 switches Target's Light off / on, F8 its ParticleEmitter.
- [y] F9 stops Target's script (its OnUpdate stops), F9 again resumes it.
- [y] F7 (light off), then F6, F6: the light STAYS off.
- [y] Stop: everything is back as it was in the scene.

## 14. Physics callbacks - `CollisionTest`

Setup A: on a falling crate (Collider + RigidBody) above a floor with a Collider.
Setup B: on an entity with an Is Trigger Collider; move a crate or the mech through it.

- [y] A: OnCollisionEnter on landing (point, normal, impact speed); hard hits are called out.
- [y] A: "Mass" from the field is applied (log at start).
- [y] A: lifting it off (or rolling away) logs OnCollisionExit.
- [y] B: OnTriggerEnter / OnTriggerExit when something goes in and out; nothing is blocked.

## 15. Collider - `ColliderTest`

Setup: crate with Collider (layer Moving), MeshRenderer and RigidBody above a floor. Turn on
physics debug draw. Press Play.

- [y] T: Is Trigger on -> it falls through the floor and logs trigger events; off -> collisions.
- [y] V hides the mesh; it still collides.
- [custom behavior] K switches the Collider off (falls through, no events) and on.
- [y] J cycles Box -> Sphere -> Capsule (debug draw shows it); it keeps falling smoothly.
- [y] 9 / 0 shrink / grow the collider.
- [y] Y toggles bounciness; R puts it back to drop again and it bounces.
- [y] B tries bad sizes: the log says they were refused.

## 16. Mesh renderer - `MeshRendererTest`

Setup: entity with a MeshRenderer showing a model; "Alt Material" = a .mat, "Alt Model" =
another model. Add a MaterialParams and give the material some emissive. Press Play.

- [ ] V hides / shows the mesh.
- [ ] M swaps the whole-object material and back.
- [ ] N swaps the model and back (slot overrides are cleared).
- [ ] S puts Alt Material on the next slot, one slot at a time; X clears them all.
- [ ] X also logs that a bad slot (999) was refused.
- [ ] E pulses the glow; E again goes back to normal.
- [ ] C tints the object red -> green -> blue -> back to normal (needs MaterialParams); other objects with the same material keep their colour, and shadows are unchanged.
- [ ] B steps through the 8 built-in meshes (Cube ... Disc) and clears the model; N brings the model back.
- [ ] L's log matches what you see (model, mesh, material, slots, glow).
- [ ] On an animated model: after N, the animation starts over without errors.

## 17. Keybinds - `KeybindTest`

Setup: on the mech (PlayerController + CharacterController). Press Play.

- [y] F1 swaps WASD / arrow keys; the mech walks with the new keys only.
- [y] F2 then a key rebinds jump (Esc cancels); the new key jumps, Space no longer does.
- [y] F3 doubles the walk speed and back.
- [y] F4 stops the mech and it ignores keys; F4 again gives control back.
- [y] Stop and Play: the changed keys are still in use (player settings, not the scene).
- [y] F6 saves; close and reopen the editor: the keys are as saved
      (`UserSettings.json` "input" / "keys" next to Editor.exe).
- [y] F7 resets to W A S D / Space / GamepadA; F6 to save that.

## 18. Window - `ScreenTest`

In the editor: on any entity, press Play.

- [ ] F8 / F9 / F10 / G / 1 - 4 do NOT change the editor window.
- [ ] "ignored in the editor" is logged once only.
- [ ] H and L work (they only read).

In the game build (`Mekanomachia.exe`, scene with this script):

- [ ] F8 windowed, F9 maximized, F10 fullscreen, G toggles fullscreen.
- [ ] L lists the monitor's resolutions.
- [ ] 1 / 2 set a 1280 x 720 / 1600 x 900 window (height kept to the aspect lock).
- [ ] 3 goes exclusive fullscreen at a listed resolution (screen blinks); 3 again steps on.
- [ ] In exclusive fullscreen, alt-tab minimizes the game and the desktop is at its normal resolution.
- [ ] 4 goes back to borderless fullscreen.
- [ ] Close and restart the game: same mode, window size and resolution as when it closed.

## 19. World text and world canvases - `WorldTextTest`

Setup: entity with a WorldText. Optional "Damage Number" (prefab with a WorldText) and
"World Canvas" (a World Space UICanvas with a UIText child). Press Play.

- [ ] The text counts the seconds up.
- [ ] C red / white; B cycles Billboard (walk around it to see); O outline; G glow (brighter, blooms).
- [ ] F fades it out between 5 and 10 m; T draws it through walls.
- [ ] J cycles left / centre / right alignment.
- [ ] K spawns a red damage number that floats up and is gone after 1 s.
- [ ] N cycles the canvas's Billboard; M draws the canvas through walls.

---

## Editor checks (no script)

### Prefab entity references

- [ ] Entity with a script `Entity` field pointing at its own child: Create Prefab, then place it
      twice. Each copy's field points at ITS OWN child.
- [ ] A PhysicsJoint inside the prefab connecting two of its parts: placed copies connect their own parts.
- [ ] A reference to something outside the prefab: Create Prefab warns and clears it.
- [ ] Change the prefab and Apply: other instances (also in other scenes) keep correct references.
- [ ] Prefabs made before this fix: re-create or Apply them, then check the above.

### Duplicate / copy-paste references

- [ ] Parent with a script `Entity` field pointing at its child: Ctrl+D. The copy points at the
      copy's child; the original is unchanged.
- [ ] Copy / Paste the same: same result.
- [ ] A field pointing at an entity outside the copied group still points at that entity.
- [ ] Undo removes the copy; Redo brings it back with the same references.

---

## When done

- [ ] Note anything that failed (script, key, what happened, Console line) for fixing.
- [ ] Then: cleanup (remove test content, keep `Assets/Scripts` out of the build, don't commit
      `EngineConfig.json`), and commit.
