/*************************************************************************
* \file:         WaveTargeting.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Sends a share of a wave's enemies after tagged objects
*                instead of the players (the escort payload, later anything
*                to defend). All in the Inspector; no mission code.
*
* \details
*    Put it on any entity in the scene (e.g. the Sub Level Generator); one
*    per rule. For every enemy the Spawn Director spawns from `wave` (empty
*    = every wave, roaming included), a `share` chance (0.3 = the GDD's
*    30%) makes it one of this rule's enemies. Every recheckSeconds:
*      targets  = the entities tagged targetTag that are alive and
*                 switched on (FindAllWithTag);
*      its enemies (alive, not dead) get a target override
*                 (StateGraphAgent.SetTargetOverride): the nearest target
*                 (nearest on) or one picked at random and kept; with no
*                 target they go back to the nearest player;
*      goals    = with addNavGoal, every target is also a goal of the
*                 enemies' flow field (Navigation.AddGoal), so they find
*                 their way round walls to it.
*    So a target switched off (the escort payload destroyed) or untagged
*    (it arrived) is dropped at once, and taken again when it is back.
*    Example (Escort.scene): wave "Wave Escort", targetTag "Payload",
*    share 0.3. The target needs health others can hit (a CombatHealth,
*    e.g. PayloadHealth) for attacks to do anything.
*
* \history
*    -v1.0       new: wave + tag + share rule; target overrides and flow
*                field goals
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class WaveTargeting : EntityScript
    {
        public string wave = "Wave Escort";     // the Spawn Director child whose enemies this applies to; empty = every wave
        public string targetTag = "Payload";    // what they go for (Assets/Config/Tags.json)
        public float share = 0.3f;              // 0..1 of that wave's enemies (GDD: 30%)
        public bool nearest = true;             // each goes for the nearest tagged target; off = a random one, kept
        public bool addNavGoal = true;          // the targets are flow-field goals too (enemies path round walls to them)
        public float recheckSeconds = 0.25f;    // how often targets are looked up again
        public bool logAssignments = true;      // a console line when the rule picks an enemy, and when targets change

        // One enemy this rule picked: what it's pointed at now.
        private sealed class Member
        {
            public Entity Enemy;
            public StateGraphAgent Brain;
            public Entity Target;               // the override it has now (null = the nearest player)
            public Entity Picked;               // nearest off: the random pick it keeps while it exists
        }

        private readonly List<Member> members = new List<Member>();
        private readonly List<Entity> pending = new List<Entity>();    // picked, waiting for their brain to be ready
        private readonly List<Entity> targets = new List<Entity>();
        private readonly List<Entity> goals = new List<Entity>();      // added with Navigation.AddGoal
        private SpawnDirector director;
        private readonly Random random = new Random();
        private float timer;
        private int picked, lastTargetCount = -1;

        public override void OnUpdate(float deltaTime)
        {
            if (director == null)
            {
                director = SpawnDirector.Instance;
                if (director == null) return;
                director.SpawnedFromWave += OnSpawned;
            }
            timer -= deltaTime;
            if (timer > 0f) return;
            timer = Math.Max(0.02f, recheckSeconds);

            FindTargets();
            UpdateGoals();
            AdoptPending();
            for (int i = members.Count - 1; i >= 0; i--)
            {
                Member m = members[i];
                if (!Alive(m.Enemy)) { members.RemoveAt(i); continue; }
                Point(m, Choose(m));
            }
        }

        public override void OnDestroy()
        {
            if (director != null) director.SpawnedFromWave -= OnSpawned;
            foreach (Member m in members) if (Alive(m.Enemy)) Point(m, null);
            foreach (Entity g in goals) Navigation.RemoveGoal(g);
            members.Clear();
            goals.Clear();
        }

        private void OnSpawned(string waveName, Entity enemy)
        {
            if (enemy == null) return;
            if (!string.IsNullOrEmpty(wave) && waveName != wave) return;
            if (random.NextDouble() >= Math.Max(0f, Math.Min(1f, share))) return;
            pending.Add(enemy);
        }

        // The picked enemies whose brain is ready (prefab components exist at once; still, give it a frame).
        private void AdoptPending()
        {
            for (int i = pending.Count - 1; i >= 0; i--)
            {
                Entity e = pending[i];
                pending.RemoveAt(i);
                if (!Alive(e)) continue;
                StateGraphAgent brain = e.GetComponent<StateGraphAgent>();
                if (brain == null) continue;                    // not a state-graph enemy: nothing to point
                members.Add(new Member { Enemy = e, Brain = brain });
                picked++;
                if (logAssignments)
                    Log.Info("WaveTargeting: " + e.Name + " goes for '" + targetTag + "' (" + picked + " picked from " +
                             (string.IsNullOrEmpty(wave) ? "every wave" : wave) + ")");
            }
        }

        private void FindTargets()
        {
            targets.Clear();
            if (string.IsNullOrEmpty(targetTag)) return;
            Entity[] found = null;
            try { found = Entity.FindAllWithTag(targetTag); } catch (Exception) { found = null; }
            if (found != null)
                foreach (Entity t in found)
                    if (t != null && t.IsAlive && t.IsActive) targets.Add(t);
            if (logAssignments && targets.Count != lastTargetCount)
                Log.Info("WaveTargeting: " + targets.Count + " active '" + targetTag + "' target(s)");
            lastTargetCount = targets.Count;
        }

        // Flow-field goals = the active targets (added and removed as they come and go).
        private void UpdateGoals()
        {
            for (int i = goals.Count - 1; i >= 0; i--)
                if (!addNavGoal || !Contains(targets, goals[i])) { Navigation.RemoveGoal(goals[i]); goals.RemoveAt(i); }
            if (!addNavGoal) return;
            foreach (Entity t in targets)
                if (!Contains(goals, t) && Navigation.AddGoal(t)) goals.Add(t);
        }

        private Entity Choose(Member m)
        {
            if (targets.Count == 0) return null;
            if (!nearest)
            {
                if (m.Picked == null || !Contains(targets, m.Picked)) m.Picked = targets[random.Next(targets.Count)];
                return m.Picked;
            }
            Vector3 at = m.Enemy.Transform.WorldPosition;
            Entity best = null;
            float bestSq = float.PositiveInfinity;
            foreach (Entity t in targets)
            {
                Vector3 p = t.Transform.WorldPosition;
                float dx = p.X - at.X, dz = p.Z - at.Z, sq = dx * dx + dz * dz;
                if (sq < bestSq) { bestSq = sq; best = t; }
            }
            return best;
        }

        // Only tells the engine when it changes.
        private static void Point(Member m, Entity target)
        {
            if (SameEntity(m.Target, target)) return;
            if (m.Brain.SetTargetOverride(target)) m.Target = target;
        }

        private static bool Alive(Entity e)
        {
            if (e == null || !e.IsAlive || !e.IsActive) return false;
            Enemy enemy = e.GetComponent<Enemy>();
            return enemy == null || !enemy.IsDead;
        }

        private static bool Contains(List<Entity> list, Entity e)
        {
            foreach (Entity x in list) if (SameEntity(x, e)) return true;
            return false;
        }

        private static bool SameEntity(Entity a, Entity b)
        {
            if (ReferenceEquals(a, null) || ReferenceEquals(b, null)) return ReferenceEquals(a, null) && ReferenceEquals(b, null);
            return a == b;
        }
    }
}
