/*************************************************************************
* \file:         NetMechPlayer.cs
* \author:       Hugo Low Ren Hao, Tok Duan Jie Nelson (MechPlayer, the original);
*                Abhishek Akilandeswari Patchamuthu (this networked copy)
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: a copy of MechPlayer where the
*                host runs every player's mech and each player only sends
*                its input. MechPlayer itself is unchanged (its logic is
*                still being updated, so this is a copy).
*
* \details
*    On the NetMechPlayer prefab (Prefabs/network_tests/network_gameplay_tests),
*    the player prefab of net_gameplay_test.scene (spawn per player).
*
*    Who does what:
*      - the OWNER (Entity.IsMine; offline: this machine) reads its keys and
*        camera every frame and sends them with Network.SetLocalInput: the
*        move direction in the world (camera-relative, as MechPlayer works
*        it out), the camera's yaw and pitch, and the actions (NetActions,
*        an ActionMap). Its camera, FOV pulse and camera-wall avoidance
*        are its own.
*      - the HOST (Network.IsAuthority; offline: this machine) runs the
*        original movement, dash, jump and abilities for every mech, from
*        PlayerActions.Of(Entity): what that mech's owner sent. The mechs'
*        CharacterControllers are host only (Network Object settings).
*      - everyone else just draws it: the Transform and the legs' Animator
*        are replicated.
*    Until NetMatchState says Playing (the countdown) the host ignores the
*    movement and actions; gravity still applies.
*
*    What the host decides and every machine sees (NetVariables / NetData on
*    the mech, written by the host each frame - see "network sync" below):
*      - health (clients mirror it into their own MechHealth, so the HUD,
*        the damage shake and IsDead work there too); a destroyed mech
*        comes back at its spawn point after respawnSeconds
*      - the loadout (the part id in each socket), every weapon's ammo,
*        magazine, reload and cooldown, every ability's cooldown: the HUD
*        (NetMechHud) reads them through HudWeapons / HudAbilities
*      - the part the mech has selected to pick up (its label shows the
*        swap prompt on the owner's screen)
*    Attacks: the owner sends the attack keys as actions; the host runs the
*    original attack code. A ranged shot aims along the owner's camera (its
*    yaw and pitch, from the Camera Pivot) and is tested with
*    Network.RaycastAt at about the moment the owner saw (its ping); melee is
*    the original box. Projectiles fly on the host only (they do the damage);
*    the attack sound and Fire animation play everywhere (an effect).
*    Parts (NetMechPart): V / 1 / 2 are actions too; the host equips and
*    drops, and the parts' parent, pose and id replicate, so a part one
*    player drops can be picked up by another.
*
*    Loadout (L): the owner's HUD lists what is in each socket; while it is
*    open, 1-5 drop the part in head / arm L / arm R / shoulder L / shoulder
*    R (an action; the host drops it at the mech's feet, where anyone can
*    pick it up). Torso and legs are built in (no mount to drop from).
*    The mech starts with only its built-in torso and legs: the parts pad has
*    a full set of heads, arms, shoulders and legs for four players, so every
*    part on a mech is a real one that can be dropped and picked up.
*    F (Use) is an action too: the zoo's floor pads (NetZooPad) ask for it.
*
*    Not in the networked copy: lock-on, hit stop, debug keys.
*
* \history
*    -v1.0       copy of MechPlayer v1.2: owner input, host simulation; host
*                attacks (RaycastAt aim), synced health, loadout, ammo and
*                cooldowns, respawn, networked part pickup and swap; L loadout
*                with drops, F use, empty starting sockets; deaths count
*
* The original's notes follow.
*
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
*        Space               legs ability (Fast Legs: jump; other legs: nothing)
*        LMB / RMB (hold)    left arm / right arm attack
*        Q / E               left / right shoulder weapon
*        R                   reload partly empty magazines
*        Tab / Ctrl          head ability (Heal) / torso ability (Damage Amp)
*        V                   cycle the highlighted nearby part
*        1, 2                swap it into the left (or only) / right socket
*    Showcase-only debug keys: K take 25 damage, H full heal / revive.
*
*    Hits: ranged weapons hit what is under the screen centre (Unity's free
*    aim ray), at full damage up to their Range, where the shot ends, with splash;
*    melee hits targets in a 100 degree arc in front within reach. Targets
*    are scripts registered in MechCombatTargets. There are no projectile
*    visuals yet, and no lock-on / auto-aim.
*
*    Not ported: targeting / lock-on, projectile visuals, hit stop,
*    locomotion modules (hover/tank), weapon animations.
*
*    v1.1  parts and swapping, stats from parts, weapon hits, legs animation.
*    v1.2  Unity main values: run after 3 s at 2x walk, dash in the air,
*          dash FOV pulse +5 (0.05 s up, 0.25 s down), full damage to Range.
*          Abilities: Heal, Damage Amp, Fast Legs jump; Space = legs ability.
*          Melee push: a hit slides a golem about 3 m along the swing, a
*          killing hit throws it (18 m/s forward, 7 m/s up).
*          Dash distance from Unity's curve integral: 8.08 m with no
*          stick input at any frame rate (was 7.3-8.0 m).
*          Camera obstacles (MechCameraObstacles): walls pull the camera
*          in at once, it eases back out over 0.25 s.
*          Melee reach assist (the box slides up to 1 m to the target over
*          0.12 s); camera shake on dash start, grounded dash stop, damage.
*          Hit stop (MechHitStop): the mech and its victim freeze briefly
*          when its hits land; the mech and a melee attacker when hurt.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts.NetGameplay
{
    public enum MechMovementState { Grounded, GroundBoost, Airborne, QuickBoost }

    // A CombatHealth so the enemies' attacks and bolts find it (CombatHealth.Find). The health
    // itself is MechHealth (the HUD reads it), so the two overrides below hand over to that;
    // CombatHealth's own Maximum Health field does nothing on the mech.
    public sealed class NetMechPlayer : CombatHealth
    {
        // --- Ground locomotion (MechMovementController defaults) ------------------- //
        public float moveSpeed = 5f;           // m/s when the legs have no Speed stat (FullPlayer_mech_specialist: 10)
        // Unity main (8 Nov, FullPlayer_mech_specialist): run after 3 s at twice the walk speed.
        // A multiplier, not a speed, so heavy parts slow running exactly as much as walking.
        public float autoRunSpeedMultiplier = 2f;
        public float autoRunDelay = 3f;         // seconds of uninterrupted walking
        public float acceleration = 20f;
        public float deceleration = 32f;
        public float boostAcceleration = 28f;   // acceleration while running
        public float rotationSharpness = 12f;
        public bool faceCameraDirection = true; // moving: face the camera's way (strafe), else face the move
        public bool applyPartSpeedModifiers = true;
        // No jumpHeight: jumping is the Fast Legs ability now (MechPartDef.AbilityPower golem lengths).

        // --- Quick Boost (dodge) --------------------------------------------------- //
        public float quickBoostPeakSpeed = 50f;
        public float quickBoostDuration = 0.25f;
        public float quickBoostRecovery = 0.4f;
        public float quickBoostExitBlendStart = 0.8f;   // normal movement returns over the last 20%
        public float boostStopDeceleration = 64f;       // braking back to walking after a boost
        // Dash FOV pulse (Unity's Main Camera prefab, CameraControlThirdPerson): +5 degrees,
        // eased up over 0.05 s from the dash press, then eased back down over 0.25 s.
        public float quickBoostFovKick = 5f;            // degrees
        public float quickBoostFovRiseTime = 0.05f;     // seconds
        public float quickBoostFovRecoveryTime = 0.25f; // seconds

        // --- Starting loadout (Unity: EquipManager.startingLoadout) ------------------ //
        // Used for a socket with no mount entity, or a mount with no NetMechPart under it. Part ids
        // from MechPartCatalogue; empty = the socket starts empty. Defaults = Unity's
        // FullPlayer_mech_specialist: Golem Torso, Golem Head, Fast Legs, Sword in the left arm.
        public string startShoulderL = "";
        public string startShoulderR = "";
        public string startArmL = "";       // networked copy: arms, shoulders and head come from the parts pad
        public string startArmR = "";
        public string startHead = "";
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
        // MechCombat "Melee push on enemies": speeds in m/s, NOT scaled by unityScale (they move
        // the golems, which are Unity-sized). Only Enemy scripts are pushed.
        public bool meleePush = true;
        public float meleeImpulseSpeed = 6.666667f;  // a living golem's slide starts this fast...
        public float meleePushSeconds = 0.9f;        // ...and slows evenly to 0 over this long (about 3 m)
        public float meleeKillImpulseSpeed = 18f;    // a killing hit throws it this fast along the swing...
        public float meleeKillLiftSpeed = 7f;        // ...and this fast upwards
        // MechCombat reach assist (see StartMelee): Unity metres are x unityScale, like the box.
        public float maximumMeleeApproach = 1f;      // the box slides at most this far forward...
        public float maximumMeleeAssistAngle = 30f;  // ...for a target this many degrees off the facing (none at the edge)
        public float meleeApproachDuration = 0.12f;  // seconds the slide takes; the hit lands at its end
        public float muzzleOffsetX = 0.65f;     // MechCombat.muzzleOffset (0.65, 0.5, 0.65): parts without a muzzle
        public float muzzleOffsetY = 0.5f;
        public float muzzleOffsetZ = 0.65f;
        public float weaponIdleDelay = 3f;      // MechWeaponIdle: stand still this long, then one weapon idles

        // --- Presentation / debug --------------------------------------------------- //
        public string legsEntityName = "Legs";          // the AnimatedModel the movement drives
        public float boostStopDuration = 0.4f;          // LegsBiped_Dash frames 18-30 (Unity's boostStopClip)
        public float movingStopDuration = 0.18f;        // steering cuts the brake short after this
        public float movementVolume = 0.3f;             // the legs' looping walk sound (on the Legs AudioSource)
        public bool debugKeys = false;   // off in the networked copy (K / H were the showcase's)
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
        public float RunSpeed { get { return WalkSpeed * Math.Max(1f, autoRunSpeedMultiplier); } }
        public bool GameplayInputActive { get; private set; }

        public event Action<MechWeapon> WeaponFired;
        public event Action<MechAbility> AbilityActivated;
        public event Action QuickBoostStarted;
        public event Action QuickBoostEnded;

        /// <summary>This machine's own mech (Entity.IsMine), or null.</summary>
        public static NetMechPlayer Local { get; private set; }

        // The actions a player sends (bit numbers of Network.SetLocalInput's actions).
        public static class NetActions
        {
            public const int Dash = 0, LegsAbility = 1, Reload = 2, HeadAbility = 3, TorsoAbility = 4;
            public const int AttackLeft = 5, AttackRight = 6, ShoulderLeft = 7, ShoulderRight = 8;
            public const int CyclePart = 9, SwapFirst = 10, SwapSecond = 11;
            public const int Use = 12;          // F: a zoo floor pad
            public const int DropBase = 13;     // + (int)MechSocket: drop that socket's part (sockets 0-4)
        }

        /// <summary>This machine's loadout panel (L) is open: 1-5 drop parts instead of swapping.</summary>
        public static bool LoadoutOpen { get; private set; }

        // The panel's keys, in its order: 1 head, 2 arm L, 3 arm R, 4 shoulder L, 5 shoulder R.
        static readonly MechSocket[] DropOrder = { MechSocket.Head, MechSocket.ArmL, MechSocket.ArmR, MechSocket.ShoulderL, MechSocket.ShoulderR };
        static readonly KeyCode[] DropKeys = { KeyCode.Num1, KeyCode.Num2, KeyCode.Num3, KeyCode.Num4, KeyCode.Num5 };
        ActionMap actionMap;

        // --- network sync: the mech's NetVariables (the host writes, every machine reads) --------- //
        const int VarHealth = 0, VarMaxHealth = 1, VarRespawnTick = 2, VarLoadout = 3;
        const int VarWeapons = 10;      // 4 weapons x [remaining ammo, magazine | reloading << 16, cooldown end tick, cooldown ticks]
        const int VarAbilities = 26;    // 3 abilities x [ready tick, cooldown ticks, active]
        const int VarSelectedPart = 35, VarNearbyCount = 36, VarDeaths = 37;
        const int ReloadingBit = 1 << 16;

        /// <summary>Seconds a destroyed mech waits before it comes back at its spawn point.</summary>
        public float respawnSeconds = 5f;

        readonly int[] writtenInts = new int[NetVars.SlotCount];
        readonly bool[] writtenKnown = new bool[NetVars.SlotCount];
        float writtenHealth = -1f, writtenMaxHealth = -1f;
        int writtenLoadout = -1;        // the LoadoutVersion last put in NetData
        int appliedLoadout = -1;        // a client: the host's loadout number it has applied
        int respawnTick;                // the host: when this mech comes back (Network.Tick)
        int deaths;                     // the host: times destroyed (the plaza's player board)
        Vector3 spawnPosition;
        Quaternion spawnRotation;
        bool spawnKnown;
        Entity aimPivot;

        static readonly NetEffect AttackEffect = NetEffect.Register("netgameplay.mech.attack", PlayAttackEffect);

        /// <summary>One weapon as the HUD sees it, on any machine.</summary>
        public sealed class HudWeapon
        {
            public MechPartDef Def;
            public MechSocket Socket;
            public string Name { get { return Def.Name; } }
            public bool IsCoolingDown, IsReloading, IsEmpty;
            public float CooldownProgress;
            public string AmmoText = "N/A";
        }

        /// <summary>One ability as the HUD sees it, on any machine.</summary>
        public sealed class HudAbility
        {
            public MechPartDef Def;
            public MechSocket Socket;
            public string Name { get { return Def.Name; } }
            public float Remaining, Progress;
        }

        /// <summary>The weapons (MechLoadout.WeaponSockets order) for the HUD; null = empty socket.</summary>
        public HudWeapon[] HudWeapons { get; private set; }
        /// <summary>The abilities (MechLoadout.AbilitySockets order) for the HUD; null = none.</summary>
        public HudAbility[] HudAbilities { get; private set; }

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
        readonly NetMechPart[] mountedParts = new NetMechPart[SocketCount];
        readonly MechWeapon[] socketWeapons = new MechWeapon[SocketCount];
        readonly Entity[] mounts = new Entity[SocketCount];
        bool loadoutBuilt;
        readonly List<NetMechPart> nearbyParts = new List<NetMechPart>();
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
            if (Entity.IsMine) Local = this;
            if (!players.Contains(this)) players.Add(this);
            body = CharacterController;
            actionMap = new ActionMap()
                .Bind(NetActions.Dash, KeyCode.LeftShift).Bind(NetActions.Dash, KeyCode.GamepadB)
                .Bind(NetActions.LegsAbility, InputSettings.JumpKey).Bind(NetActions.LegsAbility, InputSettings.JumpButton)
                .Bind(NetActions.Reload, KeyCode.R)
                .Bind(NetActions.HeadAbility, KeyCode.Tab).Bind(NetActions.TorsoAbility, KeyCode.LeftControl)
                .Bind(NetActions.AttackLeft, KeyCode.MouseLeft).Bind(NetActions.AttackRight, KeyCode.MouseRight)
                .Bind(NetActions.ShoulderLeft, KeyCode.Q).Bind(NetActions.ShoulderRight, KeyCode.E)
                .Bind(NetActions.CyclePart, KeyCode.V).Bind(NetActions.SwapFirst, KeyCode.Num1).Bind(NetActions.SwapSecond, KeyCode.Num2)
                .Bind(NetActions.Use, KeyCode.F);
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
            Health.Damaged += amount => StartShake(damageShake, damageShakeDuration);   // CombatFeedback.OnDamaged
            Health.Damaged += amount => { if (hitStop) MechHitStop.Request(Entity, damageHitStop); };   // refused if it killed
            MechHitStop.Reset();   // Unity resets its statics on play start: last session's stops and clock are gone
            Health.Died += () =>
            {
                Log.Info("NetMechPlayer: " + Entity.Name + " (player " + Entity.Owner + ") destroyed.");
                if (Network.IsAuthority)
                {
                    respawnTick = Network.Tick + (int)Math.Round(respawnSeconds * Network.TickRate);
                    deaths++;
                }
            };
            HudWeapons = new HudWeapon[MechLoadout.WeaponSockets.Length];
            HudAbilities = new HudAbility[MechLoadout.AbilitySockets.Length];
            State = MechMovementState.Airborne;
        }

        public override void OnDestroy()
        {
            if (gameplayCamera != null && baseFov > 0f) gameplayCamera.FieldOfView = baseFov;
            if (Local == this) Local = null;
            players.Remove(this);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (deltaTime <= 0f || body == null) return;
            // Parts are equipped on the first frame, once every NetMechPart has run its OnCreate.
            if (!loadoutBuilt) BuildLoadout();
            bool mine = Entity.IsMine;
            if (mine)
            {
                if (Local != this) Local = this;
                if (gameplayCamera == null) gameplayCamera = Camera.Main;
                if (gameplayCamera != null && baseFov < 0f) baseFov = gameplayCamera.FieldOfView;
                SendInput();
            }
            if (Network.IsAuthority)
            {
                Simulate(deltaTime);
                WriteSync();
            }
            else ReadSync();
            if (mine)
            {
                UpdateHudView();
                PlaceCamera(deltaTime);
                UpdateFov(deltaTime);
            }
        }

        // The owner: this frame's input, for the host (this machine too when it is the host).
        void SendInput()
        {
            // Cursor rule: gameplay input only while the mouse is captured and no menu is open.
            bool gameplay = Input.IsMouseLocked && !UI.MenuOpen;
            GameplayInputActive = gameplay;
            Vector3 cameraForward, cameraRight;
            GetCameraAxes(out cameraForward, out cameraRight);
            Vector3 move = Vector3.Zero;
            uint actions = 0;
            if (gameplay && Input.IsPressed(KeyCode.L)) LoadoutOpen = !LoadoutOpen;
            if (gameplay)
            {
                Vector2 stick = ReadMove();
                move = MechMath.ClampLength(cameraRight * stick.X + cameraForward * stick.Y, 1f);
                actions = actionMap.Sample();
                if (LoadoutOpen)
                {
                    // 1-5 drop instead of picking up while the panel is open.
                    actions &= ~((1u << NetActions.SwapFirst) | (1u << NetActions.SwapSecond) | (1u << NetActions.CyclePart));
                    for (int i = 0; i < DropKeys.Length; i++)
                        if (Input.IsDown(DropKeys[i])) actions |= 1u << (NetActions.DropBase + (int)DropOrder[i]);
                }
                // The FOV pulse is this machine's: a client starts it on the press (the host's dash
                // follows a moment later).
                if (!Network.IsAuthority && Input.IsPressed(KeyCode.LeftShift)) quickBoostFovAge = 0f;
            }
            float yaw = (float)(Math.Atan2(cameraForward.X, cameraForward.Z) * 180.0 / Math.PI);
            Network.SetLocalInput(move, yaw, CameraPitch(), actions);
        }

        // Degrees the camera looks up (+) or down (-), for aiming later.
        float CameraPitch()
        {
            if (gameplayCamera == null) return 0f;
            Transform view = gameplayCamera.Entity.Transform;
            Vector3 look = Vector3.Cross(view.Up, view.Right);   // Transform.Forward is -Z: build the look from right and up
            Vector3 flat = MechMath.Flat(look);
            return (float)(Math.Atan2(look.Y, Math.Max(1e-4f, flat.Length)) * 180.0 / Math.PI);
        }

        // The host: the original MechPlayer frame, driven by what this mech's owner sent.
        void Simulate(float deltaTime)
        {
            NetInput input = PlayerActions.Of(Entity);
            bool gameplay = input.IsFresh && NetMatchState.MovementAllowed;
            float now = Time.ElapsedTime;
            if (!spawnKnown)
            {
                // Where this mech was spawned (its spawn point): it comes back here.
                spawnPosition = Transform.WorldPosition;
                spawnRotation = Transform.WorldRotation;
                spawnKnown = true;
            }

            if (Health.IsDead)
            {
                if (Network.Tick >= respawnTick) { Respawn(); return; }
                attacksArmed = false;
                meleeWeapon = null;   // a swing still sliding in dies with the mech
                planarVelocity = Vector3.Zero;
                quickBoostTimer = 0f;
                uninterruptedMoveTime = 0f; IsAutoRunning = false;
                body.SimpleMove(Vector3.Zero);
                Velocity = body.Velocity;
                UpdateLegsAnimation();
                return;
            }

            // Actions first: a successful attack cancels running before this frame's movement.
            if (gameplay)
            {
                if (input.WasPressed(NetActions.Reload)) foreach (var weapon in Weapons) if (weapon != null) weapon.TryReload(now);
                if (input.WasPressed(NetActions.HeadAbility)) RequestAbility(MechSocket.Head, now);
                if (input.WasPressed(NetActions.TorsoAbility)) RequestAbility(MechSocket.Torso, now);
                // Unity main: Space asks the legs for their ability (Fast Legs jump; other legs nothing).
                if (input.WasPressed(NetActions.LegsAbility)) RequestAbility(MechSocket.Legs, now);
                foreach (MechSocket socket in DropOrder)
                    if (input.WasPressed(NetActions.DropBase + (int)socket)) DropPart(socket);
            }
            UpdateAbilities(deltaTime, now);
            UpdateAttacks(gameplay, now, input);
            UpdatePickups(gameplay, input);

            Vector3 move = gameplay ? input.Move : Vector3.Zero;
            bool quickBoost = gameplay && input.WasPressed(NetActions.Dash);
            float yaw = (float)(input.Yaw * Math.PI / 180.0);
            Vector3 cameraForward = new Vector3((float)Math.Sin(yaw), 0f, (float)Math.Cos(yaw));
            Tick(move, cameraForward, quickBoost, deltaTime);
            UpdateMelee(deltaTime);   // after movement: a dash or a jump this frame cancels an assisted swing
            UpdateLegsAnimation();
            UpdateWeaponIdle(deltaTime, now);
        }

        // The host: the part in a socket falls at the mech's feet (as a swapped-out part does), with
        // its ammo and cooldowns, for anyone to pick up. Only a mounted, physical part can be dropped.
        void DropPart(MechSocket socket)
        {
            int s = (int)socket;
            NetMechPart part = mountedParts[s];
            if (part == null || mounts[s] == null) return;
            if (meleeWeapon != null && meleeWeapon == socketWeapons[s]) meleeWeapon = null;
            if (idleWeapon == part) ResetWeaponIdle();
            part.Drop(GroundBelow(Transform.WorldPosition), FacingYawDegrees());
            mountedParts[s] = null;
            equipped[s] = null;
            socketWeapons[s] = null;
            AddNearbyPart(part);   // at the mech's feet: in reach again
            Log.Info("NetMechPlayer: " + Entity.Name + " dropped " + (part.Def != null ? part.Def.Name : part.partId) + " from " + socket + ".");
            RefreshLoadout(false);
        }

        /// <summary>The loadout panel's text (any machine: the host's synced part ids).</summary>
        public string LoadoutText()
        {
            var text = new System.Text.StringBuilder("LOADOUT   [L] close\n");
            for (int i = 0; i < DropOrder.Length; i++)
            {
                MechPartDef def = equipped[(int)DropOrder[i]];
                text.Append("[" + (i + 1) + "] " + SocketLabel(DropOrder[i]) + ":  ")
                    .Append(def != null ? def.Name + "   (drop)" : "empty").Append("\n");
            }
            text.Append("Torso:  " + PartName(MechSocket.Torso) + "   (built in)\n");
            text.Append("Legs:  " + PartName(MechSocket.Legs) + "   (built in)\n");
            text.Append("Health " + (int)Math.Ceiling(Health.Maximum) + "   speed " + WalkSpeed.ToString("0.0") + " m/s");
            return text.ToString();
        }

        string PartName(MechSocket socket) { MechPartDef def = equipped[(int)socket]; return def != null ? def.Name : "none"; }

        static string SocketLabel(MechSocket socket)
        {
            switch (socket)
            {
                case MechSocket.ArmL:      return "Arm L";
                case MechSocket.ArmR:      return "Arm R";
                case MechSocket.ShoulderL: return "Shoulder L";
                case MechSocket.ShoulderR: return "Shoulder R";
                default:                   return socket.ToString();
            }
        }

        // The host: back to full health at the spawn point, standing still.
        void Respawn()
        {
            Health.Refill();
            Transform.WorldPosition = spawnPosition;
            Transform.WorldRotation = spawnRotation;
            planarVelocity = Vector3.Zero;
            quickBoostTimer = 0f;
            brakingAfterBoost = false;
            attacksArmed = false;
            Log.Info("NetMechPlayer: " + Entity.Name + " (player " + Entity.Owner + ") is back at its spawn point.");
        }

        /// <summary>Seconds until this destroyed mech comes back (0 while it is alive), on any machine.</summary>
        public float RespawnRemaining
        {
            get
            {
                if (Health == null || !Health.IsDead) return 0f;
                int tick = Network.IsAuthority ? respawnTick : Entity.NetVars.GetInt(VarRespawnTick);
                return (float)Math.Max(0.0, tick / (double)Network.TickRate - Network.Time);
            }
        }

        // ---------------------------------------------------------------------------- //
        // Network sync: the host writes the mech's state, the clients read it
        // ---------------------------------------------------------------------------- //

        // Offline there is nothing to send (and the mech may not be a network object).
        static bool Sending { get { return Network.IsOnline && Network.IsAuthority; } }

        // Writes a slot only when it changed by more than tolerance (end ticks wobble by a tick
        // from frame to frame; that is not worth a send).
        void WriteInt(int slot, int value, int tolerance = 0)
        {
            if (writtenKnown[slot] && Math.Abs(writtenInts[slot] - value) <= tolerance) return;
            if (Entity.NetVars.SetInt(slot, value)) { writtenInts[slot] = value; writtenKnown[slot] = true; }
        }

        static int EndTick(float remainingSeconds)
        {
            return remainingSeconds > 0f ? Network.Tick + (int)Math.Round(remainingSeconds * Network.TickRate) : 0;
        }

        static int DurationTicks(float remaining, float progress)
        {
            return remaining > 0f && progress < 1f ? (int)Math.Round(remaining / (1f - progress) * Network.TickRate) : 0;
        }

        void WriteSync()
        {
            if (!Sending) return;
            float now = Time.ElapsedTime;
            NetVars vars = Entity.NetVars;
            if (Math.Abs(Health.Current - writtenHealth) > 0.01f && vars.SetFloat(VarHealth, Health.Current)) writtenHealth = Health.Current;
            if (Math.Abs(Health.Maximum - writtenMaxHealth) > 0.01f && vars.SetFloat(VarMaxHealth, Health.Maximum)) writtenMaxHealth = Health.Maximum;
            WriteInt(VarRespawnTick, Health.IsDead ? respawnTick : 0);
            WriteInt(VarDeaths, deaths);

            // The loadout: each socket's part id in NetData, then its number in a variable (a client
            // applies the data once the two agree).
            if (writtenLoadout != LoadoutVersion)
            {
                NetWriter data = new NetWriter().WriteInt(LoadoutVersion);
                for (int s = 0; s < SocketCount; s++) data.WriteString(equipped[s] != null ? equipped[s].Id : string.Empty);
                if (Entity.NetData.Set(data)) { writtenLoadout = LoadoutVersion; WriteInt(VarLoadout, LoadoutVersion); }
            }

            for (int i = 0; i < Weapons.Length; i++)
            {
                MechWeapon weapon = Weapons[i];
                int slot = VarWeapons + i * 4;
                if (weapon == null) { WriteInt(slot, 0); WriteInt(slot + 1, 0); WriteInt(slot + 2, 0); WriteInt(slot + 3, 0); continue; }
                float remaining = weapon.CooldownRemaining(now);
                WriteInt(slot, weapon.RemainingAmmo);
                WriteInt(slot + 1, weapon.MagazineRounds | (weapon.IsReloading(now) ? ReloadingBit : 0));
                WriteInt(slot + 2, EndTick(remaining), remaining > 0f ? 2 : 0);
                WriteInt(slot + 3, DurationTicks(remaining, weapon.CooldownProgress(now)), 2);
            }
            for (int i = 0; i < Abilities.Length; i++)
            {
                MechAbility ability = Abilities[i];
                int slot = VarAbilities + i * 3;
                if (ability == null) { WriteInt(slot, 0); WriteInt(slot + 1, 0); WriteInt(slot + 2, 0); continue; }
                float remaining = ability.Remaining(now);
                WriteInt(slot, EndTick(remaining), remaining > 0f ? 2 : 0);
                WriteInt(slot + 1, DurationTicks(remaining, ability.Progress(now)), 2);
                WriteInt(slot + 2, ability.IsActive(now) ? 1 : 0);
            }

            NetMechPart selected = SelectedPart;
            WriteInt(VarSelectedPart, selected != null ? (int)selected.Entity.NetworkId : 0);
            WriteInt(VarNearbyCount, nearbyParts.Count);
        }

        // A client: health and the loadout from the host.
        void ReadSync()
        {
            NetVars vars = Entity.NetVars;
            int loadout = vars.GetInt(VarLoadout);
            if (loadout != appliedLoadout) ApplyLoadout(loadout);

            float maximum = vars.GetFloat(VarMaxHealth), current = vars.GetFloat(VarHealth);
            if (maximum <= 0f) return;   // nothing from the host yet
            if (Math.Abs(Health.Maximum - maximum) > 0.01f) Health.SetMaximum(maximum, false);
            float have = Health.Current;
            if (Math.Abs(have - current) <= 0.01f) return;
            if (current < have) Health.ApplyDamage(have - current);      // the damage shake and Died happen here too
            else if (Health.IsDead) { Health.Refill(); if (current < Health.Maximum) Health.ApplyDamage(Health.Maximum - current); }
            else Health.Heal(current - have);
        }

        // A client: the host's part ids per socket (NetData), once its number matches the variable.
        void ApplyLoadout(int version)
        {
            NetReader reader = Entity.NetData.Read();
            if (reader.Remaining < 4 || reader.ReadInt() != version) return;   // the data has not arrived yet
            for (int s = 0; s < SocketCount; s++)
            {
                string id = reader.ReadString();
                if (!reader.Ok) return;
                MechPartDef def = string.IsNullOrEmpty(id) ? null : MechPartCatalogue.Find(id);
                equipped[s] = def;
                socketWeapons[s] = def != null && def.IsWeapon ? new MechWeapon(def) : null;   // for names only
            }
            appliedLoadout = version;
            RefreshLoadout(false);
        }

        // The owner: what the HUD shows. The host has the real weapons and abilities; a client
        // builds the same picture from the variables.
        void UpdateHudView()
        {
            float now = Time.ElapsedTime;
            bool authority = Network.IsAuthority;
            NetVars vars = Entity.NetVars;
            for (int i = 0; i < HudWeapons.Length; i++)
            {
                MechWeapon weapon = Weapons[i];
                if (weapon == null) { HudWeapons[i] = null; continue; }
                HudWeapon view = HudWeapons[i];
                if (view == null || view.Def != weapon.Def) view = HudWeapons[i] = new HudWeapon { Def = weapon.Def };
                view.Socket = weapon.Socket;
                int remainingAmmo, magazine;
                if (authority)
                {
                    remainingAmmo = weapon.RemainingAmmo;
                    magazine = weapon.MagazineRounds;
                    view.IsCoolingDown = weapon.IsCoolingDown(now);
                    view.CooldownProgress = weapon.CooldownProgress(now);
                    view.IsReloading = weapon.IsReloading(now);
                }
                else
                {
                    int slot = VarWeapons + i * 4;
                    remainingAmmo = vars.GetInt(slot);
                    int packed = vars.GetInt(slot + 1);
                    magazine = packed & (ReloadingBit - 1);
                    float remaining, progress;
                    Cooldown(vars.GetInt(slot + 2), vars.GetInt(slot + 3), out remaining, out progress);
                    view.IsCoolingDown = remaining > 0f;
                    view.CooldownProgress = progress;
                    view.IsReloading = (packed & ReloadingBit) != 0 && remaining > 0f;
                }
                view.IsEmpty = weapon.UsesAmmo && remainingAmmo <= 0;
                view.AmmoText = weapon.UsesAmmo ? magazine + " / " + remainingAmmo : "N/A";
            }
            for (int i = 0; i < HudAbilities.Length; i++)
            {
                MechAbility ability = Abilities[i];
                if (ability == null) { HudAbilities[i] = null; continue; }
                HudAbility view = HudAbilities[i];
                if (view == null || view.Def != ability.Def) view = HudAbilities[i] = new HudAbility { Def = ability.Def };
                view.Socket = ability.Socket;
                if (authority) { view.Remaining = ability.Remaining(now); view.Progress = ability.Progress(now); }
                else
                {
                    int slot = VarAbilities + i * 3;
                    Cooldown(vars.GetInt(slot), vars.GetInt(slot + 1), out view.Remaining, out view.Progress);
                }
            }
        }

        // Seconds left and progress (0 -> 1) of a cooldown that ends at endTick and lasts ticks.
        static void Cooldown(int endTick, int ticks, out float remaining, out float progress)
        {
            remaining = endTick > 0 ? (float)Math.Max(0.0, endTick / (double)Network.TickRate - Network.Time) : 0f;
            progress = ticks > 0 ? MechMath.Clamp01(1f - remaining * Network.TickRate / ticks) : 1f;
        }

        /// <summary>Is this the part the mech has selected to pick up? (Any machine.)</summary>
        public bool IsSelected(NetMechPart part)
        {
            if (part == null) return false;
            if (Network.IsAuthority) return SelectedPart == part;
            uint id = part.Entity.NetworkId;
            return id != 0 && (uint)Entity.NetVars.GetInt(VarSelectedPart) == id;
        }

        /// <summary>Times this mech has been destroyed (any machine).</summary>
        public int Deaths { get { return Network.IsAuthority ? deaths : Entity.NetVars.GetInt(VarDeaths); } }

        /// <summary>How many of the head, arm and shoulder sockets hold a part (any machine).</summary>
        public int MountedPartCount
        {
            get
            {
                int count = 0;
                foreach (MechSocket socket in DropOrder) if (equipped[(int)socket] != null) count++;
                return count;
            }
        }

        /// <summary>How many parts are in the mech's reach (any machine).</summary>
        public int NearbyCount { get { return Network.IsAuthority ? nearbyParts.Count : Entity.NetVars.GetInt(VarNearbyCount); } }

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
                NetMechPart part = mounts[s] != null && Network.IsAuthority ? FirstPartUnder(mounts[s]) : null;
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

        static NetMechPart FirstPartUnder(Entity mount)
        {
            for (int i = 0; i < mount.Transform.ChildCount; i++)
            {
                Entity child = mount.Transform.GetChild(i);
                NetMechPart part = child != null ? child.GetComponent<NetMechPart>() : null;
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
                // A part with no ability gets no MechAbility, so the HUD hides its pod. A new part type
                // gets a fresh one, which also ends the old part's running effect (Unity: the effect
                // ends when its part is taken off).
                if (def == null || def.Ability == MechAbilityKind.None) Abilities[i] = null;
                else if (Abilities[i] == null || Abilities[i].Def != def) Abilities[i] = new MechAbility(def, socket);
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
        public bool Equip(NetMechPart part, MechSocket socket)
        {
            if (part == null || part.Def == null || part.IsMounted || !MechPartDef.Fits(part.Def.Kind, socket)) return false;
            int s = (int)socket;
            MechPartDef oldDef = equipped[s];
            NetMechPart old = mountedParts[s];
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

        public void AddNearbyPart(NetMechPart part)
        {
            if (part != null && !part.IsMounted && !nearbyParts.Contains(part)) nearbyParts.Add(part);
        }

        public void RemoveNearbyPart(NetMechPart part)
        {
            int index = nearbyParts.IndexOf(part);
            if (index < 0) return;
            part.SetHighlight(false, null);
            nearbyParts.RemoveAt(index);
            if (selectedPart >= nearbyParts.Count) selectedPart = 0;
        }

        /// <summary>A part being destroyed leaves every player's list (on the host, every mech has one).</summary>
        public static void ForgetPart(NetMechPart part)
        {
            foreach (NetMechPlayer player in players) player.RemoveNearbyPart(part);
        }

        static readonly List<NetMechPlayer> players = new List<NetMechPlayer>();

        /// <summary>Every mech on this machine (the host has them all). Read only.</summary>
        public static List<NetMechPlayer> All { get { return players; } }

        public NetMechPart SelectedPart { get { return nearbyParts.Count > 0 ? nearbyParts[Math.Min(selectedPart, nearbyParts.Count - 1)] : null; } }

        void UpdatePickups(bool gameplay, NetInput input)
        {
            for (int i = nearbyParts.Count - 1; i >= 0; i--)
                if (nearbyParts[i] == null || !nearbyParts[i].Entity.IsAlive || nearbyParts[i].IsMounted) nearbyParts.RemoveAt(i);
            if (nearbyParts.Count == 0) { selectedPart = 0; return; }
            if (selectedPart >= nearbyParts.Count) selectedPart = 0;

            if (gameplay && input.WasPressed(NetActions.CyclePart)) selectedPart = (selectedPart + 1) % nearbyParts.Count;
            NetMechPart selected = nearbyParts[selectedPart];
            // The labels: each machine shows its own player's selection (NetMechPart asks IsSelected).

            // Unity's MechPlayerInput.SwapSelected: 1 = the left / only slot, 2 = the right slot
            // (ignored for a part with one slot). F is the camera lock in Unity, not a swap.
            if (!gameplay || selected.Def == null) return;
            MechSocket first, second;
            bool twoSockets = SocketsFor(selected.Def.Kind, out first, out second);
            if (input.WasPressed(NetActions.SwapFirst)) Equip(selected, first);
            else if (input.WasPressed(NetActions.SwapSecond) && twoSockets) Equip(selected, second);
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

        /// <summary>The swap prompt for a selected part (any machine).</summary>
        public string SwapPrompt(MechPartDef def)
        {
            if (def == null) return string.Empty;
            MechSocket first, second;
            string cycle = NearbyCount > 1 ? "   [V] next" : string.Empty;
            if (SocketsFor(def.Kind, out first, out second))
                return "[1] left   [2] right" + cycle;
            return "[1] equip" + cycle;
        }

        // ---------------------------------------------------------------------------- //
        // Movement: MechMovementController.Tick
        // ---------------------------------------------------------------------------- //

        // The networked copy's Tick takes the move already in the world (the owner worked it out from
        // its camera, as MechPlayer does) and the owner's camera forward (from its yaw).
        void Tick(Vector3 worldMove, Vector3 cameraForward, bool quickBoostPressed, float deltaTime)
        {
            bool wasGrounded = IsGrounded;   // last frame's, for the dash-stop shake below
            IsGrounded = body.IsGrounded;
            Vector3 moveDirection = MechMath.ClampLength(MechMath.Flat(worldMove), 1f);
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
            if (quickBoostRecoveryTimer < 0.000001f) quickBoostRecoveryTimer = 0f;   // float crumbs: no extra frame of wait
            // No IsGrounded check: biped legs may dash in the air (Unity: BipedLocomotionModule.
            // CanQuickBoost returns true). Gravity keeps acting, so an air dash still falls.
            bool canQuickBoost = !IsQuickBoosting && quickBoostRecoveryTimer <= 0f;
            bool boostStarted = quickBoostPressed && canQuickBoost;
            if (boostStarted)
            {
                quickBoostFovAge = 0f;   // restarts the FOV pulse (Unity: OnQuickBoost)
                quickBoostDirection = hasMove ? moveDirection.Normalized : cameraForward;
                RebuildBoostIntegral();  // per press, as Unity does: picks up Inspector changes to the blend
                quickBoostTimer = ActiveBoostDuration;
                quickBoostRecoveryTimer = quickBoostTimer + Math.Max(0f, quickBoostRecovery);
                // Unity counts the press frame towards the recovery too, as the dash timer does below,
                // so the next dash is allowed exactly duration + recovery after this press.
                quickBoostRecoveryTimer = Math.Max(0f, quickBoostRecoveryTimer - deltaTime);
                uninterruptedMoveTime = 0f; IsAutoRunning = false;
            }

            bool boostEnded;
            Vector3 planarStep = StepPlanarMotion(sprint, moveDirection, deltaTime, out boostEnded);
            UpdateFacing(moveDirection, hasMove, cameraForward, deltaTime);

            // Gravity, the ground and jumping (the legs ability, RequestAbility) belong to the
            // engine's CharacterController; this only sets the planar speed. SimpleMove takes a
            // speed, not a distance (Unity's Move took the distance), so this frame's movement goes
            // in as its average speed. The physics step (60 Hz) then moves that speed x 1/60 s.
            body.SimpleMove(planarStep * (1f / deltaTime));
            Velocity = body.Velocity;

            State = IsQuickBoosting ? MechMovementState.QuickBoost
                  : !IsGrounded ? MechMovementState.Airborne
                  : sprint && hasMove ? MechMovementState.GroundBoost
                  : MechMovementState.Grounded;

            if (boostStarted && QuickBoostStarted != null) QuickBoostStarted();
            if (boostEnded && QuickBoostEnded != null) QuickBoostEnded();

            // Unity's QuickDashFeedback: a kick on every dash, and a small dip when one ends on the
            // ground. Not when the mech is landing this very frame: that is a landing, not a stop.
            if (boostStarted) StartShake(dashStartShake, dashStartShakeDuration);
            if (boostEnded && IsGrounded && wasGrounded) StartShake(dashStopShake, dashStopShakeDuration);
        }

        /// <summary>
        /// Unity's StepPlanarMotion: this frame's flat movement in metres. Walking is speed x time.
        /// The dash is the exact area under its speed curve between where it was at the start of
        /// the frame and where it is at the end, so its length doesn't depend on the frame rate
        /// (sampling the curve once per frame came up 0.4-0.8 m short at 30-60 fps).
        /// ended is true on the frame the Quick Boost finishes.
        /// </summary>
        Vector3 StepPlanarMotion(bool sprint, Vector3 requested, float deltaTime, out bool ended)
        {
            ended = false;
            if (!IsQuickBoosting)
            {
                UpdatePlanarVelocity(sprint, requested, deltaTime);
                return planarVelocity * deltaTime;
            }
            // Only the part of the frame the dash is still running for (the last frame may be longer).
            float duration = ActiveBoostDuration;
            float active = Math.Min(deltaTime, quickBoostTimer);
            float start = MechMath.Clamp01(1f - quickBoostTimer / duration);
            float finish = MechMath.Clamp01(start + active / duration);

            // The dash keeps its captured direction; walking blends back in over its final fraction.
            // The tables hold the area as a fraction of the whole dash, so x speed x duration = metres.
            Vector3 exit = requested * WalkSpeed;
            Vector3 step = quickBoostDirection * (quickBoostPeakSpeed * duration * (IntegralAt(boostIntegral, finish) - IntegralAt(boostIntegral, start)))
                         + exit * (duration * (IntegralAt(exitIntegral, finish) - IntegralAt(exitIntegral, start)));

            // The speed at the end of the frame: what the next frame, the animation and the walk
            // that follows carry on from.
            float blend = ExitBlend(finish);
            planarVelocity = quickBoostDirection * (quickBoostPeakSpeed * BoostCurve(finish) * (1f - blend)) + exit * blend;
            quickBoostTimer = Math.Max(0f, quickBoostTimer - active);
            if (quickBoostTimer > 0.000001f) return step;

            quickBoostTimer = 0f;
            ended = true;
            brakingAfterBoost = true;
            planarVelocity = exit;
            // The dash finished part-way through this frame: the rest of the frame is walking.
            float remainder = deltaTime - active;
            if (remainder > 0f)
            {
                UpdatePlanarVelocity(sprint, requested, remainder);
                step = step + planarVelocity * remainder;
            }
            return step;
        }

        // Unity's boostIntegral / exitIntegral: running areas under the dash's speed curve (with the
        // exit blend taken out) and under the exit blend, from progress 0 to 1 in 128 slices. Built
        // once per press, so each frame's share is two lookups instead of a fresh integral.
        const int BoostIntegrationSteps = 128;
        readonly float[] boostIntegral = new float[BoostIntegrationSteps + 1];
        readonly float[] exitIntegral = new float[BoostIntegrationSteps + 1];

        void RebuildBoostIntegral()
        {
            boostIntegral[0] = exitIntegral[0] = 0f;
            float previousBlend = ExitBlend(0f);
            float previous = BoostCurve(0f) * (1f - previousBlend);
            for (int i = 1; i <= BoostIntegrationSteps; i++)
            {
                float progress = i / (float)BoostIntegrationSteps;
                float blend = ExitBlend(progress);
                float speed = BoostCurve(progress) * (1f - blend);
                // Trapezoid rule: each slice's area is its width times the average of its two edges.
                boostIntegral[i] = boostIntegral[i - 1] + (previous + speed) * (0.5f / BoostIntegrationSteps);
                exitIntegral[i] = exitIntegral[i - 1] + (previousBlend + blend) * (0.5f / BoostIntegrationSteps);
                previous = speed;
                previousBlend = blend;
            }
        }

        // The running area at any progress: straight-line between the two nearest slices.
        static float IntegralAt(float[] samples, float progress)
        {
            float position = MechMath.Clamp01(progress) * BoostIntegrationSteps;
            int index = Math.Min(BoostIntegrationSteps - 1, (int)Math.Floor(position));
            return samples[index] + (samples[index + 1] - samples[index]) * (position - index);
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

        // Seconds since the last dash started; infinity = no pulse running. Infinity plus a
        // frame time stays infinity, so nothing has to switch the pulse off.
        float quickBoostFovAge = float.PositiveInfinity;

        // Unity's UpdateMovementFeedback. Its pulse curve has keys (0, 0) (0.2, 1) (1, 0) with flat
        // tangents, which is a smoothstep up and a smoothstep down, so SmoothStep01 is that curve
        // exactly. The pulse is timed from the press, not from the dash's own progress, and the FOV
        // is set directly (no damping), as Unity sets lens.FieldOfView.
        void UpdateFov(float deltaTime)
        {
            if (gameplayCamera == null || baseFov <= 0f) return;
            quickBoostFovAge += deltaTime;
            float rise = Math.Max(0.01f, quickBoostFovRiseTime);
            float recovery = Math.Max(0.01f, quickBoostFovRecoveryTime);
            float pulse = 0f;
            if (quickBoostFovAge < rise)
                pulse = MechMath.SmoothStep01(quickBoostFovAge / rise);
            else if (quickBoostFovAge < rise + recovery)
                pulse = 1f - MechMath.SmoothStep01((quickBoostFovAge - rise) / recovery);
            float fov = baseFov + pulse * quickBoostFovKick;
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

        void UpdateAttacks(bool gameplay, float now, NetInput input)
        {
            bool left = input.IsDown(NetActions.AttackLeft), right = input.IsDown(NetActions.AttackRight);
            bool q = input.IsDown(NetActions.ShoulderLeft), e = input.IsDown(NetActions.ShoulderRight);
            // (The owner sends no actions while its mouse is free or a menu is open.)
            if (!gameplay) { attacksArmed = false; return; }
            // The click that captures the mouse must not also attack: arm once everything is released.
            if (!attacksArmed) { attacksArmed = !left && !right && !q && !e; return; }
            if (MechHitStop.BlocksNewActions(Entity)) return;   // Unity's MechCombat.TryAttack during a hit stop
            foreach (var weapon in Weapons)
            {
                if (weapon == null) continue;
                int action = ActionFor(weapon.Socket);
                if (action < 0) continue;
                bool requested = weapon.RepeatWhileHeld ? input.IsDown(action) : input.WasPressed(action);
                // One melee swing at a time (Unity's MeleeAttack.TryStart refuses while Pending): the
                // other arm can't swing while an assisted swing is still sliding in.
                if (weapon.IsMelee && meleeWeapon != null) continue;
                if (!requested || !weapon.TryAttack(now)) continue;
                NotifyCombatAction();
                ResolveHit(weapon, input);
                // Presentation, on every machine: the physical part's Fire animation and attack sound,
                // and the owner's camera shake (PlayAttackEffect).
                NetMechPart mounted = mountedParts[(int)weapon.Socket];
                if (mounted != null) AttackEffect.Play(mounted.Entity);
                ResetWeaponIdle();   // an attack ends the weapon idle (MechWeaponAnimator.PlayAttack)
                if (WeaponFired != null) WeaponFired(weapon);
            }
        }

        static int ActionFor(MechSocket socket)
        {
            switch (socket)
            {
                case MechSocket.ShoulderL: return NetActions.ShoulderLeft;
                case MechSocket.ShoulderR: return NetActions.ShoulderRight;
                case MechSocket.ArmL:      return NetActions.AttackLeft;
                case MechSocket.ArmR:      return NetActions.AttackRight;
                default:                   return -1;
            }
        }

        // Every machine: an accepted attack's Fire animation and sound on the part, and the camera
        // shake if the part is on this machine's own mech.
        static void PlayAttackEffect(Entity target, Vector3 position, NetReader extra)
        {
            NetMechPart part = target != null && target.IsAlive ? target.GetComponent<NetMechPart>() : null;
            if (part == null) return;
            part.PlayAttack();
            NetMechPlayer local = Local;
            if (local == null) return;
            for (Entity e = part.Transform.Parent; e != null; e = e.Transform.Parent)
                if (e == local.Entity) { local.StartShake(part); return; }
        }

        // The moment the owner was looking at, for the host's rewound aim test: the host's own mech
        // now; a client's about half its ping plus the 100 ms clients draw others behind.
        double ShooterViewTick()
        {
            if (!Network.IsOnline || Entity.IsMine) return Network.Tick;
            int ping = Math.Max(0, Network.GetPing(Entity.Owner));
            return Network.Tick - (ping * 0.5 / 1000.0 * Network.TickRate + 0.1 * Network.TickRate);
        }

        void ResolveHit(MechWeapon weapon, NetInput input)
        {
            Vector3 origin = Transform.WorldPosition;
            if (weapon.IsMelee) { StartMelee(weapon, origin); return; }

            // Free aim along the owner's camera (Unity's AimSolver): from the Camera Pivot (what the
            // camera orbits) along the yaw and pitch it sent, tested where things were when it saw
            // them. The point it finds, or the ray's end at range.
            if (aimPivot == null) aimPivot = Entity.FindChild("Camera Pivot");
            Vector3 eye = aimPivot != null ? aimPivot.Transform.WorldPosition : origin + Vector3.Up * 2f;
            double yaw = input.Yaw * Math.PI / 180.0, pitch = input.Pitch * Math.PI / 180.0;
            Vector3 look = new Vector3((float)(Math.Sin(yaw) * Math.Cos(pitch)), (float)Math.Sin(pitch), (float)(Math.Cos(yaw) * Math.Cos(pitch)));
            float reach = weapon.Def.Range + (origin - eye).Length;
            RaycastHit hit;
            bool aimed = Network.RaycastAt(ShooterViewTick(), eye, look, out hit, reach, Entity);
            Vector3 aimPoint = aimed ? hit.Point : eye + look * reach;

            // RangedAttack.Fire: a straight projectile from the part's muzzle toward the aim point.
            NetMechPart part = mountedParts[(int)weapon.Socket];
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
                // The Damage Amp is captured now, at the press, as Unity's RangedAttack.Fire does: a shot
                // fired during the amp still hits harder if the amp ends while it flies.
                if (shot != null) { MechProjectile.Fire(shot, weapon, Entity, direction, DamageMultiplier); return; }
                Log.Warning("MechPlayer: could not spawn " + weapon.Name + "'s projectile; using an instant hit.");
            }

            // No projectile prefab: an instant hit on what is under the screen centre. Same rule as a
            // projectile: full damage up to Range, nothing past it (the ray reaches from the camera,
            // so it can find something slightly beyond Range from the mech).
            if (!aimed) return;
            if ((hit.Point - origin).Length > weapon.Def.Range) return;
            float amp = DamageMultiplier;
            bool direct = MechCombatTargets.TryDamage(hit.Entity, weapon.Def.Damage * amp, origin);
            if (direct) ConfirmHit(MechCombatTargets.Owning(hit.Entity)?.Entity, MechHitKind.Projectile);

            // Splash: everyone else inside the radius takes the splash damage.
            if (weapon.Def.Attack != MechAttackType.Splash || weapon.Def.SplashRadius <= 0f) return;
            var targets = new List<MechCombatTargets.Target>(MechCombatTargets.All);
            foreach (var target in targets)
            {
                if (!target.Entity.IsAlive || (direct && IsHitEntity(target.Entity, hit.Entity))) continue;
                if ((target.Entity.Transform.WorldPosition - hit.Point).Length <= weapon.Def.SplashRadius + 0.5f)
                {
                    target.OnDamage(weapon.Def.SplashDamage * amp, origin);
                    ConfirmHit(target.Entity, MechHitKind.Blast);
                }
            }
        }

        static bool IsHitEntity(Entity target, Entity hit)
        {
            for (Entity e = hit; e != null; e = e.Transform.Parent)
                if (MechCombatTargets.Same(e, target)) return true;
            return false;
        }

        // --- melee swing with reach assist (Unity's MeleeAttack) ------------------------------------ //
        // A swing at the current target that is just out of reach doesn't hit at once: the hit box
        // slides forward to it over meleeApproachDuration, then hits. Only the box moves, never the
        // mech, and the target and direction are fixed when the swing starts.
        MechWeapon meleeWeapon;                 // the swing waiting for its box to arrive, or null
        MechCombatTargets.Target meleeTarget;
        Vector3 meleeForward;
        float meleeDamage, meleeExtension, meleeApproachTime;

        void StartMelee(MechWeapon weapon, Vector3 origin)
        {
            // Fixed at the swing's start, as Unity's strikeDamage / StrikeRotation: a Damage Amp running
            // out, or the mech turning, during the approach changes nothing.
            meleeDamage = weapon.Def.Damage * DamageMultiplier;   // Unity's MeleeAttack: x DamageModifiers.Total
            meleeForward = MechMath.Flat(Transform.Forward).Normalized;
            meleeExtension = FindMeleeApproach(origin, meleeForward, out meleeTarget);
            if (meleeExtension > 0.01f) { meleeWeapon = weapon; meleeApproachTime = 0f; return; }
            MeleeHit(meleeDamage, origin, meleeForward, 0f);
        }

        // Each frame after movement (Unity's MeleeAttack.Tick). The swing is lost, hitting nothing,
        // if the target dies, the weapon is swapped off, or the mech dashes or leaves the ground.
        void UpdateMelee(float deltaTime)
        {
            if (meleeWeapon == null) return;
            bool targetGone = meleeTarget == null || !meleeTarget.Entity.IsAlive || !MechCombatTargets.All.Contains(meleeTarget);
            if (targetGone || socketWeapons[(int)meleeWeapon.Socket] != meleeWeapon || IsQuickBoosting || !IsGrounded)
            {
                meleeWeapon = null;
                return;
            }
            meleeApproachTime += deltaTime;
            if (meleeApproachTime < meleeApproachDuration) return;
            meleeWeapon = null;
            // From where the mech is now (it may have walked a little), along the swing's own direction.
            MeleeHit(meleeDamage, Transform.WorldPosition, meleeForward, meleeExtension);
        }

        // Unity's FindApproach: how far the box must slide forward to reach the current target, or 0
        // for no assist. Uses the same target point as MeleeHit (its centre), so an assisted swing
        // can't miss on a technicality.
        float FindMeleeApproach(Vector3 origin, Vector3 forward, out MechCombatTargets.Target target)
        {
            target = null;
            float k = unityScale;
            // No assist without a selected target, or while dashing or in the air (Unity's rules).
            if (maximumMeleeApproach <= 0f || maximumMeleeAssistAngle <= 0f || Targeting == null || Targeting.IsFreeAim
                || IsQuickBoosting || !IsGrounded) return 0f;
            MechCombatTargets.Target candidate = Targeting.CurrentTarget;
            if (candidate == null || !candidate.Entity.IsAlive) return 0f;

            Vector3 point = candidate.Entity.Transform.WorldPosition + Vector3.Up * 1f;
            Vector3 offset = point - origin;
            Vector3 flat = MechMath.Flat(offset);
            if (flat.Length < 1e-3f) return 0f;
            // The further off to the side, the less reach: the full metre straight ahead, none at 30 degrees.
            float angle = (float)(Math.Acos(MechMath.Clamp(Vector3.Dot(forward, flat.Normalized), -1f, 1f)) * 180.0 / Math.PI);
            if (angle >= maximumMeleeAssistAngle) return 0f;
            float budget = maximumMeleeApproach * k * (1f - angle / maximumMeleeAssistAngle);

            // Sliding the box forward only helps a target beside or above it if it is already
            // inside the box's width and height; then the gap is how far beyond its front face it is.
            Vector3 right = Vector3.Cross(forward, Vector3.Up).Normalized;
            if (Math.Abs(Vector3.Dot(offset, right)) > meleeSizeX * 0.5f * k || Math.Abs(offset.Y) > meleeSizeY * 0.5f * k) return 0f;
            float gap = Vector3.Dot(offset, forward) - (meleeCenterZ + meleeSizeZ * 0.5f) * k;
            if (gap <= 0f || gap + 0.03f > budget) return 0f;     // already in reach, or too far for the assist

            // Never through a wall (Unity's LineBlocked / LimitApproach): a level collider between
            // the mech's chest (MechCombat.MeleeWallOrigin) and the target cancels the assist.
            Vector3 chest = origin + Vector3.Up * (muzzleOffsetY * k);
            Vector3 toTarget = point - chest;
            RaycastHit wall;
            if (Physics.Raycast(chest, toTarget.Normalized, out wall, toTarget.Length,
                                LayerMask.GetMask(PhysicsLayer.NonMoving), Entity)) return 0f;

            target = candidate;
            return gap + 0.03f;   // 3 cm past the face, so the centre is inside, not on the edge
        }

        // Unity's MeleeAttack box: MechCombat.meleeCenter / meleeSize in the mech's own frame (oriented
        // along forward), slid forward by extension (reach assist), each enemy damaged once per
        // swing. Targets count by their centre (feet + 1 m).
        void MeleeHit(float damage, Vector3 origin, Vector3 forward, float extension)
        {
            Vector3 right = Vector3.Cross(forward, Vector3.Up).Normalized;
            float k = unityScale;
            // Unity's TryMeleePush: along the swing (the mech's facing), not from the mech to each
            // golem, so everything in the box is shoved the same way.
            Vector3 push = forward * Math.Max(0f, meleeImpulseSpeed);
            Vector3 killLaunch = forward * Math.Max(0f, meleeKillImpulseSpeed) + Vector3.Up * Math.Max(0f, meleeKillLiftSpeed);
            var targets = new List<MechCombatTargets.Target>(MechCombatTargets.All);
            foreach (var target in targets)
            {
                if (!target.Entity.IsAlive) continue;
                Vector3 offset = target.Entity.Transform.WorldPosition + Vector3.Up * 1f - origin;
                float side = Vector3.Dot(offset, right), up = offset.Y, ahead = Vector3.Dot(offset, forward);
                if (Math.Abs(side) > meleeSizeX * 0.5f * k || Math.Abs(up) > meleeSizeY * 0.5f * k) continue;
                if (Math.Abs(ahead - (meleeCenterZ * k + extension)) > meleeSizeZ * 0.5f * k) continue;
                // Only golems are pushed (Unity: only victims with an EnemyAgent); anything else
                // (a Zoo dummy) just takes the damage through its registered callback.
                Enemy enemy = meleePush ? target.Entity.GetComponent<Enemy>() : null;
                bool landed = true;
                if (enemy != null) landed = enemy.ApplyMeleeHit(damage, push, killLaunch, meleePushSeconds);
                else target.OnDamage(damage, origin);
                if (landed) ConfirmHit(target.Entity, MechHitKind.Melee);
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
        NetMechPart idleWeapon;
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
                NetMechPart part = weapon != null ? mountedParts[(int)weapon.Socket] : null;
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

        // --- camera obstacles (CameraControlThirdPerson "Obstacle Avoidance", MechCameraObstacles) --- //
        public bool avoidObstacles = true;
        public float cameraRadius = 0.35f;          // minimum clearance from walls (grows to protect the near plane)
        public float obstacleReturnDamping = 0.25f; // seconds to ease back out once the wall is out of the way
        readonly MechCameraObstacles cameraObstacles = new MechCameraObstacles();

        // --- camera shake: firing (WeaponVisualSettings.firingShake / firingShakeDuration) ---------- //
        // Unity's QuickDashFeedback and CombatFeedback impulses, played as the same fading wobble as
        // the firing shake: strength in Unity metres (x unityScale), then seconds.
        public float dashStartShake = 0.035f;       // every accepted dash
        public float dashStartShakeDuration = 0.07f;
        public float dashStopShake = 0.018f;        // a dash that ends on the ground (not one landing as it ends)
        public float dashStopShakeDuration = 0.06f;
        public float damageShake = 0.16f;           // the mech takes damage
        public float damageShakeDuration = 0.24f;

        // --- hit stop (CombatFeedback "Confirmed hits and actor hitstop"; MechHitStop) ---------------- //
        public bool hitStop = false;   // off in the networked copy: a freeze on the host would not reach the clients
        public float meleeHitStop = 0.07f;          // seconds: one of the mech's melee swings lands
        public float projectileHitStop = 0.035f;    // a shot hits a target directly
        public float blastHitStop = 0.085f;         // a splash catches a target
        public float damageHitStop = 0.08f;         // the mech takes damage

        /// <summary>
        /// One of the mech's attacks landed on victim (Unity's CombatFeedback.OnConfirmedHit): the mech
        /// stops for that kind's hit stop. MechProjectile calls it too, for shots and splashes.
        /// </summary>
        public void ConfirmHit(Entity victim, MechHitKind kind)
        {
            if (!hitStop) return;
            float seconds = kind == MechHitKind.Melee ? meleeHitStop
                          : kind == MechHitKind.Blast ? blastHitStop : projectileHitStop;
            MechHitStop.Request(Entity, seconds);
            MechHitStop.Request(victim, seconds);   // and the victim (refused if the hit killed it)
        }

        /// <summary>
        /// An enemy's melee hit landed on the mech (Unity's CombatFeedback: not a projectile): the
        /// attacker stops for damageHitStop too. The mech's own stop comes from Health.Damaged.
        /// </summary>
        public void ConfirmHurtBy(Entity attacker)
        {
            if (hitStop) MechHitStop.Request(attacker, damageHitStop);
        }
        float shakeAmplitude, shakeDuration, shakeTime;
        Vector3 cameraRest;
        bool cameraRestKnown;

        void StartShake(NetMechPart part)
        {
            if (part != null) StartShake(part.firingShake, part.firingShakeDuration);
        }

        // Every shake goes through here: strength in Unity metres, then seconds. A new shake takes
        // over only if it is at least as strong as what is left of the running one (which fades),
        // so a small dash dip never cuts a big damage shake short.
        void StartShake(float strength, float duration)
        {
            if (strength <= 0f) return;
            float amplitude = strength * unityScale;
            float left = shakeAmplitude * (shakeDuration > 0f ? shakeTime / shakeDuration : 0f);
            if (amplitude < left) return;
            shakeAmplitude = amplitude;
            shakeDuration = shakeTime = Math.Max(0.01f, duration);
        }

        // The camera's local position each frame: its authored spot, pulled in front of any wall,
        // plus the firing shake. One function writes it, because both move the same transform and
        // the shake must wobble around the corrected spot, not the one inside the wall. After
        // UpdateLockCamera, which raises the pivot the casts start from.
        void PlaceCamera(float deltaTime)
        {
            if (gameplayCamera == null) return;
            Transform view = gameplayCamera.Entity.Transform;
            if (!cameraRestKnown) { cameraRest = view.Position; cameraRestKnown = true; }

            // Only for the third-person rig: a Camera under a CameraOrbit pivot.
            Vector3 rest = cameraRest;
            Entity rig = view.Parent;
            if (avoidObstacles && rig != null && rig.HasComponent<CameraOrbit>())
            {
                cameraObstacles.CameraRadius = cameraRadius;          // Inspector changes apply live
                cameraObstacles.ReturnDamping = obstacleReturnDamping;
                rest = cameraObstacles.Resolve(rig.Transform, cameraRest, gameplayCamera, Entity, deltaTime);
            }

            Vector3 shake = Vector3.Zero;
            if (shakeTime > 0f)
            {
                shakeTime = Math.Max(0f, shakeTime - deltaTime);
                float a = shakeAmplitude * (shakeTime / shakeDuration);
                shake = new Vector3((float)(idleRandom.NextDouble() * 2 - 1) * a,
                                    (float)(idleRandom.NextDouble() * 2 - 1) * a, 0f);
            }
            else shakeAmplitude = 0f;
            view.Position = rest + shake;
        }

        // --- abilities (Unity main's MechAbilities) ---------------------------------------- //
        // One switch on MechAbilityKind instead of Unity's effect classes: three abilities, and
        // the effects need MechPlayer's own body and health anyway.

        // Unity's MechAbilities.Request. Refused, without spending the cooldown, when the socket has
        // no ability, it is still running or cooling down (ConfirmActivation), or it cannot start
        // here: Jump needs the ground. body.IsGrounded is read directly because this runs before
        // Tick refreshes IsGrounded this frame.
        void RequestAbility(MechSocket socket, float now)
        {
            foreach (var ability in Abilities)
            {
                if (ability == null || ability.Socket != socket) continue;
                if (ability.Kind == MechAbilityKind.Jump && !body.IsGrounded) return;
                if (!ability.ConfirmActivation(now)) return;
                // Jump is instant: the engine's own jump, Power golem lengths high. Heal and Damage
                // Amp just run for their duration (UpdateAbilities, DamageMultiplier).
                if (ability.Kind == MechAbilityKind.Jump)
                    body.Jump(ability.Power * MechPartDef.MetresPerGolem);
                if (AbilityActivated != null) AbilityActivated(ability);
                return;
            }
        }

        // Unity's ITickingAbilityEffect: Heal restores Power % of maximum health per second while
        // it runs. The other kinds need nothing per frame.
        void UpdateAbilities(float deltaTime, float now)
        {
            foreach (var ability in Abilities)
                if (ability != null && ability.Kind == MechAbilityKind.Heal && ability.IsActive(now))
                    Health.Heal(Health.Maximum * ability.Power / 100f * deltaTime);
        }

        /// <summary>
        /// Weapon damage scale right now: x (1 + Power %) for each running Damage Amp, multiplied
        /// together (Unity: DamageModifiers.Total). 1 when none is running.
        /// </summary>
        public float DamageMultiplier
        {
            get
            {
                float now = Time.ElapsedTime, total = 1f;
                foreach (var ability in Abilities)
                    if (ability != null && ability.Kind == MechAbilityKind.DamageAmp && ability.IsActive(now))
                        total *= 1f + ability.Power / 100f;
                return total;
            }
        }

        void UpdateDebugKeys()
        {
            if (Input.IsPressed(KeyCode.K)) Health.ApplyDamage(debugDamage);
            if (Input.IsPressed(KeyCode.H)) Health.Refill();
        }
    }
}
