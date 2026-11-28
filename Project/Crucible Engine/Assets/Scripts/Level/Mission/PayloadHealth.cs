/*************************************************************************
* \file:         PayloadHealth.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         The escort payload's health: a CombatHealth (so enemy hits
*                find it) that can also heal and be revived.
*
* \details
*    On the Escort Payload prefab's root. Damage code finds it like any
*    CombatHealth (CombatHealth.Find on the hit entity). CombatHealth
*    keeps its health private, so, like MechPlayer, this keeps its own
*    pool: ApplyDamage and IsDead are overridden; CombatHealth's Maximum
*    Health field is not used. EscortObjective sets the maximum (its
*    Inspector field), heals it, and on Died hides the payload and
*    revives it at the last checkpoint.
*
*    No teams in the engine yet: the player's direct shots damage it too
*    (Unity put it on the player's team).
*
* \history
*    -v1.0       new: own pool with Heal / Revive and a Died event
*
**************************************************************************/
using System;

namespace Crucible.@Scripts
{
    public sealed class PayloadHealth : CombatHealth
    {
        private float health = 1f, maximum = 1f;

        public float Health { get { return health; } }
        public float Maximum { get { return maximum; } }
        public override bool IsDead { get { return health <= 0f; } }

        /// <summary>Destroyed (health reached 0). EscortObjective listens.</summary>
        public event Action Died;

        public override void OnCreate()
        {
            base.OnCreate();
            maximum = Math.Max(1f, maximumHealth);
            health = maximum;
        }

        /// <summary>Full health at this maximum (spawn and respawn).</summary>
        public void Revive(float maximumHealth)
        {
            maximum = Math.Max(1f, maximumHealth);
            health = maximum;
        }

        /// <summary>Up to amount, never past the maximum; the destroyed stay destroyed until revived.</summary>
        public void Heal(float amount)
        {
            if (IsDead || !(amount > 0f)) return;
            health = Math.Min(maximum, health + amount);
        }

        public override bool ApplyDamage(float amount)
        {
            if (IsDead || !(amount > 0f) || float.IsInfinity(amount)) return false;
            health = Math.Max(0f, health - amount);
            if (health <= 0f && Died != null) Died();
            return true;
        }
    }
}
