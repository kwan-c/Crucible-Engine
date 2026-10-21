# Animation graphs and C# parameter preview

This step uses the 19 clips already in golem2_v3.cmodel: five leg clips and
14 weapon clips. It takes the state names and separate-part organization from
the Unity project at E:/Project/Gam3/Gam3333. The saved Unity-only locomotion clips
are not imported here. There is no movement/input/combat logic or root-motion
extraction.

## Try it

Build the Mono-enabled Debug Editor, then open
**Assets/Scenes/Animator Parameter Preview.scene**. Its seven independent model
entities remain at their authored positions. They use the normal MeshRenderer,
materials, AnimatedModel and render extraction path.

1. Select **Legs - toggle Walk, Boost, Jump**. Click **Edit Controller Graph** in
   AnimatedModel to view its complete graph.
2. Press Play. In the **LegAnimationPreview** script section toggle **Walk**,
   **Boost** and **Jump**. Watch the model and the Animator's current/next state.
   Boost takes priority over Jump; Jump takes priority over walking. Turning
   Boost off enters BoostStop, then returns to walking or Neutral. Jump holds
   its pose until its checkbox is cleared.
3. Select a weapon. Its **WeaponFirePreview** or **WeaponReloadPreview** script
   exposes **Fire**, and **Reload** only where a reload clip exists. Holding Fire
   repeats the fire animation; clearing it lets the current action finish.
   Reload requests the reload animation, including after a shot.
4. Pause holds playback. Script field edits during Pause take effect on Resume.
   Stop restores authored checkbox values and poses. Edit Mode values can be
   saved with the scene through the existing public-field workflow.

An enabled preview script writes its parameters every update. Disable that
script component if you want to control the graph through its direct Runtime
Parameters widgets instead. Saving an edited graph and saving scene assignments/
script values remain separate operations.

No-Mono builds can still use these graphs and manual Runtime Parameters; the
C# preview scripts require the Mono-enabled build.

## Available graphs

All new graphs are under **Assets/Animation/Parameter Preview**. Existing demo
controllers/scenes are preserved.

| Graph | States and source clip indices |
|---|---|
| Legs | Neutral = Fast_Neutral (2), Locomotion = Heavy_Walk (4), Boost = Fast_Boost (0), BoostStop = Fast_BoostStop (1), Jump = Heavy_Jump (3) |
| Cannon | Neutral (6), Fire (5), Reload (7) |
| Arm Crossbow | Neutral (9), Fire (8), Reload (10) |
| Crossbow | Neutral (12), Fire (11) |
| Mace | Neutral (14), Fire (13) |
| Sword | Neutral (16), Fire (15) |
| Throwing Axe | Neutral (18), Fire (17) |
| All Animations | All 19 exact imported clip names as individual nodes |

The current leg graph deliberately uses the available Fast/Heavy clips; it is
not a complete Fast or Heavy Unity controller. Unity's additional saved walk/
action variants, idle/walk blend trees, animation layers and pose adapters are
separate follow-up work.

Open **All Animations** from the Asset Browser to inspect every node. Its Integer
**Clip** plus Trigger **Select** allow explicitly choosing a clip when this
controller is assigned to a model entity. The trigger prevents a held clip index
from continually retriggering a finished animation. Use Whole model to see clips
that animate different part rigs.

## Authoring nodes

The Animator canvas now provides **Add Animation**, which creates a named state
from a model clip, and **Add Missing Clips**, which adds one node per unused
clip index. These operate on the selected/remembered clip-source model, including
all of its parts. Adding missing clips is a single Undo step. Clicking again
when every clip is represented is a no-op. Duplicate clip names get unique state
names; the clip index remains authoritative. Generic added states default to
looping; choose Loop off for one-shot actions as appropriate.

These are authoring operations and remain disabled in Play/Pause. Selecting a
model, browsing clips, Fit, pan/zoom and small node-selection click movement
do not author edits.

## C# API

An EntityScript now receives its entity-bound **Animator** property before
OnCreate:

    public bool boost;
    public override void OnUpdate(float deltaTime)
    {
        Animator.SetBool("Boost", boost);
    }

The methods are SetBool, SetFloat, SetInteger, SetTrigger and ResetTrigger.
They return whether the typed parameter request was accepted. Names must match
the assigned controller. Constructors do not yet have a bound Animator.

The native animation adapter validates the current entity index/generation,
Play session, model/controller assignment and parameter type. Writes are
coalesced per parameter and applied after controller initialization and before
pose evaluation. Requests for replaced owners, models, controllers or sessions
are dropped; duplicate writes in a frame use the last value. Paused evaluation
can retain parameter values without advancing transitions. This API queues
parameter changes only; it does not move entities or bypass graph transitions.

Only the initialization thread has a bound adapter. Worker/finalizer threads,
missing targets, invalid names/types/non-finite values and calls outside a Play
session return false. Native exceptions cannot cross the managed boundary.
Core clears the non-owning adapter before world/system destruction and the
runtime clears it before final Mono cleanup. No raw Mono objects leave the host.

## Verification

Editor and Game Debug/x64 builds pass with Mono enabled. ScriptRuntime also
compiles with Mono disabled without changing the active build configuration.
The rendered Editor checks cover Add Missing Clips, one-step Undo/Redo and the
public Boost/Reload checkboxes. Runtime checks sample all 19 clips, validate all
eight graphs, exercise leg/weapon transitions and Pause/Stop, preserve entity
transforms, and reject stale owners, wrong threads and invalid parameter calls.
OnCreate-only parameter writes survive the first controller initialization.

These checks also caught and corrected a pre-existing one-shot exit-time rounding
bug: a clip at its final sample could report progress just below one and never
take a 100% exit. Fire -> Reload -> Neutral is checked in forward and reverse
playback. Existing assembly, lifecycle, loading and modular-animation regressions
pass. Verification fixtures/logs stay ignored under build/setup-tools.
