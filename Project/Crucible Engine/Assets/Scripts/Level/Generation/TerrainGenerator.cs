/*************************************************************************
* \file:         TerrainGenerator.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.1
* \brief         Seeded terraced terrain as plain data: TerrainSettings in,
*                TerrainMap out. No engine calls, so it can be tested alone.
*
* \details
*    Ported from the Unity prototype (Level/Generation/TerrainGenerator.cs),
*    algorithm unchanged:
*      1. Layered Perlin noise gives rolling ground, stretched by
*         heightSpread so it reaches every level.
*      2. Heights are rounded towards terrace levels: where the noise rises
*         gently the step becomes a wide ramp, where it rises sharply it
*         stays a cliff.
*      3. A second, much larger noise decides where the map is terraced
*         with cliffs and where it stays smooth (cliffCoverage), blending
*         over regionBlend; smooth regions keep smoothAreaTerracing.
*      4. Any cell steeper than maxWalkableSlope is a wall (a cliff).
*    Changes: SeededRandom instead of System.Random (so seeds give different
*    maps than in Unity), and TerrainMap.Regions() for Terrain.Build.
*
*    The map is centred on the origin of the entity it is built on, the
*    same layout as the engine's Terrain: (cols + 1) * (rows + 1) corner
*    heights, row-major.
*
* \history
*    -v1.0       ported from Unity
*    -v1.1       TerrainMap.Tunnel (set by CaveGenerator), painted as the
*                Tunnel region
**************************************************************************/
namespace Crucible.@Scripts
{
    // What the terrain generator needs, as plain data (SubLevelGenerator fills it in).
    public sealed class TerrainSettings
    {
        public float mapSize = 500f;            // metres, square, centred on the origin
        public float cellSize = 4f;             // metres (one golem)
        public float hillSize = 160f;           // metres across a typical hill
        public int noiseLayers = 4;
        public float layerStrength = 0.5f;      // how much each smaller layer adds (persistence)
        public float layerDetail = 2f;          // how much smaller each layer is (lacunarity)
        public float heightSpread = 2.2f;       // stretches the noise so it reaches every level
        public int terraceLevels = 4;
        public float levelHeight = 12f;         // metres between levels
        public float terraceSharpness = 0.75f;  // 0 = smooth hills, 1 = sharp steps
        public float maxWalkableSlope = 35f;    // degrees; steeper cells are walls (cliffs)
        public float cliffCoverage = 0.75f;     // roughly how much of the map is terraced with cliffs
        public float variationSize = 350f;      // metres across a cliffy or smooth region
        public float regionBlend = 0.25f;       // width of the change from cliffy to smooth
        public float smoothAreaTerracing = 0.4f; // how much terracing smooth regions keep (0..1)
    }

    // The generated terrain: a height per grid corner, walkable/road per cell.
    public sealed class TerrainMap
    {
        public int Cols { get; private set; }
        public int Rows { get; private set; }
        public float CellSize { get; private set; }
        public float MinX { get; private set; }    // the grid's -X edge, in the terrain entity's space
        public float MinZ { get; private set; }    // the grid's -Z edge
        public float[] Heights { get; private set; }   // (Cols + 1) * (Rows + 1) corners
        public bool[] Walkable { get; private set; }   // Cols * Rows cells
        public bool[] Road { get; private set; }       // Cols * Rows cells, set by the road builder later
        public bool[] Tunnel { get; private set; }     // Cols * Rows cells, set by the cave feature (CaveGenerator)

        public TerrainMap(int cols, int rows, float cellSize, float minX, float minZ)
        {
            Cols = cols; Rows = rows; CellSize = cellSize; MinX = minX; MinZ = minZ;
            Heights = new float[(cols + 1) * (rows + 1)];
            Walkable = new bool[cols * rows];
            Road = new bool[cols * rows];
            Tunnel = new bool[cols * rows];
        }

        public float Height(int cornerX, int cornerZ) { return Heights[cornerZ * (Cols + 1) + cornerX]; }
        public bool IsWalkable(int col, int row) { return Walkable[row * Cols + col]; }
        public bool IsRoad(int col, int row) { return Road[row * Cols + col]; }
        public bool IsTunnel(int col, int row) { return Tunnel[row * Cols + col]; }

        // True on cliffs and anywhere off the map (x, z in the map's own space).
        public bool WallAt(float x, float z)
        {
            int col = (int)System.Math.Floor((x - MinX) / CellSize), row = (int)System.Math.Floor((z - MinZ) / CellSize);
            if (col < 0 || row < 0 || col >= Cols || row >= Rows) return true;
            return !IsWalkable(col, row);
        }

        public int WalkableCount
        {
            get { int n = 0; foreach (bool w in Walkable) if (w) n++; return n; }
        }

        // One TerrainRegion per cell, for Terrain.Build: a road wins over everything, then a wall is
        // a cliff (navigation reads Cliff as a wall, so a tunnel never opens one), then tunnel, then ground.
        public byte[] Regions()
        {
            byte[] regions = new byte[Cols * Rows];
            for (int i = 0; i < regions.Length; i++)
                regions[i] = (byte)(Road[i] ? TerrainRegion.Road : !Walkable[i] ? TerrainRegion.Cliff
                                  : Tunnel[i] ? TerrainRegion.Tunnel : TerrainRegion.Ground);
            return regions;
        }

        // FNV-1a over every height's bits: the same seed and settings must always give the same value.
        public uint HeightHash()
        {
            uint hash = 2166136261u;
            foreach (float h in Heights)
            {
                int bits = System.BitConverter.ToInt32(System.BitConverter.GetBytes(h), 0);
                unchecked { hash = (hash ^ (uint)bits) * 16777619u; }
            }
            return hash;
        }
    }

    public static class TerrainGenerator
    {
        public static TerrainMap Generate(int seed, TerrainSettings s)
        {
            float cell = System.Math.Max(0.5f, s.cellSize);
            int count = System.Math.Max(1, (int)System.Math.Round(s.mapSize / cell));
            float half = count * cell * 0.5f;
            TerrainMap map = new TerrainMap(count, count, cell, -half, -half);

            SeededNoise noise = new SeededNoise(seed);
            SeededRandom offsets = new SeededRandom(seed ^ 0x5bd1e995);
            float offsetX = offsets.NextFloat() * 1000f, offsetZ = offsets.NextFloat() * 1000f;
            float scale = 1f / System.Math.Max(1f, s.hillSize);
            SeededNoise regions = new SeededNoise(seed ^ 0x1b873593);
            float regionScale = 1f / System.Math.Max(1f, s.variationSize);
            float top = System.Math.Max(1, s.terraceLevels - 1) * s.levelHeight;
            float threshold = 1f - Clamp01(s.cliffCoverage);

            for (int z = 0; z <= count; z++)
                for (int x = 0; x <= count; x++)
                {
                    float wx = map.MinX + x * cell, wz = map.MinZ + z * cell;
                    float raw = noise.Layered(wx * scale + offsetX, wz * scale + offsetZ, s.noiseLayers, s.layerStrength, s.layerDetail);
                    raw = Clamp01((raw - 0.5f) * System.Math.Max(0.1f, s.heightSpread) + 0.5f);
                    float terraced = Terrace(raw, s.terraceLevels, s.levelHeight, s.terraceSharpness);
                    float region = regions.Layered(wx * regionScale + offsetZ, wz * regionScale + offsetX, 2, 0.5f, 2f);
                    region = Clamp01((region - 0.5f) * 2.2f + 0.5f);
                    float blendHalf = System.Math.Max(0.01f, s.regionBlend);
                    float cliffy = SmoothStep(threshold - blendHalf, threshold + blendHalf, region);   // 1 = terraced, 0 = smooth
                    float floor = Clamp01(s.smoothAreaTerracing);
                    float terracing = floor + (1f - floor) * cliffy;
                    float smooth = raw * top;
                    map.Heights[z * (count + 1) + x] = smooth + (terraced - smooth) * terracing;
                }

            for (int row = 0; row < count; row++)
                for (int col = 0; col < count; col++)
                    map.Walkable[row * count + col] = IsWalkable(map.Height(col, row), map.Height(col + 1, row),
                        map.Height(col, row + 1), map.Height(col + 1, row + 1), cell, s.maxWalkableSlope);
            return map;
        }

        // 0..1 noise to a height on (or between) terrace levels. Levels sit at 0, levelHeight, 2x...
        public static float Terrace(float raw01, int levels, float levelHeight, float sharpness)
        {
            int steps = System.Math.Max(1, levels - 1);
            float t = Clamp01(raw01) * steps;
            int level = System.Math.Min(steps - 1, (int)System.Math.Floor(t));
            float within = t - level;                    // 0..1 between this level and the next
            float band = System.Math.Max(0.02f, 1f - Clamp01(sharpness));
            float a = 0.5f - band * 0.5f, b = 0.5f + band * 0.5f;
            float k = Clamp01((within - a) / (b - a));
            float shaped = k * k * (3f - 2f * k);        // smooth step across the band
            return (level + shaped) * levelHeight;
        }

        // A cell is walkable when the rise across it is no steeper than the limit.
        public static bool IsWalkable(float h00, float h10, float h01, float h11, float cellSize, float maxSlopeDegrees)
        {
            float high = System.Math.Max(System.Math.Max(h00, h10), System.Math.Max(h01, h11));
            float low = System.Math.Min(System.Math.Min(h00, h10), System.Math.Min(h01, h11));
            double slope = System.Math.Atan2(high - low, cellSize) * 180.0 / System.Math.PI;
            return slope <= maxSlopeDegrees;
        }

        private static float SmoothStep(float a, float b, float v)
        {
            float k = Clamp01((v - a) / System.Math.Max(0.0001f, b - a));
            return k * k * (3f - 2f * k);
        }

        private static float Clamp01(float v) { return v < 0f ? 0f : (v > 1f ? 1f : v); }
    }
}
