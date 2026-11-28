/*************************************************************************
* \file:         MechTrailingBar.cs
* \brief         The delayed-damage ("lost health") bar behind the player's
*                health bar. Port of the white-bar logic in Unity's
*                MechHealthHud (Refresh / TickLostHealth).
*
* \details
*    Presentation only: it is told the health values and keeps two fill
*    ratios. It never changes health.
*
*      - Damage: the front (real health) drops at once; the trailing ratio
*        stays where it was.
*      - After Delay seconds with no further damage it drains to the front
*        over DrainDuration seconds. More damage meanwhile restarts the
*        delay, and the drain continues from wherever it had got to.
*      - Healing snaps the trailing ratio to the new front: there is no lost
*        health left to show (Unity).
*      - A new maximum (equipping a part), death and the first reading
*        reset it to the front, so nothing stale is drawn after a respawn.
*
*    Unity's Player Health HUD prefab: delay 1 s, drain 0.65 s.
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    public sealed class MechTrailingBar
    {
        public float Delay = 1f;
        public float DrainDuration = 0.65f;

        /// <summary>Real health, 0..1.</summary>
        public float Front { get; private set; }
        /// <summary>The trailing (lost-health) bar, 0..1; never below Front.</summary>
        public float Trailing { get; private set; }

        float lastDamageTime = float.NegativeInfinity;
        float drainFrom, drainAge, boundMaximum;
        bool snapshot;

        public MechTrailingBar() { Front = Trailing = 1f; }

        /// <summary>Reads health this frame (current, maximum, dead) at time now.</summary>
        public void Observe(float current, float maximum, bool dead, float now)
        {
            float ratio = MechMath.Clamp01(current / Math.Max(1f, maximum));
            bool reset = !snapshot || Math.Abs(boundMaximum - maximum) > 1e-4f || dead;
            if (reset) Trailing = ratio;
            else if (ratio < Front - 1e-6f) { lastDamageTime = now; drainFrom = Trailing; drainAge = 0f; }
            else if (ratio > Front + 1e-6f) Trailing = ratio;
            boundMaximum = maximum;
            snapshot = true;
            Front = ratio;
            if (Trailing < Front) Trailing = Front;
        }

        /// <summary>Advances the drain. Call once per frame after Observe.</summary>
        public void Tick(float now, float deltaTime)
        {
            if (Trailing <= Front) return;
            if (now - lastDamageTime < Math.Max(0f, Delay)) return;
            drainAge += Math.Max(0f, deltaTime);
            Trailing = MechMath.Lerp(drainFrom, Front, MechMath.Clamp01(drainAge / Math.Max(0.01f, DrainDuration)));
            if (Trailing < Front) Trailing = Front;
        }

        /// <summary>Seconds until the drain starts (0 once it is draining or empty).</summary>
        public float DelayRemaining(float now)
        {
            return Trailing > Front ? Math.Max(0f, Delay - (now - lastDamageTime)) : 0f;
        }
    }
}
