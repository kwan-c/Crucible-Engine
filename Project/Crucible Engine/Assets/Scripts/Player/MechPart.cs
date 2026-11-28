/*************************************************************************
* \file:         MechPart.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \author:       Hugo Low Ren Hao (M3: settleOnSpawn for enemy drops)
* \par:          Email: low.h@digipen.edu
* \date          8 November 2026
* \version       1.2
* \brief         One physical part: lying on the ground as a pickup, or
*                mounted on the mech. Port of Unity's Part (+ the pickup
*                side of PickupDetector): stand near it, V cycles the
*                selection, 1 / 2 swaps it into the left (or only) / right
*                socket.
*
* \details
*    Entity setup:
*
*        Part Sword      Collider (Is Trigger, the pickup radius),
*        |               Script: MechPart (partId = "sword")
*        |- Model ...    MeshRenderer(s): the look (a GLB or primitives)
*        '- Label        WorldText (optional; name / swap prompt)
*
*    The root is what moves; its Model children are authored in the frame of
*    the socket the part mounts on. For the specialist GLBs that is the
*    Mount_* node's frame, which is Z-up (Blender's): mountFrame = true makes
*    the part stand upright (-90 degrees about X) while it lies on the ground.
*
*    A part mounted on the mech is a child of one of the player's mount
*    entities ("Mount ArmL" ...); MechPlayer equips those at start. When it is
*    swapped out it is unparented and lands at the mech's feet, keeping its
*    ammo and cooldowns (Unity: Unequip -> SetParent(null) and it drops).
*
*    partId must be an id from MechPartCatalogue (MechLoadout.cs).
*
*    v1.1  Model children, upright on the ground, drop at the mech's feet;
*          F no longer swaps (Unity: 1 / 2).
*    v1.2  settleOnSpawn: a spawned pickup (an enemy's drop) lands upright
*          on the ground below it.
*    v1.3  rollLootStats: a part that starts loose rolls its stats +-10%
*          (MechPartStatRoll, Unity's PartStatRoll); mounted parts don't.
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    public sealed class MechPart : EntityScript
    {
        public string partId = "sword";
        public float mountedScale = 1f;     // local scale while on the mech (under the mount)
        public float groundScale = 1f;      // scale while lying in the world
        public float groundLift = 0.6f;     // metres above the ground when dropped
        public bool mountFrame = true;      // Model children are Z-up (a Mount_* frame)
        // Unity's MechWeaponAnimationSet: the attack sound and the "Model" child's Fire trigger.
        public AudioClip attackSound;
        public float attackVolume = 0.75f;
        // Unity's WeaponVisualSettings.firingShake / firingShakeDuration (camera shake per shot).
        public float firingShake = 0.02f;
        public float firingShakeDuration = 0.12f;
        // Unity's PartData.projectilePrefab: what a ranged attack launches (a MechProjectile prefab).
        public Prefab projectile;
        // Unity's Socket.weaponAttachments: a correction of this part's pose on particular sockets,
        // "Socket:px,py,pz,qx,qy,qz,qw;..." in the mount's frame (SceneGen converts Unity's values).
        public string socketPoses = "";
        // Tick on a pickup PREFAB that something spawns (an enemy's drop). Unity's dropped part falls
        // with its Rigidbody; parts here have no physics, so it puts itself on the ground below the
        // spawn point instead. Off for parts placed in a scene: they are already where the level's
        // author put them, maybe on a rack, and must not move.
        public bool settleOnSpawn = false;
        // Unity's Part.RollLootStats: a part that starts loose in the world (an enemy's drop, a
        // test spawner's, one lying in a level) rolls its numeric stats +-10% once
        // (MechPartStatRoll). A part that starts mounted on the mech keeps the catalogue's
        // stats, and a rolled part keeps its roll through every swap. Untick for exact balance.
        public bool rollLootStats = true;

        public MechPartDef Def { get; private set; }
        public MechWeapon Weapon { get; private set; }     // weapons only
        public bool IsMounted { get; private set; }

        WorldText label;
        bool highlighted;
        string prompt = string.Empty;

        public override void OnCreate()
        {
            Def = MechPartCatalogue.Find(partId);
            Entity parent = Transform.Parent;
            bool startsMounted = parent != null && parent.Name.StartsWith("Mount ");
            if (rollLootStats && !startsMounted && Def != null) Def = MechPartStatRoll.Create(Def, MechPartStatRoll.NewSeed());
            if (Def != null && Def.IsWeapon) Weapon = new MechWeapon(Def);
            Entity labelEntity = Entity.FindChild("Label");
            label = labelEntity != null ? labelEntity.GetComponent<WorldText>() : null;
            SetMounted(startsMounted);
            if (settleOnSpawn && !IsMounted) SettleOnGround();
        }

        /// <summary>
        /// Re-rolls this loose part's stats from a known seed (Unity: Part.RollLootStats(seed), for
        /// loot spawners or saves). Refused once the part is mounted. A fresh weapon state comes
        /// with it (full ammo for the rolled magazine and reserve).
        /// </summary>
        public bool RollLootStats(int seed)
        {
            if (IsMounted || Def == null) return false;
            Def = MechPartStatRoll.Create(Def, seed);
            Weapon = Def.IsWeapon ? new MechWeapon(Def) : null;
            RefreshLabel();
            return true;
        }

        /// <summary>Back to the catalogue's authored stats (Unity: KeepAuthoredStats). Loose parts only.</summary>
        public bool KeepAuthoredStats()
        {
            if (IsMounted || Def == null || Def.Base == null) return false;
            Def = Def.Base;
            Weapon = Def.IsWeapon ? new MechWeapon(Def) : null;
            RefreshLabel();
            return true;
        }

        // Prefab.Instantiate(position, rotation) REPLACES the root's rotation, so a spawned part
        // arrives with only the enemy's random yaw and has lost the upright tilt mountFrame needs.
        // Drop() puts both back and lifts it by groundLift, exactly like a part swapped off the mech.
        void SettleOnGround()
        {
            float yaw = (float)(Math.Atan2(-Transform.Forward.X, -Transform.Forward.Z) * 180.0 / Math.PI);
            Vector3 from = Transform.WorldPosition;
            // Level geometry only: the golem that dropped this is still toppling underneath it, and
            // a part resting on the corpse would float once the corpse is removed.
            RaycastHit hit;
            bool ground = Physics.Raycast(from, new Vector3(0f, -1f, 0f), out hit, 50f,
                                          LayerMask.GetMask(PhysicsLayer.NonMoving), Entity);
            Drop(ground ? hit.Point : from, yaw);   // no ground found: stay put, but upright
        }

        public override void OnDestroy()
        {
            MechPlayer.ForgetPart(this);
        }

        /// <summary>
        /// An accepted attack (Unity's MechWeaponAnimator.PlayAttack): the model's Fire trigger and
        /// the attack sound. One voice per weapon - a new attack restarts it instead of stacking.
        /// </summary>
        public void PlayAttack()
        {
            SetIdle(false);   // MechWeaponAnimator.PlayAttack resets the weapon idle
            Entity model = Entity.FindChild("Model");
            if (model != null && model.HasComponent<Animator>() && !model.Animator.SetTrigger("Fire") && !fireWarned)
            {
                // False before the controller's first update, or without a "Fire" Trigger parameter.
                fireWarned = true;
                Log.Warning("MechPart " + partId + ": the Model's animator refused the Fire trigger (no controller with a 'Fire' Trigger yet?).");
            }
            if (attackSound == null || attackVolume <= 0f || AudioSource == null) return;
            AudioSource.Stop();
            AudioSource.Clip = attackSound;
            AudioSource.Volume = attackVolume;
            AudioSource.Play();
        }

        /// <summary>
        /// A swap without a mount entity (legs): this pickup takes the dropped part's identity
        /// (and its live weapon state, if any) while its model stays the same.
        /// </summary>
        public void BecomePart(MechPartDef def, MechWeapon weapon)
        {
            if (def == null) return;
            Def = def;
            partId = def.Id;
            Weapon = def.IsWeapon ? (weapon ?? new MechWeapon(def)) : null;
            RefreshLabel();
        }

        /// <summary>Into a mount: the socket's attachment pose (identity by default) at the mounted scale.</summary>
        public void AttachTo(Entity mount, MechSocket socket)
        {
            Transform.SetParent(mount, false);
            Vector3 position; Quaternion rotation;
            SocketPose(socket, out position, out rotation);
            Transform.Position = position;
            Transform.Rotation = rotation;
            Transform.Scale = Vector3.One * mountedScale;
            SetMounted(true);
        }

        /// <summary>This part's pose on a socket: its socketPoses entry, else identity.</summary>
        public void SocketPose(MechSocket socket, out Vector3 position, out Quaternion rotation)
        {
            position = Vector3.Zero; rotation = Quaternion.Identity;
            if (string.IsNullOrEmpty(socketPoses)) return;
            foreach (string entry in socketPoses.Split(';'))
            {
                int colon = entry.IndexOf(':');
                if (colon <= 0 || entry.Substring(0, colon).Trim() != socket.ToString()) continue;
                string[] v = entry.Substring(colon + 1).Split(',');
                if (v.Length != 7) { Log.Warning("MechPart " + partId + ": bad socketPoses entry '" + entry + "'."); return; }
                float[] f = new float[7];
                for (int i = 0; i < 7; i++)
                    f[i] = float.Parse(v[i].Trim(), System.Globalization.CultureInfo.InvariantCulture);
                position = new Vector3(f[0], f[1], f[2]);
                rotation = new Quaternion(f[3], f[4], f[5], f[6]);
                return;
            }
        }

        public bool HasMuzzle { get { return Entity.FindChild("Muzzle") != null; } }

        /// <summary>Where shots leave: the "Muzzle" child (on the model's muzzle node), else the part itself.</summary>
        public Vector3 MuzzlePosition
        {
            get
            {
                Entity muzzle = Entity.FindChild("Muzzle");
                return muzzle != null ? muzzle.Transform.WorldPosition : Transform.WorldPosition;
            }
        }

        /// <summary>
        /// Unity's MechWeaponIdle: only the one weapon it picks plays its Neutral clip; every other weapon
        /// holds the clip's first frame. (A reload after a shot is part of the attack animation; R alone
        /// plays none, as in Unity.)
        /// </summary>
        public void SetIdle(bool playing)
        {
            if (idlePlaying == playing) return;
            idlePlaying = playing;
            Entity model = Entity.FindChild("Model");
            if (model != null) model.Animator.SetBool("Idle", playing);
        }
        bool idlePlaying, fireWarned;

        /// <summary>Does this part have an animated model (a weapon controller)?</summary>
        public bool CanAnimate
        {
            get
            {
                Entity model = Entity.FindChild("Model");
                return model != null && model.HasComponent<Animator>() && Def != null && Def.IsWeapon;
            }
        }

        /// <summary>Out of the mech: a root again, upright on the ground at this point, facing yawDegrees.</summary>
        public void Drop(Vector3 groundPoint, float yawDegrees)
        {
            Transform.SetParent(null, true);
            Quaternion upright = mountFrame ? Quaternion.AngleAxis(-90f, new Vector3(1f, 0f, 0f)) : Quaternion.Identity;
            Transform.WorldRotation = Quaternion.AngleAxis(yawDegrees, Vector3.Up) * upright;
            Transform.WorldPosition = groundPoint + Vector3.Up * groundLift;
            Transform.Scale = Vector3.One * groundScale;
            SetMounted(false);
        }

        // The pickup radius: tell the player it is in reach (Unity's PickupDetector trigger).
        public override void OnTriggerEnter(Entity other)
        {
            if (IsMounted || other == null || !other.IsAlive) return;
            MechPlayer player = other.GetComponent<MechPlayer>();
            if (player != null) player.AddNearbyPart(this);
        }

        public override void OnTriggerExit(Entity other)
        {
            if (other == null || !other.IsAlive) return;
            MechPlayer player = other.GetComponent<MechPlayer>();
            if (player != null) player.RemoveNearbyPart(this);
        }

        /// <summary>Equipped parts have no pickup trigger (Unity: SetEquippedPhysics).</summary>
        public void SetMounted(bool mounted)
        {
            IsMounted = mounted;
            if (HasComponent<Collider>()) Entity.SetEnabled<Collider>(!mounted);
            if (mounted) highlighted = false;
            RefreshLabel();
        }

        /// <summary>The selected pickup shows the swap prompt, the others just their name.</summary>
        public void SetHighlight(bool on, string swapPrompt)
        {
            if (highlighted == on && prompt == swapPrompt) return;
            highlighted = on;
            prompt = swapPrompt ?? string.Empty;
            RefreshLabel();
        }

        void RefreshLabel()
        {
            if (label == null) return;
            string name = Def != null ? Def.Name : partId;
            if (IsMounted) label.Text = string.Empty;
            else if (highlighted) { label.Text = name + "\n" + prompt; label.Color = new Color(1f, 0.85f, 0.25f); }
            else { label.Text = name; label.Color = Color.White; }
        }
    }
}
