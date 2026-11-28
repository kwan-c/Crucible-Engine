/*************************************************************************
* \file:         SpawnDirector.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.1
* \brief         The scene's one spawn manager: runs the roaming wave and the
*                objective wave another script starts.
*
* \details
*    Ported from the Unity prototype (AI/Spawning/SpawnDirector.cs). On the
*    root of the Spawn Director prefab; its children hold the waves (Wave).
*    Objective wave: started by another script (RunWave: Capture's areas,
*      later the escort payload and Liberation's camp) on the spawn
*      sources it gives, stopped with StopWave. One at a time: another
*      owner's RunWave is refused while one runs. The owner can set a
*      budget (most spawn cost alive), scale the resource, and hear every
*      spawn (Spawned). Enemies already out stay when a wave stops.
*    Roaming: the roamingWave child, on a ring roamingRadius round a
*      player (each spawn takes the next player in turn: multiplayer).
*      It runs whenever no objective wave that pauses roaming is running
*      and at least one player is outside every RoamingBlockers area;
*      players inside one are skipped. Paused, it keeps its place in its
*      cycle.
*    Spawn spots: on the ring, open on the nav grid (Navigation.IsWalkable;
*      nothing until the grid is baked), at the ground height from
*      SpawnGround (the generated terrain) or else a ray down, plus
*      spawnLift. Each enemy is woken the frame after it spawns (its
*      script attaches a frame late; some golems start asleep).
*    Alive = entity alive and active and its Enemy not dead (a dead golem
*      stays as a corpse for a while).
*    Every 0.5 s: the leash (members past the wave's leash distance from
*      its owner's anchor are removed and refunded) and the cleared-wave
*      shortcut. The rules themselves are WaveRun / WaveCycle (plain code).
*    Multiplayer: spawning should run on the host only once networking is
*      in; nothing here assumes a single player.
*
* \history
*    -v1.0       ported from Unity; roaming rotates between players
*    -v1.1       SpawnedFromWave(wave name, enemy) for every spawn, roaming
*                included (WaveTargeting picks enemies by wave)
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class SpawnDirector : EntityScript, IWaveAnchor
    {
        public string roamingWave = "Wave Roaming";   // child holding the roaming wave; empty = no roaming
        public float roamingRadius = 50f;             // roaming enemies appear this far from a player
        public int spawnAttempts = 12;                // random spots on a ring tried before skipping a spawn
        public float spawnLift = 0.5f;                // metres above the ground a spawn starts
        public bool logWaves = true;                  // a console line when a wave starts, runs out, refills, pauses

        private const float kLeashCheckInterval = 0.5f;
        private const float kRayHeight = 100f;        // ground ray starts this far above the ring's centre

        private static SpawnDirector instance;
        private readonly Random random = new Random();
        private WaveRun<Entity> objective;
        private WaveRun<Entity> roaming;
        private RingSource roamingRing;
        private readonly List<Entity> pendingWake = new List<Entity>();
        private readonly List<Entity> eligible = new List<Entity>();
        private int roamingTurn;
        private float untilLeashCheck;
        private Vector3 roamingCentre;
        private bool roamingWasActive, objectiveWasRefilling, roamingWasRefilling;
        private string pauseReason = "";

        /// <summary>Every enemy a wave spawns, with the wave's owner (the director itself for roaming).</summary>
        public event Action<object, Entity> Spawned;
        /// <summary>Every spawn, roaming included: (the wave's name, e.g. "Wave Escort", the new enemy).</summary>
        public event Action<string, Entity> SpawnedFromWave;

        public int TotalSpawned { get; private set; }
        public int TotalLeashed { get; private set; }
        public bool RoamingActive { get { return roamingWasActive; } }
        public string CurrentWave { get { return objective != null ? objective.Settings.Name : roamingWasActive && roaming != null ? roaming.Settings.Name : ""; } }
        public int ResourceLeft { get { WaveRun<Entity> w = objective ?? (roamingWasActive ? roaming : null); return w != null ? w.Cycle.Resource : 0; } }
        public Vector3 WaveAnchor { get { return roamingCentre; } }   // roaming's anchor: the player it last spawned round

        /// <summary>The scene's director (null until its script has attached, a frame after the scene starts).</summary>
        public static SpawnDirector Instance
        {
            get
            {
                if (instance != null && instance.Entity != null && instance.Entity.IsAlive) return instance;
                instance = null;
                Entity e = Entity.Find("Spawn Director");
                if (e != null) instance = e.GetComponent<SpawnDirector>();
                return instance;
            }
        }

        public override void OnCreate()
        {
            instance = this;
            roamingRing = MakeRing(PickRoamingCentre, roamingRadius);
        }

        public override void OnDestroy()
        {
            if (instance == this) instance = null;
        }

        // ---------------------------------------------------------------- objective waves

        /// <summary>The wave on child `name` (e.g. "Wave Capture"), or null.</summary>
        public Wave FindWave(string name)
        {
            if (string.IsNullOrEmpty(name)) return null;
            Entity child = Entity.FindChild(name);
            return child != null ? child.GetComponent<Wave>() : null;
        }

        /// <summary>A ring of spawn spots `radius` round wherever `centre` says (asked per spawn).</summary>
        public RingSource MakeRing(Func<Vector3?> centre, float radius)
        {
            var ring = new RingSource(centre, radius) { Attempts = spawnAttempts, Lift = spawnLift, IsOpen = IsOpen };
            ring.GroundHeight = (x, z) => GroundAt(x, z, ring);
            return ring;
        }

        /// <summary>
        /// Starts owner's wave on these sources. Only one objective wave runs at a time: false if
        /// another owner's is running. The same owner calling again restarts its wave.
        /// </summary>
        public bool RunWave(Wave wave, IList<ISpawnSource> sources, object owner)
        {
            if (wave == null) { Log.Warning("SpawnDirector: RunWave without a wave"); return false; }
            return RunWave(wave.Settings(), sources, owner);
        }

        public bool RunWave(WaveSettings settings, IList<ISpawnSource> sources, object owner)
        {
            if (settings == null || sources == null || sources.Count == 0)
            {
                Log.Warning("SpawnDirector: a wave needs settings and at least one spawn source");
                return false;
            }
            if (objective != null && objective.Owner != owner) return false;
            objective = new WaveRun<Entity>(settings, sources, owner);
            objectiveWasRefilling = false;
            if (logWaves) Log.Info("SpawnDirector: " + Describe(settings) + " started");
            return true;
        }

        /// <summary>Stops owner's wave. Enemies it already spawned stay until killed.</summary>
        public void StopWave(object owner)
        {
            if (objective == null || objective.Owner != owner) return;
            if (logWaves) Log.Info("SpawnDirector: " + objective.Settings.Name + " stopped (" + objective.TotalSpawned + " spawned)");
            objective = null;
        }

        public bool IsRunning(object owner) { return objective != null && objective.Owner == owner; }

        /// <summary>Limits owner's running wave to this much spawn cost alive at once; negative = no limit.</summary>
        public void SetBudget(object owner, int budget) { if (IsRunning(owner)) objective.Budget = budget; }

        /// <summary>Scales owner's running wave's resource from its next refill (1.25 = 25% more).</summary>
        public void ScaleWave(object owner, float scale) { if (IsRunning(owner)) objective.Cycle.ScaleTotal(scale); }

        // ---------------------------------------------------------------- per frame

        public override void OnUpdate(float deltaTime)
        {
            WakePending();
            if (deltaTime <= 0f) return;

            bool roamingNow = UpdateRoamingActive();
            if (objective != null && objective.Tick(deltaTime)) Spawn(objective);
            if (roamingNow && roaming.Tick(deltaTime)) Spawn(roaming);

            untilLeashCheck -= deltaTime;
            if (untilLeashCheck <= 0f)
            {
                untilLeashCheck = kLeashCheckInterval;
                ApplyLeash(objective);
                if (objective != null) objective.ShortenIfCleared(Alive);
                if (roaming != null) roaming.ShortenIfCleared(Alive);
                ApplyLeash(roaming);
            }
            LogRefills();
        }

        private void Spawn(WaveRun<Entity> run)
        {
            Entity member = run.TrySpawn(random, Alive, Make);
            if (member == null) return;
            TotalSpawned++;
            pendingWake.Add(member);
            if (Spawned != null) Spawned(run.Owner, member);
            if (SpawnedFromWave != null) SpawnedFromWave(run.Settings.Name, member);
        }

        private static Entity Make(WaveEnemy enemy, Vector3 position, Quaternion rotation)
        {
            return enemy.Prefab != null ? enemy.Prefab.Instantiate(position, rotation) : null;
        }

        // A golem's script attaches the frame after it spawns; wake it then (some start asleep).
        private void WakePending()
        {
            for (int i = pendingWake.Count - 1; i >= 0; i--)
            {
                Entity e = pendingWake[i];
                if (e == null || !e.IsAlive) { pendingWake.RemoveAt(i); continue; }
                Enemy enemy = e.GetComponent<Enemy>();
                if (enemy == null) continue;
                enemy.Wake();
                pendingWake.RemoveAt(i);
            }
        }

        private static bool Alive(Entity e)
        {
            if (e == null || !e.IsAlive || !e.IsActive) return false;
            Enemy enemy = e.GetComponent<Enemy>();
            return enemy == null || !enemy.IsDead;
        }

        // Members past the wave's leash from its owner's anchor are removed (not killed) and refunded.
        private void ApplyLeash(WaveRun<Entity> run)
        {
            if (run == null || run.Settings.LeashDistance <= 0f) return;
            IWaveAnchor anchor = run.Owner as IWaveAnchor;
            if (anchor == null) return;
            TotalLeashed += run.ApplyLeash(anchor.WaveAnchor, m => m.Transform.WorldPosition, Alive, m => Entity.Destroy(m));
        }

        // ---------------------------------------------------------------- roaming

        // Roaming runs when no objective wave pauses it and some player is outside every pausing area.
        private bool UpdateRoamingActive()
        {
            string reason = "";
            if (string.IsNullOrEmpty(roamingWave)) reason = "no roaming wave set";
            else if (objective != null && objective.Settings.PausesRoaming) reason = objective.Settings.Name + " is running";
            else if (roaming == null && !BuildRoaming()) reason = "no " + roamingWave + " child with a Wave yet";
            else
            {
                FindEligiblePlayers();
                if (eligible.Count == 0) reason = "every player is inside an objective area (or none found)";
            }
            bool active = reason.Length == 0;
            if (logWaves && roaming != null && (active != roamingWasActive || (!active && reason != pauseReason)))
                Log.Info("SpawnDirector: " + roaming.Settings.Name + (active ? " running" : " paused - " + reason));
            roamingWasActive = active;
            pauseReason = reason;
            return active;
        }

        private bool BuildRoaming()
        {
            Wave wave = FindWave(roamingWave);
            if (wave == null) return false;
            roaming = new WaveRun<Entity>(wave.Settings(), new ISpawnSource[] { roamingRing }, this);
            if (logWaves) Log.Info("SpawnDirector: " + Describe(roaming.Settings) + ", " + roamingRadius + " m round the players");
            return true;
        }

        private void FindEligiblePlayers()
        {
            eligible.Clear();
            foreach (Entity p in Entity.FindAllWithTag("Player"))
                if (p != null && p.IsAlive && !RoamingBlockers.Contains(p.Transform.WorldPosition)) eligible.Add(p);
        }

        // The roaming ring's centre for this spawn: the next eligible player in turn.
        private Vector3? PickRoamingCentre()
        {
            if (eligible.Count == 0) return null;
            Entity p = eligible[roamingTurn++ % eligible.Count];
            roamingCentre = p.Transform.WorldPosition;
            return roamingCentre;
        }

        // ---------------------------------------------------------------- spawn spots

        private static bool IsOpen(Vector3 position)
        {
            return Navigation.IsBaked && Navigation.IsWalkable(position);
        }

        // The generated terrain's height if registered, else the first thing a ray down hits
        // (from kRayHeight above the ring's centre); NaN = nothing below.
        private static float GroundAt(float x, float z, RingSource ring)
        {
            float h = SpawnGround.HeightAt(x, z);
            if (!float.IsNaN(h)) return h;
            RaycastHit hit;
            var top = new Vector3(x, ring.LastCentre.Y + kRayHeight, z);
            if (Physics.Raycast(top, new Vector3(0f, -1f, 0f), out hit, kRayHeight * 3f)) return hit.Point.Y;
            return float.NaN;
        }

        // ---------------------------------------------------------------- logging

        private void LogRefills()
        {
            if (!logWaves) return;
            LogRefill(objective, ref objectiveWasRefilling);
            LogRefill(roaming, ref roamingWasRefilling);
        }

        private static void LogRefill(WaveRun<Entity> run, ref bool wasRefilling)
        {
            if (run == null) return;
            bool now = run.Cycle.Refilling;
            if (now && !wasRefilling)
                Log.Info("SpawnDirector: " + run.Settings.Name + " spent (" + run.TotalSpawned + " spawned so far) - refills in " + run.Cycle.ResetRemaining.ToString("0.#") + " s");
            else if (!now && wasRefilling)
                Log.Info("SpawnDirector: " + run.Settings.Name + " refilled");
            wasRefilling = now;
        }

        private static string Describe(WaveSettings s)
        {
            return s.Name + " (" + s.ResourceTotal + " resource, every " + s.SpawnInterval + " s, reset " + s.TimerReset + " s" +
                   (s.MaxConcurrent > 0 ? ", max " + s.MaxConcurrent + " alive" : "") +
                   (s.LeashDistance > 0f ? ", leash " + s.LeashDistance + " m" : "") + ", " + s.Enemies.Count + " enemy type(s))";
        }
    }
}
