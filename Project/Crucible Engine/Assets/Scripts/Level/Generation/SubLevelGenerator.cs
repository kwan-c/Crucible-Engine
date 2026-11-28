/*************************************************************************
* \file:         SubLevelGenerator.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       2.8
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
*                order from the placement (ChunkPlacement.Order); roads
*                join the chunks (RoadBuilder)
*    -v2.3       Scattered trees in Play (DecorScatter; one tree prefab
*                field per theme)
*    -v2.4       Chunk interiors (ChunkInterior) after the roads; chunk
*                Setup and outposts (ChunkOutpost + OutpostPlanner) once
*                the chunk's scripts are attached, then the nav re-bake
*    -v2.5       Preset, SpawnedChunks, Generation and ChunksReady for the
*                MissionController
*    -v2.6       players found by the "Player" tag (the player field is an
*                optional override); every player placed at the start,
*                3 m apart; slope limit on every player
*    -v2.7       in Play, registers the terrain's heights for spawn spots
*                (SpawnGround: spawns see through trees and pieces)
*    -v2.8       the mission's numbers come from its layout script on this
*                entity (MissionLayout, e.g. CaptureLayout) when there is one,
*                else MissionPresets' defaults; the log says which
**************************************************************************/
using System;
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
        public Prefab broadleafTree;            // scattered trees, one prefab per theme (StaticObstacle trunk collider);
        public Prefab pineTree;                 // themes are TEMPORARY quadrants (DecorScatter) until level themes are set
        public Prefab deadTree;
        public Prefab burntTree;

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
        public Entity player;                   // optional: leave empty - every entity tagged "Player" is used (see Players)

        private TerrainMap map;
        private bool playerPlaced;
        private MissionPreset preset;                                              // null: terrain only
        private List<ChunkPlacement> placements = new List<ChunkPlacement>();
        private readonly List<Entity> chunks = new List<Entity>();                 // spawned in Play
        private readonly List<Entity> trees = new List<Entity>();                  // spawned in Play
        private List<Road> roads = new List<Road>();                               // this map's roads (entrances for interiors)
        private readonly List<PendingChunk> pending = new List<PendingChunk>();    // spawned chunks waiting for their scripts
        private int outpostPieces;
        private bool chunksSpawned;

        /// <summary>The mission preset in use (null: terrain only).</summary>
        public MissionPreset Preset { get { return preset; } }
        /// <summary>The chunk entities spawned in Play (ChunkDefinition on each root).</summary>
        public IList<Entity> SpawnedChunks { get { return chunks; } }
        /// <summary>Goes up every time the chunks are spawned again (Play start, Generate in Play).</summary>
        public int Generation { get; private set; }
        /// <summary>Play: the chunks are spawned and set up (Setup and outposts done).</summary>
        public bool ChunksReady { get { return chunksSpawned && pending.Count == 0; } }
        private const int kScriptWaitFrames = 30;                                  // frames to wait for a chunk's scripts

        // A chunk just spawned: its prefab's scripts are attached a frame later, so Setup and the outpost wait.
        private sealed class PendingChunk
        {
            public Entity Chunk;
            public int Placement;                                                  // index into placements
            public int Frames;
        }
        private ChunkPlacement spawnPlacement;
        private Entity spawnStart;                                                  // the spawn chunk's PlayerStart, if any
        private const float kPlayerDrop = 2f;                                       // metres above the start the player drops from
        private const float kPlayerSpacing = 3f;                                    // metres between players at the start
        private int playerSearchFrames;

        public override void OnCreate()
        {
            if (randomSeedOnPlay) seed = (int)new SeededRandom(System.Environment.TickCount).NextUInt();
            Generate();
        }

        public override void OnUpdate(float deltaTime)
        {
            // The player is put on the ground once the terrain's world transform exists.
            if (!playerPlaced && map != null) playerPlaced = PlacePlayer();
            if (pending.Count > 0) FinishChunks();
        }

        /// <summary>
        /// Builds the ground from the seed and settings and hands it to the terrain; in Play, also to
        /// navigation and the player. Also an Inspector button, which previews the terrain outside Play.
        /// </summary>
        // The mission's preset: from its layout script on this entity (the Inspector numbers), else
        // the layout's defaults (MissionPresets.For), e.g. when an Edit-mode preview can't see it.
        private MissionPreset LoadPreset()
        {
            MissionLayout[] layouts = null;
            try { layouts = Entity.GetComponents<MissionLayout>(); } catch (Exception) { layouts = null; }
            if (layouts != null)
                foreach (MissionLayout layout in layouts)
                    if (layout != null && layout.Mission == mission)
                    {
                        Log.Info("SubLevelGenerator: " + mission + " layout from the " + layout.GetType().Name + " script");
                        return layout.BuildPreset();
                    }
            MissionPreset fallback = MissionPresets.For(mission);
            if (fallback != null)
                Log.Info("SubLevelGenerator: " + mission + " layout = defaults (no " + mission + "Layout script found on this entity" +
                         (Application.IsPlaying ? "" : ", or not visible to the Edit preview") + ")");
            return fallback;
        }

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

            preset = string.IsNullOrEmpty(mission) ? null : LoadPreset();
            if (!string.IsNullOrEmpty(mission) && preset == null)
                Log.Warning("SubLevelGenerator: no layout for mission '" + mission + "' (so far: Capture) - terrain only");

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
                TerrainMap built = map;
                SpawnGround.Register(Entity, (x, z) => DecorScatter.HeightAt(built, x, z));
                SpawnChunks();
                SpawnTrees();
                CheckNavigation();
                ApplyPlayerSlope();
                if (pending.Count == 0) Navigation.Rebake();   // else once the chunks' outposts are in (FinishChunks)
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
            BuildRoads();
            BuildInteriors();
        }

        // The circular chunks' interiors (ChunkInterior: blocks, walls, an open middle, lanes from the road
        // entrances), after the roads. A warning for any chunk whose centre an entrance can't walk to.
        private void BuildInteriors()
        {
            ChunkInterior.Build(map, seed, placements, preset.Chunks, roads, maxWalkableSlope);
            int shaped = 0;
            for (int i = 0; i < placements.Count; i++)
            {
                ChunkRequest request = preset.Chunks[placements[i].Request];
                if (request.Interior == null || !request.Footprint.Circle) continue;
                shaped++;
                if (!ChunkInterior.CentreReachable(map, placements[i], request.Footprint, ChunkInterior.Entrances(i, placements[i], roads)))
                    Log.Warning("SubLevelGenerator: on seed " + seed + " a road entrance can't walk to the middle of " + Describe(request) + " " + placements[i].Order);
            }
            if (shaped > 0) Log.Info("SubLevelGenerator: " + shaped + " chunk interior(s) shaped");
        }

        // Roads between the placed chunks (RoadBuilder: A* routes over the terrain), pressed into the
        // ground and painted as Road, after the chunks are flattened. One log line; a warning when a
        // road found no route, and a note for roads carved through a cliff.
        private void BuildRoads()
        {
            RoadSettings r = preset.Roads;
            r.maxWalkableSlope = maxWalkableSlope;
            roads = RoadBuilder.Build(map, placements, preset.Chunks, r);
            int wanted = RoadBuilder.LinkCount(placements.Count, r);
            int carved = 0, cells = 0;
            foreach (Road road in roads) if (road.Carved) carved++;
            for (int i = 0; i < map.Road.Length; i++) if (map.Road[i]) cells++;
            Log.Info("SubLevelGenerator: " + roads.Count + " of " + wanted + " roads (" + (r.chain ? "along the route" : "network") + "), " +
                     cells + " road cells" + (carved > 0 ? ", " + carved + " carved through a cliff" : ""));
            if (roads.Count < wanted)
                Log.Warning("SubLevelGenerator: " + (wanted - roads.Count) + " road(s) found no route on seed " + seed + " - skipped");
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
            foreach (Entity old in chunks) if (old.IsAlive) Destroy(old);   // their outpost pieces go with them
            chunks.Clear();
            pending.Clear();
            outpostPieces = 0;
            chunksSpawned = true;
            Generation++;
            spawnPlacement = null;
            spawnStart = null;
            playerPlaced = false;
            playerSearchFrames = 0;

            List<string> warned = new List<string>();    // no HashSet: it lives in System.Core, which the engine's Mono doesn't load
            for (int index = 0; index < placements.Count; index++)
            {
                ChunkPlacement p = placements[index];
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
                if (request.Role == MissionPresets.Spawn) spawnStart = chunk.FindChild("PlayerStart");
                chunks.Add(chunk);
                pending.Add(new PendingChunk { Chunk = chunk, Placement = index });
            }
        }

        // Each frame while chunks are pending: once a chunk's scripts are attached, give it its role, order
        // and footprint (ChunkDefinition.Setup) and build its outpost (ChunkOutpost); a chunk still without
        // a ChunkDefinition after kScriptWaitFrames gets a warning. When none are left, navigation re-bakes
        // (so it sees the outpost pieces).
        private void FinishChunks()
        {
            for (int i = pending.Count - 1; i >= 0; i--)
            {
                PendingChunk item = pending[i];
                if (item.Chunk == null || !item.Chunk.IsAlive) { pending.RemoveAt(i); continue; }
                ChunkPlacement p = placements[item.Placement];
                ChunkRequest request = preset.Chunks[p.Request];
                ChunkDefinition definition = item.Chunk.GetComponent<ChunkDefinition>();
                if (definition == null)
                {
                    if (++item.Frames < kScriptWaitFrames) continue;
                    Log.Warning("SubLevelGenerator: the " + request.Role + " chunk prefab has no ChunkDefinition");
                    pending.RemoveAt(i);
                    continue;
                }
                definition.Setup(request.Role, p.Order, request.Footprint);
                ChunkOutpost outpost = item.Chunk.GetComponent<ChunkOutpost>();
                if (outpost != null) SpawnOutpost(item.Chunk, item.Placement, outpost);
                pending.RemoveAt(i);
            }
            if (pending.Count > 0) return;
            if (outpostPieces > 0) Log.Info("SubLevelGenerator: " + outpostPieces + " outpost pieces");
            Navigation.Rebake();
        }

        // One chunk's outpost (OutpostPlanner on its generated interior), spawned under the chunk.
        private void SpawnOutpost(Entity chunk, int index, ChunkOutpost outpost)
        {
            ChunkPlacement p = placements[index];
            ChunkRequest request = preset.Chunks[p.Request];
            if (!request.Footprint.Circle) return;
            List<OutpostItem> items = OutpostPlanner.Plan(map, seed ^ unchecked(0x2545f491 * (index + 1)), p, request.Footprint,
                ChunkInterior.Entrances(index, p, roads), request.Interior, outpost.ToSettings());
            foreach (OutpostItem item in items)
            {
                Prefab prefab = outpost.Piece(item.Slot, item.Piece);
                if (prefab == null) continue;
                Entity piece = prefab.Instantiate(chunk);
                if (piece == null) continue;
                piece.Transform.Position = new Vector3(item.X - p.X, item.Height - p.Height, item.Z - p.Z);
                piece.Transform.Rotation = Quaternion.Euler(0f, item.Yaw, 0f);
                piece.Transform.Scale = piece.Transform.Scale * item.Scale;
                outpostPieces++;
            }
        }

        // Play only, on a mission level: the scattered trees (DecorScatter, Unity's numbers), under the
        // Terrain like the chunks, turned and sized per item. Spawned before navigation re-bakes, so their
        // StaticObstacle trunk colliders block their cells. Replaced on every Generate. One log line.
        private void SpawnTrees()
        {
            foreach (Entity old in trees) if (old.IsAlive) Destroy(old);
            trees.Clear();
            if (preset == null) return;

            Prefab[] byTheme = { broadleafTree, pineTree, deadTree, burntTree };
            string[] names = { "broadleaf", "pine", "dead", "burnt" };
            int[] counts = new int[byTheme.Length];
            int skipped = 0;
            foreach (DecorItem item in DecorScatter.Scatter(map, seed ^ 0x7f4a7c15, preset.Decor, placements, preset.Chunks))
            {
                Prefab prefab = item.Theme < byTheme.Length ? byTheme[item.Theme] : null;
                if (prefab == null) { skipped++; continue; }
                Entity tree = prefab.Instantiate(terrain);
                if (tree == null) continue;
                tree.Transform.Position = new Vector3(item.X, item.Height, item.Z);
                tree.Transform.Rotation = Quaternion.Euler(0f, item.Yaw, 0f);
                tree.Transform.Scale = tree.Transform.Scale * item.Scale;
                trees.Add(tree);
                counts[item.Theme]++;
            }
            string line = "SubLevelGenerator: " + trees.Count + " trees (";
            for (int t = 0; t < names.Length; t++) line += (t > 0 ? ", " : "") + counts[t] + " " + names[t];
            Log.Info(line + ")");
            if (skipped > 0) Log.Warning("SubLevelGenerator: " + skipped + " trees skipped - set the tree prefab fields");
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
            foreach (Entity p in Players())
            {
                CharacterController controller = p.GetComponent<CharacterController>();
                if (controller != null) controller.SlopeLimit = maxWalkableSlope;
            }
        }

        /// <summary>
        /// The players: the `player` field if it is set to a live entity (test scenes), otherwise every entity
        /// tagged "Player" - so a level needs no link to its Player, and networked players count as they come.
        /// </summary>
        public List<Entity> Players()
        {
            List<Entity> list = new List<Entity>();
            if (player != null && player.IsAlive) { list.Add(player); return list; }
            foreach (Entity e in Entity.FindAllWithTag("Player")) if (e != null && e.IsAlive) list.Add(e);
            return list;
        }

        // Player i stands kPlayerSpacing metres round the first one (0 = on the spot), so several don't overlap.
        private static Vector3 PlayerOffset(int i)
        {
            if (i == 0) return Vector3.Zero;
            float a = (i - 1) * 2f * (float)System.Math.PI / 7f;
            return new Vector3((float)System.Math.Cos(a) * kPlayerSpacing, 0f, (float)System.Math.Sin(a) * kPlayerSpacing);
        }

        // On a mission level, on the spawn chunk's PlayerStart (or its centre), dropped from 2 m as in
        // Unity; otherwise on the walkable cell nearest the map's centre, standing on the ground.
        private bool PlacePlayer()
        {
            List<Entity> players = Players();
            if (players.Count == 0)
            {
                // Players may appear a little after the level (spawned); warn once if none come.
                if (++playerSearchFrames == 120) Log.Warning("SubLevelGenerator: no player - tag the Player entity \"Player\" (or set the player field)");
                return false;
            }
            Terrain ground = terrain.GetComponent<Terrain>();
            Vector3 origin = terrain.Transform.WorldPosition;

            if (spawnPlacement != null)
            {
                Vector3 offset = spawnStart != null && spawnStart.IsAlive ? spawnStart.Transform.Position : Vector3.Zero;
                for (int i = 0; i < players.Count; i++)
                {
                    Vector3 spread = PlayerOffset(i);
                    float x = origin.X + spawnPlacement.X + offset.X + spread.X, z = origin.Z + spawnPlacement.Z + offset.Z + spread.Z;
                    float y;
                    if (!ground.GroundHeight(x, z, out y)) return false;   // world transform not ready yet: try next frame
                    players[i].Transform.WorldPosition = new Vector3(x, System.Math.Max(y, origin.Y + spawnPlacement.Height + offset.Y) + kPlayerDrop, z);
                }
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

                        for (int i = 0; i < players.Count; i++)
                        {
                            Vector3 spread = PlayerOffset(i);
                            float x = origin.X + map.MinX + (col + 0.5f) * map.CellSize + spread.X;
                            float z = origin.Z + map.MinZ + (row + 0.5f) * map.CellSize + spread.Z;
                            float y;
                            if (!ground.GroundHeight(x, z, out y)) return false;   // world transform not ready yet: try next frame
                            players[i].Transform.WorldPosition = new Vector3(x, y + 1.5f, z);
                        }
                        return true;
                    }
            Log.Warning("SubLevelGenerator: no walkable cell for the player");
            return true;
        }
    }
}
