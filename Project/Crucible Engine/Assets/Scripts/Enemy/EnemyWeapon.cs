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
    // attack. During the wind-up a lane on the ground warns the player.
    //
    // Melee contact timing: the body's MeleeSlash lunge lands at releaseFrame (0.6 of 0.7 s
    // = clip frame 15 of Fodder_MeleeSlash at speed 1.19). The equipped weapon has its own
    // short Fire clip (MaceArm_Fire / SwordArm_Fire, 0.375 s); weaponContactSeconds says how
    // far into that clip it connects, and the swing is started that long before the release,
    // so the weapon, the body and the damage meet on the same frame. 0 keeps the old
    // behaviour (the weapon swings as the attack starts).
    //
    // Heavy Cannon (groundTargetSplash): the shot is committed at the start of the attack
    // to a point on the ground just in front of the target's feet (straight down from its
    // centre against the level, so jumps, slopes and steps are handled), shown by a splash
    // disc. The shell flies there in a straight line (no homing) and bursts on the first
    // thing it hits - the ground, a wall, or the target if it steps into the path - and the
    // splash is centred on that actual impact point.
    //
    // Armed prefabs use the player's MechPartCatalogue. The enemy owns its attack
    // timing and aim. Older prefabs can still use their original damage and Bolt.
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
        public Prefab boltPrefab;                 // shared MechProjectile or a legacy Bolt
        public bool telegraph = true;             // the orange warning lane during the wind-up
        public string equippedWeaponId = "";
        public float weaponDamageMultiplier = 1f;
        public float projectileSpeedMultiplier = 1f;
        public int spawnCost = 1;
        public float weaponContactSeconds = 0f;   // melee: seconds into the weapon's Fire clip where it connects (0 = swing at attack start)
        public float minimumAttackDistance = 0f;  // ranged: never fires closer than this (the weapon's own safe distance)
        public bool groundTargetSplash = false;   // splash weapons: aim at the ground at the target's feet (Heavy Cannon)
        public float groundImpactOffset = -1f;    // metres in front of the feet, toward the shooter; < 0 = just clear the target's capsule
        public float groundProbeDepth = 8f;       // metres below the target's centre to look for ground

        bool attacking, released, warnedNoBolt;
        bool weaponSwung, groundTargeted;
        float age, cooldownLeft, weaponSwingAt;
        CombatHealth attackTarget;                // who the current attack is aimed at
        Vector3 aim, strikeFacing;                // where the bolt goes; which way the swing faces
        Entity telegraphEntity, splashEntity;
        Entity weaponMuzzle;
        MechPartDef equippedWeapon;
        bool invalidWeapon;
        float telegraphThickness = 0.02f;

        public bool AttackInProgress { get { return attacking; } }
        public float CooldownRemaining { get { return cooldownLeft; } }
        /// <summary>Seconds from the start of an attack to its hit / shot.</summary>
        public float ReleaseTime { get { return fireDuration * releaseFrame; } }
        /// <summary>Seconds from the start of an attack to the weapon's own swing animation.</summary>
        public float WeaponSwingTime { get { return melee && weaponContactSeconds > 0f ? Math.Max(0f, ReleaseTime - weaponContactSeconds) : 0f; } }
        /// <summary>Where the current ranged attack is committed to (valid while AttackInProgress).</summary>
        public Vector3 CommittedAim { get { return aim; } }
        /// <summary>The current shot is a Cannon ground shot (aim is on the ground at the target's feet).</summary>
        public bool CommittedToGround { get { return attacking && groundTargeted; } }
        /// <summary>Hits and shots released so far (QA).</summary>
        public int Releases { get; private set; }
        public float AttackAge { get { return attacking ? age : 0f; } }
        public bool WeaponSwingStarted { get { return attacking && weaponSwung; } }

        void CreateWeapon()
        {
            if (!string.IsNullOrEmpty(equippedWeaponId))
            {
                equippedWeapon = MechPartCatalogue.Find(equippedWeaponId);
                invalidWeapon = equippedWeapon == null || !equippedWeapon.IsWeapon || equippedWeapon.Attack == MechAttackType.Special;
                if (invalidWeapon) Log.Warning("[Enemy] " + Entity.Name + ": invalid equipped weapon " + equippedWeaponId);
                else melee = equippedWeapon.Attack == MechAttackType.Melee;
            }
            weaponMuzzle = Entity.FindChild("Weapon Muzzle");
            telegraphEntity = Entity.FindChild("Telegraph");
            if (telegraph && (telegraphEntity == null || !telegraphEntity.HasComponent<MeshRenderer>()))
            {
                Log.Warning("[Enemy] " + Entity.Name + ": no child \"Telegraph\" with a MeshRenderer - no attack warning");
                telegraphEntity = null;
            }
            if (telegraphEntity != null) telegraphThickness = telegraphEntity.Transform.Scale.Y;
            splashEntity = Entity.FindChild("Telegraph Splash");
            if (splashEntity != null && !splashEntity.HasComponent<MeshRenderer>()) splashEntity = null;
            HideTelegraph();
        }

        // Every frame (Unity's EnemyAttack.Tick in LateUpdate).
        void UpdateWeapon(float deltaTime)
        {
            cooldownLeft = Math.Max(0f, cooldownLeft - deltaTime);

            if (attacking && previewSpinnerWindup)
                TickSpinner(deltaTime);
            else if (attacking)
            {
                // A lost or dead target cancels the hit that hasn't landed. A bolt already
                // flying keeps flying.
                if (attackTarget == null || attackTarget.IsDead || !attackTarget.Entity.IsActive)
                {
                    CancelAttack();
                }
                else
                {
                    age += deltaTime;
                    if (melee && !weaponSwung && age >= weaponSwingAt) SwingWeapon();
                    bool releasing = !released && age >= fireDuration * releaseFrame;
                    UpdateAttackSound(age, releasing);       // EnemyAudio.cs: starts early enough to peak on the hit
                    if (releasing)
                    {
                        released = true;
                        Releases++;
                        if (melee) StrikeMelee();
                        else FireBolt();
                        AnimateAttackRelease();
                    }
                    if (age >= fireDuration) CancelAttack();
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
            if (!IsReady || asleep || invalidWeapon || attacking || cooldownLeft > 0f) return false;
            if (MechHitStop.BlocksNewActions(Entity)) return false;   // Unity's EnemyAttack: none during a hit stop
            if (targetHealth == null || targetHealth == this || targetHealth.IsDead || !targetHealth.Entity.IsActive) return false;

            Entity victim = targetHealth.Entity;
            if (!CanReach(victim) || !ClearPathFrom(Position, victim, melee)) return false;
            if (previewSpinnerWindup && !PrepareSpinner(victim)) return false;
            if (!previewSpinnerWindup && !AdmitMeleeAttack()) return false;

            attackTarget = targetHealth;
            aim = AimPoint(victim);                  // ranged shots aim once; bolts never home
            groundTargeted = false;
            Vector3 impact;
            if (!melee && groundTargetSplash && TryGroundImpact(victim, out impact)) { aim = impact; groundTargeted = true; }
            if (!melee && aimSpreadDegrees > 0f) aim = Spread(aim);
            strikeFacing = Facing;                   // melee strikes where the swing began
            age = 0f;
            cooldownLeft = previewSpinnerWindup ? spinnerCooldown : cooldown;
            attacking = true;
            released = false;
            weaponSwung = false;
            weaponSwingAt = WeaponSwingTime;
            AnimateAttackStart();                    // EnemyAnimation.cs: the slash, or the cannon's charge
            ScheduleAttackSound(previewSpinnerWindup ? spinnerWindup : ReleaseTime);
            return true;
        }

        // Drops a hit that hasn't landed; the cooldown keeps running (Unity's CancelPendingAttack).
        public void CancelAttack()
        {
            if (attacking && !released) AnimateAttackCancel();
            if (SpinnerStage != SpinnerPhase.Idle) EndSpinner();
            ReleaseMeleeAttack();
            attacking = false;
            released = false;
            groundTargeted = false;
            attackTarget = null;
            HideTelegraph();
            StopAttackSound();                       // EnemyAudio.cs: a wind-up or charge sound still pending
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

            float amount = equippedWeapon != null ? equippedWeapon.Damage * weaponDamageMultiplier : damage;
            if (!attackTarget.ApplyDamage(amount)) return;
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
            Vector3 start = weaponMuzzle != null ? weaponMuzzle.Transform.WorldPosition
                : Position + Vector3.Up * 0.5f + strikeFacing * 1.5f;
            // A ground shot flies at the point one shell radius above the committed ground point, so
            // the shell's sphere touches the ground right there instead of grazing it short.
            Vector3 shotTarget = groundTargeted && equippedWeapon != null
                ? aim + Vector3.Up * Math.Max(0f, equippedWeapon.ProjectileSize) : aim;
            Vector3 direction = (shotTarget - start).Normalized;
            if (direction.Length < 0.5f) direction = strikeFacing;

            // LookRotation points the bolt's Transform.Forward along direction; Bolt flies that way.
            Entity shot = boltPrefab.Instantiate(start, Quaternion.LookRotation(direction));
            if (equippedWeapon != null)
                MechProjectile.Fire(shot, equippedWeapon, Entity, direction, weaponDamageMultiplier,
                    speedMultiplier: projectileSpeedMultiplier, enemyTarget: attackTarget,
                    groundTarget: groundTargeted ? shotTarget : (Vector3?)null);
        }

        // The Cannon's committed impact point (see the top of this file): the ground straight below
        // the target's centre, moved toward the shooter just far enough that the shell meets the
        // ground before the target's capsule, then the ground again at that spot (a slope or step).
        // With no ground in probe range, or cover between the muzzle and the impact, use the
        // ordinary committed body shot. Only a measured surface gets a ground telegraph.
        bool TryGroundImpact(Entity victim, out Vector3 impact)
        {
            Vector3 centre = AimPoint(victim);
            Vector3 down = new Vector3(0f, -1f, 0f);
            float depth = Math.Max(0.5f, groundProbeDepth);
            float capsuleRadius = 0.5f;
            if (victim.HasComponent<CharacterController>())
            {
                capsuleRadius = victim.CharacterController.Radius;
            }
            float shell = equippedWeapon != null ? Math.Max(0f, equippedWeapon.ProjectileSize) : 0.3f;
            float offset = groundImpactOffset >= 0f ? groundImpactOffset : capsuleRadius + shell + 0.15f;

            Vector3 toShooter = Flat(Position - centre);
            float apart = toShooter.Length;
            toShooter = apart > 0.01f ? toShooter * (1f / apart) : Facing * -1f;
            Vector3 probe = centre + toShooter * Math.Min(offset, Math.Max(0f, apart - 0.5f));
            RaycastHit ground;
            if (Physics.Raycast(probe, down, out ground, depth, MovementQueryMask, victim))
                impact = ground.Point + Vector3.Up * 0.05f;
            else if (Physics.Raycast(centre, down, out ground, depth, MovementQueryMask, victim))
                impact = ground.Point + Vector3.Up * 0.05f;
            else { impact = centre; return false; }

            Vector3 muzzle = weaponMuzzle != null ? weaponMuzzle.Transform.WorldPosition
                : Position + Vector3.Up * 0.5f + Facing * 1.5f;
            Vector3 line = impact - muzzle;
            float length = line.Length;
            RaycastHit wall;
            if (length > .01f && Physics.Raycast(muzzle, line * (1f / length), out wall, length - .01f, MovementQueryMask, Entity))
                return false;
            return true;
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
        // bolt, the committed lane for the Spinner. Unity drew its outline and faded it in;
        // here a flat box grows along the lane as the hit comes, which says the same thing
        // without transparency. A Cannon ground shot also shows its splash disc (the weapon's
        // splash radius) at the committed impact point. Each prefab's Telegraph uses one of the
        // team's existing glow materials so it reads in shade and sun alike: orange for melee
        // (Unity's warning colour), red for shots and splash, violet for the Spinner's charge.
        // Shown only while winding up; a completed, cancelled or dead attack hides it.
        void UpdateTelegraph()
        {
            if (telegraphEntity == null) return;
            bool show = telegraph && attacking && !released && !IsDead;
            MeshRenderer renderer = telegraphEntity.MeshRenderer;
            if (renderer.Visible != show) renderer.Visible = show;
            UpdateSplashTelegraph(show);
            if (!show) return;

            float groundY = GroundAtFeet() + .06f;
            Vector3 start = new Vector3(Position.X, groundY, Position.Z);
            // Melee: the real strike box (weaponReach deep, meleeWidth wide, from the golem out).
            Vector3 end = previewSpinnerWindup ? start + chargeDirection * (chargeSpeed * chargeDuration)
                : melee ? start + strikeFacing * weaponReach : new Vector3(aim.X, groundY, aim.Z);

            Vector3 lane = end - start;
            float length = lane.Length;
            Vector3 direction = length > 0.01f ? lane * (1f / length) : strikeFacing;
            float progress = Math.Max(0.1f, Math.Min(1f, age / Math.Max(0.01f, fireDuration * releaseFrame)));
            float filled = Math.Max(0.01f, length * (previewSpinnerWindup ? 1f : progress));

            telegraphEntity.Transform.WorldPosition = start + direction * (filled * 0.5f);
            telegraphEntity.Transform.WorldRotation = Quaternion.LookRotation(direction);
            // Scale is local and most golem roots are half scale: express the warning in world metres.
            Vector3 scale = Transform.Scale;
            float width = previewSpinnerWindup ? 2f * contactRadius : melee ? meleeWidth : 0.55f;
            telegraphEntity.Transform.Scale = new Vector3(width / Math.Max(.001f, Math.Abs(scale.X)),
                telegraphThickness, filled / Math.Max(.001f, Math.Abs(scale.Z)));
        }

        void HideTelegraph()
        {
            if (telegraphEntity != null && telegraphEntity.MeshRenderer.Visible) telegraphEntity.MeshRenderer.Visible = false;
            if (splashEntity != null && splashEntity.MeshRenderer.Visible) splashEntity.MeshRenderer.Visible = false;
        }

        // The splash disc (built-in Disc, radius 0.5, facing up) at the committed ground impact,
        // the weapon's splash radius in world metres.
        void UpdateSplashTelegraph(bool laneShown)
        {
            if (splashEntity == null) return;
            bool show = laneShown && groundTargeted && equippedWeapon != null && equippedWeapon.SplashRadius > 0f;
            if (splashEntity.MeshRenderer.Visible != show) splashEntity.MeshRenderer.Visible = show;
            if (!show) return;
            Vector3 scale = Transform.Scale;
            float diameter = equippedWeapon.SplashRadius * 2f;
            splashEntity.Transform.WorldPosition = aim + Vector3.Up * 0.04f;
            splashEntity.Transform.WorldRotation = Quaternion.Identity;
            splashEntity.Transform.Scale = new Vector3(diameter / Math.Max(.001f, Math.Abs(scale.X)), 1f,
                                                       diameter / Math.Max(.001f, Math.Abs(scale.Z)));
        }
    }
}
