/*************************************************************************
* \file:         MechPart.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.1
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
*
**************************************************************************/
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

        public MechPartDef Def { get; private set; }
        public MechWeapon Weapon { get; private set; }     // weapons only
        public bool IsMounted { get; private set; }

        WorldText label;
        bool highlighted;
        string prompt = string.Empty;

        public override void OnCreate()
        {
            Def = MechPartCatalogue.Find(partId);
            if (Def != null && Def.IsWeapon) Weapon = new MechWeapon(Def);
            Entity labelEntity = Entity.FindChild("Label");
            label = labelEntity != null ? labelEntity.GetComponent<WorldText>() : null;
            Entity parent = Transform.Parent;
            SetMounted(parent != null && parent.Name.StartsWith("Mount "));
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
            if (model != null && !model.Animator.SetTrigger("Fire") && !fireWarned)
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
        public bool CanAnimate { get { return Entity.FindChild("Model") != null && Def != null && Def.IsWeapon; } }

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
