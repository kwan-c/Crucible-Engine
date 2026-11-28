/*************************************************************************
* \file:         TorsoSelector.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          25 November 2026
* \version       1.0
* \brief         The main menu's torso carousel: a ring of torso copies whose
*                front slot is the golem's own torso. The arrows turn the
*                ring, dragging spins the front torso, Confirm puts the
*                front one back on the golem and remembers it for the game.
*
* \details
*    Goes on the scene's "manager" entity; MainMenuFlow drives it:
*      Open()     when the camera reaches the torso view: the golem's torso
*                 hides behind its copy and the other torsos drop from above into the ring.
*      Close()    a tween MainMenuFlow runs before the ground slides home:
*                 the other torsos lift back up out of view, then the golem shows the
*                 equipped torso again.
*      Confirm()  the front slot becomes the equipped one (shown on the
*                 golem after the lift) and the choice is stored in the
*                 statics below for MechPlayer.
*
*    The ring: the front slot sits exactly on the golem's torso and the
*    ring extends AWAY from the torso view's camera (viewFrom), so the other
*    torsos stand behind the golem. Every copy turns with the ring, so the
*    front one always faces the way the golem's torso does. Scripts can't
*    reparent entities, so "taking the torso off" is done by hiding the
*    golem's torso and standing a copy in the same spot.
*
*    While the ring is out the golem's head rises headLift metres (it is a
*    child of the golem's "Torso", not of the torso mesh, so the copies
*    would clip through it) and drops back down as the others lift away.
*
*    One copy per slot prefab (torso1..torso6, empty ones skipped), spawned
*    once at start and kept hidden until Open.
*
*    Unity's version (StarterTorsoPreview) is a single preview you drag to
*    spin; the ring is ours.
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class TorsoSelector : EntityScript
    {
        // ---- Set in the Inspector ----
        public Entity golemTorso;            // the golem's "TORSO_Specialist" (needs a MaterialParams for tints)
        public Entity viewFrom;              // camPosition3: the ring extends away from this spot
        public Entity head;                  // lifted while the torsos are out (empty: the "Head" next to golemTorso)
        public float headLift = 1.5f;        // metres up, so the torsos don't clip through it
        public float headSeconds = 0.4f;
        public Prefab torso1;                // the slots in order: each a torso with a TorsoSlot
        public Prefab torso2;                // and a MaterialParams tint (see TorsoSlot.cs)
        public Prefab torso3;
        public Prefab torso4;
        public Prefab torso5;
        public Prefab torso6;
        public float ringRadius = 2.5f;      // metres from the ring's centre to each torso
        public float dropHeight = 6f;        // metres above the ring the other torsos drop from (and lift back to)
        public float dropSeconds = 0.45f;    // falling into the ring, all together
        public float liftSeconds = 0.4f;     // rising back out on Back / Confirm
        public float turnSeconds = 0.5f;     // one slot along
        public float dragSpin = 0.4f;        // degrees the front torso turns per pixel dragged
        public float spinBackSeconds = 0.3f; // back to facing forward after a drag

        // ---- Sound (played at the camera on the SFX bus; empty = silent) ----
        public AudioClip swapSound;          // each time the ring turns to the next / previous torso
        public float swapVolume = 1f;

        // ---- The head light: off while the ring is out, on otherwise ----
        public Entity headLight;             // "MainMenuHeadLight" (empty: MainMenuIntro's, else found by name)

        // ---- The confirmed torso, for the game (MechPlayer reads these when a level starts) ----
        public static bool HasChoice;
        public static string ChosenPartId = "golem_torso";
        public static Color ChosenTint = Color.White;

        /// <summary>True from Open until Close: the ring is out and takes the arrows and drags.</summary>
        public bool IsOpen { get; private set; }

        private readonly List<Entity> slots = new List<Entity>();
        private int selected;                // the slot in front
        private int equipped;                // the slot the golem wears (the last confirmed)
        private float drop;                  // how far above the ring the other torsos are now (0 = in place)
        private float turn;                  // extra ring angle while a turn runs (degrees)
        private int turnDirection;           // +1 Right, -1 Left
        private float spin;                  // the front torso's own drag spin (degrees)
        private bool dragging;
        private MenuTween dropping;          // ticked here; Close's lift is ticked by MainMenuFlow
        private MenuTween turning;
        private MenuTween spinBack;
        private MenuTween headRise;
        private Vector3 headHome;            // the head's world position before it was lifted

        // The golem's torso pose, taken at each Open (see CapturePose).
        private Vector3 front;               // where the golem's torso stands: the ring's front slot
        private Quaternion frontRotation;
        private Vector3 away;                // flat direction from viewFrom through the torso

        public override void OnCreate()
        {
            if (golemTorso == null) { Log.Warning("TorsoSelector: golemTorso is not set."); return; }
            if (head == null)
            {
                // The head is the torso mesh's sibling (both under the golem's "Torso" entity).
                Entity parent = golemTorso.Transform.Parent;
                head = parent != null ? parent.FindChild("Head") : null;
                if (head == null) Log.Warning("TorsoSelector: no head found to lift (set the head field).");
            }
            CapturePose();

            foreach (Prefab prefab in new[] { torso1, torso2, torso3, torso4, torso5, torso6 })
            {
                if (prefab == null) continue;
                Entity copy = prefab.Instantiate(front, frontRotation);
                if (copy == null) { Log.Warning("TorsoSelector: a torso prefab could not be spawned."); continue; }
                copy.SetActive(false);   // hidden until Open
                slots.Add(copy);
            }
            if (slots.Count == 0) Log.Warning("TorsoSelector: no torso prefabs set (torso1..torso6).");

            if (headLight == null)
            {
                MainMenuIntro intro = Entity.GetComponent<MainMenuIntro>();
                headLight = intro != null && intro.headLight != null ? intro.headLight : Entity.Find("MainMenuHeadLight");
            }
            if (headLight == null) Log.Warning("TorsoSelector: no head light found (set the headLight field).");
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!IsOpen) return;
            if (dropping != null) { dropping.Update(deltaTime); if (dropping.Done) dropping = null; }
            if (spinBack != null) { spinBack.Update(deltaTime); if (spinBack.Done) spinBack = null; }
            if (headRise != null) { headRise.Update(deltaTime); if (headRise.Done) headRise = null; }
            if (turning != null)
            {
                turning.Update(deltaTime);
                if (turning.Done) LandTurn();
            }
            UpdateDrag();
        }

        public override void OnUIAction(string actionId)
        {
            if (!IsOpen || dropping != null) return;   // not out, or still dropping in
            // The arrow buttons' Action Ids (Custom) in Main Menu Scene V2.
            if (actionId == "TorsoNext") Turn(+1);
            else if (actionId == "TorsoPrev") Turn(-1);
        }

        // ------------------------------------------------------------------ MainMenuFlow's calls

        /// <summary>
        /// The camera reached the torso view: take the golem's torso "off" (its copy stands in its
        /// place) and drop the other torsos from above into the ring, all together.
        /// </summary>
        public void Open()
        {
            if (IsOpen || slots.Count == 0) return;
            IsOpen = true;
            // NOW, not at boot: the intro straightens the golem after OnCreate, so a pose taken then
            // put the front copy somewhere else and it jerked into place when the torso swapped.
            CapturePose();
            selected = equipped;
            turn = 0f; spin = 0f; dragging = false;
            foreach (Entity slot in slots) slot.SetActive(true);
            golemTorso.SetEnabled<MeshRenderer>(false);   // the front copy stands in its place
            SetHeadLightActive(false);                    // no head light while the ring is out
            drop = dropHeight;
            Layout();
            // Ease In: they speed up as they fall, so they land hard instead of floating down.
            dropping = MenuTween.Value(dropHeight, 0f, dropSeconds, h => { drop = h; Layout(); }, Ease.In);
            // The head rises out of the way meanwhile (Ease Out: quick off, gentle stop).
            if (head != null)
            {
                headHome = head.Transform.WorldPosition;
                headRise = MenuTween.Move(head.Transform, headHome + new Vector3(0f, headLift, 0f), headSeconds, Ease.Out);
            }
        }

        /// <summary>
        /// Lifts the other torsos back up out of view. Returns the lift for MainMenuFlow to run
        /// (before the ground slides home); at its end the copies hide and the golem shows the
        /// equipped torso.
        /// </summary>
        public MenuTween Close()
        {
            if (!IsOpen) return null;
            IsOpen = false;   // from now on no turns, drags or arrows
            dragging = false;
            dropping = null; spinBack = null; headRise = null;
            LandTurn();   // a turn in progress lands first, so the lift starts from a clean layout
            float fromSpin = spin;
            // From wherever the head got to (Back pressed before it finished rising, say).
            float fromHead = head != null ? head.Transform.WorldPosition.Y - headHome.Y : 0f;
            return MenuTween.Value(0f, 1f, liftSeconds, k =>
            {
                drop = dropHeight * k;
                spin = fromSpin * (1f - k);   // the front torso turns back to forward meanwhile
                Layout();
                // The head drops back onto the torso as the others rise (Ease In: it lands, not floats).
                if (head != null) head.Transform.WorldPosition = headHome + new Vector3(0f, fromHead * (1f - k), 0f);
                if (k >= 1f) PutBack();   // exactly 1 at the end (Finish writes the end value)
            }, Ease.In);
        }

        /// <summary>The front torso becomes the golem's (shown when the others lift away) and the game's.</summary>
        public void Confirm()
        {
            if (!IsOpen || slots.Count == 0) return;
            LandTurn();   // mid-turn: the torso it was turning to counts as the one in front
            equipped = selected;
            Entity slot = slots[equipped];
            TorsoSlot info = slot.GetComponent<TorsoSlot>();
            HasChoice = true;
            ChosenPartId = info != null && !string.IsNullOrEmpty(info.partId) ? info.partId : "golem_torso";
            ChosenTint = TintOf(slot);
            Log.Info("TorsoSelector: confirmed slot " + (equipped + 1) + " (" + ChosenPartId + ", tint " + ChosenTint + ")");
        }

        // ------------------------------------------------------------------ the ring

        // Puts every copy on the ring for the current drop, turn and spin.
        private void Layout()
        {
            int count = slots.Count;
            if (count == 0) return;
            float step = 360f / count;
            // The ring's centre is ringRadius behind the golem's torso, so the front slot is exactly on it.
            Vector3 centre = front + away * ringRadius;
            Vector3 centreToFront = away * -ringRadius;
            for (int i = 0; i < count; i++)
            {
                float angle = (i - selected) * step + turn;
                Transform t = slots[i].Transform;
                Vector3 onRing = centre + Quaternion.Euler(0f, angle, 0f) * centreToFront;
                // The torso in front stays on the golem; the others hang `drop` above their spots.
                t.WorldPosition = i == selected ? onRing : onRing + new Vector3(0f, drop, 0f);
                // Each copy turns with the ring (so the front one faces like the golem's torso);
                // the front one also carries the drag spin.
                float own = i == selected ? spin : 0f;
                t.WorldRotation = Quaternion.Euler(0f, angle + own, 0f) * frontRotation;
            }
        }

        // One slot along: the next (Right) or previous (Left) torso swings into the front.
        private void Turn(int direction)
        {
            if (turning != null || slots.Count < 2) return;
            dragging = false;
            if (spin != 0f) SpinBack();   // turns back to forward while the ring moves
            turnDirection = direction;
            float step = 360f / slots.Count;
            // Slot selected+1 sits at +step; turning the ring by -step brings it to the front.
            turning = MenuTween.Value(0f, -direction * step, turnSeconds, a => { turn = a; Layout(); });
            PlaySwapSound();
        }

        /// <summary>The torso swap sound (swapSound), as the ring starts turning to another torso.</summary>
        private void PlaySwapSound()
        {
            if (swapSound == null) return;
            // At the menu camera (MainMenuFlow's viewCamera), else at this entity. PlayClipAtPoint
            // needs no AudioSource, so the manager's own AudioSource is left to the music.
            MainMenuFlow flow = Entity.GetComponent<MainMenuFlow>();
            Entity ear = flow != null && flow.viewCamera != null ? flow.viewCamera : Entity;
            Audio.PlayClipAtPoint(swapSound, ear.Transform.WorldPosition, swapVolume, AudioBus.SFX);
        }

        // Where the golem's torso is right now (the ring's front slot) and which way is "away" from
        // the torso view's camera, flattened so the ring lies level behind the golem.
        private void CapturePose()
        {
            front = golemTorso.Transform.WorldPosition;
            frontRotation = golemTorso.Transform.WorldRotation;
            away = new Vector3(0f, 0f, -1f);   // no viewFrom: behind a golem facing +Z
            if (viewFrom == null) return;
            Vector3 d = front - viewFrom.Transform.WorldPosition;
            d = new Vector3(d.X, 0f, d.Z);
            if (d.Length > 1e-3f) away = d.Normalized;
        }

        // Ends a turn (done, or cut short): the slot it was heading for counts as in front, and the
        // turn angle drops to 0. Together those give the same layout as the turn's end, so nothing jumps.
        private void LandTurn()
        {
            if (turning == null) return;
            selected = Wrap(selected + turnDirection);
            turn = 0f;
            turning = null;
            Layout();
        }

        // Hold the left button and drag sideways to spin the front torso; it eases back when let go.
        private void UpdateDrag()
        {
            if (turning == null && dropping == null && Input.IsPressed(KeyCode.MouseLeft) && !Input.IsPointerOverUI)
            {
                dragging = true;   // a press on the arrows or Confirm doesn't start a drag
                spinBack = null;
            }
            if (!dragging) return;
            if (Input.IsDown(KeyCode.MouseLeft))
            {
                spin += Input.MouseDelta.X * dragSpin;
                Layout();
            }
            else
            {
                dragging = false;
                SpinBack();
            }
        }

        private void SpinBack()
        {
            spinBack = MenuTween.Value(spin, 0f, spinBackSeconds, s => { spin = s; Layout(); }, Ease.Out);
        }

        /// <summary>The head light (with its Sprite and Sparks) on or off: off exactly while the torso view's ring is out.</summary>
        private void SetHeadLightActive(bool active)
        {
            if (headLight != null) headLight.SetActive(active);
        }

        // The lift has ended: copies away, and the golem wears the equipped slot's model and tint.
        private void PutBack()
        {
            foreach (Entity slot in slots) slot.SetActive(false);
            golemTorso.SetEnabled<MeshRenderer>(true);
            SetHeadLightActive(true);   // the ring is away and the head is back down: light on again
            if (slots.Count == 0) return;
            Entity worn = slots[equipped];
            MeshRenderer target = golemTorso.GetComponent<MeshRenderer>();
            MeshRenderer source = worn.GetComponent<MeshRenderer>();
            // Only swap the model when it differs: setting it also clears the slot's material
            // overrides (the golem's texture), so the same model is left alone.
            if (target != null && source != null && source.Model != null && target.Model != source.Model)
            {
                target.Model = source.Model;
                for (int i = 0; i < source.MaterialCount; i++) target.SetMaterial(i, source.GetMaterial(i));
            }
            MaterialParams tint = golemTorso.GetComponent<MaterialParams>();
            if (tint != null) tint.Tint = TintOf(worn);
            else if (HasChoice) Log.Warning("TorsoSelector: golemTorso has no MaterialParams, so the chosen tint can't show.");
        }

        private static Color TintOf(Entity slot)
        {
            MaterialParams p = slot.GetComponent<MaterialParams>();
            return p != null ? p.Tint : Color.White;
        }

        private int Wrap(int index)
        {
            int count = slots.Count;
            return count == 0 ? 0 : ((index % count) + count) % count;
        }

    }   // TORSOSELECTOR CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
