# Milestone 6: visual Animator graph editor

The Animator panel authors the same `.animcontroller` definitions used by the
Milestone 5 runtime. Its graph layout, draft, selection and undo history belong
to the Editor. No runtime node graph, new importer or third-party dependency is added.

## Open and use it

1. Open `Scenes/Animator Controller Preview.scene` in the Editor.
2. Select a golem, expand AnimatedModel and click **Edit Controller Graph**.
   Alternatively use **View > Animator**, then **Open** or **Use Selected Entity**;
   double-clicking an `.animcontroller` in the Asset Browser opens it too.
3. The graph shows the saved controller. Orange identifies the entry state, green
   the selected entity's current state and blue its crossfade destination.
   Select a state or numbered arrow to inspect its properties.
4. Drag states to arrange them. Middle-drag pans; the wheel zooms; **Fit** frames
   the graph. A newly opened graph fits automatically if it exceeds the canvas.
5. Create states with **+ State** or the canvas context menu. Drag from a state's
   right-hand dot to a destination, or choose **Create transition** and click a
   destination. Esc cancels a connection. Any State is a source only.
6. Save the controller to apply changes. Scene saving still stores entity
   assignments; controller saving stores the state machine and graph layout.

The original Animation Preview scene and the sample controller do not need to be
changed to try the editor. Use **Save As** to make a new controller asset, then
assign it in the golem's AnimatedModel Controller picker.

## Authoring

- **States:** edit name, clip, playback speed and looping; set the entry, duplicate
  a state, or delete it and its links. Clip names come automatically from the
  selected entity's MeshRenderer when it uses this controller (or while creating
  a new controller). The last inferred model remains available after deselection
  or Save As. Selecting entities does not dirty the controller.
- **Standalone clip source:** when no matching model entity is selected, the
  collapsed **Advanced** section provides **Clip source model** for authoring an
  unassigned controller. It falls back to remembered/saved model context. This
  manual setting is hidden during normal entity-based editing. Without a source,
  edit clip indices directly (`-1` means rest pose). It is editor metadata; the
  entity's MeshRenderer still determines the actual runtime model.
- **Transitions:** edit source/destination, crossfade seconds, normalized offset,
  exit time and self-transition policy. Conditions are ANDed. Add/remove conditions,
  choose a parameter and a compatible operator, and enter typed numeric thresholds.
- **Priority:** arrow numbers are transition array indices, including Any State
  transitions. **Earlier/Later** changes evaluation order and keeps the selected
  transition attached to its settings. Parallel and reverse edges use separate routes.
- **Parameters:** add, rename and edit Float/Integer/Bool/Trigger defaults. Changing
  type resets the default and adapts referencing conditions to a valid operator.
  Deleting a parameter removes its conditions and remaps later indices; Undo
  restores both. An empty condition list is unconditional when its exit gate passes.
- **Deletion:** removing a state removes incident transitions and remaps later
  indices. Removing the entry chooses the first remaining state. The final state
  cannot be deleted. State duplication copies its clip settings, gives it a unique
  name and does not copy its transitions.

Draft names/settings may temporarily be invalid while typing. Validation reports
the problem and blocks Save rather than publishing an incomplete controller.
The inferred or manually chosen clip source also validates clip indices before
saving. A successful Save/Save As remembers it in the existing `previewModel`
editor metadata field. Failed saves keep the draft and its save point intact;
inferred context does not create an extra undo step.

The asset has a separate 20-step undo/redo history; a continuous drag/text edit is
one step. Use the panel's buttons or Ctrl+Z/Ctrl+Y (also Ctrl+Shift+Z) while it has
focus. Ctrl+S saves this asset, Ctrl+N creates a draft and Ctrl+O opens its picker.
These shortcuts do not also edit/save the scene. Text fields retain their text
undo while typing. The existing scene Edit menu/history is preserved.

## Saving and lifecycle

**New** and Asset Browser **Create Animator Controller** start an unsaved draft.
**Save As** creates a new `.animcontroller` inside Assets, with a normal `.meta`
GUID. It requires a new filename and does not overwrite an existing asset.
**Save** preserves the current asset's GUID. Asset renaming still uses the GUID
lookup, so saving follows the asset's indexed path after an Asset Browser rename.

Node positions, Any State position and optional clip-source Model GUID are stored as
versioned editor metadata in that same file:

```json
"editor": {
  "version": 1,
  "positions": [[0, 0], [280, 0]],
  "anyState": [0, -140],
  "previewModel": "0000000000000000"
}
```

Existing controllers without editor metadata receive a deterministic grid layout.
The runtime codec ignores editor metadata. Invalid metadata is reported without
partially replacing the current draft. Pan, zoom, selection and live values are
session state, not saved authored settings.

Opening/reloading another asset, creating a draft, closing the panel or quitting
with unsaved changes offers Save/Discard/Cancel. The normal scene quit guard runs
first; the Animator guard then protects its independent asset. Failed saves keep
the draft. If the file changed externally since it was opened, Save refuses to
overwrite it; use Reload or Save As. The external conflict check is performed on
Save, not by a background file watcher.

Discard restores the complete last-saved document and clears its draft history;
discarding an untitled controller removes that draft. Closing the panel after
Save or Discard therefore leaves no abandoned unsaved document to prompt again
when another controller is opened. Cancel keeps the current draft. Pending close
requests are resolved once, before another close can be queued on that frame.

Writes validate before touching disk, use a same-directory temporary file and
rename, then refresh the existing AssetManager index. ResourceManager publishes
a new immutable definition only when runtime data changed. Layout-only saves
preserve the cached definition and current animation playback. State/parameter/
transition saves reset affected instances on their next animation update while
preserving each instance's running/paused flag. Instances pin their old shared
definition until reset; copied components cannot retain dangling runtime pointers.

Authoring and saving are disabled during global Play/Pause sessions. The draft
survives Play/Stop. Stop to edit/save it. File watcher hot reload is not added;
an external edit becomes live after it is opened/reloaded and saved through the
panel, or after restarting the engine.

## Live inspection

The live section resolves the Inspector's currently selected entity every frame;
it does not retain component pointers across scene changes. When the entity uses
this controller it shows current/next states, transition weight, sample time,
normalized progress and typed parameter controls. Play/Pause affects just that
instance. Global Pause still freezes advancement.

Live values are temporary and never replace authored defaults or enter undo
history. If the draft runtime data differs from the saved definition, highlights
are suppressed and live controls explicitly use the saved definition. A layout
edit alone does not suppress highlights. Use the Inspector/Hierarchy selection to
switch between independent instances. Selection restoration after Play/Stop uses
the editor's existing scene-GUID mechanism.

Inspector state/transition/time rows keep a fixed height. The Animator live view
also reserves its next-state row and blend bar, showing no transition between
blends. Repeated transitions therefore do not move the playback or parameter
controls below the status display.

## Implementation and verification

- `Editor/src/Animation/AnimatorDocument`: layout codec, draft/save-point tracking,
  bounded history, structural edits and reference remapping. It has no ImGui or
  renderer dependency. Unchanged frames reuse the history checkpoint.
- `ImGuiPanel_Animator`: dockable canvas, hit testing, properties, validation,
  asset actions, dialogs and live inspection. It uses existing ImGui drawing;
  no node-editor library is required. Edge lanes are grouped in linear time and
  offscreen drawing is skipped.
- ImGuiManager, Menu, Asset Browser and Inspector register/open the panel and route
  its shortcuts/quit guard through the existing editor.
- ResourceManager, AnimatedModelSystem and AnimatedModel add validated atomic
  controller saves, definition publication and safe per-instance revision ownership.
  Renderer/material submission and animation sampling/blending remain unchanged.

Normal Debug/x64 Editor and Game builds pass with VS 2022/v143. The offline model
build reports five unchanged models and no failures. Focused document checks pass
for history, deletion/type remapping, layout/GUID round trips, invalid drafts and
metadata rejection. An ignored native probe links the actual Editor objects and
drives ImGui input in the Vulkan engine. Its checks cover node dragging, connection
creation, keyboard undo/redo, unsaved Cancel/Discard, Save/Save As, GUID preservation,
external conflict detection, layout-only revision retention, and live definition
replacement/old-instance lifetime. A fresh process reopens the saved layout/defaults.
Results: 27 document checks, 41 panel interaction/save checks, four fresh-process
reload checks, and 61 controller ECS/render/lifecycle regression checks pass.
The normal Editor loads the controller preview for six seconds and closes with
exit code 0. Verification stderr is empty. The controller regression deliberately
logs an incompatible model and a missing controller; no Vulkan validation errors
were reported.

The panel draw data and font atlas were rendered locally for visual layout review;
this is not a GPU pixel readback. Temporary verification controllers/metas and
their save files are removed afterward. Ignored logs/probes/render artifacts live
under `build/setup-tools/milestone6-*`; no permanent test target is added.

## Milestone 7 handoff

Controllers still produce the same per-instance model pose through the common
renderer. Milestone 7 now adds modular attachments, animated sockets and independent
weapon animation; see [ModularAnimation.md](ModularAnimation.md). Layers, blend trees
and root-motion extraction remain outside this graph editor milestone.
