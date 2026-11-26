/*************************************************************************
* \file:         ChunkInterior.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Shapes the ground inside circular chunks (Capture's areas):
*                blocks, slopes and walls around an open middle, with a
*                clear lane from every road entrance to the centre. Plain
*                data (no engine calls), so it can be tested alone.
*
* \details
*    Ported from the Unity prototype (Level/Generation/ChunkInterior.cs),
*    algorithm unchanged. For each placed circular chunk whose request has
*    InteriorSettings (MissionPresets; null = stays flat):
*      1. A seeded noise (featureSize across) is stepped into terraces of
*         stepHeight, up to maxHeight above the chunk's base: one step is
*         chest-high cover, two stacked steps are a wall (any step steeper
*         than the generator's maxWalkableSlope is a wall cell).
*      2. The hold area (the middle holdArea of the radius) gets at most
*         one step, and only where the noise peaks (holdCover).
*      3. Every road entrance gets a lane laneWidth wide to the centre,
*         kept at base level, so the middle can always be reached.
*      4. The outer edgeFade of the radius fades back to base, so roads
*         join smoothly.
*    Walkability is recomputed afterwards. Run it after RoadBuilder (it
*    needs the road entrances). CentreReachable is the check Unity used:
*    every entrance can walk to the centre.
*
*    To adjust: every number is in InteriorSettings, per chunk request in
*    MissionPresets; a chunk with no settings is left flat.
*
* \history
*    -v1.0       ported from Unity
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class InteriorSettings
    {
        public float featureSize = 20f;     // metres across a typical block or dip
        public float stepHeight = 3f;       // metres per step: one step = cover, stacked steps = walls
        public float maxHeight = 6f;        // metres: the tallest the interior rises above the chunk's base
        public float sharpness = 0.85f;     // 0 = smooth slopes, 1 = sheer steps
        public float holdArea = 0.3f;       // fraction of the radius kept as the open hold area in the middle
        public float holdCover = 0.5f;      // 0..1: how much light cover the hold area gets (one step at most)
        public float laneWidth = 8f;        // metres: lanes from each road entrance to the centre stay at base level
        public float edgeFade = 0.15f;      // fraction of the radius over which the interior fades back to base
    }

    public static class ChunkInterior
    {
        /// <summary>Shapes every circular chunk that has interior settings, then recomputes walkability.</summary>
        public static void Build(TerrainMap map, int seed, IList<ChunkPlacement> placed, IList<ChunkRequest> requests,
                                 IList<Road> roads, float maxWalkableSlope)
        {
            bool changed = false;
            for (int index = 0; index < placed.Count; index++)
            {
                ChunkPlacement p = placed[index];
                ChunkRequest request = requests[p.Request];
                InteriorSettings s = request.Interior;
                if (!request.Footprint.Circle || s == null) continue;
                changed = true;

                SeededNoise noise = new SeededNoise(seed ^ unchecked(0x5f356495 * (index + 1)));
                List<float[]> entrances = Entrances(index, p, roads);
                float r = request.Footprint.Radius, step = Math.Max(0.1f, s.stepHeight);
                int steps = Math.Max(1, (int)Math.Floor(Math.Max(step, s.maxHeight) / step));
                float scale = 1f / Math.Max(1f, s.featureSize);
                float fadeStart = 1f - Clamp01(s.edgeFade);

                for (int z = 0; z <= map.Rows; z++)
                    for (int x = 0; x <= map.Cols; x++)
                    {
                        float dx = map.MinX + x * map.CellSize - p.X, dz = map.MinZ + z * map.CellSize - p.Z;
                        float along = (float)Math.Sqrt(dx * dx + dz * dz) / r;   // 0 at the centre, 1 at the edge
                        if (along >= 1f) continue;
                        float raw = noise.Layered(dx * scale + 500f, dz * scale + 500f, 2, 0.5f, 2f);
                        raw = Clamp01((raw - 0.5f) * 2.2f + 0.5f);
                        float offset = along <= s.holdArea
                            ? TerrainGenerator.Terrace(raw * Clamp01(s.holdCover), 2, step, s.sharpness)   // light cover only
                            : TerrainGenerator.Terrace(raw, steps + 1, step, s.sharpness);
                        float fade = along <= fadeStart ? 1f : 1f - SmoothStep((along - fadeStart) / Math.Max(0.0001f, 1f - fadeStart));
                        if (CornerOfLaneCell(dx, dz, map.CellSize, entrances, s.laneWidth * 0.5f)) offset = 0f;
                        map.Heights[z * (map.Cols + 1) + x] = p.Height + offset * fade;
                    }
            }
            if (!changed) return;
            for (int row = 0; row < map.Rows; row++)
                for (int col = 0; col < map.Cols; col++)
                    map.Walkable[row * map.Cols + col] = TerrainGenerator.IsWalkable(map.Height(col, row), map.Height(col + 1, row),
                        map.Height(col, row + 1), map.Height(col + 1, row + 1), map.CellSize, maxWalkableSlope);
        }

        /// <summary>Where roads meet placement `index`'s edge, as offsets {x, z} from its centre.</summary>
        public static List<float[]> Entrances(int index, ChunkPlacement p, IList<Road> roads)
        {
            List<float[]> list = new List<float[]>();
            if (roads == null) return list;
            foreach (Road road in roads)
            {
                int last = road.X.Count - 1;
                if (road.From == index) list.Add(new[] { road.X[0] - p.X, road.Z[0] - p.Z });
                if (road.To == index) list.Add(new[] { road.X[last] - p.X, road.Z[last] - p.Z });
            }
            return list;
        }

        // A corner belongs to the lane when any of the four cells around it has its centre in the lane, so
        // every lane cell is fully at base level and the lane stays a connected walkable path at any angle.
        private static bool CornerOfLaneCell(float dx, float dz, float cell, List<float[]> entrances, float halfWidth)
        {
            float h = cell * 0.5f;
            return InLane(dx - h, dz - h, entrances, halfWidth) || InLane(dx + h, dz - h, entrances, halfWidth)
                || InLane(dx - h, dz + h, entrances, halfWidth) || InLane(dx + h, dz + h, entrances, halfWidth);
        }

        /// <summary>Within halfWidth of the centre, or of the line from an entrance to the centre.</summary>
        public static bool InLane(float dx, float dz, List<float[]> entrances, float halfWidth)
        {
            if (dx * dx + dz * dz <= halfWidth * halfWidth) return true;
            if (entrances == null) return false;
            foreach (float[] e in entrances)
            {
                float ex = e[0], ez = e[1], len = ex * ex + ez * ez;
                float t = len > 0f ? Math.Max(0f, Math.Min(1f, (dx * ex + dz * ez) / len)) : 0f;
                float px = ex * t - dx, pz = ez * t - dz;
                if (px * px + pz * pz <= halfWidth * halfWidth) return true;
            }
            return false;
        }

        /// <summary>True when every entrance can walk to the centre over walkable cells inside the chunk.</summary>
        public static bool CentreReachable(TerrainMap map, ChunkPlacement p, ChunkFootprint size, List<float[]> entrances)
        {
            int centre = CellAt(map, p.X, p.Z);
            if (centre < 0) return false;
            bool[] seen = new bool[map.Cols * map.Rows];
            Queue<int> queue = new Queue<int>();
            queue.Enqueue(centre);
            seen[centre] = true;
            while (queue.Count > 0)
            {
                int c = queue.Dequeue(), cx = c % map.Cols, cz = c / map.Cols;
                for (int d = 0; d < 4; d++)
                {
                    int nx = cx + (d == 0 ? 1 : d == 1 ? -1 : 0), nz = cz + (d == 2 ? 1 : d == 3 ? -1 : 0);
                    if (nx < 0 || nz < 0 || nx >= map.Cols || nz >= map.Rows) continue;
                    int n = nz * map.Cols + nx;
                    float wx = map.MinX + (nx + 0.5f) * map.CellSize - p.X, wz = map.MinZ + (nz + 0.5f) * map.CellSize - p.Z;
                    if (seen[n] || !map.Walkable[n] || !size.Inside(wx, wz, map.CellSize)) continue;
                    seen[n] = true;
                    queue.Enqueue(n);
                }
            }
            foreach (float[] e in entrances)
            {
                // The entrance sits on the edge; step one cell inwards so it lies inside the circle.
                float len = (float)Math.Sqrt(e[0] * e[0] + e[1] * e[1]);
                float inward = len > 0f ? Math.Max(0f, len - map.CellSize) / len : 0f;
                int cell = CellAt(map, p.X + e[0] * inward, p.Z + e[1] * inward);
                if (cell < 0 || !seen[cell]) return false;
            }
            return true;
        }

        private static int CellAt(TerrainMap map, float x, float z)
        {
            int c = (int)Math.Floor((x - map.MinX) / map.CellSize), r = (int)Math.Floor((z - map.MinZ) / map.CellSize);
            return c < 0 || r < 0 || c >= map.Cols || r >= map.Rows ? -1 : r * map.Cols + c;
        }

        private static float Clamp01(float v) { return Math.Max(0f, Math.Min(1f, v)); }
        private static float SmoothStep(float t) { t = Clamp01(t); return t * t * (3f - 2f * t); }
    }
}
