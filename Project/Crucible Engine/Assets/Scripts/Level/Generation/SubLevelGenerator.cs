/*************************************************************************
* \file:         SubLevelGenerator.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
* \brief         Generates a sub-level from a seed. Terrain only for now:
*                chunks, roads and decor are later port steps.
*
* \details
*    The Unity prototype's SubLevelGenerator, rebuilt for Crucible. It
*    holds the settings (Unity's SubLevelGenSettings asset, as plain fields,
*    since Inspector lists and assets of settings aren't supported) and
*    hands each job to the entity that owns it:
*
*        Sub Level Generator   this script: seed and settings
*        |- Terrain            Terrain, MeshRenderer, Collider (HeightField)
*        '- Nav Surface        FlowFieldSurface: sized to the map here
*
*    Each run: TerrainGenerator builds the map (plain data), the Terrain
*    child draws and collides with it, the Nav Surface is sized to it with
*    the same slope limit, the player gets that limit too (so player and
*    enemies agree on what is a wall), and navigation re-bakes. Nothing is
*    saved: the same seed and settings always rebuild the same ground, and
*    the log prints a height hash to prove it.
*
*    Defaults are the values the Unity mission presets used
*    (LevelCatalogue/SubLevelGen_*.asset).
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class SubLevelGenerator : EntityScript
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

        // --- movement ------------------------------------------------------- //
        public float maxWalkableSlope = 35f;    // degrees: the default walker's limit, for the player and enemies

        // --- who does what (found by name under this entity when left empty) -- //
        public Entity terrain;                  // has Terrain, MeshRenderer, Collider (HeightField)
        public Entity navSurface;               // has FlowFieldSurface
        public Entity player;                   // has CharacterController; moved to the map's centre

        private TerrainMap map;
        private bool playerPlaced;

        public override void OnCreate()
        {
            if (randomSeedOnPlay) seed = (int)new SeededRandom(System.Environment.TickCount).NextUInt();
            if (terrain == null) terrain = Entity.FindChild("Terrain");
            if (navSurface == null) navSurface = Entity.FindChild("Nav Surface");
            Generate();
        }

        public override void OnUpdate(float deltaTime)
        {
            // The player is put on the ground once the terrain's world transform exists.
            if (!playerPlaced && map != null) playerPlaced = PlacePlayer();
        }

        /// <summary>Builds the ground from the seed and settings, and hands it to the terrain and navigation.</summary>
        public void Generate()
        {
            Terrain ground = terrain != null ? terrain.GetComponent<Terrain>() : null;
            if (ground == null)
            {
                Log.Error("SubLevelGenerator: no Terrain - set the terrain field or add a child called Terrain");
                return;
            }

            map = TerrainGenerator.Generate(seed, ToTerrainSettings());
            if (!ground.Build(map.Cols, map.Rows, map.CellSize, map.Heights, map.Regions()))
            {
                Log.Error("SubLevelGenerator: Terrain.Build refused the map");
                map = null;
                return;
            }

            ShareWithNavigation();
            ApplyPlayerSlope();
            Navigation.Rebake();

            int percent = map.WalkableCount * 100 / (map.Cols * map.Rows);
            Log.Info("SubLevelGenerator: seed " + seed + ", " + map.Cols + " x " + map.Rows + " cells, " + percent +
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

        // The navigation grid covers exactly the map (its entity's scale is its area) and walls
        // the same slopes the terrain generator called cliffs.
        private void ShareWithNavigation()
        {
            FlowFieldSurface surface = navSurface != null ? navSurface.GetComponent<FlowFieldSurface>() : null;
            if (surface == null)
            {
                Log.Warning("SubLevelGenerator: no FlowFieldSurface - enemies won't path on the generated ground");
                return;
            }
            float size = map.Cols * map.CellSize;
            navSurface.Transform.WorldPosition = terrain.Transform.WorldPosition;
            navSurface.Transform.Scale = new Vector3(size, 1f, size);
            surface.MaxWalkableSlope = maxWalkableSlope;
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
            Log.Warning("SubLevelGenerator: no walkable cell for the player");
            return true;
        }
    }
}
