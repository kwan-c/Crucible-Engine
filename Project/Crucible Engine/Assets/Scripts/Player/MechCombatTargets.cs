/*************************************************************************
* \file:         MechCombatTargets.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.0
* \brief         Everything the player's weapons can damage. Stands in for
*                Unity's GetComponentInParent<CombatHealth>() on a hit.
*
* \details
*    GetComponent<T>() only accepts concrete component/script types, so a
*    weapon cannot ask a hit entity for "anything damageable". Damageable
*    scripts register here instead (OnCreate) and unregister (OnDestroy):
*
*        MechCombatTargets.Register(Entity, (amount, from) => TakeDamage(amount));
*        MechCombatTargets.Unregister(Entity);
*
*    The weapon then asks TryDamage(hitEntity, ...) for a raycast hit, or
*    walks All for splash / melee. Static state resets with each script
*    reload, like every static field.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public static class MechCombatTargets
    {
        public sealed class Target
        {
            public Entity Entity;
            public Action<float, Vector3> OnDamage;   // amount, attacker position
            public float AimHeight = 1f;              // Unity's MechTargetable aim point, above the root
            /// <summary>Where weapons and the lock-on aim (MechTargetable.AimPosition).</summary>
            public Vector3 AimPosition { get { return Entity.Transform.WorldPosition + Vector3.Up * AimHeight; } }
        }

        static readonly List<Target> targets = new List<Target>();

        public static IList<Target> All { get { return targets; } }

        public static void Register(Entity entity, Action<float, Vector3> onDamage, float aimHeight = 1f)
        {
            Unregister(entity);
            targets.Add(new Target { Entity = entity, OnDamage = onDamage, AimHeight = aimHeight });
        }

        /// <summary>The target this entity (or one of its parents) belongs to, or null.</summary>
        public static Target Owning(Entity hit)
        {
            for (Entity e = hit; e != null; e = e.Transform.Parent)
            {
                Target target = Find(e);
                if (target != null) return target;
            }
            return null;
        }

        public static void Unregister(Entity entity)
        {
            for (int i = targets.Count - 1; i >= 0; i--)
                if (Same(targets[i].Entity, entity)) targets.RemoveAt(i);
        }

        /// <summary>Damages the target this entity (or one of its parents) belongs to. False if it is not one.</summary>
        public static bool TryDamage(Entity hit, float amount, Vector3 from)
        {
            for (Entity e = hit; e != null; e = e.Transform.Parent)
            {
                Target target = Find(e);
                if (target == null) continue;
                if (amount > 0f && target.OnDamage != null) target.OnDamage(amount, from);
                return true;
            }
            return false;
        }

        static Target Find(Entity e)
        {
            foreach (var target in targets) if (Same(target.Entity, e)) return target;
            return null;
        }

        public static bool Same(Entity a, Entity b)
        {
            return !ReferenceEquals(a, null) && !ReferenceEquals(b, null) && a.Index == b.Index && a.Generation == b.Generation;
        }
    }
}
