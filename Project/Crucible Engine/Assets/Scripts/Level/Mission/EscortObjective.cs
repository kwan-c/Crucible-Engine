/*************************************************************************
* \file:         EscortObjective.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Runs an Escort level's payload: spawns it at the 1st
*                checkpoint, moves it along the escort route while a player
*                is near, and handles its health, destruction and respawn.
*                Put it on the same entity as the SubLevelGenerator.
*
* \details
*    The engine half of Unity's EscortPayload (its rules are EscortRules,
*    its ring and guide line EscortVisuals, its wave EscortWaves - a later
*    step). Like MissionController for Capture, it lives on the Sub Level
*    Generator, so the payload prefab is only a body (PayloadHealth, a
*    collider, a model).
*
*    Once the generator's chunks are ready (again after every Generate):
*      the route = generator.Route; the payload runs from the 1st
*      checkpoint to the last (the extract after it is not part of the
*      run); the payload prefab is spawned under the Terrain there.
*    Each frame:
*      in range = flat distance <= radius: any one player (generator
*      Players) pushes it; enemies = Enemy.All alive and switched on.
*      EscortRules.Step moves it (a player in range always advances it;
*      only enemies roll it back to the last checkpoint reached); it heals
*      advancingHeal % of its maximum per second while Advancing (player
*      near, no enemies). It faces its direction of travel and hovers
*      hoverHeight over the route.
*      Destroyed (PayloadHealth.Died): hidden for respawnDelay seconds,
*      then back at the last checkpoint reached at full health.
*    One log line per change.
*
*    Hooks (HUD, waves, mission): Started, CheckpointReached(passed),
*    Arrived, Destroyed, Respawned; State, CheckpointsPassed,
*    CheckpointTotal, Health, MaximumHealth, RespawnRemaining, Payload,
*    Rules, Route, BuildCount, IsComplete.
*
* \history
*    -v1.0       new: Unity's EscortPayload movement, health and respawn
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class EscortObjective : EntityScript
    {
        public Prefab payload;                  // the payload body: PayloadHealth on the root, a collider, a model
        public float speed = 3f;                // metres per second while a player pushes it (Unity)
        public float reverseSpeed = 1.5f;       // metres per second it rolls back when only enemies are near; 0 = never
        public float radius = 12f;              // metres: a player (or enemies) this close moves it
        public float maxHealth = 1500f;         // GDD (Unity M2: 500)
        public float advancingHeal = 2f;        // % of max health per second while advancing with no enemies near; 0 = off
        public float respawnDelay = 5f;         // seconds destroyed before it returns at the last checkpoint
        public float hoverHeight = 2.5f;        // metres above the route
        public bool contestedStops = false;     // on = Unity's rule: enemies in range stop it even with a player there (GDD: off)

        // Hooks.
        public event Action Started;            // first push (EscortWaves starts its wave)
        public event Action<int> CheckpointReached;   // checkpoints passed so far (the last one = arrived)
        public event Action Arrived;
        public event Action Destroyed;
        public event Action Respawned;

        public EscortRules Rules { get { return rules; } }
        public EscortRoute Route { get { return route; } }
        public Entity Payload { get { return body != null ? payloadEntity : null; } }   // null until its scripts are attached
        public int BuildCount { get; private set; }                                    // goes up with every new payload
        public bool HasStarted { get; private set; }
        public bool IsComplete { get { return rules != null && rules.Arrived; } }
        public bool IsDestroyed { get { return respawnIn > 0f; } }
        public float RespawnRemaining { get { return Math.Max(0f, respawnIn); } }
        public EscortState State { get { return rules != null ? rules.State : EscortState.Idle; } }
        public int CheckpointsPassed { get { return rules != null ? rules.CheckpointsPassed : 0; } }
        public int CheckpointTotal { get { return rules != null ? rules.CheckpointCount : 0; } }
        public float Health { get { return body != null ? body.Health : 0f; } }
        public float MaximumHealth { get { return body != null ? body.Maximum : maxHealth; } }

        private SubLevelGenerator generator;
        private int builtFor = -1;
        private EscortRoute route;
        private EscortRules rules;
        private Entity payloadEntity;
        private PayloadHealth body;
        private int waitFrames;
        private float respawnIn;
        private bool diedThisFrame;
        private EscortState lastState = EscortState.Idle;
        private bool warnedNoGenerator, warnedNoPayload;
        private const int kScriptWaitFrames = 30;   // frames to wait for the payload's PayloadHealth

        public override void OnUpdate(float deltaTime)
        {
            if (generator == null)
            {
                generator = Entity.GetComponent<SubLevelGenerator>();
                if (generator == null)
                {
                    if (!warnedNoGenerator) Log.Warning("EscortObjective: put it on the entity with the SubLevelGenerator");
                    warnedNoGenerator = true;
                    return;
                }
            }
            if (!generator.ChunksReady) return;
            if (generator.Generation != builtFor) Build();
            if (payloadEntity == null || rules == null) return;
            if (body == null && !FindBody()) return;

            if (diedThisFrame) OnDestroyedPayload();
            if (respawnIn > 0f)
            {
                respawnIn -= deltaTime;
                if (respawnIn <= 0f) Respawn();
                return;
            }
            if (rules.Arrived) return;

            Vector3 at = payloadEntity.Transform.WorldPosition;
            bool playerNear = PlayerNear(at), enemiesNear = EnemiesNear(at);
            int passed = rules.Step(deltaTime, playerNear, enemiesNear, speed, reverseSpeed, contestedStops);
            if (!HasStarted && rules.Moving)
            {
                HasStarted = true;
                Log.Info("EscortObjective: the escort has started");
                if (Started != null) Started();
            }
            if (rules.State != lastState)
            {
                Log.Info("EscortObjective: payload " + rules.State.ToString().ToLowerInvariant() + " (" + rules.Distance.ToString("F0") + " of " +
                         rules.Length.ToString("F0") + " m)");
                lastState = rules.State;
            }
            if (passed > 0)
            {
                Log.Info("EscortObjective: checkpoint " + rules.CheckpointsPassed + " of " + rules.CheckpointCount + " reached");
                if (CheckpointReached != null) CheckpointReached(rules.CheckpointsPassed);
            }
            if (rules.Arrived)
            {
                Log.Info("EscortObjective: the payload has arrived");
                if (Arrived != null) Arrived();
            }
            if (rules.State == EscortState.Advancing && advancingHeal > 0f)
                body.Heal(body.Maximum * advancingHeal / 100f * deltaTime);
            Place();
        }

        // A new map: the route from the generator, the rules between its checkpoints, a new payload at the 1st.
        private void Build()
        {
            builtFor = generator.Generation;
            if (payloadEntity != null && payloadEntity.IsAlive) Destroy(payloadEntity);
            if (body != null) body.Died -= OnDied;
            payloadEntity = null;
            body = null;
            rules = null;
            respawnIn = 0f;
            diedThisFrame = false;
            HasStarted = false;
            lastState = EscortState.Idle;
            waitFrames = 0;
            BuildCount++;

            route = generator.Route;
            if (route == null || route.CheckpointDistances.Count == 0) return;   // not an Escort level
            List<float> stops = new List<float>();
            for (int i = 0; i < route.Roles.Count; i++)
                if (route.Roles[i] == MissionPresets.Checkpoint) stops.Add(route.CheckpointDistances[i]);
            if (stops.Count < 2)
            {
                Log.Warning("EscortObjective: the escort needs at least 2 checkpoints on its route (found " + stops.Count + ")");
                if (stops.Count == 0) return;
            }
            List<float> between = new List<float>();
            for (int i = 1; i < stops.Count; i++) between.Add(stops[i]);
            rules = new EscortRules(stops[0], stops[stops.Count - 1], between);

            if (payload == null)
            {
                if (!warnedNoPayload) Log.Warning("EscortObjective: no payload prefab - set the Payload field");
                warnedNoPayload = true;
                rules = null;
                return;
            }
            Entity parent = generator.terrain != null ? generator.terrain : Entity;
            payloadEntity = payload.Instantiate(parent);
            if (payloadEntity == null) { rules = null; return; }
            Place();
            Log.Info("EscortObjective: payload at the 1st of " + stops.Count + " checkpoints, " + (rules.Length - rules.Start).ToString("F0") +
                     " m to the last");
        }

        // The payload's PayloadHealth attaches a frame after spawning.
        private bool FindBody()
        {
            body = payloadEntity.IsAlive ? payloadEntity.GetComponent<PayloadHealth>() : null;
            if (body == null)
            {
                if (++waitFrames == kScriptWaitFrames)
                    Log.Warning("EscortObjective: the payload prefab has no PayloadHealth - it can't be damaged or destroyed");
                return false;
            }
            body.Revive(maxHealth);
            body.Died += OnDied;
            return true;
        }

        // PayloadHealth.Died comes from inside a hit; act on it in our own update.
        private void OnDied() { diedThisFrame = true; }

        private void OnDestroyedPayload()
        {
            diedThisFrame = false;
            respawnIn = Math.Max(0.01f, respawnDelay);
            payloadEntity.SetActive(false);
            Log.Info("EscortObjective: payload destroyed - back at checkpoint " + rules.CheckpointsPassed + " in " + respawnDelay.ToString("F0") + " s");
            if (Destroyed != null) Destroyed();
        }

        private void Respawn()
        {
            respawnIn = 0f;
            rules.ReturnToCheckpoint();
            lastState = EscortState.Idle;
            body.Revive(maxHealth);
            payloadEntity.SetActive(true);
            Place();
            Log.Info("EscortObjective: payload back at checkpoint " + rules.CheckpointsPassed);
            if (Respawned != null) Respawned();
        }

        // On the route at the rules' distance (the route is in the Terrain's space, like the payload), facing along it.
        private void Place()
        {
            float x, y, z, dx, dz;
            route.Sample(rules.Distance, out x, out y, out z, out dx, out dz);
            payloadEntity.Transform.Position = new Vector3(x, y + hoverHeight, z);
            payloadEntity.Transform.Rotation = Quaternion.LookRotation(new Vector3(dx, 0f, dz), new Vector3(0f, 1f, 0f));
        }

        private bool PlayerNear(Vector3 at)
        {
            foreach (Entity p in generator.Players())
                if (p.IsActive && FlatDistance(p.Transform.WorldPosition, at) <= radius) return true;
            return false;
        }

        private bool EnemiesNear(Vector3 at)
        {
            foreach (Enemy enemy in Enemy.All)
            {
                if (enemy == null || enemy.IsDead) continue;
                Entity e = enemy.Entity;
                if (!e.IsAlive || !e.IsActive) continue;
                if (FlatDistance(e.Transform.WorldPosition, at) <= radius) return true;
            }
            return false;
        }

        private static float FlatDistance(Vector3 a, Vector3 b)
        {
            float dx = a.X - b.X, dz = a.Z - b.Z;
            return (float)Math.Sqrt(dx * dx + dz * dz);
        }
    }
}
