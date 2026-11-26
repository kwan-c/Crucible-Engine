/*************************************************************************
* \file:         SubLevelGenerator.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       2.2
* \brief         Generates a sub-level from a seed: the terrain and, for a
*                mission level, its chunks. Roads and decor are later port
*                steps.
*
* \details
*    The Unity prototype's SubLevelGenerator, rebuilt for Crucible. It
*    holds the settings (Unity's SubLevelGenSettings asset, as plain fields,
*    since Inspector lists and assets of settings aren't supported) and
*    hands each job to the entity that owns it:
*
*        Sub Level Generator   this script: seed and settings
*        |- Terrain            Terrain, MeshRenderer, Collider (HeightField)
*        '- Nav Surface        FlowFieldSurface: navigation's settings
*
*    Each run: TerrainGenerator builds the map (plain data), optional
*    features add to it (caves: CaveGenerator), a mission level places its
*    chunks on it and flattens the ground under them (ChunkPlacer, with
*    the numbers from MissionPresets), the Terrain
*    child draws and collides with it, the player gets the slope limit (so
*    player and enemies agree on what is a wall), and navigation re-bakes:
*    it reads the terrain's heights and cliffs and fits its grid to the
*    terrain itself, so nothing here sizes the Nav Surface. Nothing is
*    saved: the same seed and settings always rebuild the same ground, and
*    the log prints a height hash to prove it.
*
*    Defaults are the values the Unity mission presets used
*    (LevelCatalogue/SubLevelGen_*.asset).
*
*    Missions: with `mission` empty the map is terrain only, as before (the
*    test scenes). With a mission name ("Capture") the mission's preset
*    sets the map size and the chunks; in Play the chunk prefabs are
*    spawned under the Terrain at their places (ChunkDefinition.Setup
*    gives each its role, order and footprint) and the player starts on
*    the spawn chunk's PlayerStart. The Edit-mode preview flattens the
*    chunks' ground but spawns nothing, so nothing is saved into the scene.
*
*    Generate is also an Inspector button ([EditorButton]): outside Play
*    it builds only the terrain, as a preview (the Nav Surface's Bake
*    button then previews navigation on it). The player's slope and the
*    re-bake need Play (and would otherwise be
*    saved into the scene with no undo). The preview is not saved: it is
*    gone after Play stops or the scene reloads; click Generate again.
*
* \history
*    -v1.0       terrain from a seed, shared with navigation and the player
*    -v1.1       Generate as an [EditorButton] preview in Edit mode
*    -v1.2       No longer sizes or moves the Nav Surface: navigation fits
*                its grid to the terrain on its own (Edit and Play), and
*                takes its cliffs from here, so the surface's slope setting
*                is gone (maxWalkableSlope here is the one slope limit)
*    -v1.3       Optional caves feature (CaveGenerator): plans tunnel
*                networks, painted as the Tunnel region; ground unchanged
*    -v1.4       Caves carve plateaus and trenches (tunnelHeight,
*                roofThickness, caveBlend)
*    -v1.5       Caves are a rock hill with tunnels at ground level and
*                entrances (caveHillHeight, caveHillNoise,
*                caveHillNoiseSize, caveEntrances); roofThickness is the
*                least rock over a tunnel
*    -v2.0       Caves are a cave volume (one system per map): a mountain
*                with rooms on two levels, winding tunnels and mouths;
*                handed to Terrain.Build. Settings replaced (caveVoxelSize,
*                caveMountainHeight, caveRooms, caveDeepRooms, tunnelHeight,
*                caveEntrances, caveWallNoise)
*    -v2.1       The cave log line says how many layouts were tried; the
*                warning is for rooms you can't walk to (was: no air path)
*    -v2.2       Missions: `mission` picks a MissionPresets entry; its
*                chunks are placed and flattened (ChunkPlacer), spawned in
*                Play (spawnChunk, extractChunk, captureChunk), and the
*                player starts on the spawn chunk; chunks get their route
*                order from the placement (ChunkPlacement.Order)
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class SubLevelGenerator : EntityScript
    {
        // --- seed ---------------------------------------------------------- //
        public int seed = 12345;                // the same seed always builds the same map
        public bool randomSeedOnPlay = false;   // pick a new seed each time Play starts

        // --- mission (MissionPresets; empty = terrain only) ------------------- //
        public string mission = "";             // "Capture"; its preset sets the map size and the chunks
        public Prefab spawnChunk;               // chunk prefabs (ChunkDefinition on the root)
        public Prefab extractChunk;
        public Prefab captureChunk;

        // --- terrain (Unity's TerrainSettings) ------------------------------- //
        public float mapSize = 500f;            // metres, square (a mission's preset sets its own)
        public float cellSize = 4f;             // metres (one golem)
        public float hillSize = 160f;           // metres across a typical hill
        public int noiseLayers = 4;
        public float layerStrength = 0.5f;      // how much each smaller layer adds
        public float layerDetail = 2f;          // how much smaller each layer is
        public float heightSpread = 2.2f;       // stretches the noise so it reaches every level
        public int terraceLevels = 4;
        public float levelHeight = 12f;         // metres between levels
        public float terraceSharpness = 0.75f;  // 0 = smooth hills, 1 = sharp steps
        public float cliffCoverage = 0.75f;     // roughly how much of the map is cliffy
        public float variationSize = 350f;      // metres across a cliffy or smooth region
        public float regionBlend = 0.25f;       // width of the change from cliffy to smooth
        public float smoothAreaTerracing = 0.4f; // how much terracing smooth regions keep (0..1)

        // --- caves (optional feature; off = the map is exactly as without it) -- //
        public bool caves = false;              // one cave system: a mountain with rooms and tunnels through it
        public float caveZoneSize = 200f;       // metres across the cave's box
        public float caveVoxelSize = 2f;        // metres per cave sample; must divide cellSize
        public float caveBlend = 12f;           // metres over which the cave's levelled base blends into the land around
        public float caveMountainHeight = 34f;  // metres the mountain rises over its base
        public int caveRooms = 8;               // chambers in the system
        public int caveDeepRooms = 4;           // of those, under the ground; the rest inside the mountain
        public float tunnelHeight = 8f;         // metres from a tunnel's floor to its ceiling
        public int caveEntrances = 3;           // tunnels out of the mountainside
        public float caveWallNoise = 2.5f;      // metres the cave walls are pushed in or out

        // --- movement ------------------------------------------------------- //
        public float maxWalkableSlope = 35f;    // degrees: the default walker's limit, for the player and enemies

        // --- who does what (found by name under this entity when left empty) -- //
        public Entity terrain;                  // has Terrain, MeshRenderer, Collider (HeightField)
        public Entity navSurface;               // has FlowFieldSurface
        public Entity player;                   // has CharacterController; moved to the map's centre

        private TerrainMap map;
        private bool playerPlaced;
        private MissionPreset preset;                                              // null: terrain only
        private List<ChunkPlacement> placements = new List<ChunkPlacement>();
        private readonly List<Entity> chunks = new List<Entity>();                 // spawned in Play
        private ChunkPlacement spawnPlacement;
        private Entity spawnStart;                                                  // the spawn chunk's PlayerStart, if any
        private const float kPlayerDrop = 2f;                                       // metres above the start the player drops from

        public override void OnCreate()
        {
            if (randomSeedOnPlay) seed = (int)new SeededRandom(System.Environment.TickCount).NextUInt();
            Generate();
        }

        public override void OnUpdate(float deltaTime)
        {
            // The player is put on the ground once the terrain's world transform exists.
            if (!playerPlaced && map != null) playerPlaced = PlacePlayer();
        }

        /// <summary>
        /// Builds the ground from the seed and settings and hands it to the terrain; in Play, also to
        /// navigation and the player. Also an Inspector button, which previews the terrain outside Play.
        /// </summary>
        [EditorButton]
        public void Generate()
        {
            // Found by name when not set (OnCreate doesn't run for an Inspector button, so not there).
            if (terrain == null) terrain = Entity.FindChild("Terrain");
            if (navSurface == null) navSurface = Entity.FindChild("Nav Surface");

            Terrain ground = terrain != null ? terrain.GetComponent<Terrain>() : null;
            if (ground == null)
            {
                Log.Error("SubLevelGenerator: no Terrain - set the terrain field or add a child called Terrain");
                return;
            }

            preset = string.IsNullOrEmpty(mission) ? null : MissionPresets.For(mission);
            if (!string.IsNullOrEmpty(mission) && preset == null)
                Log.Warning("SubLevelGenerator: no preset for mission '" + mission + "' (so far: Capture) - terrain only");

            TerrainSettings terrainSettings = ToTerrainSettings();
            map = TerrainGenerator.Generate(seed, terrainSettings);
            if (caves) AddCaves(terrainSettings);
            placements.Clear();
            if (preset != null) PlaceChunks();
            if (!ground.Build(map.Cols, map.Rows, map.CellSize, map.Heights, map.Regions(), map.Volume))
            {
                Log.Error("SubLevelGenerator: Terrain.Build refused the map");
                map = null;
                return;
            }

            if (Application.IsPlaying)
            {
                SpawnChunks();
                CheckNavigation();
                ApplyPlayerSlope();
                Navigation.Rebake();
            }

            int percent = map.WalkableCount * 100 / (map.Cols * map.Rows);
            Log.Info("SubLevelGenerator: " + (Application.IsPlaying ? "" : "preview, ") + "seed " + seed + ", " + map.Cols + " x " + map.Rows + " cells, " + percent +
                     "% walkable, height hash " + map.HeightHash().ToString("X8"));
        }

        private TerrainSettings ToTerrainSettings()
        {
            TerrainSettings s = new TerrainSettings();
            s.mapSize = preset != null ? preset.MapSize : mapSize; s.cellSize = cellSize; s.hillSize = hillSize;
            s.noiseLayers = noiseLayers; s.layerStrength = layerStrength; s.layerDetail = layerDetail;
            s.heightSpread = heightSpread; s.terraceLevels = terraceLevels; s.levelHeight = levelHeight;
            s.terraceSharpness = terraceSharpness; s.maxWalkableSlope = maxWalkableSlope;
            s.cliffCoverage = cliffCoverage; s.variationSize = variationSize; s.regionBlend = regionBlend;
            s.smoothAreaTerracing = smoothAreaTerracing;
            return s;
        }

        private CaveSettings ToCaveSettings()
        {
            CaveSettings s = new CaveSettings();
            s.zoneSize = caveZoneSize; s.voxelSize = caveVoxelSize; s.blendWidth = caveBlend; s.mountainHeight = caveMountainHeight;
            s.rooms = caveRooms; s.deepRooms = caveDeepRooms; s.tunnelHeight = tunnelHeight; s.entrances = caveEntrances;
            s.wallNoise = caveWallNoise;
            return s;
        }

        // The cave system, with one log line (centre in metres from the terrain's centre).
        private void AddCaves(TerrainSettings terrainSettings)
        {
            CaveSystem cave = CaveGenerator.Generate(map, seed, ToCaveSettings(), terrainSettings);
            if (cave == null)
            {
                Log.Warning("SubLevelGenerator: no cave - the box doesn't fit the map, or caveVoxelSize doesn't divide cellSize");
                return;
            }
            Log.Info("SubLevelGenerator: cave at (" + cave.CentreX.ToString("F0") + ", " + cave.CentreZ.ToString("F0") + "): base " +
                     cave.BaseHeight.ToString("F0") + " m (ground there varied " + cave.HeightRange.ToString("F0") + " m), mountain up to " +
                     cave.TopHeight.ToString("F0") + " m, " + cave.Rooms + " rooms (" + cave.DeepRooms + " deep), " + cave.Tunnels +
                     " tunnels (steepest " + cave.SteepestDegrees.ToString("F0") + " deg), " + cave.Entrances + " mouths, " + cave.Samples + " samples, " +
                     cave.Attempts + (cave.Attempts == 1 ? " layout" : " layouts") + " tried");
            if (cave.Rooms < caveRooms)
                Log.Warning("SubLevelGenerator: room for only " + cave.Rooms + " of " + caveRooms + " cave rooms (a ground-level room needs mountain over it)");
            if (cave.UnreachableRooms > 0)
                Log.Warning("SubLevelGenerator: " + cave.UnreachableRooms + " cave rooms can't be walked to from outside and back (best of " +
                            cave.Attempts + " layouts)");
        }

        // The mission's chunks: placed on the map and the ground under them flattened (before the
        // Terrain is built, so it draws and collides with the flattened ground). One log line, and a
        // warning for each chunk that found no room (skipped).
        private void PlaceChunks()
        {
            ChunkPlacementSettings s = preset.Placement;
            s.levelHeight = levelHeight;              // chunks sit on this map's terrace levels
            s.terraceLevels = terraceLevels;
            s.maxWalkableSlope = maxWalkableSlope;
            placements = ChunkPlacer.Place(map, seed, preset.Chunks, s);
            ChunkPlacer.Flatten(map, placements, preset.Chunks, s);

            bool[] found = new bool[preset.Chunks.Count];
            foreach (ChunkPlacement p in placements) found[p.Request] = true;
            for (int i = 0; i < preset.Chunks.Count; i++)
                if (!found[i]) Log.Warning("SubLevelGenerator: no room for " + Describe(preset.Chunks[i]) + " on seed " + seed + " - skipped");
            Log.Info("SubLevelGenerator: " + preset.Name + ": " + placements.Count + " of " + preset.Chunks.Count + " chunks placed, at least " +
                     ChunkPlacer.SpacingFor(seed, s).ToString("F0") + " m apart");
        }

        private static string Describe(ChunkRequest request)
        {
            return request.Order > 0 ? request.Role + " area" : request.Role;
        }

        // Play only: the chunk prefabs, under the Terrain (whose space is the map's), each told its
        // role, order and footprint. Spawned again on every Generate, replacing the last ones.
        // True the first time a key is seen (one warning per role).
        private static bool Warn(List<string> seen, string key)
        {
            if (seen.Contains(key)) return false;
            seen.Add(key);
            return true;
        }

        private void SpawnChunks()
        {
            foreach (Entity old in chunks) if (old.IsAlive) Destroy(old);
            chunks.Clear();
            spawnPlacement = null;
            spawnStart = null;
            playerPlaced = false;

            List<string> warned = new List<string>();    // no HashSet: it lives in System.Core, which the engine's Mono doesn't load
            foreach (ChunkPlacement p in placements)
            {
                ChunkRequest request = preset.Chunks[p.Request];
                Prefab prefab = PrefabFor(request.Role);
                if (request.Role == MissionPresets.Spawn) spawnPlacement = p;
                if (prefab == null)
                {
                    if (Warn(warned, request.Role)) Log.Warning("SubLevelGenerator: no prefab for the " + request.Role + " chunk - set its field");
                    continue;
                }
                Entity chunk = prefab.Instantiate(terrain);
                if (chunk == null) continue;
                chunk.Transform.Position = new Vector3(p.X, p.Height, p.Z);
                ChunkDefinition definition = chunk.GetComponent<ChunkDefinition>();
                if (definition != null) definition.Setup(request.Role, p.Order, request.Footprint);
                else if (Warn(warned, request.Role + "!")) Log.Warning("SubLevelGenerator: the " + request.Role + " chunk prefab has no ChunkDefinition");
                if (request.Role == MissionPresets.Spawn) spawnStart = chunk.FindChild("PlayerStart");
                chunks.Add(chunk);
            }
        }

        private Prefab PrefabFor(string role)
        {
            if (role == MissionPresets.Spawn) return spawnChunk;
            if (role == MissionPresets.Extract) return extractChunk;
            if (role == MissionPresets.Capture) return captureChunk;
            return null;
        }

        // Navigation fits its grid to the terrain and reads its cliffs on its own (they come from
        // maxWalkableSlope above); this only warns when there is no Nav Surface to do it.
        private void CheckNavigation()
        {
            if (navSurface == null || navSurface.GetComponent<FlowFieldSurface>() == null)
                Log.Warning("SubLevelGenerator: no FlowFieldSurface - enemies won't path on the generated ground");
        }

        // Unity's ApplyPlayerSlope: the player climbs exactly what enemies can.
        private void ApplyPlayerSlope()
        {
            CharacterController controller = player != null ? player.GetComponent<CharacterController>() : null;
            if (controller != null) controller.SlopeLimit = maxWalkableSlope;
        }

        // On a mission level, on the spawn chunk's PlayerStart (or its centre), dropped from 2 m as in
        // Unity; otherwise on the walkable cell nearest the map's centre, standing on the ground.
        private bool PlacePlayer()
        {
            if (player == null) return true;
            Terrain ground = terrain.GetComponent<Terrain>();
            Vector3 origin = terrain.Transform.WorldPosition;

            if (spawnPlacement != null)
            {
                Vector3 offset = spawnStart != null && spawnStart.IsAlive ? spawnStart.Transform.Position : Vector3.Zero;
                float x = origin.X + spawnPlacement.X + offset.X, z = origin.Z + spawnPlacement.Z + offset.Z;
                float y;
                if (!ground.GroundHeight(x, z, out y)) return false;   // world transform not ready yet: try next frame
                player.Transform.WorldPosition = new Vector3(x, System.Math.Max(y, origin.Y + spawnPlacement.Height + offset.Y) + kPlayerDrop, z);
                return true;
            }

            int centre = map.Cols / 2;
            for (int radius = 0; radius < map.Cols / 2; ++radius)
                for (int dz = -radius; dz <= radius; ++dz)
                    for (int dx = -radius; dx <= radius; ++dx)
                    {
                        if (System.Math.Max(System.Math.Abs(dx), System.Math.Abs(dz)) != radius) continue;   // this ring only
                        int col = centre + dx, row = centre + dz;
                        if (col < 0 || row < 0 || col >= map.Cols || row >= map.Rows || !map.IsWalkable(col, row)) continue;

                        float x = origin.X + map.MinX + (col + 0.5f) * map.CellSize;
                        float z = origin.Z + map.MinZ + (row + 0.5f) * map.CellSize;
                        float y;
                        if (!ground.GroundHeight(x, z, out y)) return false;   // world transform not ready yet: try next frame
                        player.Transform.WorldPosition = new Vector3(x, y + 1.5f, z);
                        return true;
                    }
            Log.Warning("SubLevelGenerator: no walkable cell for the player");
            return true;
        }
    }
}
