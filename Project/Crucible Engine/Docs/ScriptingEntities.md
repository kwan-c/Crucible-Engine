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
- Not yet: remapping references inside spawned prefabs, more component types.

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
