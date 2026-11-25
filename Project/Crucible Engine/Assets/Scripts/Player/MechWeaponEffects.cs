using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed partial class MechPlayer
    {
        readonly Random projectileSpread = new Random();
        readonly float[] shieldUntil = new float[SocketCount];
        readonly List<RockSweep> rockSweeps = new List<RockSweep>();
        MechWeapon punchWeapon, hookWeapon;
        int punchesLeft;
        float punchAge, punchDamageScale, hookRemaining;
        Vector3 punchDirection, hookPoint;

        sealed class RockSweep
        {
            public MechWeapon Weapon;
            public float Age, Damage;
            public readonly List<MechCombatTargets.Target> Hit = new List<MechCombatTargets.Target>();
        }

        bool IsStillEquipped(MechWeapon weapon)
        {
            return weapon != null && socketWeapons[(int)weapon.Socket] == weapon;
        }

        bool FireWeaponProjectiles(MechWeapon weapon, Prefab projectile, Vector3 muzzle, Vector3 direction,
                                   Vector3 aimPoint, int count)
        {
            MechPartDef def = weapon.Def;
            float damage = DamageMultiplier;
            bool salvo = def.Mechanic == MechWeaponMechanic.Katyusha;
            bool shotgun = def.Mechanic == MechWeaponMechanic.Shotgun;
            Quaternion frame = Quaternion.LookRotation(direction, Vector3.Up);
            var hookContact = def.Mechanic == MechWeaponMechanic.Hook
                ? (Action<Entity, Vector3>)((hit, point) => BeginHookPull(weapon, hit, point)) : null;
            Vector3? arcTarget = null;
            if (def.Mechanic == MechWeaponMechanic.Mortar)
            {
                Vector3 offset = aimPoint - muzzle;
                arcTarget = muzzle + MechMath.ClampLength(MechMath.Flat(offset), def.Range) + Vector3.Up * offset.Y;
            }

            bool spawned = false;
            for (int i = 0; i < count; i++)
            {
                Vector3 start = muzzle, heading = direction;
                if (salvo)
                {
                    int columns = (count + 1) / 2;
                    int rows = count > 1 ? 2 : 1;
                    var offset = new Vector3(i % columns - (columns - 1) * .5f, i / columns - (rows - 1) * .5f, 0);
                    start += frame * (offset * .3f);
                    float angle = count > 1 ? (i / (float)(count - 1) - .5f) * 8f : 0f;
                    heading = Quaternion.AngleAxis(angle, Vector3.Up) * direction;
                }
                else if (shotgun)
                {
                    double azimuth = projectileSpread.NextDouble() * 2 * Math.PI;
                    float radius = (float)(Math.Sqrt(projectileSpread.NextDouble()) * Math.Tan(8 * Math.PI / 180));
                    var offset = new Vector3((float)Math.Cos(azimuth) * radius, (float)Math.Sin(azimuth) * radius, -1);
                    heading = (frame * offset).Normalized;
                }

                Entity shot = projectile.Instantiate(start, Quaternion.LookRotation(heading, Vector3.Up));
                if (shot == null) continue;
                MechProjectile.Fire(shot, def, Entity, heading, shotgun ? damage / count : damage,
                    onContact: hookContact, arcTarget: arcTarget);
                spawned = true;
            }
            return spawned;
        }

        void BeginHookPull(MechWeapon weapon, Entity contact, Vector3 point)
        {
            if (contact == null || Health.IsDead || IsQuickBoosting || !IsStillEquipped(weapon)) return;
            hookWeapon = weapon;
            hookPoint = point;
            hookRemaining = 2f;
            planarVelocity = Vector3.Zero;
            NotifyCombatAction();
        }

        // SimpleMove exposes planar motion through the existing character controller.
        bool ApplyHookPull(bool boostPressed, float deltaTime)
        {
            if (hookRemaining <= 0f) return false;
            Vector3 offset = MechMath.Flat(hookPoint - Transform.WorldPosition);
            float distance = offset.Length;
            if (boostPressed || !IsStillEquipped(hookWeapon) || distance <= 2.03f)
            {
                hookRemaining = 0f;
                return false;
            }
            hookRemaining -= deltaTime;
            float speed = Math.Min(24f, (distance - 2f) / deltaTime);
            body.SimpleMove(offset * (speed / distance));
            Velocity = body.Velocity;
            State = IsGrounded ? MechMovementState.Grounded : MechMovementState.Airborne;
            return true;
        }

        bool BlocksWeaponDamage()
        {
            float now = Time.ElapsedTime;
            foreach (var weapon in Weapons)
                if (weapon != null && weapon.Def.Mechanic == MechWeaponMechanic.Shield
                    && now < shieldUntil[(int)weapon.Socket]) return true;
            return false;
        }

        void UseSpecialWeapon(MechWeapon weapon, MechCombatTargets.Target target, float now)
        {
            float damage = weapon.Def.Damage * DamageMultiplier;
            Vector3 origin = Transform.WorldPosition + Vector3.Up * .5f;
            switch (weapon.Def.Mechanic)
            {
                case MechWeaponMechanic.Shield:
                    shieldUntil[(int)weapon.Socket] = now + .5f;
                    break;
                case MechWeaponMechanic.RockRing:
                    rockSweeps.Add(new RockSweep { Weapon = weapon, Damage = damage });
                    break;
                case MechWeaponMechanic.Tentacles:
                    Vector3 aim = target.AimPosition - origin;
                    ResolveWeaponBox(origin, aim.Normalized, Math.Min(weapon.Def.Range, aim.Length + .5f), 1.25f,
                        damage, 0f, 0f, false);
                    break;
                case MechWeaponMechanic.Tesla:
                    var hit = new List<MechCombatTargets.Target>(5);
                    for (int i = 0; i < 5 && target != null; i++)
                    {
                        Vector3 point = target.AimPosition;
                        hit.Add(target);
                        HitWithWeapon(target, damage, origin, MechHitKind.Projectile);
                        target = FindWeaponTarget(point, 4f, null, hit);
                        origin = point;
                    }
                    break;
            }
        }

        void ResolveAuthoredMelee(MechWeapon weapon, Vector3 origin, Vector3 facing, float damageScale)
        {
            MechPartDef def = weapon.Def;
            ResolveWeaponBox(origin + Vector3.Up * .5f, facing, def.Range, def.MeleeWidth,
                def.Damage * damageScale, def.SplashRadius, def.SplashDamage * damageScale,
                def.Mechanic == MechWeaponMechanic.Scythe);
        }

        void ResolveWeaponBox(Vector3 origin, Vector3 forward, float reach, float width, float damage,
                              float splashRadius, float splashDamage, bool lifesteal)
        {
            if (forward.Length < .01f) forward = Transform.Forward;
            Vector3 right = Vector3.Cross(forward, Vector3.Up).Normalized;
            if (right.Length < .01f) right = Transform.Right;
            Vector3 up = Vector3.Cross(right, forward).Normalized;
            Vector3 tip = origin + forward * reach;
            var targets = new List<MechCombatTargets.Target>(MechCombatTargets.All);
            foreach (var target in targets)
            {
                if (!IsWeaponTarget(target)) continue;
                Vector3 offset = target.AimPosition - origin;
                float ahead = Vector3.Dot(offset, forward);
                bool inside = ahead >= 0 && ahead <= reach
                    && Math.Abs(Vector3.Dot(offset, right)) <= width * .5f && Math.Abs(Vector3.Dot(offset, up)) <= 1.5f;
                bool splash = splashRadius > 0f && (target.AimPosition - tip).Length <= splashRadius;
                if ((!inside && !splash) || !HasWeaponLine(origin, target.AimPosition)) continue;
                float actual = HitWithWeapon(target, inside ? damage : splashDamage, origin, MechHitKind.Melee, forward);
                if (lifesteal) Health.Heal(actual * .1f);
            }
        }

        void UpdateWeaponEffects(float deltaTime)
        {
            if (punchWeapon != null)
            {
                if (!IsStillEquipped(punchWeapon)) punchWeapon = null;
                else
                {
                    punchAge += deltaTime;
                    float spacing = Math.Min(.2f, punchWeapon.ShotInterval * .49f);
                    while (punchesLeft > 0 && punchAge >= spacing)
                    {
                        punchAge -= spacing;
                        punchesLeft--;
                        ResolveAuthoredMelee(punchWeapon, Transform.WorldPosition, punchDirection, punchDamageScale);
                    }
                    if (punchesLeft == 0) punchWeapon = null;
                }
            }

            for (int i = rockSweeps.Count - 1; i >= 0; i--)
            {
                RockSweep sweep = rockSweeps[i];
                if (!IsStillEquipped(sweep.Weapon)) { rockSweeps.RemoveAt(i); continue; }
                TickRockSweep(sweep, deltaTime);
                if (sweep.Age >= .8f) rockSweeps.RemoveAt(i);
            }
        }

        void TickRockSweep(RockSweep sweep, float deltaTime)
        {
            float start = sweep.Age * 450f;
            sweep.Age = Math.Min(.8f, sweep.Age + deltaTime);
            float end = sweep.Age * 450f;
            Vector3 origin = Transform.WorldPosition + Vector3.Up * .5f;
            float outer = sweep.Weapon.Def.Range, inner = outer - sweep.Weapon.Def.SplashRadius;
            var targets = new List<MechCombatTargets.Target>(MechCombatTargets.All);
            foreach (var target in targets)
            {
                if (!IsWeaponTarget(target) || sweep.Hit.Contains(target)) continue;
                Vector3 offset = target.AimPosition - origin;
                float radial = MechMath.Flat(offset).Length;
                if (radial < inner || radial > outer || Math.Abs(offset.Y) > 1.5f) continue;
                float angle = (float)(Math.Atan2(offset.X, offset.Z) * 180 / Math.PI + 360) % 360;
                if (angle < start || angle > end || !HasWeaponLine(origin, target.AimPosition)) continue;
                sweep.Hit.Add(target);
                HitWithWeapon(target, sweep.Damage, origin, MechHitKind.Melee, MechMath.Flat(offset).Normalized);
            }
        }

        void ClearWeaponEffects()
        {
            punchWeapon = null;
            hookRemaining = 0f;
            rockSweeps.Clear();
            Array.Clear(shieldUntil, 0, shieldUntil.Length);
        }

        MechCombatTargets.Target FindWeaponTarget(Vector3 origin, float range, MechCombatTargets.Target preferred,
                                                   List<MechCombatTargets.Target> excluded)
        {
            if (IsWeaponTarget(preferred) && MechCombatTargets.All.Contains(preferred)
                && (preferred.AimPosition - origin).Length <= range && HasWeaponLine(origin, preferred.AimPosition)) return preferred;
            MechCombatTargets.Target nearest = null;
            float best = range;
            foreach (var target in MechCombatTargets.All)
            {
                if (!IsWeaponTarget(target) || (excluded != null && excluded.Contains(target))) continue;
                float distance = (target.AimPosition - origin).Length;
                if (distance > best || !HasWeaponLine(origin, target.AimPosition)) continue;
                nearest = target;
                best = distance;
            }
            return nearest;
        }

        static bool IsWeaponTarget(MechCombatTargets.Target target)
        {
            return target != null && target.Entity != null && target.Entity.IsActive;
        }

        bool HasWeaponLine(Vector3 origin, Vector3 point)
        {
            Vector3 offset = point - origin;
            return offset.Length < .001f || !Physics.Raycast(origin, offset.Normalized, out _, offset.Length,
                LayerMask.GetMask(PhysicsLayer.NonMoving), Entity);
        }

        float HitWithWeapon(MechCombatTargets.Target target, float damage, Vector3 origin, MechHitKind kind,
                            Vector3 meleeDirection = default(Vector3))
        {
            if (damage <= 0f || !IsWeaponTarget(target) || !MechCombatTargets.All.Contains(target)) return 0f;
            CombatHealth health = CombatHealth.Find(target.Entity);
            ZooEnemy zooEnemy = health == null ? target.Entity.GetComponent<ZooEnemy>() : null;
            float before = health != null ? health.CurrentHealth : zooEnemy != null ? zooEnemy.Health : 0f;
            if (kind == MechHitKind.Melee)
            {
                if (!ApplyMeleeDamage(target, damage, origin, meleeDirection)) return 0f;
            }
            else target.OnDamage(damage, origin);
            ConfirmHit(target.Entity, kind);
            float after = health != null ? health.CurrentHealth : zooEnemy != null ? zooEnemy.Health : 0f;
            return Math.Max(0f, before - Math.Max(0f, after));
        }
    }
}
