/*************************************************************************
* \file:         ChunkPlacer.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          26 November 2026
* \version       1.2
* \brief         Places a mission's chunks on a TerrainMap and flattens the
*                ground under them, as plain data (no engine calls), so it
*                can be tested alone.
*
* \details
*    Ported from the Unity prototype (Level/Generation/ChunkPlacer.cs):
*      - each chunk tries `candidates` random spots inside the edge margin;
*      - a spot is refused if its footprint comes closer than the spacing
*        to a placed chunk's footprint (spacing is edge to edge);
*      - the Start chunk (the spawn) goes first, on the flattest spot in
*        the middle third of the map;
*      - every other chunk scores high for spreading out (distance to the
*        nearest placed chunk) and low for uneven ground (flatnessWeight
*        per metre of height range under the footprint), so the chunks
*        fill the map evenly;
*      - the End chunk (the extract) scores on distance from the Start
*        instead: as far from it as it can;
*      - the chunk sits on the terrace level nearest its ground's average;
*      - Flatten sets each footprint to its height, fades the edge into
*        the land around it over blendDistance (smoothstep), and re-marks
*        walkable cells, so the faded edges become ramps.
*    M3 changes (GDD side quests):
*      - "Spacing range" is Unity's minSpacing given as a range (Capture
*        50 - 70): each level picks its minimum gap in the range from the
*        seed (SpacingFor), so some maps are more spread out than others;
*      - ordered chunks (Capture's 1st, 2nd, 3rd) are numbered after
*        placing as a route: 1st = nearest the Start, 2nd = nearest the
*        1st, and so on (Unity had a single objective);
*      - a chunk that can't be placed is skipped (the caller warns), as
*        in Unity: every seed is usable as-is.
*    The same seed always places the same chunks.
*
* \history
*    -v1.0       ported from Unity, with M3's spacing range and links
*    -v1.1       journey layout: Start chunk near the edge, chain heads away from it
*    -v1.2       back to Unity's layout (spread out, Start in the middle third,
*                End far from the Start); the spacing range is a minimum gap
*                picked per seed; links removed; ordered chunks numbered as a route
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public enum ChunkShape { Box, Circle }

    // A chunk's footprint in metres: a box (Width by Depth) or a circle (Width is the diameter).
    // Every "how far from / inside this chunk" question goes through here.
    public struct ChunkFootprint
    {
        public readonly float Width, Depth;
        public readonly bool Circle;
        public float Radius { get { return Width * 0.5f; } }

        public ChunkFootprint(ChunkShape shape, float width, float depth)
        {
            Width = Math.Max(1f, width);
            Circle = shape == ChunkShape.Circle;
            Depth = Circle ? Width : Math.Max(1f, depth);
        }

        // dx, dz: offset from the chunk's centre. Inside, with the footprint grown by pad metres.
        public bool Inside(float dx, float dz, float pad)
        {
            return Circle ? dx * dx + dz * dz <= (Radius + pad) * (Radius + pad)
                          : Math.Abs(dx) <= Width * 0.5f + pad && Math.Abs(dz) <= Depth * 0.5f + pad;
        }

        // Distance from the footprint's edge (0 inside it).
        public float OutsideDistance(float dx, float dz)
        {
            if (Circle) return Math.Max(0f, (float)Math.Sqrt(dx * dx + dz * dz) - Radius);
            float ox = Math.Max(0f, Math.Abs(dx) - Width * 0.5f), oz = Math.Max(0f, Math.Abs(dz) - Depth * 0.5f);
            return (float)Math.Sqrt(ox * ox + oz * oz);
        }

        // Gap between two footprints (negative when they overlap); b sits at (dx, dz) from a.
        public static float Gap(ChunkFootprint a, ChunkFootprint b, float dx, float dz)
        {
            if (a.Circle && b.Circle) return (float)Math.Sqrt(dx * dx + dz * dz) - a.Radius - b.Radius;
            if (a.Circle) return b.OutsideDistance(-dx, -dz) - a.Radius;
            if (b.Circle) return a.OutsideDistance(dx, dz) - b.Radius;
            return Math.Max(Math.Abs(dx) - (a.Width + b.Width) * 0.5f, Math.Abs(dz) - (a.Depth + b.Depth) * 0.5f);
        }
    }

    // One chunk a mission wants, in the order they are placed.
    public sealed class ChunkRequest
    {
        public string Role;                 // "Spawn", "Extract", "Capture", ... (MissionPresets)
        public int Order;                   // > 0: one of an ordered set (Capture's areas), numbered as a route after placing
        public ChunkFootprint Footprint;
        public bool Start;                  // Unity's start chunk (the spawn): flattest spot in the middle third, placed first
        public bool End;                    // Unity's end chunk (the extract): as far from the Start as it can
    }

    // Where one chunk went: its centre and the ground height it sits on, in the map's space.
    public sealed class ChunkPlacement
    {
        public float X, Z, Height;
        public int Request;                 // index into the requests passed to Place
        public int Order;                   // its place on the route (1st, 2nd, 3rd); 0 for chunks with no order
    }

    public sealed class ChunkPlacementSettings
    {
        public float minSpacing = 50f;      // metres of gap between chunk footprints (edge to edge): the range's low end
        public float minSpacingHigh = 50f;  // the range's high end; each level picks its gap in [minSpacing, minSpacingHigh]
        public float edgeMargin = 20f;      // metres kept clear at the map border
        public float blendDistance = 24f;   // metres over which a flattened footprint fades into the hills
        public int candidates = 300;        // random spots tried per chunk
        public float flatnessWeight = 10f;  // how much uneven ground counts against a spot, versus spreading out
        public float levelHeight = 12f;     // flattened chunks snap to a terrace level
        public int terraceLevels = 4;
        public float maxWalkableSlope = 35f;
    }

    public static class ChunkPlacer
    {
        /// <summary>This level's minimum gap between chunks: picked in the spacing range from the seed.</summary>
        public static float SpacingFor(int seed, ChunkPlacementSettings s)
        {
            float high = Math.Max(s.minSpacing, s.minSpacingHigh);
            return new SeededRandom(seed ^ 0x51ac3d07).Range(s.minSpacing, high);
        }

        public static List<ChunkPlacement> Place(TerrainMap map, int seed, IList<ChunkRequest> requests, ChunkPlacementSettings s)
        {
            List<ChunkPlacement> placed = new List<ChunkPlacement>();
            SeededRandom random = new SeededRandom(seed ^ 0x2c1b3c6d);
            float spacing = SpacingFor(seed, s);
            float minX = map.MinX, maxX = map.MinX + map.Cols * map.CellSize;
            float minZ = map.MinZ, maxZ = map.MinZ + map.Rows * map.CellSize;
            float centreX = (minX + maxX) * 0.5f, centreZ = (minZ + maxZ) * 0.5f;
            float mapHalf = (maxX - minX) * 0.5f;
            ChunkPlacement start = null;

            for (int r = 0; r < requests.Count; r++)
            {
                ChunkRequest request = requests[r];
                ChunkFootprint size = request.Footprint;
                float loX = minX + s.edgeMargin + size.Width * 0.5f, hiX = maxX - s.edgeMargin - size.Width * 0.5f;
                float loZ = minZ + s.edgeMargin + size.Depth * 0.5f, hiZ = maxZ - s.edgeMargin - size.Depth * 0.5f;
                if (loX > hiX || loZ > hiZ) continue;
                bool isStart = request.Start && start == null;
                bool isEnd = request.End && start != null;

                ChunkPlacement best = null;
                float bestScore = float.NegativeInfinity;
                for (int c = 0; c < Math.Max(1, s.candidates); c++)
                {
                    float x = random.Range(loX, hiX);
                    float z = random.Range(loZ, hiZ);
                    if (isStart)
                    {
                        // The start stays within the middle third of the map.
                        x = Math.Max(loX, Math.Min(hiX, random.Range(centreX - mapHalf / 3f, centreX + mapHalf / 3f)));
                        z = Math.Max(loZ, Math.Min(hiZ, random.Range(centreZ - mapHalf / 3f, centreZ + mapHalf / 3f)));
                    }
                    if (Overlaps(x, z, size, placed, requests, spacing)) continue;

                    float low, high;
                    Range(map, x, z, size, out low, out high);
                    float score = -(high - low) * s.flatnessWeight;
                    if (isEnd) score += (float)Math.Sqrt((x - start.X) * (x - start.X) + (z - start.Z) * (z - start.Z));
                    else if (!isStart) score += NearestDistance(x, z, placed);
                    if (score > bestScore) { bestScore = score; best = new ChunkPlacement { X = x, Z = z, Request = r, Order = request.Order }; }
                }
                if (best == null) continue;
                best.Height = TargetHeight(map, best.X, best.Z, size, s);
                placed.Add(best);
                if (isStart) start = best;
            }
            NumberRoute(placed, start);
            return placed;
        }

        // Ordered chunks get their numbers as a route: 1st = nearest the start, then each next one
        // nearest the last. Without a start they keep the order they were asked for.
        private static void NumberRoute(List<ChunkPlacement> placed, ChunkPlacement start)
        {
            if (start == null) return;
            List<ChunkPlacement> left = new List<ChunkPlacement>();
            foreach (ChunkPlacement p in placed) if (p.Order > 0) left.Add(p);
            float fromX = start.X, fromZ = start.Z;
            for (int order = 1; left.Count > 0; order++)
            {
                int nearest = 0;
                float best = float.PositiveInfinity;
                for (int i = 0; i < left.Count; i++)
                {
                    float d = (left[i].X - fromX) * (left[i].X - fromX) + (left[i].Z - fromZ) * (left[i].Z - fromZ);
                    if (d < best) { best = d; nearest = i; }
                }
                left[nearest].Order = order;
                fromX = left[nearest].X; fromZ = left[nearest].Z;
                left.RemoveAt(nearest);
            }
        }

        // Sets each footprint to its height and fades the edge into the surrounding ground, then
        // re-marks walkable cells (the faded edges become ramps up to the chunk).
        public static void Flatten(TerrainMap map, IList<ChunkPlacement> placed, IList<ChunkRequest> requests, ChunkPlacementSettings s)
        {
            float blend = Math.Max(0.01f, s.blendDistance);
            foreach (ChunkPlacement p in placed)
            {
                ChunkFootprint size = requests[p.Request].Footprint;
                for (int z = 0; z <= map.Rows; z++)
                    for (int x = 0; x <= map.Cols; x++)
                    {
                        float wx = map.MinX + x * map.CellSize, wz = map.MinZ + z * map.CellSize;
                        float outside = size.OutsideDistance(wx - p.X, wz - p.Z);
                        if (outside >= blend) continue;
                        float t = outside / blend;
                        float keep = t * t * (3f - 2f * t);   // 0 at the footprint, 1 at the blend's edge
                        int i = z * (map.Cols + 1) + x;
                        map.Heights[i] = p.Height + (map.Heights[i] - p.Height) * keep;
                    }
            }
            for (int row = 0; row < map.Rows; row++)
                for (int col = 0; col < map.Cols; col++)
                    map.Walkable[row * map.Cols + col] = TerrainGenerator.IsWalkable(map.Height(col, row), map.Height(col + 1, row),
                        map.Height(col, row + 1), map.Height(col + 1, row + 1), map.CellSize, s.maxWalkableSlope);
        }

        // The terrace level nearest the footprint's average height.
        private static float TargetHeight(TerrainMap map, float x, float z, ChunkFootprint size, ChunkPlacementSettings s)
        {
            double sum = 0; int count = 0;
            ForCorners(map, x, z, size, delegate (float h) { sum += h; count++; });
            float average = count > 0 ? (float)(sum / count) : 0f;
            float level = (float)Math.Round(average / Math.Max(0.01f, s.levelHeight));
            level = Math.Max(0f, Math.Min(Math.Max(1, s.terraceLevels) - 1, level));
            return level * s.levelHeight;
        }

        private static void Range(TerrainMap map, float x, float z, ChunkFootprint size, out float low, out float high)
        {
            float lo = float.PositiveInfinity, hi = float.NegativeInfinity;
            ForCorners(map, x, z, size, delegate (float h) { if (h < lo) lo = h; if (h > hi) hi = h; });
            low = float.IsPositiveInfinity(lo) ? 0f : lo;
            high = float.IsNegativeInfinity(hi) ? 0f : hi;
        }

        private static void ForCorners(TerrainMap map, float x, float z, ChunkFootprint size, Action<float> visit)
        {
            int x0 = Clamp((int)Math.Ceiling((x - size.Width * 0.5f - map.MinX) / map.CellSize), 0, map.Cols);
            int x1 = Clamp((int)Math.Floor((x + size.Width * 0.5f - map.MinX) / map.CellSize), 0, map.Cols);
            int z0 = Clamp((int)Math.Ceiling((z - size.Depth * 0.5f - map.MinZ) / map.CellSize), 0, map.Rows);
            int z1 = Clamp((int)Math.Floor((z + size.Depth * 0.5f - map.MinZ) / map.CellSize), 0, map.Rows);
            for (int cz = z0; cz <= z1; cz++)
                for (int cx = x0; cx <= x1; cx++)
                    if (size.Inside(map.MinX + cx * map.CellSize - x, map.MinZ + cz * map.CellSize - z, 0f)) visit(map.Height(cx, cz));
        }

        public static bool Overlaps(float x, float z, ChunkFootprint size, IList<ChunkPlacement> placed, IList<ChunkRequest> requests, float spacing)
        {
            foreach (ChunkPlacement p in placed)
                if (ChunkFootprint.Gap(requests[p.Request].Footprint, size, x - p.X, z - p.Z) < spacing) return true;
            return false;
        }

        private static float NearestDistance(float x, float z, IList<ChunkPlacement> placed)
        {
            float nearest = float.PositiveInfinity;
            foreach (ChunkPlacement p in placed)
                nearest = Math.Min(nearest, (float)Math.Sqrt((x - p.X) * (x - p.X) + (z - p.Z) * (z - p.Z)));
            return float.IsPositiveInfinity(nearest) ? 0f : nearest;
        }

        private static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    }
}
