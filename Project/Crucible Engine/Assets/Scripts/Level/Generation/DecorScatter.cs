/*************************************************************************
* \file:         DecorScatter.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Scatters decor (trees for now) over a generated map, as
*                plain data (no engine calls), so it can be tested alone.
*
* \details
*    Ported from the Unity prototype (Level/Generation/DecorScatter.cs),
*    algorithm and numbers unchanged (Unity's SubLevelGen_KOTH "Trees"):
*      - candidate spots sit on a grid `spacing` apart, each jittered
*        inside its grid square; no two items closer than `spacing`;
*      - a large noise (clusterSize across) forms woods and clearings: a
*        spot is kept with chance density x (1 - clusterStrength +
*        clusterStrength x wood), so most trees stand in woods;
*      - spots on cliffs, ground steeper than maxSlope, within
*        roadClearance of a road, within chunkClearance of a chunk or
*        within startClearance of the start chunk (the spawn) are skipped;
*      - every random number for a spot is drawn up front, so a spot's
*        outcome depends only on the seed.
*    Unity let each group list its prefabs with weights. Here each item
*    gets a theme (DecorItem.Theme) instead:
*
*    TEMPORARY - quadrant themes, until each level's theme is decided:
*      the map is split into 4 quadrants, each favouring one theme
*      (themeFavour of the picks, the rest shared by the other themes);
*      the seed rotates which quadrant gets which theme, and the themes
*      blend across a themeBlend strip along the quadrant edges. All of
*      it is in ThemeWeights, so it can be replaced on its own.
*
* \history
*    -v1.0       ported from Unity; quadrant themes (temporary)
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class DecorSettings
    {
        public int themes = 4;              // how many themes items are picked from (Broadleaf, Pine, Dead, Burnt)
        public float spacing = 6f;          // metres: no two items closer than this
        public float density = 0.15f;       // 0..1 chance an available spot gets an item, before clustering
        public float clusterStrength = 0.85f; // 0 = even spread, 1 = only in woods (clearings stay empty)
        public float clusterSize = 120f;    // metres across a wood or clearing
        public float roadClearance = 4f;    // metres kept clear beside roads
        public float chunkClearance = 6f;   // metres kept clear around chunks
        public float startClearance = 30f;  // metres kept clear around the start chunk
        public float maxSlope = 30f;        // degrees; nothing on ground steeper than this (or on cliffs)
        public float minScale = 0.8f, maxScale = 1.3f;
        public float themeFavour = 0.7f;    // TEMPORARY quadrants: share of picks that go to the quadrant's own theme
        public float themeBlend = 40f;      // TEMPORARY quadrants: metres across the strip where neighbouring themes mix
    }

    // One placed item: where (map space), on what height, turned and sized how, and its theme.
    public sealed class DecorItem
    {
        public float X, Z, Height, Yaw, Scale;
        public int Theme;
    }

    public static class DecorScatter
    {
        public static List<DecorItem> Scatter(TerrainMap map, int seed, DecorSettings g, IList<ChunkPlacement> placed, IList<ChunkRequest> requests)
        {
            List<DecorItem> items = new List<DecorItem>();
            int themes = Math.Max(1, g.themes);
            SeededRandom random = new SeededRandom(seed);
            SeededNoise woods = new SeededNoise(seed ^ 0x3c6ef372);
            int themeTurn = new SeededRandom(seed ^ 0x6c8e9cf5).Next(themes);   // which quadrant gets which theme
            float spacing = Math.Max(0.5f, g.spacing);
            float minX = map.MinX, minZ = map.MinZ, size = map.Cols * map.CellSize;
            float centreX = minX + size * 0.5f, centreZ = minZ + size * 0.5f;
            int grid = Math.Max(1, (int)Math.Floor(size / spacing));
            DecorItem[] taken = new DecorItem[grid * grid];
            float tanSlope = (float)Math.Tan(Math.Max(0f, Math.Min(89f, g.maxSlope)) * Math.PI / 180.0);
            float[] weights = new float[themes];

            for (int gz = 0; gz < grid; gz++)
                for (int gx = 0; gx < grid; gx++)
                {
                    // Draw every number up front so each spot's outcome depends only on the seed.
                    float jx = random.NextFloat(), jz = random.NextFloat(), roll = random.NextFloat();
                    float pick = random.NextFloat(), yaw = random.NextFloat(), scale = random.NextFloat();
                    float x = minX + (gx + jx) * spacing, z = minZ + (gz + jz) * spacing;

                    float wood = woods.Layered(x / Math.Max(1f, g.clusterSize), z / Math.Max(1f, g.clusterSize), 2, 0.5f, 2f);
                    wood = SmoothStep(0.42f, 0.58f, wood);
                    float chance = Math.Max(0f, g.density) * (1f - g.clusterStrength + g.clusterStrength * wood);
                    if (roll >= chance) continue;
                    if (!Usable(map, x, z, tanSlope, g.roadClearance)) continue;
                    if (NearChunk(x, z, placed, requests, g)) continue;
                    if (TooClose(taken, grid, gx, gz, x, z, spacing)) continue;

                    ThemeWeights(x - centreX, z - centreZ, themeTurn, g, weights);
                    DecorItem item = new DecorItem
                    {
                        X = x, Z = z, Height = HeightAt(map, x, z),
                        Yaw = yaw * 360f, Scale = g.minScale + (g.maxScale - g.minScale) * scale, Theme = Pick(weights, pick),
                    };
                    taken[gz * grid + gx] = item;
                    items.Add(item);
                }
            return items;
        }

        // TEMPORARY (until level themes are decided): how likely each theme is at (dx, dz) from the
        // map's centre. Quadrant q (0 = -X-Z, 1 = +X-Z, 2 = -X+Z, 3 = +X+Z) favours theme (q + turn) %
        // themes; near the centre lines the four quadrants' weights are blended over themeBlend metres.
        private static void ThemeWeights(float dx, float dz, int turn, DecorSettings g, float[] weights)
        {
            int themes = weights.Length;
            float own = Math.Max(0f, Math.Min(1f, g.themeFavour));
            float other = themes > 1 ? (1f - own) / (themes - 1) : 0f;
            float half = Math.Max(0.01f, g.themeBlend * 0.5f);
            float east = SmoothStep(-half, half, dx), north = SmoothStep(-half, half, dz);   // 0..1 towards +X, +Z
            for (int t = 0; t < themes; t++) weights[t] = 0f;
            for (int q = 0; q < 4; q++)
            {
                float influence = ((q & 1) != 0 ? east : 1f - east) * ((q & 2) != 0 ? north : 1f - north);
                int favoured = (q + turn) % themes;
                for (int t = 0; t < themes; t++) weights[t] += influence * (t == favoured ? own : other);
            }
        }

        // Walkable, gentle enough, and no road cell within the clearance.
        private static bool Usable(TerrainMap map, float x, float z, float tanSlope, float roadClearance)
        {
            int col = (int)Math.Floor((x - map.MinX) / map.CellSize), row = (int)Math.Floor((z - map.MinZ) / map.CellSize);
            if (col < 0 || row < 0 || col >= map.Cols || row >= map.Rows || !map.IsWalkable(col, row)) return false;
            float h00 = map.Height(col, row), h10 = map.Height(col + 1, row), h01 = map.Height(col, row + 1), h11 = map.Height(col + 1, row + 1);
            float rise = Math.Max(Math.Max(h00, h10), Math.Max(h01, h11)) - Math.Min(Math.Min(h00, h10), Math.Min(h01, h11));
            if (rise > tanSlope * map.CellSize) return false;
            int reach = (int)Math.Ceiling(Math.Max(0f, roadClearance) / map.CellSize);
            for (int dz = -reach; dz <= reach; dz++)
                for (int dx = -reach; dx <= reach; dx++)
                {
                    int c = col + dx, r = row + dz;
                    if (c >= 0 && r >= 0 && c < map.Cols && r < map.Rows && map.IsRoad(c, r)) return false;
                }
            return true;
        }

        // Inside a chunk's footprint grown by its clearance (more round the start chunk).
        private static bool NearChunk(float x, float z, IList<ChunkPlacement> placed, IList<ChunkRequest> requests, DecorSettings g)
        {
            foreach (ChunkPlacement p in placed)
            {
                ChunkRequest request = requests[p.Request];
                float pad = request.Start ? g.startClearance : g.chunkClearance;
                if (request.Footprint.Inside(x - p.X, z - p.Z, Math.Max(0f, pad))) return true;
            }
            return false;
        }

        // Another item within `spacing` in this grid square or the 8 around it.
        private static bool TooClose(DecorItem[] taken, int grid, int gx, int gz, float x, float z, float spacing)
        {
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++)
                {
                    int cx = gx + dx, cz = gz + dz;
                    if (cx < 0 || cz < 0 || cx >= grid || cz >= grid) continue;
                    DecorItem other = taken[cz * grid + cx];
                    if (other != null && (other.X - x) * (other.X - x) + (other.Z - z) * (other.Z - z) < spacing * spacing) return true;
                }
            return false;
        }

        /// <summary>Ground height at a point (map space), blended from the four corners of its cell.</summary>
        public static float HeightAt(TerrainMap map, float x, float z)
        {
            float fx = (x - map.MinX) / map.CellSize, fz = (z - map.MinZ) / map.CellSize;
            int col = Math.Max(0, Math.Min(map.Cols - 1, (int)Math.Floor(fx))), row = Math.Max(0, Math.Min(map.Rows - 1, (int)Math.Floor(fz)));
            float tx = Math.Max(0f, Math.Min(1f, fx - col)), tz = Math.Max(0f, Math.Min(1f, fz - row));
            float a = map.Height(col, row) + (map.Height(col + 1, row) - map.Height(col, row)) * tx;
            float b = map.Height(col, row + 1) + (map.Height(col + 1, row + 1) - map.Height(col, row + 1)) * tx;
            return a + (b - a) * tz;
        }

        // Index picked by weight: roll 0..1 over the weights' total.
        private static int Pick(float[] weights, float roll)
        {
            float total = 0f;
            foreach (float w in weights) total += Math.Max(0f, w);
            float r = roll * total;
            for (int i = 0; i < weights.Length; i++)
            {
                float w = Math.Max(0f, weights[i]);
                if (r < w) return i;
                r -= w;
            }
            return weights.Length - 1;
        }

        private static float SmoothStep(float a, float b, float v)
        {
            float k = Math.Max(0f, Math.Min(1f, (v - a) / (b - a)));
            return k * k * (3f - 2f * k);
        }
    }
}
