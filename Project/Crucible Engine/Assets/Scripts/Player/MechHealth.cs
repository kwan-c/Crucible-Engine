/*************************************************************************
* \file:         MechHealth.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \author:       Hugo Low Ren Hao (M3: Heal, for the Heal ability)
* \par:          Email: low.h@digipen.edu
* \date          7 November 2026
* \version       1.1
* \brief         The mech's single health pool. Port of Unity's CombatHealth
*                (Scripts/Damage/CombatHealth.cs).
*
* \details
*    A plain C# object owned by MechPlayer (the engine allows one script per
*    entity, so the Unity components become parts of one script). Same rules
*    as Unity: damage is clamped to what is left, death happens once, and
*    SetMaximum without refill never restores lost health.
*
*    Unlike Unity, death does not deactivate the entity: in the showcase the
*    mech just stops and the HUD hides its health meter, so it can be revived.
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    public sealed class MechHealth
    {
        public float Maximum { get; private set; }
        public float Current { get; private set; }
        public bool IsDead { get { return Current <= 0f; } }
        public float Normalized { get { return MechMath.Clamp01(Current / Math.Max(1f, Maximum)); } }
        internal Func<bool> BlocksDamage { get; set; }

        /// <summary>Health actually lost (after clamping).</summary>
        public event Action<float> Damaged;
        public event Action Died;
        /// <summary>Damage, capacity changes and refills.</summary>
        public event Action Changed;

        public MechHealth(float maximum)
        {
            Maximum = Math.Max(1f, maximum);
            Current = Maximum;
        }

        /// <summary>Without refill, lost health is not restored and a lower maximum cuts current health to fit.</summary>
        public void SetMaximum(float maximum, bool refill)
        {
            if (!MechMath.Finite(maximum)) return;
            Maximum = Math.Max(1f, maximum);
            Current = refill ? Maximum : Math.Min(Current, Maximum);
            if (Changed != null) Changed();
        }

        public bool ApplyDamage(float amount)
        {
            if (IsDead || amount <= 0f || !MechMath.Finite(amount) || (BlocksDamage != null && BlocksDamage())) return false;
            float applied = Math.Min(Current, amount);
            Current -= applied;
            if (Damaged != null) Damaged(applied);
            if (Changed != null) Changed();
            if (IsDead && Died != null) Died();
            return true;
        }

        /// <summary>
        /// Restores up to amount, never past the maximum; the dead stay dead (Unity: CombatHealth.Heal).
        /// Returns what was actually restored.
        /// </summary>
        public float Heal(float amount)
        {
            if (IsDead || amount <= 0f || !MechMath.Finite(amount)) return 0f;
            float restored = Math.Min(Maximum - Current, amount);
            if (restored <= 0f) return 0f;   // already full: no Changed event every frame
            Current += restored;
            if (Changed != null) Changed();
            return restored;
        }

        /// <summary>Back to full (also revives). Showcase/debug use.</summary>
        public void Refill()
        {
            Current = Maximum;
            if (Changed != null) Changed();
        }
    }
}
