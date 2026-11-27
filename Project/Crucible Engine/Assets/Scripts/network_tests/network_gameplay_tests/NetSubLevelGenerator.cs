/*************************************************************************
* \file:         NetSubLevelGenerator.cs
* \author:       Jean David Vidal Cruzado (SubLevelGenerator, the original);
*                Abhishek Akilandeswari Patchamuthu (this networked copy)
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: a copy of SubLevelGenerator that
*                builds the same ground on every machine and flattens a
*                spawn plaza and three region pads into it.
*
* \details
*    Changes from SubLevelGenerator (which is unchanged; its logic is still
*    being updated, so this is a copy):
*      - the seed: Network.SceneSeed in a session (the host picks it; the
*        lobby's Network.LoadScene sends it before the level loads), so
*        every machine generates the same map; offline as before
*      - flats: after generating, the plaza (plazaRadius, round the
*        terrain's centre) and, with regionPads, three pads padDistance away
*        (+X combat, -X zoo, +Z parts) and the corridors to them are levelled
*        to plazaHeight, blending into the hills over flatBlend metres; the
*        cells' walkability is recomputed there
*      - no single player to place: the scene places four player spawn
*        points on the plaza (at plazaHeight)
*    TerrainGenerator, CaveGenerator and SeededRandom are used unchanged.
*
* \history
*    -v1.0       copy of SubLevelGenerator v2.1: network seed, plaza and pads; a client
*                that built its map before joining rebuilds it with the session's seed
*
**************************************************************************/
namespace Crucible.@Scripts.NetGameplay
{
    public sealed class NetSubLevelGenerator : EntityScript
    {
        // --- seed ---------------------------------------------------------- //
        public int seed = 12345;                // the same seed always builds the same map
        public bool randomSeedOnPlay = false;   // pick a new seed each time Play starts

        // --- terrain (Unity's TerrainSettings) ------------------------------- //
        public float mapSize = 500f;            // metres, square
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

        // --- Flats for the networked test (plaza, region pads, corridors) ------------------- //
        public float plazaHeight = 6f;          // metres above the terrain entity: the spawn points stand here
        public float plazaRadius = 18f;
        public bool regionPads = true;          // combat (+X), zoo (-X) and parts (+Z) pads, joined to the plaza
        public float padDistance = 60f;         // metres from the centre to each pad's centre
        public float padRadius = 22f;
        public float corridorWidth = 12f;
        public float flatBlend = 16f;           // metres over which a flat blends back into the hills

        public override void OnCreate()
        {
            // In a session every machine must build the same map: the host's seed, never a random one.
            if (Network.HasSceneSeed) seed = Network.SceneSeed;
            else if (randomSeedOnPlay) seed = (int)new SeededRandom(System.Environment.TickCount).NextUInt();
            Generate();
        }

        public override void OnUpdate(float deltaTime)
        {
            // A client that pressed Play in this scene built its map before it joined (the lobby's
            // scene change sends the seed first, so that path never needs this): once the host's
            // seed is known and differs, the map is built again with it.
            if (Network.HasSceneSeed && seed != Network.SceneSeed)
            {
                Log.Info("NetSubLevelGenerator: rebuilding with the session's seed " + Network.SceneSeed + " (was " + seed + ")");
                seed = Network.SceneSeed;
                Generate();
            }
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
                Log.Error("NetSubLevelGenerator: no Terrain - set the terrain field or add a child called Terrain");
                return;
            }

            TerrainSettings terrainSettings = ToTerrainSettings();
            map = TerrainGenerator.Generate(seed, terrainSettings);
            if (caves) AddCaves(terrainSettings);
            Flatten();
            if (!ground.Build(map.Cols, map.Rows, map.CellSize, map.Heights, map.Regions(), map.Volume))
            {
                Log.Error("NetSubLevelGenerator: Terrain.Build refused the map");
                map = null;
                return;
            }

            if (Application.IsPlaying)
            {
                CheckNavigation();
                Navigation.Rebake();   // the players' slope limit is their own here (no single player)
            }

            int percent = map.WalkableCount * 100 / (map.Cols * map.Rows);
            Log.Info("NetSubLevelGenerator: " + (Application.IsPlaying ? "" : "preview, ") + (Network.HasSceneSeed ? "session " : "") + "seed " + seed + ", " + map.Cols + " x " + map.Rows + " cells, " + percent +
                     "% walkable, height hash " + map.HeightHash().ToString("X8"));
        }

        private TerrainSettings ToTerrainSettings()
        {
            TerrainSettings s = new TerrainSettings();
            s.mapSize = mapSize; s.cellSize = cellSize; s.hillSize = hillSize;
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
                Log.Warning("NetSubLevelGenerator: no cave - the box doesn't fit the map, or caveVoxelSize doesn't divide cellSize");
                return;
            }
            Log.Info("NetSubLevelGenerator: cave at (" + cave.CentreX.ToString("F0") + ", " + cave.CentreZ.ToString("F0") + "): base " +
                     cave.BaseHeight.ToString("F0") + " m (ground there varied " + cave.HeightRange.ToString("F0") + " m), mountain up to " +
                     cave.TopHeight.ToString("F0") + " m, " + cave.Rooms + " rooms (" + cave.DeepRooms + " deep), " + cave.Tunnels +
                     " tunnels (steepest " + cave.SteepestDegrees.ToString("F0") + " deg), " + cave.Entrances + " mouths, " + cave.Samples + " samples, " +
                     cave.Attempts + (cave.Attempts == 1 ? " layout" : " layouts") + " tried");
            if (cave.Rooms < caveRooms)
                Log.Warning("NetSubLevelGenerator: room for only " + cave.Rooms + " of " + caveRooms + " cave rooms (a ground-level room needs mountain over it)");
            if (cave.UnreachableRooms > 0)
                Log.Warning("NetSubLevelGenerator: " + cave.UnreachableRooms + " cave rooms can't be walked to from outside and back (best of " +
                            cave.Attempts + " layouts)");
        }

        // Navigation fits its grid to the terrain and reads its cliffs on its own (they come from
        // maxWalkableSlope above); this only warns when there is no Nav Surface to do it.
        private void CheckNavigation()
        {
            if (navSurface == null || navSurface.GetComponent<FlowFieldSurface>() == null)
                Log.Warning("NetSubLevelGenerator: no FlowFieldSurface - enemies won't path on the generated ground");
        }

        // Unity's ApplyPlayerSlope: the player climbs exactly what enemies can.
        private void ApplyPlayerSlope()
        {
            CharacterController controller = player != null ? player.GetComponent<CharacterController>() : null;
            if (controller != null) controller.SlopeLimit = maxWalkableSlope;
        }

        // On the walkable cell nearest the map's centre, standing on the ground.
        private bool PlacePlayer()
        {
            if (player == null) return true;
            Terrain ground = terrain.GetComponent<Terrain>();
            Vector3 origin = terrain.Transform.WorldPosition;

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
            Log.Warning("NetSubLevelGenerator: no walkable cell for the player");
            return true;
        }

        // --- the flats ---------------------------------------------------------------------- //

        // How far (metres) a point of the terrain's own space is outside the flats: 0 inside.
        private float DistanceToFlats(float x, float z)
        {
            float d = Circle(x, z, 0f, 0f, plazaRadius);
            if (!regionPads) return d;
            float[] px = { padDistance, -padDistance, 0f };
            float[] pz = { 0f, 0f, padDistance };
            for (int i = 0; i < 3; ++i)
            {
                d = System.Math.Min(d, Circle(x, z, px[i], pz[i], padRadius));
                d = System.Math.Min(d, Segment(x, z, px[i], pz[i], corridorWidth * 0.5f));
            }
            return d;
        }

        private static float Circle(float x, float z, float cx, float cz, float r)
        {
            float dx = x - cx, dz = z - cz;
            return System.Math.Max(0f, (float)System.Math.Sqrt(dx * dx + dz * dz) - r);
        }

        // Distance to the segment from the centre to (ex, ez), minus a half width.
        private static float Segment(float x, float z, float ex, float ez, float half)
        {
            float len2 = ex * ex + ez * ez;
            float t = len2 > 0f ? System.Math.Max(0f, System.Math.Min(1f, (x * ex + z * ez) / len2)) : 0f;
            float dx = x - ex * t, dz = z - ez * t;
            return System.Math.Max(0f, (float)System.Math.Sqrt(dx * dx + dz * dz) - half);
        }

        // Levels the flats to plazaHeight (blending back over flatBlend), then recomputes which cells
        // there can be walked on: level ones yes, the blend by its slope (maxWalkableSlope).
        private void Flatten()
        {
            if (map == null) return;
            int cols = map.Cols, rows = map.Rows;
            float cell = map.CellSize;
            float[] h = map.Heights;
            float[] weight = new float[(cols + 1) * (rows + 1)];
            for (int cz = 0; cz <= rows; ++cz)
                for (int cx = 0; cx <= cols; ++cx)
                {
                    float d = DistanceToFlats(map.MinX + cx * cell, map.MinZ + cz * cell);
                    float w = d <= 0f ? 1f : d >= flatBlend ? 0f : 1f - SmoothStep(d / flatBlend);
                    int i = cz * (cols + 1) + cx;
                    weight[i] = w;
                    if (w > 0f) h[i] = h[i] + (plazaHeight - h[i]) * w;
                }
            float maxRise = (float)System.Math.Tan(maxWalkableSlope * System.Math.PI / 180.0) * cell;
            for (int row = 0; row < rows; ++row)
                for (int col = 0; col < cols; ++col)
                {
                    int a = row * (cols + 1) + col, b = a + 1, c = a + cols + 1, e = c + 1;
                    if (weight[a] <= 0f && weight[b] <= 0f && weight[c] <= 0f && weight[e] <= 0f) continue;
                    float hi = System.Math.Max(System.Math.Max(h[a], h[b]), System.Math.Max(h[c], h[e]));
                    float lo = System.Math.Min(System.Math.Min(h[a], h[b]), System.Math.Min(h[c], h[e]));
                    map.Walkable[row * cols + col] = hi - lo <= maxRise;
                }
        }

        private static float SmoothStep(float t)
        {
            t = System.Math.Max(0f, System.Math.Min(1f, t));
            return t * t * (3f - 2f * t);
        }
    }
}
