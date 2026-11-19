/*************************************************************************
* \file:         MechProjectile.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \author:       Hugo Low Ren Hao (M3: no falloff, Damage Amp, hit stop)
* \par:          Email: low.h@digipen.edu
* \date          8 November 2026
* \version       1.2
* \brief         A straight shot from a ranged weapon. Port of Unity's
*                MechProjectile + RangedAttack: the direction is captured at
*                launch and never tracks a target; it flies at the part's
*                Projectile Speed, stops on the first solid thing, and does
*                full damage; at the weapon's Range it disappears.
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
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class MechProjectile : EntityScript
    {
        public float spinDegreesPerSecond = 0f;   // Unity's angularVelocity.x on the visual (the axe: 720)
        public float targetRadius = 0.9f;         // how close to a target's centre counts as a hit

        MechWeapon weapon;
        Entity owner;
        Vector3 direction;
        float speed, radius, remaining, travelled;
        float damageScale = 1f;   // the shooter's Damage Amp at the moment it fired
        bool live;
        Transform visual;

        // A spawned prefab's script is created later in the frame ("OnCreate deferred"), so right after
        // Prefab.Instantiate GetComponent<MechProjectile>() is still null. Fire() queues the launch here
        // and the projectile picks it up in its own OnCreate.
        sealed class PendingLaunch { public MechWeapon Weapon; public Entity Owner; public Vector3 Direction; public float DamageScale; }
        static readonly Dictionary<ulong, PendingLaunch> pending = new Dictionary<ulong, PendingLaunch>();
        static ulong Key(Entity e) { return ((ulong)e.Generation << 32) | e.Index; }

        /// <summary>
        /// Launches the spawned shot now if its script exists, else as soon as it is created.
        /// damageScale multiplies its damage and splash (Damage Amp; 1 = normal).
        /// </summary>
        public static void Fire(Entity shot, MechWeapon source, Entity shooter, Vector3 shotDirection, float damageScale)
        {
            if (shot == null) return;
            MechProjectile ready = shot.GetComponent<MechProjectile>();
            if (ready != null) { ready.Launch(source, shooter, shotDirection, damageScale); return; }
            pending[Key(shot)] = new PendingLaunch { Weapon = source, Owner = shooter, Direction = shotDirection, DamageScale = damageScale };
        }

        public override void OnCreate()
        {
            PendingLaunch launch;
            if (!pending.TryGetValue(Key(Entity), out launch)) return;
            pending.Remove(Key(Entity));
            Launch(launch.Weapon, launch.Owner, launch.Direction, launch.DamageScale);
        }

        public override void OnDestroy()
        {
            pending.Remove(Key(Entity));
        }

        /// <summary>Starts the flight. The weapon supplies damage, range, speed and splash.</summary>
        public void Launch(MechWeapon source, Entity shooter, Vector3 shotDirection, float shotDamageScale)
        {
            weapon = source;
            owner = shooter;
            damageScale = Math.Max(0f, shotDamageScale);
            direction = shotDirection.Normalized;
            speed = Math.Max(0.01f, source.Def.ProjectileSpeed);
            radius = Math.Max(0.01f, source.Def.ProjectileSize);
            remaining = source.Def.Range;
            travelled = 0f;
            live = MechMath.SqrLength(direction) > 0f;
            if (live) Transform.WorldRotation = Quaternion.LookRotation(direction, Vector3.Up);
            Entity v = Entity.FindChild("Visual");
            visual = v != null ? v.Transform : null;
            if (!live) Destroy(Entity);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!live || deltaTime <= 0f) return;
            if (visual != null && spinDegreesPerSecond != 0f)
                visual.Rotation = visual.Rotation * Quaternion.AngleAxis(spinDegreesPerSecond * deltaTime, new Vector3(1f, 0f, 0f));

            float step = Math.Min(speed * deltaTime, remaining);
            Vector3 from = Transform.WorldPosition;

            // A target the shot passes within reach of this step.
            MechCombatTargets.Target target;
            float along;
            if (TargetOnSegment(from, step, out target, out along))
            {
                Transform.WorldPosition = from + direction * along;
                travelled += along;
                Impact(target.Entity, Transform.WorldPosition);
                return;
            }
            // Anything solid (the ground, walls, enemies' colliders).
            RaycastHit hit;
            if (Physics.Raycast(from, direction, out hit, step + radius, owner))
            {
                float distance = Math.Max(0f, hit.Distance - radius);
                Transform.WorldPosition = from + direction * distance;
                travelled += distance;
                Impact(hit.Entity, hit.Point);
                return;
            }
            Transform.WorldPosition = from + direction * step;
            travelled += step;
            remaining -= step;
            if (remaining <= 0f) Finish();   // out of range: gone, no detonation (Unity's rule)
        }

        bool TargetOnSegment(Vector3 from, float length, out MechCombatTargets.Target found, out float along)
        {
            found = null; along = 0f;
            float best = float.MaxValue;
            foreach (var t in MechCombatTargets.All)
            {
                if (!t.Entity.IsAlive || MechCombatTargets.Same(t.Entity, owner)) continue;
                Vector3 centre = t.AimPosition;   // roots sit at the feet
                Vector3 offset = centre - from;
                float s = MechMath.Clamp(Vector3.Dot(offset, direction), 0f, length);
                Vector3 closest = from + direction * s;
                if ((centre - closest).Length > targetRadius + radius || s >= best) continue;
                best = s; found = t; along = s;
            }
            return found != null;
        }

        void Impact(Entity hitEntity, Vector3 point)
        {
            if (!live) return;
            live = false;
            Vector3 from = owner != null && owner.IsAlive ? owner.Transform.WorldPosition : point - direction;
            // The shooter hears about every hit that lands (hit stop: Unity's OnConfirmedHit).
            MechPlayer shooter = owner != null && owner.IsAlive ? owner.GetComponent<MechPlayer>() : null;
            // Full damage wherever it lands: the flight already ends at Range (Unity: impactDamage = damage).
            float damage = weapon.Def.Damage * damageScale;
            if (damage > 0f && MechCombatTargets.TryDamage(hitEntity, damage, from) && shooter != null)
                shooter.ConfirmHit(MechCombatTargets.Owning(hitEntity)?.Entity, MechHitKind.Projectile);

            // Splash: everything inside the radius, the direct target included, takes the splash damage.
            if (weapon.Def.Attack == MechAttackType.Splash && weapon.Def.SplashRadius > 0f)
            {
                var targets = new List<MechCombatTargets.Target>(MechCombatTargets.All);
                foreach (var t in targets)
                {
                    if (!t.Entity.IsAlive || MechCombatTargets.Same(t.Entity, owner)) continue;
                    Vector3 centre = t.AimPosition;
                    if ((centre - point).Length <= weapon.Def.SplashRadius + targetRadius)
                    {
                        t.OnDamage(weapon.Def.SplashDamage * damageScale, from);
                        if (shooter != null) shooter.ConfirmHit(t.Entity, MechHitKind.Blast);
                    }
                }
            }
            Finish();   // hit markers / impact VFX would hook in here (Unity: CombatVisualEvents)
        }

        void Finish()
        {
            live = false;
            Destroy(Entity);
        }
    }
}
