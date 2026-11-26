/*************************************************************************
* \file:         RoadBuilder.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Joins a mission's placed chunks with roads: finds each
*                road's route over the terrain grid with A*, evens out its
*                height, presses it into the ground and paints it as Road.
*                Plain data (no engine calls), so it can be tested alone.
*
* \details
*    Ported from the Unity prototype (Level/Generation/RoadBuilder.cs),
*    algorithm and numbers unchanged (Unity's SubLevelGen_KOTH settings).
*
*    Steps, for one map:
*      1. Links - which chunks a road joins.
*           Route (RoadSettings.chain): one road after another from the
*           start chunk (the spawn) to the nearest chunk not yet visited,
*           the end chunk (the extract) kept for last. Capture uses this:
*           spawn -> 1st -> 2nd -> 3rd -> extract (the same nearest-first
*           rule that numbers the capture areas, so the roads follow the
*           objective order).
*           Network (chain off, Unity's KOTH default): the shortest set of
*           links that joins every chunk (a minimum spanning tree, Prim's
*           algorithm) plus `extraLinks` of the shortest unused links, so
*           the map gets a loop.
*      2. Ends - where a road leaves each chunk. The point on the chunk's
*         edge facing the other chunk (a circle's facing point, a box's
*         facing side). The route search starts a little outside that
*         point ("lead-out", half a road + one cell) so the road leaves the
*         chunk straight out. Unity's prefabs could also give fixed path
*         connectors; ours have none yet (a later chunk step).
*      3. Route - A* over the terrain's cells (see FindRoute below for the
*         full walk-through). It goes round cliffs and other chunks and
*         prefers gentle ground. Only when there is no way round at all
*         does it go through a cliff (carveIfNoWayRound), which step 4
*         turns into a ramp.
*      4. Height - the route's heights are averaged over `smoothing` cells
*         either side, then every rise is kept under maxRoadSlope, with
*         the two ends held at their chunks' heights.
*      5. Press - ground within half a road of a road's centre line is set
*         to the road's height, faded into the hills over roadBlend
*         (smoothstep); those cells are marked Road (TerrainMap.Road,
*         drawn in the Terrain's road colour); walkability is recomputed.
*         Chunk footprints are left alone (already flattened).
*
*    A road that finds no route is skipped (the caller warns); a skipped
*    road never stops the level from building. The same map and chunks
*    always give the same roads (no randomness here).
*
* \history
*    -v1.0       ported from Unity: route and network links, A* routes,
*                carving, height evening, pressing. One fix: the road's
*                ends are held at their chunks' heights during the slope
*                sweeps (Unity reset the far end afterwards, which could
*                leave a steep last step)
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class RoadSettings
    {
        public bool chain = false;          // one route start -> nearest -> ... -> end, instead of a network
        public int extraLinks = 1;          // network only: loops added beyond the minimum network
        public float roadWidth = 12f;       // metres
        public float maxRoadSlope = 20f;    // degrees; roads are evened out to stay under this
        public float roadBlend = 8f;        // metres over which a road's edge fades into the terrain
        public float slopeCost = 4f;        // how strongly the route avoids slopes
        public float cliffCost = 10f;       // carved roads only: how strongly they still avoid crossing cliffs
        public bool carveIfNoWayRound = true; // when no route avoids cliffs, carve a ramp through instead of skipping the road
        public int smoothing = 3;           // cells either side averaged when evening out the road
        public float maxWalkableSlope = 35f; // the generator's; walkability is recomputed after pressing
    }

    // One road between two placed chunks: its centre line, as points with heights.
    public sealed class Road
    {
        public int From, To;                                       // indices into the placements
        public readonly List<float> X = new List<float>(), Z = new List<float>(), Height = new List<float>();
        public readonly List<int> RouteCells = new List<int>();    // the cells A* chose (row * cols + col)
        public bool Carved;                                        // no way round existed: it cuts through a cliff
    }

    public static class RoadBuilder
    {
        /// <summary>
        /// Builds the roads and presses them into the map. placed: the chunks (ChunkPlacer.Place);
        /// requests: their footprints and roles (Start = the route's first chunk, End = its last).
        /// Returns the roads built; links that found no route are missing from the list.
        /// </summary>
        public static List<Road> Build(TerrainMap map, IList<ChunkPlacement> placed, IList<ChunkRequest> requests, RoadSettings s)
        {
            List<Road> roads = new List<Road>();
            if (placed.Count < 2) return roads;
            float halfWidth = Math.Max(0.5f, s.roadWidth * 0.5f);
            float leadOut = halfWidth + map.CellSize;
            bool[] blocked = FootprintCells(map, placed, requests, halfWidth);

            int start = -1, end = -1;
            for (int i = 0; i < placed.Count; i++)
            {
                if (start < 0 && requests[placed[i].Request].Start) start = i;
                if (requests[placed[i].Request].End) end = i;
            }
            if (start < 0) start = 0;

            foreach (int[] link in s.chain ? Chain(placed, start, end) : Links(placed, start, s.extraLinks))
            {
                ChunkPlacement a = placed[link[0]], b = placed[link[1]];
                float ax, az, aOutX, aOutZ, bx, bz, bOutX, bOutZ;
                FacingEdge(a, requests[a.Request].Footprint, b, out ax, out az, out aOutX, out aOutZ);
                FacingEdge(b, requests[b.Request].Footprint, a, out bx, out bz, out bOutX, out bOutZ);
                float lax = ax + aOutX * leadOut, laz = az + aOutZ * leadOut;
                float lbx = bx + bOutX * leadOut, lbz = bz + bOutZ * leadOut;
                int from = Cell(map, lax, laz), to = Cell(map, lbx, lbz);

                List<int> route = FindRoute(map, from, to, blocked, s, false);
                bool carved = false;
                if (route == null && s.carveIfNoWayRound)
                {
                    route = FindRoute(map, from, to, blocked, s, true);
                    carved = route != null;
                }
                if (route == null) continue;

                Road road = new Road { From = link[0], To = link[1], Carved = carved };
                road.RouteCells.AddRange(route);
                Add(road, ax, az, a.Height);
                Add(road, lax, laz, a.Height);
                foreach (int c in route) Add(road, CellX(map, c), CellZ(map, c), CellHeight(map, c));
                Add(road, lbx, lbz, b.Height);
                Add(road, bx, bz, b.Height);
                EvenOut(road, s);
                roads.Add(road);
            }
            Press(map, roads, placed, requests, halfWidth, s);
            return roads;
        }

        /// <summary>How many links the settings ask for (to tell the caller how many roads were skipped).</summary>
        public static int LinkCount(int placedCount, RoadSettings s)
        {
            if (placedCount < 2) return 0;
            int tree = placedCount - 1;
            if (s.chain) return tree;
            int spare = placedCount * (placedCount - 1) / 2 - tree;
            return tree + Math.Min(Math.Max(0, s.extraLinks), spare);
        }

        // ---------------------------------------------------------------- 1. Links

        // One route from the start: always on to the nearest chunk not yet visited, the end chunk
        // (if any) kept for last. Each link runs From the earlier chunk To the later one.
        private static List<int[]> Chain(IList<ChunkPlacement> placed, int start, int end)
        {
            List<int[]> links = new List<int[]>();
            bool[] visited = new bool[placed.Count];
            int current = start;
            visited[start] = true;
            for (int step = 1; step < placed.Count; step++)
            {
                int next = -1;
                float best = float.PositiveInfinity;
                for (int i = 0; i < placed.Count; i++)
                {
                    if (visited[i] || (i == end && step < placed.Count - 1)) continue;
                    float d = Distance(placed[current], placed[i]);
                    if (d < best) { best = d; next = i; }
                }
                if (next < 0) break;
                visited[next] = true;
                links.Add(new[] { current, next });
                current = next;
            }
            return links;
        }

        // Minimum spanning tree over the chunk centres (Prim's: grow from the start, always adding the
        // shortest link to a chunk not yet joined), then the shortest unused links as loops.
        private static List<int[]> Links(IList<ChunkPlacement> placed, int start, int extra)
        {
            int n = placed.Count;
            List<int[]> links = new List<int[]>();
            bool[] inTree = new bool[n];
            inTree[start] = true;
            for (int added = 1; added < n; added++)
            {
                int bestA = -1, bestB = -1;
                float best = float.PositiveInfinity;
                for (int i = 0; i < n; i++)
                {
                    if (!inTree[i]) continue;
                    for (int j = 0; j < n; j++)
                    {
                        if (inTree[j]) continue;
                        float d = Distance(placed[i], placed[j]);
                        if (d < best) { best = d; bestA = i; bestB = j; }
                    }
                }
                inTree[bestB] = true;
                links.Add(new[] { bestA, bestB });
            }

            List<int[]> spare = new List<int[]>();
            for (int i = 0; i < n; i++)
                for (int j = i + 1; j < n; j++)
                    if (!Linked(links, i, j)) spare.Add(new[] { i, j });
            spare.Sort(delegate (int[] p, int[] q)
            {
                return Distance(placed[p[0]], placed[p[1]]).CompareTo(Distance(placed[q[0]], placed[q[1]]));
            });
            for (int k = 0; k < Math.Min(Math.Max(0, extra), spare.Count); k++) links.Add(spare[k]);
            return links;
        }

        private static bool Linked(List<int[]> links, int i, int j)
        {
            foreach (int[] l in links)
                if ((l[0] == i && l[1] == j) || (l[0] == j && l[1] == i)) return true;
            return false;
        }

        // ---------------------------------------------------------------- 2. Ends

        // Where a road leaves `self` towards `other`: the point on its edge facing the other chunk
        // (x, z, in map space) and the outward direction (outX, outZ, unit length).
        private static void FacingEdge(ChunkPlacement self, ChunkFootprint size, ChunkPlacement other,
                                       out float x, out float z, out float outX, out float outZ)
        {
            float dx = other.X - self.X, dz = other.Z - self.Z;
            if (size.Circle)
            {
                float length = (float)Math.Sqrt(dx * dx + dz * dz);
                outX = length > 0.001f ? dx / length : 0f;
                outZ = length > 0.001f ? dz / length : 1f;
                x = self.X + outX * size.Radius;
                z = self.Z + outZ * size.Radius;
                return;
            }
            // A box: the middle of the side the other chunk is beyond.
            if (Math.Abs(dx) * size.Depth >= Math.Abs(dz) * size.Width)
            {
                outX = Math.Sign(dx); outZ = 0f;
                x = self.X + outX * size.Width * 0.5f; z = self.Z;
            }
            else
            {
                outX = 0f; outZ = Math.Sign(dz);
                x = self.X; z = self.Z + outZ * size.Depth * 0.5f;
            }
        }

        // Cells a road may not route through: chunk footprints, grown by half a road so a road
        // never clips a chunk's edge.
        private static bool[] FootprintCells(TerrainMap map, IList<ChunkPlacement> placed, IList<ChunkRequest> requests, float pad)
        {
            bool[] blocked = new bool[map.Cols * map.Rows];
            for (int c = 0; c < blocked.Length; c++)
                foreach (ChunkPlacement p in placed)
                    if (requests[p.Request].Footprint.Inside(CellX(map, c) - p.X, CellZ(map, c) - p.Z, pad)) { blocked[c] = true; break; }
            return blocked;
        }

        // ---------------------------------------------------------------- 3. Route (A*)

        // A* (A-star) search for the cheapest chain of cells from `start` to `goal`.
        //
        // The terrain grid is treated as a graph: every cell is a node, joined to its 8 neighbours
        // (4 straight, 4 diagonal). Moving to a neighbour costs:
        //
        //     cost = step * (1 + slopeCost * (rise / step) / tan(maxRoadSlope))
        //
        //   step  = distance between the two cell centres (cellSize, or cellSize * 1.414 diagonally);
        //   rise  = height difference between the two cells (each cell's height = average of its
        //           4 corners).
        //   (rise / step) is the slope's tangent, so dividing by tan(maxRoadSlope) gives "how steep
        //   compared to the steepest a road may be": flat ground costs just its distance, ground at
        //   the road's limit costs (1 + slopeCost) = 5 times its distance. The route therefore
        //   trades a longer way round for gentler ground, as a real road would.
        //
        //   Cells that can't be used:
        //     - chunk footprints (`blocked`), except the goal cell itself;
        //     - cliffs (unwalkable cells). When no route avoids them (throughCliffs = true, the
        //       carving retry) a cliff is allowed but costs cliffCost (10) times as much, so the
        //       route still crosses as few cliff cells as it can.
        //
        // How the search runs:
        //   g[c]     = the cheapest cost found so far from start to cell c (infinity = not reached).
        //   came[c]  = the cell we arrived at c from on that cheapest way (to trace the route back).
        //   open     = a priority queue (min-heap) of cells to look at next, ordered by
        //                  f = g[c] + h(c)
        //              where h(c) is the straight-line distance from c to the goal (the "heuristic").
        //   closed[c]= c has been expanded: its cheapest cost is final.
        //
        //   1. Put the start in `open` with g = 0.
        //   2. Take the cell with the lowest f out of `open` (skip it if already closed). If it is
        //      the goal, stop.
        //   3. Close it, and for each usable neighbour work out g[current] + cost. If that beats
        //      the neighbour's g, record it (g, came) and push the neighbour with its new f.
        //   4. Repeat from 2 until the goal comes out or `open` is empty (no route: return null).
        //   5. Follow `came` back from the goal to the start and reverse it: that is the route.
        //
        // Why A* finds the cheapest route: h never overestimates (a straight line is the shortest
        // possible way, and every step costs at least its distance), so when the goal is taken out
        // of `open` no cheaper way to it can still be waiting. Compared with Dijkstra (h = 0), the
        // heuristic makes the search reach towards the goal first instead of spreading evenly in
        // every direction, so far fewer cells are looked at. Pushing a cell again with a better
        // f instead of updating it in the heap ("lazy deletion") is why step 2 skips closed cells.
        private static List<int> FindRoute(TerrainMap map, int start, int goal, bool[] blocked, RoadSettings s, bool throughCliffs)
        {
            if (start < 0 || goal < 0) return null;
            int count = map.Cols * map.Rows;
            float[] g = new float[count];
            int[] came = new int[count];
            bool[] closed = new bool[count];
            for (int i = 0; i < count; i++) { g[i] = float.PositiveInfinity; came[i] = -1; }

            MinHeap open = new MinHeap();
            g[start] = 0f;
            open.Push(start, Heuristic(map, start, goal));
            float tanMax = (float)Math.Tan(Math.Max(1f, s.maxRoadSlope) * Math.PI / 180.0);

            while (open.Count > 0)
            {
                int current = open.Pop();
                if (current == goal) break;
                if (closed[current]) continue;
                closed[current] = true;

                int cx = current % map.Cols, cz = current / map.Cols;
                for (int dz = -1; dz <= 1; dz++)
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        if (dx == 0 && dz == 0) continue;
                        int nx = cx + dx, nz = cz + dz;
                        if (nx < 0 || nz < 0 || nx >= map.Cols || nz >= map.Rows) continue;
                        int next = nz * map.Cols + nx;
                        if (closed[next] || (blocked[next] && next != goal)) continue;

                        float step = map.CellSize * (dx != 0 && dz != 0 ? 1.41421356f : 1f);
                        float rise = Math.Abs(CellHeight(map, next) - CellHeight(map, current));
                        float cost = step * (1f + s.slopeCost * (rise / step) / tanMax);
                        if (!map.IsWalkable(nx, nz))
                        {
                            if (!throughCliffs && next != goal) continue;
                            cost *= Math.Max(1f, s.cliffCost);
                        }

                        float total = g[current] + cost;
                        if (total >= g[next]) continue;
                        g[next] = total;
                        came[next] = current;
                        open.Push(next, total + Heuristic(map, next, goal));
                    }
            }
            if (came[goal] < 0 && goal != start) return null;

            List<int> path = new List<int>();
            for (int c = goal; c >= 0; c = came[c])
            {
                path.Add(c);
                if (c == start) break;
            }
            path.Reverse();
            return path;
        }

        // Straight-line distance between two cell centres: A*'s estimate of the cost still to go.
        private static float Heuristic(TerrainMap map, int a, int b)
        {
            float dx = CellX(map, a) - CellX(map, b), dz = CellZ(map, a) - CellZ(map, b);
            return (float)Math.Sqrt(dx * dx + dz * dz);
        }

        // ---------------------------------------------------------------- 4. Height

        // Averages the route's heights (the two points at each end stay on their chunk), then keeps
        // every rise under maxRoadSlope, sweeping forwards from the start chunk and backwards from the
        // end chunk, with both ends held at their chunks' heights. Where the route crossed a cliff this
        // is what makes the carved ramp. (A road too short for the height between its chunks can't
        // stay under the limit everywhere; its steepest part is then next to the end chunk.)
        private static void EvenOut(Road road, RoadSettings s)
        {
            int n = road.Height.Count;
            float[] h = road.Height.ToArray();
            float[] smoothed = (float[])h.Clone();
            int w = Math.Max(0, s.smoothing);
            for (int i = 2; i < n - 2; i++)
            {
                float sum = 0f;
                int k = 0;
                for (int j = Math.Max(2, i - w); j <= Math.Min(n - 3, i + w); j++) { sum += h[j]; k++; }
                smoothed[i] = sum / k;
            }
            float tanMax = (float)Math.Tan(Math.Max(1f, s.maxRoadSlope) * Math.PI / 180.0);
            // The ends stay at their chunks' heights throughout (Unity let the forward sweep move the
            // far end and only put it back afterwards, which could leave a steep last step).
            for (int i = 1; i < n - 1; i++) Limit(road, smoothed, i, i - 1, tanMax);
            for (int i = n - 2; i >= 1; i--) Limit(road, smoothed, i, i + 1, tanMax);
            for (int i = 0; i < n; i++) road.Height[i] = smoothed[i];
        }

        // Point i may differ from point `from` by at most (distance between them) * tan(maxRoadSlope).
        private static void Limit(Road road, float[] h, int i, int from, float tanMax)
        {
            float run = (float)Math.Sqrt(Sq(road.X[i] - road.X[from]) + Sq(road.Z[i] - road.Z[from]));
            float rise = run * tanMax;
            h[i] = Math.Max(h[from] - rise, Math.Min(h[from] + rise, h[i]));
        }

        // ---------------------------------------------------------------- 5. Press

        // Sets the ground under each road to the road's height, fades the sides, marks road cells and
        // recomputes walkability. Chunk footprints are skipped: they are already flat.
        private static void Press(TerrainMap map, List<Road> roads, IList<ChunkPlacement> placed, IList<ChunkRequest> requests,
                                  float halfWidth, RoadSettings s)
        {
            if (roads.Count == 0) return;
            float blend = Math.Max(0.01f, s.roadBlend);
            for (int z = 0; z <= map.Rows; z++)
                for (int x = 0; x <= map.Cols; x++)
                {
                    float wx = map.MinX + x * map.CellSize, wz = map.MinZ + z * map.CellSize;
                    if (InAnyFootprint(wx, wz, placed, requests)) continue;
                    float distance, roadHeight;
                    if (!Nearest(roads, wx, wz, out distance, out roadHeight) || distance >= halfWidth + blend) continue;
                    int i = z * (map.Cols + 1) + x;
                    if (distance <= halfWidth) { map.Heights[i] = roadHeight; continue; }
                    float t = (distance - halfWidth) / blend;
                    float keep = t * t * (3f - 2f * t);     // 0 at the road's edge, 1 at the blend's edge
                    map.Heights[i] = roadHeight + (map.Heights[i] - roadHeight) * keep;
                }

            for (int c = 0; c < map.Cols * map.Rows; c++)
            {
                float cx = CellX(map, c), cz = CellZ(map, c);
                float d, unused;
                map.Road[c] = !InAnyFootprint(cx, cz, placed, requests) && Nearest(roads, cx, cz, out d, out unused) && d <= halfWidth;
            }
            for (int row = 0; row < map.Rows; row++)
                for (int col = 0; col < map.Cols; col++)
                    map.Walkable[row * map.Cols + col] = TerrainGenerator.IsWalkable(map.Height(col, row), map.Height(col + 1, row),
                        map.Height(col, row + 1), map.Height(col + 1, row + 1), map.CellSize, s.maxWalkableSlope);
        }

        private static bool InAnyFootprint(float x, float z, IList<ChunkPlacement> placed, IList<ChunkRequest> requests)
        {
            foreach (ChunkPlacement p in placed)
                if (requests[p.Request].Footprint.Inside(x - p.X, z - p.Z, 0f)) return true;
            return false;
        }

        // Distance from (x, z) to the nearest road centre line, and that road's height there.
        private static bool Nearest(List<Road> roads, float x, float z, out float distance, out float height)
        {
            distance = float.PositiveInfinity;
            height = 0f;
            foreach (Road road in roads)
                for (int i = 1; i < road.X.Count; i++)
                {
                    float ax = road.X[i - 1], az = road.Z[i - 1], bx = road.X[i], bz = road.Z[i];
                    float vx = bx - ax, vz = bz - az, len = vx * vx + vz * vz;
                    float t = len > 0f ? Math.Max(0f, Math.Min(1f, ((x - ax) * vx + (z - az) * vz) / len)) : 0f;
                    float px = ax + vx * t, pz = az + vz * t;
                    float d = (float)Math.Sqrt(Sq(x - px) + Sq(z - pz));
                    if (d < distance) { distance = d; height = road.Height[i - 1] + (road.Height[i] - road.Height[i - 1]) * t; }
                }
            return !float.IsPositiveInfinity(distance);
        }

        // ---------------------------------------------------------------- helpers

        private static void Add(Road road, float x, float z, float height) { road.X.Add(x); road.Z.Add(z); road.Height.Add(height); }

        private static int Cell(TerrainMap map, float x, float z)
        {
            int col = (int)Math.Floor((x - map.MinX) / map.CellSize), row = (int)Math.Floor((z - map.MinZ) / map.CellSize);
            if (col < 0 || row < 0 || col >= map.Cols || row >= map.Rows) return -1;
            return row * map.Cols + col;
        }

        private static float CellX(TerrainMap map, int c) { return map.MinX + (c % map.Cols + 0.5f) * map.CellSize; }
        private static float CellZ(TerrainMap map, int c) { return map.MinZ + (c / map.Cols + 0.5f) * map.CellSize; }

        // A cell's height: the average of its 4 corners.
        private static float CellHeight(TerrainMap map, int c)
        {
            int col = c % map.Cols, row = c / map.Cols;
            return (map.Height(col, row) + map.Height(col + 1, row) + map.Height(col, row + 1) + map.Height(col + 1, row + 1)) * 0.25f;
        }

        private static float Distance(ChunkPlacement a, ChunkPlacement b) { return (float)Math.Sqrt(Sq(a.X - b.X) + Sq(a.Z - b.Z)); }
        private static float Sq(float v) { return v * v; }

        // A small binary min-heap for A*'s open list: Pop always returns the item with the lowest priority.
        private sealed class MinHeap
        {
            private readonly List<int> items = new List<int>();
            private readonly List<float> priorities = new List<float>();
            public int Count { get { return items.Count; } }

            public void Push(int item, float priority)
            {
                items.Add(item);
                priorities.Add(priority);
                int i = items.Count - 1;
                while (i > 0)
                {
                    int parent = (i - 1) / 2;
                    if (priorities[parent] <= priorities[i]) break;
                    Swap(i, parent);
                    i = parent;
                }
            }

            public int Pop()
            {
                int top = items[0];
                int last = items.Count - 1;
                Swap(0, last);
                items.RemoveAt(last);
                priorities.RemoveAt(last);
                int i = 0;
                while (true)
                {
                    int l = i * 2 + 1, r = l + 1, smallest = i;
                    if (l < items.Count && priorities[l] < priorities[smallest]) smallest = l;
                    if (r < items.Count && priorities[r] < priorities[smallest]) smallest = r;
                    if (smallest == i) break;
                    Swap(i, smallest);
                    i = smallest;
                }
                return top;
            }

            private void Swap(int a, int b)
            {
                int item = items[a]; items[a] = items[b]; items[b] = item;
                float p = priorities[a]; priorities[a] = priorities[b]; priorities[b] = p;
            }
        }
    }
}
