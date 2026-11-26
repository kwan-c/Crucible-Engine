/*************************************************************************
* \file:         OutpostPlanner.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Plans the pieces of a circular chunk's outpost on its
*                generated interior: which kind of spot each piece goes on,
*                how many, where, turned and sized how. Plain data (no
*                engine calls), so it can be tested alone.
*
* \details
*    Ported from the Unity prototype (Level/Generation/OutpostPlanner.cs),
*    algorithm unchanged. Each walkable cell inside 90% of the circle is
*    sorted into a slot:
*      WallEdge  next to a wall (an unwalkable neighbour)
*      High      on top of a block (at least one step up) and flat
*      Flat      flat open ground
*      Camp      a Flat cell inside the hold area (also still Flat)
*    The entry lanes (laneWidth plus laneClearance either side) are left
*    out. Then: one Centre piece in the middle, and for Camp, WallEdge,
*    High, Flat in that order a seeded count (min..max) of pieces on
*    shuffled cells of that slot, never closer than `spacing`. Each piece
*    is turned at random, scaled minScale..maxScale, and picked from its
*    slot's pieces by weight (OutpostItem.Piece indexes the slot's list).
*
*    To adjust: every number and each slot's pieces come from the chunk
*    prefab's ChunkOutpost script (OutpostSettings); the hold area, lane
*    width and step height come from the chunk's InteriorSettings.
*
* \history
*    -v1.0       ported from Unity
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public enum OutpostSlot { Centre, Flat, WallEdge, High, Camp }

    // One slot's rules: each piece's chance (0 = empty field) and how many pieces.
    public sealed class OutpostSlotSettings
    {
        public float[] weights = new float[0];
        public int min, max;
    }

    public sealed class OutpostSettings
    {
        public OutpostSlotSettings centre = new OutpostSlotSettings { min = 1, max = 1 };
        public OutpostSlotSettings flat = new OutpostSlotSettings { min = 3, max = 6 };
        public OutpostSlotSettings wallEdge = new OutpostSlotSettings { min = 4, max = 8 };
        public OutpostSlotSettings high = new OutpostSlotSettings { min = 1, max = 3 };
        public OutpostSlotSettings camp = new OutpostSlotSettings { min = 0, max = 0 };
        public float spacing = 6f;          // metres between pieces
        public float laneClearance = 2f;    // metres kept clear either side of each entry lane
        public float minScale = 0.9f, maxScale = 1.2f;

        public OutpostSlotSettings Slot(OutpostSlot slot)
        {
            return slot == OutpostSlot.Centre ? centre : slot == OutpostSlot.Camp ? camp
                 : slot == OutpostSlot.WallEdge ? wallEdge : slot == OutpostSlot.High ? high : flat;
        }
    }

    // One planned piece, in map space.
    public sealed class OutpostItem
    {
        public float X, Z, Height, Yaw, Scale;
        public OutpostSlot Slot;
        public int Piece;                   // index into the slot's pieces
    }

    public static class OutpostPlanner
    {
        public static List<OutpostItem> Plan(TerrainMap map, int seed, ChunkPlacement p, ChunkFootprint size,
                                             List<float[]> entrances, InteriorSettings interior, OutpostSettings s)
        {
            List<OutpostItem> items = new List<OutpostItem>();
            if (!size.Circle || s == null) return items;
            InteriorSettings shape = interior ?? new InteriorSettings();
            SeededRandom random = new SeededRandom(seed);
            List<int>[] cells = new List<int>[5];
            for (int i = 0; i < cells.Length; i++) cells[i] = new List<int>();

            float r = size.Radius * 0.9f, clearHalf = shape.laneWidth * 0.5f + Math.Max(0f, s.laneClearance);
            float camp = size.Radius * Math.Max(0f, shape.holdArea);
            for (int row = 0; row < map.Rows; row++)
                for (int col = 0; col < map.Cols; col++)
                {
                    float dx = CellX(map, col) - p.X, dz = CellZ(map, row) - p.Z;
                    if (dx * dx + dz * dz > r * r || !map.IsWalkable(col, row)) continue;
                    if (InEntryLane(dx, dz, entrances, clearHalf)) continue;
                    bool flat = Flat(map, col, row);
                    int cell = row * map.Cols + col;
                    if (NextToWall(map, col, row)) cells[(int)OutpostSlot.WallEdge].Add(cell);
                    else if (Rise(map, col, row, p.Height) >= shape.stepHeight - 0.25f && flat) cells[(int)OutpostSlot.High].Add(cell);
                    else if (flat)
                    {
                        cells[(int)OutpostSlot.Flat].Add(cell);
                        if (dx * dx + dz * dz <= camp * camp) cells[(int)OutpostSlot.Camp].Add(cell);
                    }
                }

            // The centre piece sits in the middle of the hold area.
            int centreCount = Count(s.centre, random);
            if (centreCount > 0 && Total(s.centre) > 0f)
                items.Add(Make(p.X, p.Z, map, OutpostSlot.Centre, s.centre, s, random));

            foreach (OutpostSlot slot in new[] { OutpostSlot.Camp, OutpostSlot.WallEdge, OutpostSlot.High, OutpostSlot.Flat })
            {
                OutpostSlotSettings rules = s.Slot(slot);
                int want = Count(rules, random);
                if (want <= 0 || Total(rules) <= 0f) continue;
                List<int> pool = cells[(int)slot];
                Shuffle(pool, random);
                foreach (int c in pool)
                {
                    if (want <= 0) break;
                    float x = CellX(map, c % map.Cols), z = CellZ(map, c / map.Cols);
                    if (TooClose(items, x, z, s.spacing)) continue;
                    items.Add(Make(x, z, map, slot, rules, s, random));
                    want--;
                }
            }
            return items;
        }

        private static OutpostItem Make(float x, float z, TerrainMap map, OutpostSlot slot, OutpostSlotSettings rules, OutpostSettings s,
                                        SeededRandom random)
        {
            return new OutpostItem
            {
                X = x, Z = z, Height = DecorScatter.HeightAt(map, x, z), Slot = slot,
                Yaw = random.NextFloat() * 360f,
                Scale = s.minScale + (s.maxScale - s.minScale) * random.NextFloat(),
                Piece = Pick(rules.weights, random.NextFloat()),
            };
        }

        private static int Count(OutpostSlotSettings rules, SeededRandom random)
        {
            int lo = Math.Max(0, Math.Min(rules.min, rules.max)), hi = Math.Max(0, Math.Max(rules.min, rules.max));
            return random.Next(lo, hi + 1);
        }

        private static float Total(OutpostSlotSettings rules)
        {
            float t = 0f;
            foreach (float w in rules.weights) t += Math.Max(0f, w);
            return t;
        }

        private static int Pick(float[] weights, float roll)
        {
            float v = roll * Math.Max(0.0001f, TotalOf(weights));
            for (int i = 0; i < weights.Length; i++)
            {
                float w = Math.Max(0f, weights[i]);
                if (v < w) return i;
                v -= w;
            }
            for (int i = weights.Length - 1; i >= 0; i--) if (weights[i] > 0f) return i;   // rounding: the last real piece
            return 0;
        }

        private static float TotalOf(float[] weights)
        {
            float t = 0f;
            foreach (float w in weights) t += Math.Max(0f, w);
            return t;
        }

        /// <summary>Inside an entry lane: within halfWidth of the line from an entrance to the centre.</summary>
        public static bool InEntryLane(float dx, float dz, List<float[]> entrances, float halfWidth)
        {
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

        private static bool NextToWall(TerrainMap map, int col, int row)
        {
            for (int d = 0; d < 4; d++)
            {
                int c = col + (d == 0 ? 1 : d == 1 ? -1 : 0), r = row + (d == 2 ? 1 : d == 3 ? -1 : 0);
                if (c < 0 || r < 0 || c >= map.Cols || r >= map.Rows || !map.IsWalkable(c, r)) return true;
            }
            return false;
        }

        private static bool Flat(TerrainMap map, int col, int row)
        {
            float a = map.Height(col, row), b = map.Height(col + 1, row), c = map.Height(col, row + 1), d = map.Height(col + 1, row + 1);
            return Math.Max(Math.Max(a, b), Math.Max(c, d)) - Math.Min(Math.Min(a, b), Math.Min(c, d)) <= 0.2f;
        }

        private static float Rise(TerrainMap map, int col, int row, float baseHeight)
        {
            return (map.Height(col, row) + map.Height(col + 1, row) + map.Height(col, row + 1) + map.Height(col + 1, row + 1)) * 0.25f - baseHeight;
        }

        private static bool TooClose(List<OutpostItem> items, float x, float z, float spacing)
        {
            foreach (OutpostItem i in items)
                if ((i.X - x) * (i.X - x) + (i.Z - z) * (i.Z - z) < spacing * spacing) return true;
            return false;
        }

        private static void Shuffle(List<int> list, SeededRandom random)
        {
            for (int i = list.Count - 1; i > 0; i--)
            {
                int j = random.Next(i + 1);
                int swap = list[i]; list[i] = list[j]; list[j] = swap;
            }
        }

        private static float CellX(TerrainMap map, int col) { return map.MinX + (col + 0.5f) * map.CellSize; }
        private static float CellZ(TerrainMap map, int row) { return map.MinZ + (row + 0.5f) * map.CellSize; }
    }
}
