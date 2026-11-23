/*************************************************************************
* \file:         CaveGenerator.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          23 November 2026
* \version       1.2
* \brief         Cave systems as plain data: an optional generator feature
*                that raises a rock hill and carves tunnel networks through
*                it on a TerrainMap. No engine calls, so it can be tested
*                alone.
*
* \details
*    A cave system is a rock hill with a network of tunnels through it at
*    ground level, opening to the outside. Seen from above, the network is
*    the Finstinct cavern (CSD3183): "wall" is rock, "water" is tunnel.
*    Per zone:
*      1. Place: caveCount circular zones at seeded spots, away from the
*         map edge and from each other (the whole set is placed again when
*         one doesn't fit).
*      2. Random fill: inside the zone, each cell starts solid with
*         probability caveFill; everything past the hill's full-height core
*         is always solid, so tunnels stay inside the hill and its sloping
*         sides are rock (Finstinct's sealed border).
*      3. Smooth: caveSmoothing passes of the 4-5 rule over the 8
*         neighbours (a solid cell stays solid with 4+ solid neighbours, an
*         open one turns solid with 5+), rim re-forced after every pass.
*      4. Minimum width: an open cell stays only if it is part of a fully
*         open 2 x 2 block, so every passage is at least two cells wide.
*      5. Flood fill: the zone keeps its largest network (4-connected);
*         smaller pockets are filled back in.
*    The plan is TerrainMap.Tunnel. Then:
*      6. Entrances: entrances passages, spread evenly round the zone
*         from a seeded angle; each runs from the tunnel cell furthest out
*         that way to past the hill's edge, two to three cells wide.
*      7. Base: corners within the zone are set to one height - the
*         terrace level nearest their average - blending back into the
*         land around over blendWidth. This is the ground round the hill
*         and the tunnels' floor.
*      8. Hill: the zone's corners rise by hillHeight (plus noise), full
*         height over the core (60% of the radius) and easing down to the
*         base at the zone's edge. Never lower than tunnelHeight + roofThickness + the noise,
*         so a roof always fits over a tunnel inside the full-height part.
*      9. Tunnels: every corner of a tunnel cell goes back down to the
*         base, so tunnel floors are flat and level with the ground
*         outside; the rock cells beside them slope up to the hill (the
*         walls). A rock cell inside the hill left with all four corners
*         on the floor is floor too, so it joins the tunnel.
*     10. Reachability: a flood fill from the tunnel cells outside the
*         hill; tunnel cells it misses are counted (should be none).
*     11. Walkability is recomputed with the terrain's slope limit.
*    Roofs are a later step: until then the tunnels are open canyons.
*
*    Placement takes the flattest of 16 free spots (least height
*    difference inside the zone), so levelling changes as little as
*    possible. Its own random streams (seed ^ 0x27d4eb2f, hill noise
*    seed ^ 0x165667b1), so the ground outside the zones is never touched.
*
* \history
*    -v1.0       tunnel plan (steps 1-5)
*    -v1.1       flattest placement, plateau and trenches
*    -v1.2       rock hill with tunnels at ground level, entrances and a
*                reachability check (replaces plateau and trenches)
**************************************************************************/
namespace Crucible.@Scripts
{
    // What the cave feature needs, as plain data (SubLevelGenerator fills it in).
    public sealed class CaveSettings
    {
        public int caveCount = 2;               // cave systems per map
        public float zoneSize = 160f;           // metres across each system's circular zone (the hill)
        public float fill = 0.45f;              // chance a cell starts solid (Finstinct's 45%)
        public int smoothing = 5;               // passes of the 4-5 rule
        public float tunnelHeight = 6f;         // metres from the tunnel floor to the roof's underside
        public float roofThickness = 2f;        // metres: the least rock over a tunnel
        public float blendWidth = 12f;          // metres over which the base blends into the land around
        public float hillHeight = 14f;          // metres the hill rises above the base in its middle
        public float hillNoise = 4f;            // metres up or down the hill's top varies
        public float hillNoiseSize = 60f;       // metres across a bump on the hill's top
        public int entrances = 3;               // passages from each tunnel network to the outside
    }

    // One cave system: where it is and what the plan kept.
    public sealed class CaveZone
    {
        public int CentreCol { get; internal set; }
        public int CentreRow { get; internal set; }
        public int Diameter { get; internal set; }       // cells
        public int TunnelCells { get; internal set; }
        public int CulledByWidth { get; internal set; }  // open cells in no open 2 x 2 block
        public int CulledPockets { get; internal set; }  // open cells outside the largest network
        public float HeightRange { get; internal set; }  // metres between the zone's lowest and highest ground before flattening
        public float BaseHeight { get; internal set; }   // the ground round the hill and the tunnels' floor
        public float TopHeight { get; internal set; }    // the hill's highest point
        public int Entrances { get; internal set; }      // passages cut to the outside
        public int EntranceCells { get; internal set; }  // tunnel cells the passages added
        public int Unreachable { get; internal set; }    // tunnel cells no entrance leads to (should be 0)
    }

    public static class CaveGenerator
    {
        private const int kCandidates = 16;     // free spots compared per zone; the flattest wins
        private const int kPlacementRounds = 8; // tries at fitting every zone
        private const float kCore = 0.6f;       // the hill is full height out to this fraction of its radius,
                                                // and the tunnel network stays inside it; the rest is hillside

        /// <summary>
        /// Adds the cave systems to the map: plans the tunnels and their entrances (map.Tunnel), shapes
        /// the base and the rock hill in map.Heights with the tunnels at the base, and updates
        /// map.Walkable. Returns the zones placed (fewer than asked when the map has no room for more).
        /// </summary>
        public static CaveZone[] Generate(TerrainMap map, int seed, CaveSettings s, TerrainSettings terrain)
        {
            SeededRandom random = new SeededRandom(seed ^ 0x27d4eb2f);
            int diameter = System.Math.Max(8, (int)System.Math.Round(s.zoneSize / map.CellSize));
            int flat = diameter / 2 + 3;                                     // levelled base radius, corners: the hill plus its entrances' ends
            int blend = System.Math.Max(1, (int)System.Math.Round(s.blendWidth / map.CellSize));

            // Placement: zone by zone, the flattest free spot each. An early zone can take the room a
            // later one needed (one in the middle of a small map), so the whole set is placed again, up
            // to kPlacementRounds times, until every zone fits; the round that fitted the most wins.
            int count = System.Math.Max(0, s.caveCount);
            System.Collections.Generic.List<CaveZone> zones = new System.Collections.Generic.List<CaveZone>();
            for (int round = 0; round < kPlacementRounds && zones.Count < count; round++)
            {
                System.Collections.Generic.List<CaveZone> placed = new System.Collections.Generic.List<CaveZone>();
                for (int i = 0; i < count; i++)
                {
                    CaveZone zone = PlaceZone(map, random, placed, diameter, flat, flat + blend);
                    if (zone == null) break;
                    placed.Add(zone);
                }
                if (placed.Count > zones.Count) zones = placed;
            }
            foreach (CaveZone zone in zones)
                PlanZone(map, random, zone, s);

            int radius = diameter / 2;                                       // the hill's edge, corners
            foreach (CaveZone zone in zones)
                AddEntrances(map, random, zone, s, radius);

            SeededNoise hillNoise = new SeededNoise(seed ^ 0x165667b1);
            foreach (CaveZone zone in zones)
            {
                Flatten(map, zone, flat, blend, terrain);
                RaiseHill(map, zone, s, hillNoise, radius);
                CutTunnels(map, zone, radius);
                zone.Unreachable = CountUnreachable(map, zone, radius);
            }

            if (zones.Count > 0)
                for (int row = 0; row < map.Rows; row++)
                    for (int col = 0; col < map.Cols; col++)
                        map.Walkable[row * map.Cols + col] = TerrainGenerator.IsWalkable(map.Height(col, row), map.Height(col + 1, row),
                            map.Height(col, row + 1), map.Height(col + 1, row + 1), map.CellSize, terrain.maxWalkableSlope);
            return zones.ToArray();
        }

        // The flattest of kCandidates free spots: the zone and its blend inside the map, and no
        // blend touching another zone's. Null when no free spot is found in 400 tries.
        private static CaveZone PlaceZone(TerrainMap map, SeededRandom random,
                                          System.Collections.Generic.List<CaveZone> placed, int diameter, int flat, int outer)
        {
            int lo = outer + 1;
            int hiCol = map.Cols - outer - 1, hiRow = map.Rows - outer - 1;
            if (hiCol <= lo || hiRow <= lo) return null;
            float minDistance = 2 * outer + 2;

            CaveZone best = null;
            int found = 0;
            for (int attempt = 0; attempt < 400 && found < kCandidates; attempt++)
            {
                int col = random.Next(lo, hiCol), row = random.Next(lo, hiRow);
                bool clear = true;
                foreach (CaveZone other in placed)
                {
                    float dx = col - other.CentreCol, dz = row - other.CentreRow;
                    if (dx * dx + dz * dz < minDistance * minDistance) { clear = false; break; }
                }
                if (!clear) continue;
                found++;

                float low = float.MaxValue, high = float.MinValue;
                for (int z = row - flat; z <= row + flat; z++)
                    for (int x = col - flat; x <= col + flat; x++)
                    {
                        if ((x - col) * (x - col) + (z - row) * (z - row) > flat * flat) continue;
                        float h = map.Height(x, z);
                        if (h < low) low = h;
                        if (h > high) high = h;
                    }
                if (best == null || high - low < best.HeightRange)
                    best = new CaveZone { CentreCol = col, CentreRow = row, Diameter = diameter, HeightRange = high - low };
            }
            return best;
        }

        // Steps 2-5 on the zone's own square grid, then copied into the map.
        private static void PlanZone(TerrainMap map, SeededRandom random, CaveZone zone, CaveSettings s)
        {
            int size = zone.Diameter;
            int first = zone.Diameter / 2;                      // the zone's grid starts this far before its centre
            float centre = (size - 1) * 0.5f;
            float inner = size * 0.5f * kCore;                  // cells further out than this are rock: the hillside

            bool[] interior = new bool[size * size];
            for (int z = 0; z < size; z++)
                for (int x = 0; x < size; x++)
                {
                    float dx = x - centre, dz = z - centre;
                    interior[z * size + x] = dx * dx + dz * dz <= inner * inner;
                }

            // 2. Random fill (rim solid).
            bool[] solid = new bool[size * size];
            float fill = System.Math.Max(0f, System.Math.Min(1f, s.fill));
            for (int i = 0; i < solid.Length; i++)
                solid[i] = !interior[i] || random.NextFloat() < fill;

            // 3. Smooth with the 4-5 rule; outside the grid counts as solid.
            bool[] next = new bool[size * size];
            for (int pass = 0; pass < System.Math.Max(0, s.smoothing); pass++)
            {
                for (int z = 0; z < size; z++)
                    for (int x = 0; x < size; x++)
                    {
                        int i = z * size + x;
                        int walls = SolidNeighbours(solid, size, x, z);
                        next[i] = !interior[i] || (solid[i] ? walls >= 4 : walls >= 5);
                    }
                bool[] swap = solid; solid = next; next = swap;
            }

            // 4. Minimum width: keep open cells that sit in a fully open 2 x 2 block.
            bool[] kept = new bool[size * size];
            for (int z = 0; z + 1 < size; z++)
                for (int x = 0; x + 1 < size; x++)
                {
                    int i = z * size + x;
                    if (solid[i] || solid[i + 1] || solid[i + size] || solid[i + size + 1]) continue;
                    kept[i] = kept[i + 1] = kept[i + size] = kept[i + size + 1] = true;
                }
            for (int i = 0; i < solid.Length; i++)
                if (!solid[i] && !kept[i]) { solid[i] = true; zone.CulledByWidth++; }

            // 5. Flood fill: label each open network (4-connected) and keep the largest.
            int[] label = new int[size * size];
            int best = 0, bestSize = 0, labels = 0;
            int[] stack = new int[size * size];
            for (int start = 0; start < solid.Length; start++)
            {
                if (solid[start] || label[start] != 0) continue;
                int id = ++labels, cells = 0, top = 0;
                stack[top++] = start; label[start] = id;
                while (top > 0)
                {
                    int i = stack[--top]; cells++;
                    int x = i % size, z = i / size;
                    if (x > 0        && !solid[i - 1]    && label[i - 1] == 0)    { label[i - 1] = id;    stack[top++] = i - 1; }
                    if (x + 1 < size && !solid[i + 1]    && label[i + 1] == 0)    { label[i + 1] = id;    stack[top++] = i + 1; }
                    if (z > 0        && !solid[i - size] && label[i - size] == 0) { label[i - size] = id; stack[top++] = i - size; }
                    if (z + 1 < size && !solid[i + size] && label[i + size] == 0) { label[i + size] = id; stack[top++] = i + size; }
                }
                if (cells > bestSize) { best = id; bestSize = cells; }
            }
            for (int i = 0; i < solid.Length; i++)
                if (!solid[i] && label[i] != best) { solid[i] = true; zone.CulledPockets++; }

            // Into the map.
            for (int z = 0; z < size; z++)
                for (int x = 0; x < size; x++)
                {
                    if (solid[z * size + x]) continue;
                    int col = zone.CentreCol - first + x, row = zone.CentreRow - first + z;
                    if (col < 0 || row < 0 || col >= map.Cols || row >= map.Rows) continue;
                    map.Tunnel[row * map.Cols + col] = true;
                    zone.TunnelCells++;
                }
        }

        // 6. Passages from the network to past the hill's edge, spread evenly round the zone: each
        // starts at the tunnel cell furthest out in its direction and marks every cell whose centre is
        // within 1.5 cells of the line, so it is about three cells wide (at least two on a slant) and
        // joins the network. Cell (col, row)'s
        // centre is (col + 0.5, row + 0.5) in corner units; the zone's centre is corner (CentreCol, CentreRow).
        private static void AddEntrances(TerrainMap map, SeededRandom random, CaveZone zone, CaveSettings s, int radius)
        {
            int count = System.Math.Max(0, s.entrances);
            if (count == 0 || zone.TunnelCells == 0) return;
            System.Collections.Generic.List<int> added = new System.Collections.Generic.List<int>();
            int half = zone.Diameter / 2;
            float cx = zone.CentreCol, cz = zone.CentreRow;
            double start = random.NextDouble() * 2.0 * System.Math.PI;

            for (int k = 0; k < count; k++)
            {
                double angle = start + k * 2.0 * System.Math.PI / count;
                float dirX = (float)System.Math.Cos(angle), dirZ = (float)System.Math.Sin(angle);

                // The tunnel cell furthest out this way.
                int fromCol = -1, fromRow = -1;
                float furthest = float.MinValue;
                for (int row = zone.CentreRow - half; row < zone.CentreRow + half; row++)
                    for (int col = zone.CentreCol - half; col < zone.CentreCol + half; col++)
                    {
                        if (!map.IsTunnel(col, row)) continue;
                        float along = (col + 0.5f - cx) * dirX + (row + 0.5f - cz) * dirZ;
                        if (along > furthest) { furthest = along; fromCol = col; fromRow = row; }
                    }
                if (fromCol < 0) return;

                float ax = fromCol + 0.5f, az = fromRow + 0.5f;
                float bx = cx + dirX * (radius + 1.5f), bz = cz + dirZ * (radius + 1.5f);
                int minCol = (int)System.Math.Floor(System.Math.Min(ax, bx)) - 2, maxCol = (int)System.Math.Ceiling(System.Math.Max(ax, bx)) + 2;
                int minRow = (int)System.Math.Floor(System.Math.Min(az, bz)) - 2, maxRow = (int)System.Math.Ceiling(System.Math.Max(az, bz)) + 2;
                for (int row = System.Math.Max(0, minRow); row <= System.Math.Min(map.Rows - 1, maxRow); row++)
                    for (int col = System.Math.Max(0, minCol); col <= System.Math.Min(map.Cols - 1, maxCol); col++)
                    {
                        if (map.IsTunnel(col, row)) continue;
                        if (DistanceToSegment(col + 0.5f, row + 0.5f, ax, az, bx, bz) > 1.5f) continue;
                        map.Tunnel[row * map.Cols + col] = true;
                        added.Add(row * map.Cols + col);
                    }
                zone.Entrances++;
            }

            // A slanted line leaves a few cells poking out sideways: keep only passage cells that sit in
            // a fully open 2 x 2 block, as in the plan (step 4). Removing one can leave a neighbour
            // without its block, so repeat until nothing changes. The line's two-cell core always stays.
            bool removed = true;
            while (removed)
            {
                removed = false;
                foreach (int cell in added)
                {
                    if (!map.Tunnel[cell]) continue;
                    int col = cell % map.Cols, row = cell / map.Cols;
                    bool inBlock = false;
                    for (int dz = -1; dz <= 0 && !inBlock; dz++)
                        for (int dx = -1; dx <= 0 && !inBlock; dx++)
                            inBlock = OpenBlock(map, col + dx, row + dz);
                    if (!inBlock) { map.Tunnel[cell] = false; removed = true; }
                }
            }
            foreach (int cell in added)
                if (map.Tunnel[cell]) { zone.EntranceCells++; zone.TunnelCells++; }
        }

        // The 2 x 2 block whose lowest corner cell is (col, row) is all tunnel.
        private static bool OpenBlock(TerrainMap map, int col, int row)
        {
            if (col < 0 || row < 0 || col + 1 >= map.Cols || row + 1 >= map.Rows) return false;
            return map.IsTunnel(col, row) && map.IsTunnel(col + 1, row) && map.IsTunnel(col, row + 1) && map.IsTunnel(col + 1, row + 1);
        }

        private static float DistanceToSegment(float px, float pz, float ax, float az, float bx, float bz)
        {
            float vx = bx - ax, vz = bz - az;
            float lengthSq = vx * vx + vz * vz;
            float t = lengthSq > 0f ? ((px - ax) * vx + (pz - az) * vz) / lengthSq : 0f;
            t = System.Math.Max(0f, System.Math.Min(1f, t));
            float dx = px - (ax + vx * t), dz = pz - (az + vz * t);
            return (float)System.Math.Sqrt(dx * dx + dz * dz);
        }

        // 7. One height inside the zone (corners within flat cells of its centre), blending back into
        // the original ground over the next blend cells.
        private static void Flatten(TerrainMap map, CaveZone zone, int flat, int blend, TerrainSettings terrain)
        {
            int cx = zone.CentreCol, cz = zone.CentreRow, outer = flat + blend;

            float sum = 0f; int n = 0;
            for (int z = cz - flat; z <= cz + flat; z++)
                for (int x = cx - flat; x <= cx + flat; x++)
                    if ((x - cx) * (x - cx) + (z - cz) * (z - cz) <= flat * flat) { sum += map.Height(x, z); n++; }
            float level = sum / System.Math.Max(1, n);
            if (terrain.levelHeight > 0f)
            {
                int step = (int)System.Math.Round(level / terrain.levelHeight);
                step = System.Math.Max(0, System.Math.Min(System.Math.Max(1, terrain.terraceLevels) - 1, step));
                level = step * terrain.levelHeight;
            }
            zone.BaseHeight = level;

            for (int z = cz - outer; z <= cz + outer; z++)
                for (int x = cx - outer; x <= cx + outer; x++)
                {
                    float d = (float)System.Math.Sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz));
                    if (d > outer) continue;
                    int i = z * (map.Cols + 1) + x;
                    if (d <= flat) { map.Heights[i] = level; continue; }
                    float k = (d - flat) / blend;
                    k = k * k * (3f - 2f * k);                    // smooth step: base at the edge, original at the end
                    map.Heights[i] = level + (map.Heights[i] - level) * k;
                }
        }

        // 8. The hill: full height (hillHeight plus noise) over the core, then easing down to the base
        // at the radius. Its core is never lower than a tunnel, a roof and the noise.
        private static void RaiseHill(TerrainMap map, CaveZone zone, CaveSettings s, SeededNoise noise, int radius)
        {
            float amplitude = System.Math.Max(0f, s.hillNoise);
            float core = System.Math.Max(s.hillHeight, System.Math.Max(0f, s.tunnelHeight) + System.Math.Max(0f, s.roofThickness) + amplitude);
            float scale = map.CellSize / System.Math.Max(1f, s.hillNoiseSize);
            float inner = radius * kCore;
            int cx = zone.CentreCol, cz = zone.CentreRow;
            zone.TopHeight = zone.BaseHeight;

            for (int z = cz - radius; z <= cz + radius; z++)
                for (int x = cx - radius; x <= cx + radius; x++)
                {
                    float d = (float)System.Math.Sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz));
                    if (d >= radius) continue;
                    float k = d <= inner ? 0f : (d - inner) / (radius - inner);
                    float shape = 1f - k * k * (3f - 2f * k);    // 1 in the middle, 0 at the edge
                    float bump = (noise.Layered(x * scale, z * scale, 2, 0.5f, 2f) - 0.5f) * 2f * amplitude;
                    float height = zone.BaseHeight + shape * (core + bump);
                    map.Heights[z * (map.Cols + 1) + x] = height;
                    if (height > zone.TopHeight) zone.TopHeight = height;
                }
        }

        // 9. Every corner of a tunnel cell goes back to the base, so tunnel floors are flat and level
        // with the ground outside, and the rock cells beside them slope up to the hill (the walls). A
        // rock cell inside the hill left with all four corners on the floor is floor too, and joins the
        // tunnel (outside the hill every cell is on the base, and stays ground).
        private static void CutTunnels(TerrainMap map, CaveZone zone, int radius)
        {
            float floor = zone.BaseHeight;
            int half = radius + 3;                              // the zone and its entrance passages
            for (int row = zone.CentreRow - half; row < zone.CentreRow + half; row++)
                for (int col = zone.CentreCol - half; col < zone.CentreCol + half; col++)
                {
                    if (!map.IsTunnel(col, row)) continue;
                    map.Heights[row * (map.Cols + 1) + col] = floor;
                    map.Heights[row * (map.Cols + 1) + col + 1] = floor;
                    map.Heights[(row + 1) * (map.Cols + 1) + col] = floor;
                    map.Heights[(row + 1) * (map.Cols + 1) + col + 1] = floor;
                }
            for (int row = zone.CentreRow - half; row < zone.CentreRow + half; row++)
                for (int col = zone.CentreCol - half; col < zone.CentreCol + half; col++)
                {
                    if (map.IsTunnel(col, row)) continue;
                    float dx = col + 0.5f - zone.CentreCol, dz = row + 0.5f - zone.CentreRow;
                    if (dx * dx + dz * dz >= radius * radius) continue;
                    if (map.Height(col, row) != floor || map.Height(col + 1, row) != floor ||
                        map.Height(col, row + 1) != floor || map.Height(col + 1, row + 1) != floor) continue;
                    map.Tunnel[row * map.Cols + col] = true;
                    zone.TunnelCells++;
                }
        }

        // 10. Tunnel cells of this zone that no passage from outside the hill reaches (4-connected).
        private static int CountUnreachable(TerrainMap map, CaveZone zone, int radius)
        {
            int half = radius + 3;
            int first = zone.CentreCol - half, firstRow = zone.CentreRow - half, size = 2 * half;
            bool[] reached = new bool[size * size];
            int[] stack = new int[size * size];
            int top = 0;
            for (int z = 0; z < size; z++)
                for (int x = 0; x < size; x++)
                {
                    int col = first + x, row = firstRow + z;
                    if (col < 0 || row < 0 || col >= map.Cols || row >= map.Rows || !map.IsTunnel(col, row)) continue;
                    float dx = col + 0.5f - zone.CentreCol, dz = row + 0.5f - zone.CentreRow;
                    if (dx * dx + dz * dz <= radius * radius) continue;          // inside the hill
                    reached[z * size + x] = true; stack[top++] = z * size + x;
                }
            while (top > 0)
            {
                int i = stack[--top], x = i % size, z = i / size;
                for (int n = 0; n < 4; n++)
                {
                    int nx = x + (n == 0 ? -1 : n == 1 ? 1 : 0), nz = z + (n == 2 ? -1 : n == 3 ? 1 : 0);
                    if (nx < 0 || nz < 0 || nx >= size || nz >= size) continue;
                    int j = nz * size + nx, col = first + nx, row = firstRow + nz;
                    if (reached[j] || col < 0 || row < 0 || col >= map.Cols || row >= map.Rows || !map.IsTunnel(col, row)) continue;
                    reached[j] = true; stack[top++] = j;
                }
            }
            int missed = 0;
            for (int z = 0; z < size; z++)
                for (int x = 0; x < size; x++)
                {
                    int col = first + x, row = firstRow + z;
                    if (col >= 0 && row >= 0 && col < map.Cols && row < map.Rows && map.IsTunnel(col, row) && !reached[z * size + x]) missed++;
                }
            return missed;
        }

        private static int SolidNeighbours(bool[] solid, int size, int x, int z)
        {
            int walls = 0;
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++)
                {
                    if (dx == 0 && dz == 0) continue;
                    int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= size || nz >= size || solid[nz * size + nx]) walls++;
                }
            return walls;
        }
    }
}
