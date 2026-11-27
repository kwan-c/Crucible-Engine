/*************************************************************************
* \file:         MissionController.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.2
* \brief         Runs a mission level's objectives: the Capture areas in
*                order, then the extract. Put it on the same entity as the
*                SubLevelGenerator.
*
* \details
*    Replaces Unity's MissionTracker + KOTH_Zone + ExtractZone (one script
*    on each chunk there). Here one controller reads the chunks the
*    generator spawned, so chunk prefabs need no objective scripts.
*
*    Each frame, once the generator's chunks are ready:
*      1. Players: the generator's (every entity tagged "Player"; a test
*         scene may set its player field instead). Networked players only
*         need the same tag; nothing else here changes.
*      2. Capture: the current area (the lowest unfinished 1st/2nd/3rd;
*         with inOrder off, every unfinished area) runs a HoldZoneRules.
*         "Inside" = EVERY player inside the area's footprint. Hold and
*         grace seconds come from MissionPresets by player count (GDD
*         a/b/c/d = 1/2/3/4 players), fixed when the area first ticks.
*      3. MissionRules: when every area is complete the extract opens; the
*         mission completes the moment every player is inside it (GDD:
*         "collectively standing in this area").
*
*    Logic only: MissionZoneVisuals draws the zones (outlines, beacons)
*    and MissionHud shows Title / Detail; both just read this. One log
*    line per state change.
*
*    Hooks (for waves, HUD, marker, results): AreaStateChanged,
*    ExtractOpened, MissionCompleted; Zones (each with State, IsOpen,
*    Progress, timers), Active, Title, Detail, Status, State, CurrentArea.
*
* \history
*    -v1.0       Capture areas in order, collective instant extract
*    -v1.1       Zones / Active / Title / Detail for the visuals and HUD;
*                beacons moved to MissionZoneVisuals
*    -v1.2       players from SubLevelGenerator.Players (one rule for both)
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class MissionController : EntityScript
    {
        /// <summary>One objective zone (a capture area or the extract), read by the visuals and the HUD.</summary>
        public sealed class Zone
        {
            public Entity Chunk { get; internal set; }
            public ChunkDefinition Definition { get; internal set; }
            public int Order { get; internal set; }                   // 1, 2, 3 ... for capture areas; 0 for the extract
            public bool IsExtract { get; internal set; }
            public bool IsOpen { get; internal set; }                 // active now: the current area, or the extract once open
            internal HoldZoneRules Rules;
            public HoldZoneState State { get { return Rules != null ? Rules.State : HoldZoneState.Waiting; } }
            /// <summary>0..1 of the hold done (capture areas).</summary>
            public float Progress { get { return Rules != null ? Rules.Elapsed / Rules.HoldSeconds : 0f; } }
            public float Remaining { get { return Rules != null ? Rules.Remaining : 0f; } }
            public float GraceRemaining { get { return Rules != null ? Rules.GraceRemaining : 0f; } }
        }

        private SubLevelGenerator generator;
        private int builtGeneration = -1;
        private readonly List<Zone> areas = new List<Zone>();
        private readonly List<Zone> zones = new List<Zone>();       // areas, then the extract
        private Zone extract;
        private CaptureSettings settings;
        private MissionRules mission;
        private bool warnedNoTag, warnedNoGenerator;

        /// <summary>An area's state changed: (its order 1, 2, 3 ..., new state).</summary>
        public event Action<int, HoldZoneState> AreaStateChanged;
        public event Action ExtractOpened;
        public event Action MissionCompleted;

        public MissionState State { get { return mission != null ? mission.State : MissionState.InProgress; } }

        /// <summary>The order (1, 2, 3 ...) of the area being played now; 0 when none.</summary>
        public int CurrentArea { get { Zone a = Current(); return a != null ? a.Order : 0; } }

        /// <summary>Every objective zone (areas in order, then the extract). Rebuilt with the map (BuildCount).</summary>
        public IList<Zone> Zones { get { return zones; } }
        /// <summary>Goes up every time the zones are rebuilt (a new map).</summary>
        public int BuildCount { get; private set; }
        /// <summary>The zone being played now: the current area, or the extract once open; null when none.</summary>
        public Zone Active
        {
            get
            {
                if (mission == null || mission.State == MissionState.Complete) return null;
                return mission.State == MissionState.ExtractOpen ? extract : Current();
            }
        }
        public int AreasDone { get { int n = 0; foreach (Zone a in areas) if (a.State == HoldZoneState.Complete) n++; return n; } }
        public int AreasTotal { get { return areas.Count; } }

        /// <summary>The HUD's first line: what to do.</summary>
        public string Title
        {
            get
            {
                if (mission == null) return "";
                if (mission.State == MissionState.Complete) return "Mission complete";
                if (mission.State == MissionState.ExtractOpen) return "Extract";
                Zone a = Current();
                return a == null ? "" : "Capture the " + Ordinal(a.Order) + " area  (" + AreasDone + "/" + AreasTotal + ")";
            }
        }

        /// <summary>The HUD's second line: the timer or what went wrong.</summary>
        public string Detail
        {
            get
            {
                if (mission == null || mission.State == MissionState.Complete) return "";
                if (mission.State == MissionState.ExtractOpen) return "Everyone to the extraction point";
                Zone a = Current();
                if (a == null) return "";
                switch (a.State)
                {
                    case HoldZoneState.Holding: return "Hold: " + a.Remaining.ToString("0.0") + "s";
                    case HoldZoneState.Warning: return "Get back in! " + a.GraceRemaining.ToString("0.0") + "s";
                    case HoldZoneState.Failed: return "Area lost - go back in to restart";
                    default: return settings != null && players > 1 ? "Get every player inside" : "Get inside the area";
                }
            }
        }

        /// <summary>Both HUD lines in one.</summary>
        public string Status { get { string d = Detail; return d.Length > 0 ? Title + ": " + d : Title; } }
        private int players;

        public override void OnUpdate(float deltaTime)
        {
            if (generator == null)
            {
                generator = Entity.GetComponent<SubLevelGenerator>();
                if (generator == null)
                {
                    if (!warnedNoGenerator) Log.Warning("MissionController: put it on the entity with the SubLevelGenerator");
                    warnedNoGenerator = true;
                    return;
                }
            }
            if (!generator.ChunksReady) return;
            if (generator.Generation != builtGeneration) Build();
            if (mission == null || mission.State == MissionState.Complete) return;

            List<Entity> players = Players();
            this.players = players.Count;
            int completed = 0;
            foreach (Zone area in areas)
            {
                if (area.State == HoldZoneState.Complete) { completed++; continue; }
                if (settings.inOrder && area != Current()) continue;
                if (area.Rules == null)
                    area.Rules = new HoldZoneRules(ByPlayers(settings.holdSeconds, players.Count), ByPlayers(settings.graceSeconds, players.Count),
                                                   settings.failedSeconds);
                if (area.Rules.Tick(deltaTime, AllInside(players, area.Definition)))
                {
                    Log.Info("MissionController: " + Ordinal(area.Order) + " area " + area.State.ToString().ToLower());
                    if (AreaStateChanged != null) AreaStateChanged(area.Order, area.State);
                    if (area.State == HoldZoneState.Complete) { completed++; UpdateOpen(); }
                }
            }

            bool extracted = mission.State == MissionState.ExtractOpen && extract != null && AllInside(players, extract.Definition);
            if (!mission.Update(areas.Count, completed, extracted)) return;
            if (mission.State == MissionState.ExtractOpen)
            {
                if (extract == null) Log.Warning("MissionController: every area is captured but there is no extract chunk");
                Log.Info("MissionController: extract open");
                UpdateOpen();
                if (ExtractOpened != null) ExtractOpened();
            }
            else if (mission.State == MissionState.Complete)
            {
                Log.Info("MissionController: mission complete");
                UpdateOpen();
                if (MissionCompleted != null) MissionCompleted();
            }
        }

        // The generator built a new map (Play start, or Generate in Play): read its chunks afresh.
        private void Build()
        {
            builtGeneration = generator.Generation;
            areas.Clear();
            zones.Clear();
            extract = null;
            mission = null;
            BuildCount++;
            MissionPreset preset = generator.Preset;
            settings = preset != null ? preset.Capture : null;
            if (settings == null) return;                       // not a Capture level

            foreach (Entity chunk in generator.SpawnedChunks)
            {
                if (chunk == null || !chunk.IsAlive) continue;
                ChunkDefinition definition = chunk.GetComponent<ChunkDefinition>();
                if (definition == null) continue;
                Zone zone = new Zone { Chunk = chunk, Definition = definition, Order = definition.Order };
                if (definition.Role == MissionPresets.Capture) areas.Add(zone);
                else if (definition.Role == MissionPresets.Extract) { zone.IsExtract = true; zone.Order = 0; extract = zone; }
            }
            areas.Sort(delegate (Zone a, Zone b) { return a.Order.CompareTo(b.Order); });
            zones.AddRange(areas);
            if (extract != null) zones.Add(extract);
            mission = new MissionRules();
            UpdateOpen();
            Log.Info("MissionController: " + preset.Name + " - " + areas.Count + " area(s)" + (settings.inOrder ? " in order" : "") +
                     (extract != null ? ", then the extract" : ", no extract chunk"));
        }

        // The lowest-ordered area not yet complete, or null.
        private Zone Current()
        {
            foreach (Zone a in areas) if (a.State != HoldZoneState.Complete) return a;
            return null;
        }

        // Which zones are active: the current area (every unfinished one with inOrder off) while in progress,
        // the extract once open; nothing once the mission is complete.
        private void UpdateOpen()
        {
            Zone current = Current();
            bool inProgress = mission != null && mission.State == MissionState.InProgress;
            foreach (Zone a in areas)
                a.IsOpen = inProgress && a.State != HoldZoneState.Complete && (!settings.inOrder || a == current);
            if (extract != null) extract.IsOpen = mission != null && mission.State == MissionState.ExtractOpen;
        }

        // The generator's players (the "Player" tag, or its player field in test scenes).
        private List<Entity> Players()
        {
            List<Entity> list = generator.Players();
            if (list.Count == 0 && !warnedNoTag) Log.Warning("MissionController: no player - tag the Player entity \"Player\"");
            if (list.Count == 0) warnedNoTag = true;
            return list;
        }

        // Every player inside the chunk's footprint (none = not inside).
        private static bool AllInside(List<Entity> players, ChunkDefinition chunk)
        {
            if (players.Count == 0 || chunk == null) return false;
            foreach (Entity p in players)
                if (!chunk.Contains(p.Transform.WorldPosition)) return false;
            return true;
        }

        // The GDD's a/b/c/d: the value for 1, 2, 3, 4 players (the last one for more).
        private static float ByPlayers(float[] values, int players)
        {
            if (values == null || values.Length == 0) return 0f;
            return values[Math.Max(0, Math.Min(values.Length - 1, players - 1))];
        }

        private static string Ordinal(int n)
        {
            return n == 1 ? "1st" : n == 2 ? "2nd" : n == 3 ? "3rd" : n + "th";
        }
    }
}
