/*************************************************************************
* \file:         MechProjectile.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \author:       Hugo Low Ren Hao (M3: no falloff, Damage Amp, hit stop)
* \par:          Email: low.h@digipen.edu
* \date          8 November 2026
* \version       1.2
* \brief         Shared projectile flight for player and enemy weapons.
*                Weapon definitions supply damage, range and flight data.
*                Each launch captures its actor's modifiers and target.
*
* \details
*    Prefab setup (Assets/Prefabs/Player/Projectile *.cprefab, written by
*    Tools/SceneGen/SceneGen.Player.ps1):
*
*        Projectile Bolt   Script: MechProjectile   (no collider of its own)
*        '- Visual         MeshRenderer: the model (CrossbowBolt / ThrowingAxe /
*                          a sphere for the cannon); spun by spinDegreesPerSecond
*
*    MechPlayer spawns it at the weapon's muzzle and calls Launch. Each frame
*    it raycasts the step ahead (raycasts ignore triggers, so pickup zones do
*    not stop it) and also checks the damageable targets within its radius
*    (MechCombatTargets), since a thin shot can pass a capsule between rays.
*    On impact: the direct target takes the weapon's damage, and with Splash
*    every target inside the splash radius takes the splash damage too (the
*    direct one takes both - Unity's CombatBlast).
*
*    groundTarget (enemy launches only, the Heavy Cannon): the shell flies no
*    farther than that committed point and bursts there if nothing stopped it
*    first, so a missing ground hit still detonates where the warning showed.
*    Its direct hit on the target is tested against the target's actual
*    capsule (not the 0.9 m aim sphere), so a shell landing at the feet is a
*    ground hit (splash only) unless the target steps into its path.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class MechProjectile : EntityScript
    {
        public float spinDegreesPerSecond = 0f;   // Unity's angularVelocity.x on the visual (the axe: 720)
        public float targetRadius = 0.9f;         // how close to a target's centre counts as a hit

        MechPartDef definition;
        Entity owner;
        CombatHealth enemyTarget;
        Action<Entity, Vector3> onContact;
        Vector3 direction, arcVelocity;
        float speed, radius, remaining, arcTime, damageScale;
        bool groundShot;
        const float ArcGravity = 25f;
        List<ulong> pierced;
        RaycastHit[] piercingHits;
        bool live;
        Transform visual;

        // Prefab scripts may run OnCreate after the firing script returns.
        sealed class PendingLaunch
        {
            public MechPartDef Definition;
            public Entity Owner;
            public Vector3 Direction;
            public float DamageScale, SpeedMultiplier;
            public CombatHealth EnemyTarget;
            public Action<Entity, Vector3> OnContact;
            public Vector3? ArcTarget;
            public Vector3? GroundTarget;
        }

        static readonly Dictionary<ulong, PendingLaunch> pending = new Dictionary<ulong, PendingLaunch>();
        static ulong Key(Entity e) { return ((ulong)e.Generation << 32) | e.Index; }

        public static void Fire(Entity shot, MechWeapon source, Entity shooter, Vector3 shotDirection, float damageScale)
        {
            Fire(shot, source.Def, shooter, shotDirection, damageScale);
        }

        public static void Fire(Entity shot, MechPartDef definition, Entity shooter, Vector3 shotDirection, float damageScale,
                                float speedMultiplier = 1f, CombatHealth enemyTarget = null,
                                Action<Entity, Vector3> onContact = null, Vector3? arcTarget = null,
                                Vector3? groundTarget = null)
        {
            if (shot == null) return;
            var launch = new PendingLaunch
            {
                Definition = definition, Owner = shooter, Direction = shotDirection, DamageScale = damageScale,
                SpeedMultiplier = speedMultiplier, EnemyTarget = enemyTarget, OnContact = onContact, ArcTarget = arcTarget,
                GroundTarget = enemyTarget != null ? groundTarget : null
            };
            MechProjectile ready = shot.GetComponent<MechProjectile>();
            if (ready != null) ready.Launch(launch);
            else pending[Key(shot)] = launch;
        }

        public override void OnCreate()
        {
            if (!pending.TryGetValue(Key(Entity), out PendingLaunch launch)) return;
            pending.Remove(Key(Entity));
            Launch(launch);
        }

        public override void OnDestroy()
        {
            pending.Remove(Key(Entity));
        }

        public void Launch(MechWeapon source, Entity shooter, Vector3 shotDirection, float shotDamageScale)
        {
            Launch(new PendingLaunch { Definition = source.Def, Owner = shooter, Direction = shotDirection,
                DamageScale = shotDamageScale, SpeedMultiplier = 1f });
        }

        void Launch(PendingLaunch launch)
        {
            definition = launch.Definition;
            owner = launch.Owner;
            enemyTarget = launch.EnemyTarget;
            onContact = launch.OnContact;
            damageScale = Math.Max(0f, launch.DamageScale);
            direction = launch.Direction.Normalized;
            speed = Math.Max(0.01f, definition.ProjectileSpeed * launch.SpeedMultiplier);
            radius = Math.Max(0.01f, definition.ProjectileSize);
            remaining = definition.Range;
            groundShot = launch.GroundTarget.HasValue;
            if (groundShot) remaining = Math.Min(remaining, (launch.GroundTarget.Value - Transform.WorldPosition).Length + 0.05f);
            arcTime = 0f;
            pierced = definition.Piercing ? new List<ulong>() : null;
            piercingHits = definition.Piercing ? new RaycastHit[64] : null;
            if (launch.ArcTarget.HasValue)
            {
                Vector3 offset = launch.ArcTarget.Value - Transform.WorldPosition;
                arcTime = Math.Max(0.55f, MechMath.Flat(offset).Length / speed);
                arcVelocity = offset * (1f / arcTime) + Vector3.Up * (0.5f * ArcGravity * arcTime);
            }
            live = MechMath.SqrLength(direction) > 0f;
            if (live) Transform.WorldRotation = Quaternion.LookRotation(direction, Vector3.Up);
            Entity child = Entity.FindChild("Visual");
            visual = child != null ? child.Transform : null;
            if (!live) Destroy(Entity);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!live || deltaTime <= 0f) return;
            if (visual != null && spinDegreesPerSecond != 0f)
                visual.Rotation = visual.Rotation * Quaternion.AngleAxis(spinDegreesPerSecond * deltaTime, new Vector3(1f, 0f, 0f));

            if (arcTime > 0f)
            {
                float time = Math.Min(deltaTime, arcTime);
                Vector3 displacement = arcVelocity * time - Vector3.Up * (0.5f * ArcGravity * time * time);
                arcVelocity -= Vector3.Up * (ArcGravity * time);
                arcTime -= time;
                float length = displacement.Length;
                if (length > 0.0001f)
                {
                    direction = displacement * (1f / length);
                    Transform.WorldRotation = Quaternion.LookRotation(direction, Vector3.Up);
                    Advance(length);
                }
                if (live && arcTime <= 0f)
                {
                    Impact(null, Transform.WorldPosition);
                    Finish();
                }
                return;
            }

            float step = Math.Min(speed * deltaTime, remaining);
            Advance(step);
            remaining -= step;
            if (live && remaining <= 0f)
            {
                // A ground shot that reached its committed point without touching anything (no
                // ground there): it bursts where the warning was shown.
                if (groundShot) Impact(null, Transform.WorldPosition);
                Finish();
            }
        }

        void Advance(float step)
        {
            if (!(step > 0f)) return;
            Vector3 from = Transform.WorldPosition;
            // Resolve the nearest contact first so aim volumes do not hit through geometry.
            while (FindContact(from, step, out Entity hit, out Vector3 point))
            {
                Entity target = Damageable(hit);
                bool passThrough = pierced != null && target != null && !WasPierced(target);
                if (passThrough) pierced.Add(Key(target));
                Impact(hit, point);
                if (!passThrough)
                {
                    Transform.WorldPosition = point;
                    Finish();
                    return;
                }
            }
            Transform.WorldPosition = from + direction * step;
        }

        bool FindContact(Vector3 from, float length, out Entity found, out Vector3 point)
        {
            found = null;
            point = from;
            float nearest = length;
            if (piercingHits != null)
            {
                int count = Physics.RaycastNonAlloc(from, direction, piercingHits, length, LayerMask.All, owner);
                for (int i = 0; i < count; i++)
                {
                    RaycastHit hit = piercingHits[i];
                    if (WasPierced(hit.Entity)) continue;
                    found = hit.Entity;
                    point = hit.Point;
                    nearest = hit.Distance;
                    break;
                }
            }
            else if (Physics.SphereCast(from, radius, direction, out RaycastHit contact, length, LayerMask.All, owner))
            {
                found = contact.Entity;
                point = contact.Point;
                nearest = contact.Distance;
            }

            if (enemyTarget != null)
            {
                Entity victim = enemyTarget.Entity;
                float along = 0f;
                if (!enemyTarget.IsDead && victim != null && victim.IsActive && !WasPierced(victim)
                    && (groundShot ? NearCapsule(from, victim, nearest, out along)
                                   : NearSegment(from, EnemyAimCentre(victim), nearest, out along)))
                {
                    found = victim;
                    point = from + direction * along;
                }
            }
            else
            {
                foreach (var target in MechCombatTargets.All)
                {
                    if (!target.Entity.IsAlive || !target.Entity.IsActive || MechCombatTargets.Same(target.Entity, owner)
                        || WasPierced(target.Entity) || !NearSegment(from, target.AimPosition, nearest, out float along)) continue;
                    found = target.Entity;
                    point = from + direction * along;
                    nearest = along;
                }
            }
            return found != null;
        }

        static Vector3 EnemyAimCentre(Entity victim)
        {
            Transform transform = victim.Transform;
            return victim.HasComponent<CharacterController>()
                ? transform.WorldPosition + transform.WorldRotation * victim.CharacterController.Center
                : transform.WorldPosition;
        }

        // The step against the victim's upright capsule (CharacterController), sampled every 5 cm:
        // the first point within its radius plus the shell's. Without a controller: the aim sphere.
        bool NearCapsule(Vector3 from, Entity victim, float limit, out float along)
        {
            along = 0f;
            if (victim == null || !victim.HasComponent<CharacterController>())
                return NearSegment(from, EnemyAimCentre(victim), limit, out along);
            CharacterController capsule = victim.CharacterController;
            Vector3 centre = EnemyAimCentre(victim);
            float half = Math.Max(0f, capsule.Height * 0.5f - capsule.Radius);
            float reach = capsule.Radius + radius;
            int samples = Math.Max(1, (int)Math.Ceiling(limit / 0.05f));
            for (int i = 0; i <= samples; i++)
            {
                float t = limit * i / samples;
                Vector3 p = from + direction * t;
                float y = MechMath.Clamp(p.Y - centre.Y, -half, half);
                Vector3 axis = new Vector3(centre.X, centre.Y + y, centre.Z);
                if ((p - axis).Length > reach) continue;
                along = t;
                return true;
            }
            return false;
        }

        bool NearSegment(Vector3 from, Vector3 centre, float limit, out float along)
        {
            Vector3 offset = centre - from;
            along = Vector3.Dot(offset, direction);
            return along >= 0f && along < limit && (offset - direction * along).Length <= targetRadius + radius;
        }

        bool WasPierced(Entity entity)
        {
            if (pierced == null) return false;
            for (Entity part = entity; part != null; part = part.Transform.Parent)
                if (pierced.Contains(Key(part))) return true;
            return false;
        }

        Entity Damageable(Entity entity)
        {
            if (enemyTarget == null) return MechCombatTargets.Owning(entity)?.Entity;
            CombatHealth health = CombatHealth.Find(entity);
            return health != null && !(health is Enemy) && !health.IsDead ? health.Entity : null;
        }

        void Impact(Entity hitEntity, Vector3 point)
        {
            float damage = definition.Damage * damageScale;
            bool splash = definition.Attack == MechAttackType.Splash && definition.SplashRadius > 0f;
            if (enemyTarget != null)
            {
                Entity direct = Damageable(hitEntity);
                if (direct != null) CombatHealth.Find(direct).ApplyDamage(damage);
                Entity victim = enemyTarget.Entity;
                if (splash && !enemyTarget.IsDead && victim != null && victim.IsActive
                    && (EnemyAimCentre(victim) - point).Length <= definition.SplashRadius + targetRadius)
                    enemyTarget.ApplyDamage(definition.SplashDamage * damageScale);
                return;
            }

            Vector3 from = owner != null && owner.IsAlive ? owner.Transform.WorldPosition : point - direction;
            MechPlayer shooter = owner != null && owner.IsAlive ? owner.GetComponent<MechPlayer>() : null;
            var directTarget = MechCombatTargets.Owning(hitEntity);
            int kills = 0;   // for the explosion shake: each target this shot killed, once
            if (damage > 0f && directTarget != null)
            {
                bool wasDead = IsDead(directTarget.Entity);
                directTarget.OnDamage(damage, from);
                if (!wasDead && IsDead(directTarget.Entity)) kills++;
                shooter?.ConfirmHit(directTarget.Entity, MechHitKind.Projectile);
            }

            // Splash is additive for the direct target, as in CombatBlast.
            if (splash)
            {
                var targets = new List<MechCombatTargets.Target>(MechCombatTargets.All);
                foreach (var target in targets)
                {
                    if (!target.Entity.IsAlive || !target.Entity.IsActive || MechCombatTargets.Same(target.Entity, owner)) continue;
                    if ((target.AimPosition - point).Length > definition.SplashRadius + targetRadius) continue;
                    bool wasDead = IsDead(target.Entity);
                    target.OnDamage(definition.SplashDamage * damageScale, from);
                    if (!wasDead && IsDead(target.Entity)) kills++;
                    shooter?.ConfirmHit(target.Entity, MechHitKind.Blast);
                }
                shooter?.ConfirmExplosion(point, kills);
            }
            onContact?.Invoke(hitEntity, point);
        }

        static bool IsDead(Entity entity)
        {
            CombatHealth health = entity != null && entity.IsAlive ? CombatHealth.Find(entity) : null;
            return health != null && health.IsDead;
        }

        void Finish()
        {
            live = false;
            Destroy(Entity);
        }
    }
}
