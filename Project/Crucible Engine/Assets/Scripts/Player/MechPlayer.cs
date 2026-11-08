/*************************************************************************
* \file:         MechPlayer.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.1
* \brief         The player mech. Port of the Unity player behaviour:
*                MechPlayerInput (devices -> actions), MechMovementController
*                (camera-relative walking, automatic running, Quick Boost
*                dash, jump, facing), CombatHealth, MechStats, EquipManager /
*                PickupDetector (equip and swap parts) and MechCombat hits.
*
* \details
*    Entity setup (Assets/Scenes/Game/TestFolder/*.scene):
*
*        Player              CharacterController, Script: MechPlayer
*        |- Camera Pivot     CameraOrbit (the mouse turns it - engine side)
*        |  '- Camera        Camera
*        '- Mech Visual      (optional) the model; any depth below the player:
*           '- Legs          MeshRenderer + AnimatedModel (controller with the
*              |             bools Moving / Running / Dashing / Airborne)
*              '- Torso ...  sockets; "Mount ShoulderL|ShoulderR|ArmL|ArmR|Head"
*                            entities hold the equipped MechPart entities
*
*    This script is the controller. A PlayerController component is optional
*    and only marks the entity as the player for enemies and checkpoints
*    (it is switched off at start). The
*    engine still turns the CameraOrbit pivot with the mouse and cancels the
*    mech's own rotation, so the camera orbits freely while this script
*    turns the body (Unity's faceCameraDirection).
*
*    Starting parts: a MechPart under a mount starts there; any other socket
*    takes its start* field (Unity's specialist by default: Golem Torso, Golem
*    Head, Fast Legs, Sword in the left arm; shoulders and right arm empty).
*
*    Controls (Unity's Combat/ControlScheme.md):
*        WASD / left stick   camera-relative move (the player's keybinds)
*        Shift               Quick Boost dodge (cooldown, no energy)
*        Space               jump
*        LMB / RMB (hold)    left arm / right arm attack
*        Q / E               left / right shoulder weapon
*        R                   reload partly empty magazines
*        Tab / Ctrl          head / torso ability request
*        V                   cycle the highlighted nearby part
*        1, 2                swap it into the left (or only) / right socket
*    Showcase-only debug keys: K take 25 damage, H full heal / revive.
*
*    Hits: ranged weapons hit what is under the screen centre (Unity's free
*    aim ray), up to their maximum range, with distance falloff and splash;
*    melee hits targets in a 100 degree arc in front within reach. Targets
*    are scripts registered in MechCombatTargets. There are no projectile
*    visuals yet, and no lock-on / auto-aim.
*
*    Not ported: targeting / lock-on, projectile visuals, hit stop,
*    knockback, locomotion modules (hover/tank), weapon animations.
*
*    v1.1  parts and swapping, stats from parts, weapon hits, legs animation.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public enum MechMovementState { Grounded, GroundBoost, Airborne, QuickBoost }

    // A CombatHealth so the enemies' attacks and bolts find it (CombatHealth.Find). The health
    // itself is MechHealth (the HUD reads it), so the two overrides below hand over to that;
    // CombatHealth's own Maximum Health field does nothing on the mech.
    public sealed class MechPlayer : CombatHealth
    {
        // --- Ground locomotion (MechMovementController defaults) ------------------- //
        public float moveSpeed = 10f;           // m/s when the legs have no Speed stat (FullPlayer_mech_specialist: 10)
        public float sprintSpeed = 12f;         // automatic running, scaled like walking
        public float autoRunDelay = 5f;         // seconds of uninterrupted walking
        public float acceleration = 20f;
        public float deceleration = 32f;
        public float boostAcceleration = 28f;   // acceleration while running
        public float rotationSharpness = 12f;
        public bool faceCameraDirection = true; // moving: face the camera's way (strafe), else face the move
        public bool applyPartSpeedModifiers = true;
        public float jumpHeight = 1.5f;

        // --- Quick Boost (dodge) --------------------------------------------------- //
        public float quickBoostPeakSpeed = 50f;
        public float quickBoostDuration = 0.25f;
        public float quickBoostRecovery = 0.4f;
        public float quickBoostExitBlendStart = 0.8f;   // normal movement returns over the last 20%
        public float boostStopDeceleration = 64f;       // braking back to walking after a boost
        public float quickBoostFovKick = 7f;            // degrees (CameraControlThirdPerson's pulse)

        // --- Starting loadout (Unity: EquipManager.startingLoadout) ------------------ //
        // Used for a socket with no mount entity, or a mount with no MechPart under it. Part ids
        // from MechPartCatalogue; empty = the socket starts empty. Defaults = Unity's
        // FullPlayer_mech_specialist: Golem Torso, Golem Head, Fast Legs, Sword in the left arm.
        public string startShoulderL = "";
        public string startShoulderR = "";
        public string startArmL = "sword";
        public string startArmR = "";
        public string startHead = "golem_head";
        public string startTorso = "golem_torso";
        public string startLegs = "fast_legs";

        // --- Combat (FullPlayer_mech_specialist's MechCombat / MechWeaponIdle values) ------------ //
        // Unity metres are scaled by unityScale: this mech is 0.55 of the specialist model where Unity's
        // parts are 0.5 (EquipManager.partScale), so Unity distances are 1.1x here.
        public float unityScale = 1.1f;
        public float meleeCenterZ = 2.75f;      // MechCombat.meleeCenter (0, 0, 2.75): ahead of the mech
        public float meleeSizeX = 5f;           // MechCombat.meleeSize (5, 3.5, 5.5)
        public float meleeSizeY = 3.5f;
        public float meleeSizeZ = 5.5f;
        public float muzzleOffsetX = 0.65f;     // MechCombat.muzzleOffset (0.65, 0.5, 0.65): parts without a muzzle
        public float muzzleOffsetY = 0.5f;
        public float muzzleOffsetZ = 0.65f;
        public float weaponIdleDelay = 3f;      // MechWeaponIdle: stand still this long, then one weapon idles

        // --- Presentation / debug --------------------------------------------------- //
        public string legsEntityName = "Legs";          // the AnimatedModel the movement drives
        public float boostStopDuration = 0.4f;          // LegsBiped_Dash frames 18-30 (Unity's boostStopClip)
        public float movingStopDuration = 0.18f;        // steering cuts the brake short after this
        public float movementVolume = 0.3f;             // the legs' looping walk sound (on the Legs AudioSource)
        public bool debugKeys = true;
        public float debugDamage = 25f;

        // --- Read by the HUD and other scripts ------------------------------------- //
        public MechHealth Health { get; private set; }
        /// <summary>Target selection and F camera lock (Unity's MechTargetLockController).</summary>
        public MechTargetLock Targeting { get; private set; }
        /// <summary>By MechLoadout.WeaponSockets order (Q, E, LMB, RMB); an entry is null for an empty socket.</summary>
        public MechWeapon[] Weapons { get; private set; }
        /// <summary>Head, torso, legs.</summary>
        public MechAbility[] Abilities { get; private set; }
        /// <summary>Changes whenever a part is equipped, so the HUD knows to rebuild.</summary>
        public int LoadoutVersion { get; private set; }
        public MechMovementState State { get; private set; }
        public Vector3 Velocity { get; private set; }
        public bool IsGrounded { get; private set; }
        public bool IsQuickBoosting { get { return quickBoostTimer > 0f; } }
        public float QuickBoostProgress { get { return IsQuickBoosting ? MechMath.Clamp01(1f - quickBoostTimer / ActiveBoostDuration) : 0f; } }
        public float QuickBoostCooldownRemaining { get { return quickBoostRecoveryTimer; } }
        public bool IsAutoRunning { get; private set; }
        public float AutoRunProgress { get { return autoRunDelay > 0f ? MechMath.Clamp01(uninterruptedMoveTime / autoRunDelay) : IsAutoRunning ? 1f : 0f; } }
        public float WalkSpeed { get { return baseSpeed * speedMultiplier; } }
        public float RunSpeed { get { return sprintSpeed * (moveSpeed > 0f ? baseSpeed / moveSpeed : 1f) * speedMultiplier; } }
        public bool GameplayInputActive { get; private set; }

        public event Action<MechWeapon> WeaponFired;
        public event Action<MechAbility> AbilityActivated;
        public event Action QuickBoostStarted;
        public event Action QuickBoostEnded;

        static MechPlayer active;

        CharacterController body;
        Camera gameplayCamera;
        Entity legsEntity;
        Animator legsAnimator;
        AudioSource legsAudio;
        bool legsWarned;
        float legStopAge;
        bool legWasBoosting;
        bool lastMoveInput;
        float baseFov = -1f;
        float baseSpeed = 6f;
        float speedMultiplier = 1f;

        // Equipment, indexed by (int)MechSocket.
        const int SocketCount = 7;
        readonly MechPartDef[] equipped = new MechPartDef[SocketCount];
        readonly MechPart[] mountedParts = new MechPart[SocketCount];
        readonly MechWeapon[] socketWeapons = new MechWeapon[SocketCount];
        readonly Entity[] mounts = new Entity[SocketCount];
        bool loadoutBuilt;
        readonly List<MechPart> nearbyParts = new List<MechPart>();
        int selectedPart;

        Vector3 planarVelocity;
        bool brakingAfterBoost;
        float uninterruptedMoveTime;
        Vector3 quickBoostDirection;
        float quickBoostTimer;
        float quickBoostRecoveryTimer;
        bool attacksArmed;

        float ActiveBoostDuration { get { return Math.Max(0.01f, quickBoostDuration); } }

        public override bool IsDead { get { return Health == null || Health.IsDead; } }

        public override bool ApplyDamage(float amount)
        {
            return Health != null && Health.ApplyDamage(amount);
        }

        public override void OnCreate()
        {
            base.OnCreate();
            active = this;
            body = CharacterController;
            if (body == null)
                Log.Error("MechPlayer on " + Entity.Name + " needs a CharacterController component.");
            // The built-in controller would SimpleMove the mech too and fight this script. Kept on
            // the entity, switched off: it is how enemies and checkpoints know this is the player.
            if (HasComponent<PlayerController>())
                Entity.SetEnabled<PlayerController>(false);
            Weapons = new MechWeapon[MechLoadout.WeaponSockets.Length];
            Abilities = new MechAbility[MechLoadout.AbilitySockets.Length];
            Health = new MechHealth(250f);
            Targeting = new MechTargetLock(Entity);
            Health.Damaged += amount => NotifyCombatAction();
            Health.Died += () => Log.Info("MechPlayer: destroyed. Press H to revive (showcase debug key).");
            State = MechMovementState.Airborne;
        }

        public override void OnDestroy()
        {
            if (gameplayCamera != null && baseFov > 0f) gameplayCamera.FieldOfView = baseFov;
            if (active == this) active = null;
        }

        public override void OnUpdate(float deltaTime)
        {
            if (deltaTime <= 0f || body == null) return;
            // Parts are equipped on the first frame, once every MechPart has run its OnCreate.
            if (!loadoutBuilt) BuildLoadout();
            if (gameplayCamera == null) gameplayCamera = Camera.Main;
            if (gameplayCamera != null && baseFov < 0f) baseFov = gameplayCamera.FieldOfView;

            // Cursor rule: gameplay input only while the mouse is captured and no menu is open.
            bool gameplay = Input.IsMouseLocked && !UI.MenuOpen;
            GameplayInputActive = gameplay;
            float now = Time.ElapsedTime;

            if (debugKeys) UpdateDebugKeys();

            if (Health.IsDead)
            {
                attacksArmed = false;
                planarVelocity = Vector3.Zero;
                quickBoostTimer = 0f;
                uninterruptedMoveTime = 0f; IsAutoRunning = false;
                body.SimpleMove(Vector3.Zero);
                Velocity = body.Velocity;
                UpdateLegsAnimation();
                UpdateFov(deltaTime);
                return;
            }

            UpdatePickups(gameplay);

            // Actions first: a successful attack cancels running before this frame's movement.
            if (gameplay)
            {
                if (Input.IsPressed(KeyCode.R)) foreach (var weapon in Weapons) if (weapon != null) weapon.TryReload(now);
                if (Input.IsPressed(KeyCode.Tab)) RequestAbility(MechSocket.Head, now);
                if (Input.IsPressed(KeyCode.LeftControl)) RequestAbility(MechSocket.Torso, now);
            }
            if (gameplay && Input.IsPressed(KeyCode.F)) Targeting.ToggleHardLock(gameplayCamera);   // Unity: F toggles camera lock
            UpdateAttacks(gameplay, now);

            Vector2 move = gameplay ? ReadMove() : Vector2.Zero;
            bool jump = gameplay && (Input.IsPressed(InputSettings.JumpKey) || Input.IsPressed(InputSettings.JumpButton));
            bool quickBoost = gameplay && (Input.IsPressed(KeyCode.LeftShift) || Input.IsPressed(KeyCode.GamepadB));
            Tick(move, jump, quickBoost, deltaTime);
            UpdateLegsAnimation();
            Targeting.Update(gameplayCamera, gameplay ? Input.MouseDelta : Vector2.Zero, deltaTime);
            UpdateLockCamera(deltaTime);
            UpdateWeaponIdle(deltaTime, now);
            UpdateShake(deltaTime);
            UpdateFov(deltaTime);
        }

        // ---------------------------------------------------------------------------- //
        // Equipment: EquipManager / MechStats
        // ---------------------------------------------------------------------------- //

        static string MountName(MechSocket socket) { return "Mount " + socket; }

        void BuildLoadout()
        {
            loadoutBuilt = true;
            for (int s = 0; s < SocketCount; s++)
            {
                var socket = (MechSocket)s;
                mounts[s] = Entity.FindChild(MountName(socket));
                MechPart part = mounts[s] != null ? FirstPartUnder(mounts[s]) : null;
                if (part != null && part.Def != null && MechPartDef.Fits(part.Def.Kind, socket))
                {
                    // Unity: a part placed under a socket starts in that socket.
                    mountedParts[s] = part;
                    equipped[s] = part.Def;
                    socketWeapons[s] = part.Weapon;
                    part.AttachTo(mounts[s], socket);   // applies its socket pose (Unity: EquipManager.Place)
                    continue;
                }
                string id = StartingPart(socket);
                MechPartDef def = string.IsNullOrEmpty(id) ? null : MechPartCatalogue.Find(id);
                if (def != null && !MechPartDef.Fits(def.Kind, socket))
                {
                    Log.Warning("MechPlayer: starting part " + def.Name + " cannot start in " + socket + ".");
                    def = null;
                }
                equipped[s] = def;
                socketWeapons[s] = def != null && def.IsWeapon ? new MechWeapon(def) : null;
            }
            RefreshLoadout(true);
        }

        string StartingPart(MechSocket socket)
        {
            switch (socket)
            {
                case MechSocket.ShoulderL: return startShoulderL;
                case MechSocket.ShoulderR: return startShoulderR;
                case MechSocket.ArmL:      return startArmL;
                case MechSocket.ArmR:      return startArmR;
                case MechSocket.Head:      return startHead;
                case MechSocket.Torso:     return startTorso;
                default:                   return startLegs;
            }
        }

        static MechPart FirstPartUnder(Entity mount)
        {
            for (int i = 0; i < mount.Transform.ChildCount; i++)
            {
                Entity child = mount.Transform.GetChild(i);
                MechPart part = child != null ? child.GetComponent<MechPart>() : null;
                if (part != null) return part;
            }
            return null;
        }

        // Rebuilds the HUD-facing arrays, maximum health and walking speed from the equipped parts.
        void RefreshLoadout(bool refill)
        {
            for (int i = 0; i < MechLoadout.WeaponSockets.Length; i++)
            {
                MechSocket socket = MechLoadout.WeaponSockets[i];
                MechWeapon weapon = socketWeapons[(int)socket];
                if (weapon != null) weapon.Socket = socket;
                Weapons[i] = weapon;
            }
            for (int i = 0; i < MechLoadout.AbilitySockets.Length; i++)
            {
                MechSocket socket = MechLoadout.AbilitySockets[i];
                MechPartDef def = equipped[(int)socket];
                if (Abilities[i] == null || Abilities[i].Def != def)
                    Abilities[i] = def != null ? new MechAbility(def, socket) : null;
            }

            // MechStats: health is the sum of the parts; speed is the legs' (or moveSpeed) times every
            // other part's percentage.
            float health = 0f, multiplier = 1f;
            for (int s = 0; s < SocketCount; s++)
            {
                MechPartDef def = equipped[s];
                if (def == null) continue;
                health += Math.Max(0f, def.Health);
                if ((MechSocket)s != MechSocket.Legs) multiplier *= Math.Max(0f, 1f + def.Speed / 100f);
            }
            MechPartDef legs = equipped[(int)MechSocket.Legs];
            baseSpeed = legs != null && legs.Speed > 0f ? legs.Speed : moveSpeed;
            speedMultiplier = applyPartSpeedModifiers ? multiplier : 1f;
            Health.SetMaximum(health, refill);
            LoadoutVersion++;
        }

        public MechPartDef EquippedPart(MechSocket socket) { return equipped[(int)socket]; }

        /// <summary>
        /// Puts a part lying in the world into a socket (Unity: EquipManager.TryEquip). With a
        /// mount entity the parts swap physically: the old part is unparented and lands at the
        /// mech's feet. Without one (legs, whose rig carries the torso) only the stats swap and the
        /// pickup turns into the dropped part. An empty socket just takes the part.
        /// </summary>
        public bool Equip(MechPart part, MechSocket socket)
        {
            if (part == null || part.Def == null || part.IsMounted || !MechPartDef.Fits(part.Def.Kind, socket)) return false;
            int s = (int)socket;
            MechPartDef oldDef = equipped[s];
            MechPart old = mountedParts[s];
            Entity mount = mounts[s];
            RemoveNearbyPart(part);

            if (mount != null)
            {
                part.AttachTo(mount, socket);
                mountedParts[s] = part;
                equipped[s] = part.Def;
                socketWeapons[s] = part.Weapon;
                if (old != null)
                {
                    old.Drop(GroundBelow(Transform.WorldPosition), FacingYawDegrees());
                    AddNearbyPart(old);   // it lands at the player's feet: already in reach
                }
            }
            else
            {
                MechWeapon pickedWeapon = part.Weapon;
                MechPartDef pickedDef = part.Def;
                part.BecomePart(oldDef, socketWeapons[s]);
                equipped[s] = pickedDef;
                socketWeapons[s] = pickedWeapon;
                AddNearbyPart(part);
            }
            Log.Info("MechPlayer: equipped " + equipped[s].Name + " in " + socket + (oldDef != null ? ", dropped " + oldDef.Name : "") + ".");
            RefreshLoadout(false);
            return true;
        }

        // The body's heading: the yaw that turns -Z (forward) onto Transform.Forward.
        float FacingYawDegrees()
        {
            Vector3 f = Transform.Forward;
            return (float)(Math.Atan2(-f.X, -f.Z) * 180.0 / Math.PI);
        }

        // Where a dropped part lands: the first surface under the mech (it has no physics of its own).
        Vector3 GroundBelow(Vector3 from)
        {
            RaycastHit hit;
            if (Physics.Raycast(from, new Vector3(0f, -1f, 0f), out hit, 50f, Entity)) return hit.Point;
            float feet = body != null ? body.Height * 0.5f : 1f;
            return new Vector3(from.X, from.Y - feet, from.Z);
        }

        // --- nearby pickups (PickupDetector) ------------------------------------------- //

        public void AddNearbyPart(MechPart part)
        {
            if (part != null && !part.IsMounted && !nearbyParts.Contains(part)) nearbyParts.Add(part);
        }

        public void RemoveNearbyPart(MechPart part)
        {
            int index = nearbyParts.IndexOf(part);
            if (index < 0) return;
            part.SetHighlight(false, null);
            nearbyParts.RemoveAt(index);
            if (selectedPart >= nearbyParts.Count) selectedPart = 0;
        }

        /// <summary>A part being destroyed leaves the player's list.</summary>
        public static void ForgetPart(MechPart part) { if (active != null) active.RemoveNearbyPart(part); }

        public MechPart SelectedPart { get { return nearbyParts.Count > 0 ? nearbyParts[Math.Min(selectedPart, nearbyParts.Count - 1)] : null; } }

        void UpdatePickups(bool gameplay)
        {
            for (int i = nearbyParts.Count - 1; i >= 0; i--)
                if (nearbyParts[i] == null || !nearbyParts[i].Entity.IsAlive || nearbyParts[i].IsMounted) nearbyParts.RemoveAt(i);
            if (nearbyParts.Count == 0) { selectedPart = 0; return; }
            if (selectedPart >= nearbyParts.Count) selectedPart = 0;

            if (gameplay && Input.IsPressed(KeyCode.V)) selectedPart = (selectedPart + 1) % nearbyParts.Count;
            MechPart selected = nearbyParts[selectedPart];
            for (int i = 0; i < nearbyParts.Count; i++)
                nearbyParts[i].SetHighlight(i == selectedPart, i == selectedPart ? SwapPrompt(selected.Def) : null);

            // Unity's MechPlayerInput.SwapSelected: 1 = the left / only slot, 2 = the right slot
            // (ignored for a part with one slot). F is the camera lock in Unity, not a swap.
            if (!gameplay || selected.Def == null) return;
            MechSocket first, second;
            bool twoSockets = SocketsFor(selected.Def.Kind, out first, out second);
            if (Input.IsPressed(KeyCode.Num1)) Equip(selected, first);
            else if (Input.IsPressed(KeyCode.Num2) && twoSockets) Equip(selected, second);
        }

        static bool SocketsFor(MechPartKind kind, out MechSocket first, out MechSocket second)
        {
            switch (kind)
            {
                case MechPartKind.Arm:      first = MechSocket.ArmL; second = MechSocket.ArmR; return true;
                case MechPartKind.Shoulder: first = MechSocket.ShoulderL; second = MechSocket.ShoulderR; return true;
                case MechPartKind.Head:     first = second = MechSocket.Head; return false;
                case MechPartKind.Torso:    first = second = MechSocket.Torso; return false;
                default:                    first = second = MechSocket.Legs; return false;
            }
        }

        string SwapPrompt(MechPartDef def)
        {
            if (def == null) return string.Empty;
            MechSocket first, second;
            string cycle = nearbyParts.Count > 1 ? "   [V] next" : string.Empty;
            if (SocketsFor(def.Kind, out first, out second))
                return "[1] left   [2] right" + cycle;
            return "[1] equip" + cycle;
        }

        // ---------------------------------------------------------------------------- //
        // Movement: MechMovementController.Tick
        // ---------------------------------------------------------------------------- //

        void Tick(Vector2 input, bool jump, bool quickBoostPressed, float deltaTime)
        {
            IsGrounded = body.IsGrounded;
            input = MechMath.ClampLength(input, 1f);

            Vector3 cameraForward, cameraRight;
            GetCameraAxes(out cameraForward, out cameraRight);
            Vector3 moveDirection = MechMath.ClampLength(cameraRight * input.X + cameraForward * input.Y, 1f);
            bool hasMove = MechMath.SqrLength(moveDirection) > 0.001f;
            lastMoveInput = hasMove;   // MechLegAnimator: steering cuts the boost brake short

            // Automatic running: uninterrupted, actually-moving ground walking for autoRunDelay seconds.
            bool walking = hasMove && IsGrounded && !IsQuickBoosting && !quickBoostPressed;
            bool wasRunning = IsAutoRunning;
            if (!walking || MechMath.SqrLength(MechMath.Flat(body.Velocity)) < 0.01f)
            { uninterruptedMoveTime = 0f; IsAutoRunning = false; }
            else
            { uninterruptedMoveTime += deltaTime; IsAutoRunning = uninterruptedMoveTime >= autoRunDelay; }
            if (wasRunning && !IsAutoRunning && !IsQuickBoosting)
                planarVelocity = MechMath.ClampLength(planarVelocity, WalkSpeed);
            bool sprint = IsAutoRunning;

            quickBoostRecoveryTimer = Math.Max(0f, quickBoostRecoveryTimer - deltaTime);
            bool canQuickBoost = IsGrounded && !IsQuickBoosting && quickBoostRecoveryTimer <= 0f;
            bool boostStarted = quickBoostPressed && canQuickBoost;
            if (boostStarted)
            {
                quickBoostDirection = hasMove ? moveDirection.Normalized : cameraForward;
                quickBoostTimer = ActiveBoostDuration;
                quickBoostRecoveryTimer = quickBoostTimer + Math.Max(0f, quickBoostRecovery);
                uninterruptedMoveTime = 0f; IsAutoRunning = false;
            }

            bool boostEnded = StepPlanarVelocity(sprint, moveDirection, deltaTime);
            UpdateFacing(moveDirection, hasMove, cameraForward, deltaTime);

            // Gravity and the ground belong to the engine's CharacterController; Jump only
            // takes effect if it is grounded at the next physics step (Unity's grounded check).
            if (jump && jumpHeight > 0f && IsGrounded) body.Jump(jumpHeight);
            body.SimpleMove(planarVelocity);
            Velocity = body.Velocity;

            State = IsQuickBoosting ? MechMovementState.QuickBoost
                  : !IsGrounded ? MechMovementState.Airborne
                  : sprint && hasMove ? MechMovementState.GroundBoost
                  : MechMovementState.Grounded;

            if (boostStarted && QuickBoostStarted != null) QuickBoostStarted();
            if (boostEnded && QuickBoostEnded != null) QuickBoostEnded();
        }

        // Returns true on the frame the Quick Boost finishes.
        bool StepPlanarVelocity(bool sprint, Vector3 requested, float deltaTime)
        {
            if (!IsQuickBoosting)
            {
                UpdatePlanarVelocity(sprint, requested, deltaTime);
                return false;
            }
            // The dash keeps its captured direction; walking blends back in over its final fraction.
            float finish = MechMath.Clamp01(1f - (quickBoostTimer - deltaTime) / ActiveBoostDuration);
            float blend = ExitBlend(finish);
            Vector3 exit = requested * WalkSpeed;
            planarVelocity = quickBoostDirection * (quickBoostPeakSpeed * BoostCurve(finish) * (1f - blend)) + exit * blend;
            quickBoostTimer = Math.Max(0f, quickBoostTimer - deltaTime);
            if (quickBoostTimer > 0.000001f) return false;
            quickBoostTimer = 0f;
            brakingAfterBoost = true;
            planarVelocity = exit;
            return true;
        }

        void UpdatePlanarVelocity(bool sprint, Vector3 direction, float deltaTime)
        {
            float accel = acceleration, decel = deceleration;
            if (brakingAfterBoost)
            {
                // Recover movement control promptly after a dash; normal tuning resumes at the target.
                accel = Math.Max(accel, boostStopDeceleration);
                decel = Math.Max(decel, boostStopDeceleration);
            }
            Vector3 desired = direction * (sprint ? RunSpeed : WalkSpeed);
            float rate = MechMath.SqrLength(desired) > MechMath.SqrLength(planarVelocity)
                ? (sprint ? boostAcceleration : accel) : decel;
            planarVelocity = MechMath.MoveTowards(planarVelocity, desired, rate * deltaTime);
            if (MechMath.SqrLength(planarVelocity - direction * WalkSpeed) < 0.0001f)
                brakingAfterBoost = false;
        }

        void UpdateFacing(Vector3 moveDirection, bool hasMove, Vector3 cameraForward, float deltaTime)
        {
            Vector3 facing = IsQuickBoosting ? quickBoostDirection
                           : !hasMove ? Vector3.Zero
                           : faceCameraDirection ? cameraForward : moveDirection;
            // Hard lock turns the whole mech to the target (Unity: UpdateFacing).
            if (Targeting != null && Targeting.IsHardLocked)
                facing = MechMath.Flat(Targeting.CurrentTarget.AimPosition - Transform.WorldPosition);
            if (MechMath.SqrLength(facing) < 0.001f) return;
            Quaternion target = Quaternion.LookRotation(facing.Normalized, Vector3.Up);
            Transform.WorldRotation = Quaternion.Slerp(Transform.WorldRotation, target,
                                                       MechMath.Damp(rotationSharpness, deltaTime));
        }

        // Unity's quickBoostCurve keys (0,.95) (.1,1) (.3,.95) (.5,.75) (.75,.4) (1,0), linearly joined.
        static readonly float[] CurveTimes  = { 0f, .1f, .3f, .5f, .75f, 1f };
        static readonly float[] CurveValues = { .95f, 1f, .95f, .75f, .4f, 0f };
        static float BoostCurve(float t)
        {
            t = MechMath.Clamp01(t);
            for (int i = 1; i < CurveTimes.Length; i++)
                if (t <= CurveTimes[i])
                    return MechMath.Lerp(CurveValues[i - 1], CurveValues[i], MechMath.InverseLerp(CurveTimes[i - 1], CurveTimes[i], t));
            return 0f;
        }
        float ExitBlend(float progress)
        {
            return MechMath.SmoothStep01(MechMath.InverseLerp(MechMath.Clamp(quickBoostExitBlendStart, .5f, .99f), 1f, progress));
        }

        void GetCameraAxes(out Vector3 forward, out Vector3 right)
        {
            if (gameplayCamera != null)
            {
                Transform view = gameplayCamera.Entity.Transform;
                // From the camera's right, like the engine: a camera looking straight down still has a level right.
                right = MechMath.Flat(view.Right).Normalized;
                if (MechMath.SqrLength(right) > 0.001f)
                {
                    forward = Vector3.Cross(Vector3.Up, right).Normalized;
                    return;
                }
            }
            forward = MechMath.Flat(Transform.Forward).Normalized;
            if (MechMath.SqrLength(forward) < 0.001f) forward = new Vector3(0f, 0f, -1f);
            right = Vector3.Cross(forward, Vector3.Up).Normalized;
        }

        void UpdateFov(float deltaTime)
        {
            if (gameplayCamera == null || baseFov <= 0f) return;
            float pulse = IsQuickBoosting ? quickBoostFovKick * (1f - QuickBoostProgress * QuickBoostProgress) : 0f;
            float fov = MechMath.Lerp(gameplayCamera.FieldOfView, baseFov + pulse, MechMath.Damp(18f, deltaTime));
            if (Math.Abs(fov - gameplayCamera.FieldOfView) > 0.001f) gameplayCamera.FieldOfView = fov;
        }

        // Unity's MechLegAnimator poses. The legs' controller (Assets/Animation/Player/Mech Legs) reads
        // the Integer "Pose" with these values and the Float "Speed" (m/s, for the gait's playback rate).
        public enum LegPose { Neutral = 0, Locomotion = 1, Boost = 2, BoostStop = 3, Jump = 4 }
        public LegPose CurrentLegPose { get; private set; }

        void UpdateLegsAnimation()
        {
            if (legsEntity == null)
            {
                legsEntity = string.IsNullOrEmpty(legsEntityName) ? null : Entity.FindChild(legsEntityName);
                if (legsEntity == null) return;
                legsAnimator = legsEntity.Animator;
                legsAudio = legsEntity.GetComponent<AudioSource>();
            }
            float dt = Time.DeltaTime;
            float speed = MechMath.Flat(Velocity).Length;
            bool boosting = IsQuickBoosting && !Health.IsDead;

            // ResolvePose: boost, then airborne, then moving / standing.
            LegPose next = boosting ? LegPose.Boost
                         : !IsGrounded ? LegPose.Jump
                         : speed > 0.1f && !Health.IsDead ? LegPose.Locomotion : LegPose.Neutral;
            // BoostStop: right after a boost on the ground, the brake plays out (or is cut short once
            // the player has been steering for movingStopDuration).
            if (CurrentLegPose == LegPose.BoostStop) legStopAge += dt;
            if (IsGrounded && !boosting)
            {
                if (legWasBoosting && boostStopDuration > 0f) { legStopAge = 0f; next = LegPose.BoostStop; }
                else if (CurrentLegPose == LegPose.BoostStop && legStopAge < boostStopDuration
                         && !(lastMoveInput && legStopAge >= movingStopDuration)) next = LegPose.BoostStop;
            }
            legWasBoosting = boosting;
            CurrentLegPose = next;

            bool ok = legsAnimator.SetInteger("Pose", (int)next);
            ok = legsAnimator.SetFloat("Speed", speed) && ok;
            if (!ok && !legsWarned) Log.Warning("MechPlayer: '" + legsEntityName + "' needs an AnimatedModel controller with an Integer 'Pose' and a Float 'Speed'.");
            legsWarned = !ok;

            UpdateMovementAudio(speed);
        }

        // Unity's MechMovementAudio: a looping leg sound while walking on the ground, pitched by speed.
        void UpdateMovementAudio(float speed)
        {
            if (legsAudio == null) return;
            bool walking = IsGrounded && !IsQuickBoosting && !Health.IsDead && speed > 0.1f && movementVolume > 0f;
            if (!walking) { if (legsAudio.IsPlaying) legsAudio.Stop(); return; }
            legsAudio.Loop = true;
            legsAudio.Volume = movementVolume;
            legsAudio.Pitch = MechMath.Clamp(speed / 6f, 0.65f, 1.6f);
            if (!legsAudio.IsPlaying) legsAudio.Play();
        }

        static Vector2 ReadMove()
        {
            float x = 0f, y = 0f;
            if (Input.IsDown(InputSettings.RightKey)) x += 1f;
            if (Input.IsDown(InputSettings.LeftKey)) x -= 1f;
            if (Input.IsDown(InputSettings.ForwardKey)) y += 1f;
            if (Input.IsDown(InputSettings.BackKey)) y -= 1f;
            Vector2 stick = Input.GamepadLeftStick;
            return MechMath.ClampLength(new Vector2(x + stick.X, y + stick.Y), 1f);
        }

        // ---------------------------------------------------------------------------- //
        // Combat: MechPlayerInput attacks, MechCombat hits
        // ---------------------------------------------------------------------------- //

        /// <summary>Successful attacks and incoming damage end the uninterrupted walk.</summary>
        public void NotifyCombatAction()
        {
            uninterruptedMoveTime = 0f;
            IsAutoRunning = false;
            if (!IsQuickBoosting) planarVelocity = MechMath.ClampLength(planarVelocity, WalkSpeed);
            if (State == MechMovementState.GroundBoost) State = IsGrounded ? MechMovementState.Grounded : MechMovementState.Airborne;
        }

        void UpdateAttacks(bool gameplay, float now)
        {
            bool left = Input.IsDown(KeyCode.MouseLeft), right = Input.IsDown(KeyCode.MouseRight);
            bool q = Input.IsDown(KeyCode.Q), e = Input.IsDown(KeyCode.E);
            if (!gameplay || Input.IsPointerOverUI) { attacksArmed = false; return; }
            // The click that captures the mouse must not also attack: arm once everything is released.
            if (!attacksArmed) { attacksArmed = !left && !right && !q && !e; return; }
            foreach (var weapon in Weapons)
            {
                if (weapon == null) continue;
                KeyCode key = KeyFor(weapon.Socket);
                bool requested = weapon.RepeatWhileHeld ? Input.IsDown(key) : Input.IsPressed(key);
                if (!requested || !weapon.TryAttack(now)) continue;
                NotifyCombatAction();
                ResolveHit(weapon);
                // Presentation: the physical part's Fire animation and attack sound.
                MechPart mounted = mountedParts[(int)weapon.Socket];
                if (mounted != null) mounted.PlayAttack();
                StartShake(mounted);
                ResetWeaponIdle();   // an attack ends the weapon idle (MechWeaponAnimator.PlayAttack)
                if (WeaponFired != null) WeaponFired(weapon);
            }
        }

        static KeyCode KeyFor(MechSocket socket)
        {
            switch (socket)
            {
                case MechSocket.ShoulderL: return KeyCode.Q;
                case MechSocket.ShoulderR: return KeyCode.E;
                case MechSocket.ArmL:      return KeyCode.MouseLeft;
                case MechSocket.ArmR:      return KeyCode.MouseRight;
                default:                   return KeyCode.Unknown;
            }
        }

        void ResolveHit(MechWeapon weapon)
        {
            Vector3 origin = Transform.WorldPosition;
            if (weapon.IsMelee) { MeleeHit(weapon, origin); return; }

            // Free aim (Unity's AimSolver): the point under the screen centre, or the ray's end at range.
            Ray ray;
            if (gameplayCamera == null || !gameplayCamera.ViewportPointToRay(new Vector2(0.5f, 0.5f), out ray)) return;
            float cameraToMech = (origin - ray.Origin).Length;
            float reach = weapon.Def.MaximumRange + cameraToMech;
            RaycastHit hit;
            bool aimed = Physics.Raycast(ray, out hit, reach, Entity);
            Vector3 aimPoint = aimed ? hit.Point : ray.Origin + ray.Direction.Normalized * reach;
            // AimSolver: the selected enemy's aim point wins (soft or hard lock).
            if (Targeting.CurrentTarget != null) aimPoint = Targeting.CurrentTarget.AimPosition;

            // RangedAttack.Fire: a straight projectile from the part's muzzle toward the aim point.
            MechPart part = mountedParts[(int)weapon.Socket];
            if (part != null && part.projectile != null)
            {
                // The part's muzzle, else MechCombat.muzzleOffset in the mech's frame.
                Vector3 forward = MechMath.Flat(Transform.Forward).Normalized;
                Vector3 side = Vector3.Cross(forward, Vector3.Up).Normalized;
                Vector3 muzzle = part.HasMuzzle ? part.MuzzlePosition
                    : origin + (side * muzzleOffsetX + Vector3.Up * muzzleOffsetY + forward * muzzleOffsetZ) * unityScale;
                Vector3 direction = (aimPoint - muzzle).Normalized;
                if (MechMath.SqrLength(direction) < 0.0001f) direction = MechMath.Flat(Transform.Forward).Normalized;
                Entity shot = part.projectile.Instantiate(muzzle, Quaternion.LookRotation(direction, Vector3.Up));
                if (shot != null) { MechProjectile.Fire(shot, weapon, Entity, direction); return; }
                Log.Warning("MechPlayer: could not spawn " + weapon.Name + "'s projectile; using an instant hit.");
            }

            // No projectile prefab: an instant hit on what is under the screen centre.
            if (!aimed) return;
            float distance = (hit.Point - origin).Length;
            float falloff = weapon.Def.Damage > 0f ? weapon.DamageAt(distance) / weapon.Def.Damage : 0f;
            if (falloff <= 0f) return;
            bool direct = MechCombatTargets.TryDamage(hit.Entity, weapon.Def.Damage * falloff, origin);

            // Splash: everyone else inside the radius takes the splash damage (same range falloff).
            if (weapon.Def.Attack != MechAttackType.Splash || weapon.Def.SplashRadius <= 0f) return;
            var targets = new List<MechCombatTargets.Target>(MechCombatTargets.All);
            foreach (var target in targets)
            {
                if (!target.Entity.IsAlive || (direct && IsHitEntity(target.Entity, hit.Entity))) continue;
                if ((target.Entity.Transform.WorldPosition - hit.Point).Length <= weapon.Def.SplashRadius + 0.5f)
                    target.OnDamage(weapon.Def.SplashDamage * falloff, origin);
            }
        }

        static bool IsHitEntity(Entity target, Entity hit)
        {
            for (Entity e = hit; e != null; e = e.Transform.Parent)
                if (MechCombatTargets.Same(e, target)) return true;
            return false;
        }

        // Unity's MeleeAttack box: MechCombat.meleeCenter / meleeSize in the mech's own frame (oriented
        // with its facing), each enemy damaged once per swing. Targets count by their centre (feet + 1 m).
        void MeleeHit(MechWeapon weapon, Vector3 origin)
        {
            Vector3 forward = MechMath.Flat(Transform.Forward).Normalized;
            Vector3 right = Vector3.Cross(forward, Vector3.Up).Normalized;
            float k = unityScale;
            var targets = new List<MechCombatTargets.Target>(MechCombatTargets.All);
            foreach (var target in targets)
            {
                if (!target.Entity.IsAlive) continue;
                Vector3 offset = target.Entity.Transform.WorldPosition + Vector3.Up * 1f - origin;
                float side = Vector3.Dot(offset, right), up = offset.Y, ahead = Vector3.Dot(offset, forward);
                if (Math.Abs(side) > meleeSizeX * 0.5f * k || Math.Abs(up) > meleeSizeY * 0.5f * k) continue;
                if (Math.Abs(ahead - meleeCenterZ * k) > meleeSizeZ * 0.5f * k) continue;
                target.OnDamage(weapon.Def.Damage, origin);
            }
        }

        // --- camera hard lock (Unity's CameraControlThirdPerson.UpdateCameraOrbit) ------------------- //
        public float hardLockPitch = 20f;          // base pitch while locked (looks down)
        public float hardLockHeight = 1.5f;        // pivot raise while locked (Unity metres)
        public float hardLockOrbitResponse = 0.12f;
        CameraOrbit orbit;
        Entity pivot;
        float orbitSensitivity = -1f, orbitStick, orbitMaxPitch, pivotBaseY, lockHeight;

        void UpdateLockCamera(float deltaTime)
        {
            if (orbit == null)
            {
                pivot = Entity.FindChild("Camera Pivot");
                if (pivot == null || !pivot.HasComponent<CameraOrbit>()) return;
                orbit = pivot.CameraOrbit;
                pivotBaseY = pivot.Transform.Position.Y;
            }
            float response = 1f - (float)Math.Exp(-deltaTime / Math.Max(0.01f, hardLockOrbitResponse));
            bool locked = Targeting.IsHardLocked;
            if (locked)
            {
                // The mouse no longer steers the view (restored when the lock ends).
                if (orbitSensitivity < 0f) { orbitSensitivity = orbit.Sensitivity; orbitStick = orbit.StickSpeed; orbitMaxPitch = orbit.MaxPitch; }
                orbit.Sensitivity = 0f; orbit.StickSpeed = 0f; orbit.MaxPitch = 80f;
                Vector3 d = Targeting.CurrentTarget.AimPosition - Transform.WorldPosition;
                float planar = MechMath.Flat(d).Length;
                if (planar > 0.001f)
                {
                    float yaw = (float)(Math.Atan2(-d.X, -d.Z) * 180.0 / Math.PI);     // view forward = (-sin, 0, -cos)
                    float delta = ((yaw - orbit.Yaw) % 360f + 540f) % 360f - 180f;
                    orbit.Yaw = orbit.Yaw + delta * response;
                }
                // Raise the view for nearby low targets so the sightline clears the mech.
                float h = hardLockHeight * unityScale;
                float clearance = (float)(Math.Atan2(h - d.Y, Math.Max(0.1f, planar - (body != null ? body.Radius : 1f) - 0.35f)) * 180.0 / Math.PI);
                float desired = MechMath.Clamp(Math.Max(hardLockPitch, clearance), 5f, 80f);
                orbit.Pitch = orbit.Pitch + (desired - orbit.Pitch) * response;
                lockHeight = MechMath.Lerp(lockHeight, h, response);
            }
            else
            {
                if (orbitSensitivity >= 0f) { orbit.Sensitivity = orbitSensitivity; orbit.StickSpeed = orbitStick; orbit.MaxPitch = orbitMaxPitch; orbitSensitivity = -1f; }
                lockHeight = MechMath.Lerp(lockHeight, 0f, response);
            }
            Vector3 p = pivot.Transform.Position;
            pivot.Transform.Position = new Vector3(p.X, pivotBaseY + lockHeight, p.Z);
        }

        // --- MechWeaponIdle ---        // ------------------------------------------------------------------------------------ //
        // After the mech stands still for weaponIdleDelay, one randomly chosen equipped weapon plays its
        // Neutral clip; the others hold its first frame. Moving, boosting or any weapon still attacking /
        // reloading (its cooldown running) resets it.
        float weaponIdleTime;
        MechPart idleWeapon;
        readonly Random idleRandom = new Random();

        void UpdateWeaponIdle(float deltaTime, float now)
        {
            bool stationary = IsGrounded && !IsQuickBoosting && !Health.IsDead
                              && MechMath.Flat(Velocity).Length < 0.05f && Math.Abs(Velocity.Y) < 0.05f;
            bool busy = false;
            foreach (var weapon in Weapons) if (weapon != null && weapon.IsCoolingDown(now)) busy = true;
            if (!stationary || busy) { ResetWeaponIdle(); return; }
            weaponIdleTime += deltaTime;
            if (weaponIdleTime < weaponIdleDelay || idleWeapon != null) return;
            int candidates = 0;
            foreach (var weapon in Weapons)
            {
                MechPart part = weapon != null ? mountedParts[(int)weapon.Socket] : null;
                if (part == null || !part.CanAnimate) continue;
                if (idleRandom.Next(++candidates) == 0) idleWeapon = part;   // reservoir sampling, as Unity
            }
            if (idleWeapon != null) idleWeapon.SetIdle(true);
        }

        void ResetWeaponIdle()
        {
            weaponIdleTime = 0f;
            if (idleWeapon != null) idleWeapon.SetIdle(false);
            idleWeapon = null;
        }

        // --- firing shake (WeaponVisualSettings.firingShake / firingShakeDuration) ------------------- //
        float shakeAmplitude, shakeDuration, shakeTime;
        Vector3 cameraRest;
        bool cameraRestKnown;

        void StartShake(MechPart part)
        {
            if (part == null || part.firingShake <= 0f) return;
            if (part.firingShake * unityScale >= shakeAmplitude * (shakeDuration > 0f ? shakeTime / shakeDuration : 0f))
            {
                shakeAmplitude = part.firingShake * unityScale;
                shakeDuration = shakeTime = Math.Max(0.01f, part.firingShakeDuration);
            }
        }

        void UpdateShake(float deltaTime)
        {
            if (gameplayCamera == null) return;
            Transform view = gameplayCamera.Entity.Transform;
            if (!cameraRestKnown) { cameraRest = view.Position; cameraRestKnown = true; }
            if (shakeTime <= 0f) { if (shakeAmplitude > 0f) { view.Position = cameraRest; shakeAmplitude = 0f; } return; }
            shakeTime = Math.Max(0f, shakeTime - deltaTime);
            float a = shakeAmplitude * (shakeTime / shakeDuration);
            view.Position = cameraRest + new Vector3((float)(idleRandom.NextDouble() * 2 - 1) * a,
                                                     (float)(idleRandom.NextDouble() * 2 - 1) * a, 0f);
        }

        // Unity only raises the request (effects are not implemented there either). The showcase
        // treats it as accepted so the ability HUD's cooldown can be seen.
        void RequestAbility(MechSocket socket, float now)
        {
            foreach (var ability in Abilities)
            {
                if (ability == null || ability.Socket != socket || !ability.ConfirmActivation(now)) continue;
                if (AbilityActivated != null) AbilityActivated(ability);
            }
        }

        void UpdateDebugKeys()
        {
            if (Input.IsPressed(KeyCode.K)) Health.ApplyDamage(debugDamage);
            if (Input.IsPressed(KeyCode.H)) Health.Refill();
        }
    }
}
