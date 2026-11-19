/*************************************************************************
* \file:         MechHitStop.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          18 November 2026
* \version       1.0
* \brief         Brief per-actor hit stop. Port of Unity's CombatHitStop.
*
* \details
*    A hit stop freezes only the actors involved, for a few hundredths of a
*    second: the world clock, the camera, projectiles and everyone else keep
*    running. Requests come from MechPlayer (its confirmed hits, the damage it
*    takes). While an actor is stopped:
*
*      - every animation in its hierarchy holds its pose (Animator.Speed 0,
*        the old speeds are put back afterwards; done here);
*      - its own script asks IsFrozen / BlocksNewActions and holds its
*        movement and refuses new actions (MechPlayer; golems in Enemy).
*
*    Unity's rules: one stop at a time per actor, at most MaximumStop long,
*    and none again until RecoveryGap after it ends, so a burst of pellets
*    or splash victims never stacks or stretches the pause. The frame a stop
*    starts on still accepts actions (other weapons fired that same frame).
*
*    A static class, not a script: an entity holds only one script. Static
*    state outlives Play, so MechPlayer calls Reset when it is created.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    /// <summary>What kind of hit was confirmed (Unity's CombatHitKind): each has its own stop length.</summary>
    public enum MechHitKind { Melee, Projectile, Blast }

    public static class MechHitStop
    {
        // Unity's CombatHitStop defaults.
        public static float MaximumStop = 0.10f;   // seconds; longer requests are cut to this
        public static float RecoveryGap = 0.08f;   // seconds after a stop before the same actor can stop again

        sealed class Stop
        {
            public Entity Actor;
            public bool Holding;
            public float Until, NextAllowed;
            public int BeganFrame;
            public readonly List<Entity> Frozen = new List<Entity>();   // entities whose animation was held...
            public readonly List<float> Speeds = new List<float>();     // ...and the speed each had before
        }
        static readonly List<Stop> stops = new List<Stop>();

        /// <summary>
        /// Stops this actor for seconds (capped at MaximumStop). False if it is dead, already
        /// stopped, or still inside the recovery gap after its last stop.
        /// </summary>
        public static bool Request(Entity actor, float seconds)
        {
            if (ReferenceEquals(actor, null) || !actor.IsAlive || !actor.IsActive) return false;
            if (!(seconds > 0f) || float.IsInfinity(seconds)) return false;   // also catches NaN
            CombatHealth health = CombatHealth.Find(actor);
            if (health == null || health.IsDead) return false;   // a killing hit doesn't freeze the corpse

            float now = Time.ElapsedTime;
            Stop stop = Find(actor);
            if (stop == null) { stop = new Stop { Actor = actor }; stops.Add(stop); }
            if (stop.Holding && now >= stop.Until) Release(stop);       // its time was up but nobody ended it yet
            if (stop.Holding || now < stop.NextAllowed) return false;   // never stacked, never stretched

            stop.Holding = true;
            stop.BeganFrame = Time.FrameCount;
            stop.Until = now + Math.Min(seconds, MaximumStop);
            stop.NextAllowed = stop.Until + RecoveryGap;
            Hold(actor, stop);
            return true;
        }

        /// <summary>Is this actor stopped right now? Its movement, attack timers and AI should wait.</summary>
        public static bool IsFrozen(Entity actor)
        {
            Stop stop = Find(actor);
            return stop != null && stop.Holding && Time.ElapsedTime < stop.Until;
        }

        /// <summary>
        /// Frozen, and not on the frame the stop began: new attacks, abilities, swaps and jumps are
        /// refused. The first frame is let through so the other weapons fired with the same click
        /// still go off (Unity's BlocksNewActions).
        /// </summary>
        public static bool BlocksNewActions(Entity actor)
        {
            Stop stop = Find(actor);
            return IsFrozen(actor) && stop.BeganFrame != Time.FrameCount;
        }

        /// <summary>Ends the stops whose time is up (their animations play on). Once per frame.</summary>
        public static void Update()
        {
            float now = Time.ElapsedTime;
            for (int i = stops.Count - 1; i >= 0; i--)
            {
                Stop stop = stops[i];
                if (!stop.Actor.IsAlive) { stops.RemoveAt(i); continue; }   // destroyed: nothing to put back
                if (stop.Holding && now >= stop.Until) Release(stop);
            }
        }

        /// <summary>Forgets every stop (a new Play session: the clock starts again from 0).</summary>
        public static void Reset()
        {
            foreach (Stop stop in stops) Release(stop);
            stops.Clear();
        }

        static Stop Find(Entity actor)
        {
            if (ReferenceEquals(actor, null)) return null;
            foreach (Stop stop in stops) if (MechCombatTargets.Same(stop.Actor, actor)) return stop;
            return null;
        }

        // Unity's GetComponentsInChildren<Animator>: the actor and everything below it (the mech's legs
        // and weapon models, a golem's body, head and cannon). Entities without an animated model read
        // speed 1 and ignore the write, so they need no special case.
        static void Hold(Entity entity, Stop stop)
        {
            stop.Frozen.Add(entity);
            stop.Speeds.Add(entity.Animator.Speed);
            entity.Animator.Speed = 0f;
            int count = entity.Transform.ChildCount;
            for (int i = 0; i < count; i++)
            {
                Entity child = entity.Transform.GetChild(i);
                if (child != null) Hold(child, stop);
            }
        }

        // Puts every held animation back to its own speed. An entity destroyed meanwhile is skipped.
        static void Release(Stop stop)
        {
            for (int i = 0; i < stop.Frozen.Count; i++)
                if (stop.Frozen[i].IsAlive) stop.Frozen[i].Animator.Speed = stop.Speeds[i];
            stop.Frozen.Clear();
            stop.Speeds.Clear();
            stop.Holding = false;
        }
    }
}
