using System;
using Crucible;

namespace Crucible.Scripts
{
    // Enemy, part 3: the attack. Port of Gam3333's EnemyAttack (with the graph asking for
    // attacks: "externalRequests") and EnemyWeaponTelegraph.
    //
    // An attack: the graph's Request node calls TryAttack. It winds up for fireDuration
    // seconds; at releaseFrame of the way through, melee hits whatever is in the box in
    // front (as the swing began, so a sidestep dodges it) and ranged fires a bolt at the
    // point it aimed at when it started (bolts never home). The cooldown starts with the
    // attack. During the wind-up an orange lane on the ground warns the player.
    //
    // Unity took damage, cooldown and reach from the weapon PART (PartData) times per-enemy
    // multipliers; Crucible has no part system yet, so the results are the fields here:
    //   mace:     70 damage x 0.178571 = 12.5, every 1 s x 1.05, Range 0.7 golem = 3.85 m box
    //   crossbow: 70 damage x 0.285714 = 20,   every 1.667 s x 3.2 = 5.33 s, Range 10 golems = 40 m;
    //             bolt speed 160 x 0.45 = 72 m/s (the bolt prefab's own fields)
    public sealed partial class Enemy
    {
        public bool melee = true;                 // true: hits a box in front; false: fires boltPrefab
        public float damage = 12.5f;              // melee damage (a bolt's damage is on the bolt prefab)
        public float cooldown = 1.05f;            // seconds from one attack's start to the next
        public float fireDuration = 0.7f;         // length of one attack
        public float releaseFrame = 0.6f;         // 0..1: when in the attack the hit lands / the bolt flies
        public float facingAngle = 70f;           // degrees either side of straight ahead it can attack
        public float verticalReach = 2.5f;        // melee only: metres above or below it can hit
        public float weaponReach = 3.85f;         // melee: depth of the hit box; ranged: how far a shot does damage
        public float meleeWidth = 5f;             // the melee hit box (Unity's meleeSize x and y)
        public float meleeHeight = 3.5f;
        public bool respectAttackRangeAtRelease = true;   // melee: re-check range at the hit, so backing off dodges
        public float aimSpreadDegrees = 0f;       // ranged: random cone round the aim point
        public Prefab boltPrefab;                 // ranged: what it fires (a Bolt)
        public bool telegraph = true;             // the orange warning lane during the wind-up

        bool attacking, released, warnedNoBolt;
        float age, cooldownLeft;
        CombatHealth attackTarget;                // who the current attack is aimed at
        Vector3 aim, strikeFacing;                // where the bolt goes; which way the swing faces
        Entity telegraphEntity;
        float telegraphThickness = 0.02f;

        public bool AttackInProgress { get { return attacking; } }
        public float CooldownRemaining { get { return cooldownLeft; } }

        void CreateWeapon()
        {
            telegraphEntity = Entity.FindChild("Telegraph");
            if (telegraph && (telegraphEntity == null || !telegraphEntity.HasComponent<MeshRenderer>()))
            {
                Log.Warning("[Enemy] " + Entity.Name + ": no child \"Telegraph\" with a MeshRenderer - no attack warning");
                telegraphEntity = null;
            }
            if (telegraphEntity != null) telegraphThickness = telegraphEntity.Transform.Scale.Y;
            HideTelegraph();
        }

        // Every frame (Unity's EnemyAttack.Tick in LateUpdate).
        void UpdateWeapon(float deltaTime)
        {
            cooldownLeft = Math.Max(0f, cooldownLeft - deltaTime);

            if (attacking)
            {
                // A lost or dead target cancels the hit that hasn't landed. A bolt already
                // flying keeps flying.
                if (attackTarget == null || attackTarget.IsDead || !attackTarget.Entity.IsActive)
                {
                    if (!released) AnimateAttackCancel();   // EnemyAnimation.cs: don't leave a charge hanging
                    attacking = false;
                }
                else
                {
                    age += deltaTime;
                    if (!released && age >= fireDuration * releaseFrame)
                    {
                        released = true;
                        if (melee) StrikeMelee();
                        else FireBolt();
                        AnimateAttackRelease();             // EnemyAnimation.cs: the cannon's Fire
                    }
                    if (age >= fireDuration) attacking = false;
                }
            }

            UpdateTelegraph();
        }

        // Can it attack the target from where it stands, facing the way it faces?
        public bool TargetInReach()
        {
            if (!IsReady || targetHealth == null || targetHealth.IsDead || !targetHealth.Entity.IsActive) return false;
            Entity victim = targetHealth.Entity;
            return CanReach(victim) && ClearPathFrom(Position, victim, melee);
        }

        // Starts an attack if it is off cooldown and can reach. Returns whether it started.
        public bool TryAttack()
        {
            if (!IsReady || attacking || cooldownLeft > 0f) return false;
            if (MechHitStop.BlocksNewActions(Entity)) return false;   // Unity's EnemyAttack: none during a hit stop
            if (targetHealth == null || targetHealth == this || targetHealth.IsDead || !targetHealth.Entity.IsActive) return false;

            Entity victim = targetHealth.Entity;
            if (!CanReach(victim) || !ClearPathFrom(Position, victim, melee)) return false;

            attackTarget = targetHealth;
            aim = AimPoint(victim);                  // ranged shots aim once; bolts never home
            if (!melee && aimSpreadDegrees > 0f) aim = Spread(aim);
            strikeFacing = Facing;                   // melee strikes where the swing began
            age = 0f;
            cooldownLeft = cooldown;
            attacking = true;
            released = false;
            AnimateAttackStart();                    // EnemyAnimation.cs: the slash, or the cannon's charge
            return true;
        }

        // Drops a hit that hasn't landed; the cooldown keeps running (Unity's CancelPendingAttack).
        public void CancelAttack()
        {
            if (attacking && !released) AnimateAttackCancel();
            attacking = false;
            released = false;
            attackTarget = null;
        }

        // Melee: within reach on the flat and no more than verticalReach above or below.
        // Ranged: within weaponReach in a straight line, at any height. Both: no further than
        // attackRange (+0.5) on the flat, and facing the target.
        bool CanReach(Entity victim)
        {
            Vector3 offset = victim.Transform.WorldPosition - Position;
            if (melee && Math.Abs(offset.Y) > verticalReach) return false;

            Vector3 flat = Flat(offset);
            float flatDistance = flat.Length;
            float distance = melee ? flatDistance : offset.Length;
            if (distance > weaponReach + 0.5f || flatDistance > attackRange + 0.5f) return false;
            if (flatDistance < 0.03f) return true;   // on top of it: any facing will do

            float cosine = Vector3.Dot(Facing, flat * (1f / flatDistance));
            return Math.Acos(Math.Max(-1f, Math.Min(1f, cosine))) * 180.0 / Math.PI <= facingAngle;
        }

        // Unity's ClearPathFrom: nothing between "from" (half a metre up) and the victim's
        // aim point. Unity looked at every hit along the line, skipping the victim (and, for
        // melee's wallsOnly, any body). Here a layer mask does the same with one nearest hit:
        //   wallsOnly - only walls and props are on the mask, so bodies are looked straight
        //               past, and a wall behind one is still seen;
        //   otherwise - every layer; if the nearest thing is the victim, nothing came before it.
        bool ClearPathFrom(Vector3 from, Entity victim, bool wallsOnly)
        {
            Vector3 origin = from + Vector3.Up * 0.5f;
            Vector3 offset = AimPoint(victim) - origin;
            float distance = offset.Length;
            if (distance < 0.01f) return true;

            RaycastHit hit;
            int mask = wallsOnly ? MovementQueryMask : LayerMask.All;
            if (!Physics.Raycast(origin, offset * (1f / distance), out hit, distance, mask, Entity)) return true;
            return !wallsOnly && IsPartOf(hit.Entity, victim);
        }

        // Unity's AimPoint: the middle of the victim's collider (collider.bounds.center), so
        // shots go at its chest, not its feet - the line then clears low cover and other
        // golems' shoulders the way Unity's did. Unity's fallback with no collider: half a
        // metre up. A CharacterController's Center isn't scaled; a Collider's is (local space).
        static Vector3 AimPoint(Entity victim)
        {
            Transform t = victim.Transform;
            if (victim.HasComponent<CharacterController>())
                return t.WorldPosition + t.WorldRotation * victim.CharacterController.Center;
            if (victim.HasComponent<Collider>())
            {
                Vector3 c = victim.Collider.Center, s = t.Scale;
                return t.WorldPosition + t.WorldRotation * new Vector3(c.X * s.X, c.Y * s.Y, c.Z * s.Z);
            }
            return t.WorldPosition + Vector3.Up * 0.5f;
        }

        // Used by the crossbow's firing-position planning: a clear shot from that spot?
        public bool HasClearRangedPathFrom(Vector3 position)
        {
            return targetHealth != null && !targetHealth.IsDead && targetHealth.Entity.IsActive
                && ClearPathFrom(position, targetHealth.Entity, false);
        }

        // Is "part" the entity "whole", or one of its children?
        static bool IsPartOf(Entity part, Entity whole)
        {
            for (Entity e = part; !ReferenceEquals(e, null) && e.IsAlive; e = e.Transform.Parent)
                if (e == whole) return true;
            return false;
        }

        // Unity's StrikeMelee: a box in front, as the swing began - meleeWidth wide,
        // meleeHeight tall, from the golem out to weaponReach - re-checked at the hit.
        void StrikeMelee()
        {
            Entity victim = attackTarget.Entity;
            if (respectAttackRangeAtRelease && !CanReach(victim)) return;
            if (!ClearPathFrom(Position, victim, true)) return;

            Vector3 offset = victim.Transform.WorldPosition - Position;
            Vector3 side = Vector3.Cross(Vector3.Up, strikeFacing);
            float ahead = Vector3.Dot(offset, strikeFacing);
            float across = Vector3.Dot(offset, side);
            if (ahead < 0f || ahead > weaponReach) return;
            if (Math.Abs(across) > meleeWidth * 0.5f || Math.Abs(offset.Y) > meleeHeight * 0.5f) return;

            if (!attackTarget.ApplyDamage(damage)) return;
            // Unity's CombatFeedback: a melee hit that lands on the mech stops the golem that landed
            // it as well (bolts don't). The mech decides, since its hit stop settings are its own.
            MechPlayer player = attackTarget as MechPlayer;
            if (player != null) player.ConfirmHurtBy(Entity);
        }

        void FireBolt()
        {
            if (boltPrefab == null)
            {
                if (!warnedNoBolt) Log.Warning("[Enemy] " + Entity.Name + ": a ranged enemy needs a Bolt Prefab");
                warnedNoBolt = true;
                return;
            }

            // Unity's fallback muzzle: half a metre up and ahead. 1.5 m ahead here, to start
            // clear of the golem's 1.7 m deep body box.
            Vector3 start = Position + Vector3.Up * 0.5f + strikeFacing * 1.5f;
            Vector3 direction = (aim - start).Normalized;
            if (direction.Length < 0.5f) direction = strikeFacing;

            // LookRotation points the bolt's Transform.Forward along direction; Bolt flies that way.
            boltPrefab.Instantiate(start, Quaternion.LookRotation(direction));
        }

        // The aim point moved by a random amount inside a cone of aimSpreadDegrees.
        Vector3 Spread(Vector3 point)
        {
            Vector3 origin = Position + Vector3.Up * 0.5f;
            Vector3 ray = point - origin;
            Vector3 forward = ray.Normalized;
            Vector3 right = Vector3.Cross(Vector3.Up, forward).Normalized;
            if (right.Length < 0.1f) right = Vector3.Cross(Vector3.Up, Facing).Normalized;
            Vector3 up = Vector3.Cross(forward, right).Normalized;

            // A random point in the unit circle (Unity's insideUnitCircle).
            float x, y;
            do { x = (float)(random.NextDouble() * 2.0 - 1.0); y = (float)(random.NextDouble() * 2.0 - 1.0); } while (x * x + y * y > 1f);

            float scale = (float)Math.Tan(aimSpreadDegrees * Math.PI / 180.0) * ray.Length;
            return point + (right * x + up * y) * scale;
        }

        // Port of EnemyWeaponTelegraph + EnemyThreatMarker.Lane: during the wind-up, a lane
        // on the ground - 3 m by 2 m in front for melee, a thin line to the aim point for a
        // bolt. Unity drew its outline and faded it in; here a flat box grows along the lane
        // as the hit comes, which says the same thing without transparency.
        void UpdateTelegraph()
        {
            if (telegraphEntity == null) return;
            bool show = telegraph && attacking && !released;
            MeshRenderer renderer = telegraphEntity.MeshRenderer;
            if (renderer.Visible != show) renderer.Visible = show;
            if (!show) return;

            float groundY = Position.Y - (agent != null ? agent.BaseOffset : 1f) + 0.06f;   // just above the feet
            Vector3 start = new Vector3(Position.X, groundY, Position.Z);
            Vector3 end = melee ? start + strikeFacing * 3f : new Vector3(aim.X, groundY, aim.Z);

            Vector3 lane = end - start;
            float length = lane.Length;
            Vector3 direction = length > 0.01f ? lane * (1f / length) : strikeFacing;
            float progress = Math.Max(0.1f, Math.Min(1f, age / Math.Max(0.01f, fireDuration * releaseFrame)));
            float filled = Math.Max(0.01f, length * progress);

            // Scale is local; the golem roots are at scale 1, so local is world here.
            telegraphEntity.Transform.WorldPosition = start + direction * (filled * 0.5f);
            telegraphEntity.Transform.WorldRotation = Quaternion.LookRotation(direction);
            telegraphEntity.Transform.Scale = new Vector3(melee ? 2f : 0.55f, telegraphThickness, filled);
        }

        void HideTelegraph()
        {
            if (telegraphEntity != null && telegraphEntity.MeshRenderer.Visible) telegraphEntity.MeshRenderer.Visible = false;
        }
    }
}
