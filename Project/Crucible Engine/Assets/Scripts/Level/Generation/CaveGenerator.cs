/*************************************************************************
* \file:         CaveGenerator.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          23 November 2026
* \version       1.1
* \brief         Cave systems as plain data: an optional generator feature
*                that plans tunnel networks on a TerrainMap. No engine
*                calls, so it can be tested alone.
*
* \details
*    A cave system is a network of tunnels under a plateau, entered from
*    the surface. Seen from above, the network is the Finstinct cavern
*    (CSD3183): "wall" is solid ground, "water" is tunnel. Per zone:
*      1. Place: caveCount circular zones at seeded spots, away from the
*         map edge and from each other.
*      2. Random fill: inside the zone, each cell starts solid with
*         probability caveFill; the zone's rim is always solid, so tunnels
*         stay under the plateau (Finstinct's sealed border).
*      3. Smooth: caveSmoothing passes of the 4-5 rule over the 8
*         neighbours (a solid cell stays solid with 4+ solid neighbours, an
*         open one turns solid with 5+), rim re-forced after every pass.
*      4. Minimum width: an open cell stays only if it is part of a fully
*         open 2 x 2 block, so every passage is at least two cells wide.
*      5. Flood fill: the zone keeps its largest network (4-connected);
*         smaller pockets are filled back in.
*    The plan is TerrainMap.Tunnel. Then each zone is carved:
*      6. Plateau: corners within the zone are set to one height - the
*         terrace level nearest their average - blending back into the
*         land around over blendWidth.
*      7. Trenches: every corner of a tunnel cell is lowered to the floor,
*         roofThickness + tunnelHeight below the plateau. Tunnel cells are
*         flat floor; the solid cells next to them slope up to the plateau
*         and become cliffs: the tunnel walls. A solid cell whose four
*         corners all dropped (a sliver between tunnels) is floor too, so
*         it joins the tunnel.
*      8. Walkability is recomputed with the terrain's slope limit.
*    Roofs and entrances are later steps.
*
*    Placement takes the flattest of 16 free spots (least height
*    difference inside the zone), so flattening changes as little as
*    possible. Its own random stream (seed ^ 0x27d4eb2f), so the ground
*    outside the zones is never touched.
*
* \history
*    -v1.0       tunnel plan (steps 1-5)
*    -v1.1       flattest placement, plateau and trenches (steps 6-8)
**************************************************************************/
namespace Crucible.@Scripts
{
    // What the cave feature needs, as plain data (SubLevelGenerator fills it in).
    public sealed class CaveSettings
    {
        public int caveCount = 2;               // cave systems per map
        public float zoneSize = 120f;           // metres across each system's circular zone
        public float fill = 0.45f;              // chance a cell starts solid (Finstinct's 45%)
        public int smoothing = 5;               // passes of the 4-5 rule
        public float tunnelHeight = 6f;         // metres from the tunnel floor to the roof's underside
        public float roofThickness = 1f;        // metres; the roof's top is level with the plateau
        public float blendWidth = 12f;          // metres over which the plateau blends into the land around
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
        public float PlateauHeight { get; internal set; }
        public float FloorHeight { get; internal set; }  // the tunnels' floor
    }

    public static class CaveGenerator
    {
        private const int kCandidates = 16;     // free spots compared per zone; the flattest wins

        /// <summary>
        /// Adds the cave systems to the map: plans the tunnels (map.Tunnel), carves plateaus and
        /// trenches into map.Heights and updates map.Walkable. Returns the zones placed (fewer than
        /// asked when the map has no room for more).
        /// </summary>
        public static CaveZone[] Generate(TerrainMap map, int seed, CaveSettings s, TerrainSettings terrain)
        {
            SeededRandom random = new SeededRandom(seed ^ 0x27d4eb2f);
            int diameter = System.Math.Max(8, (int)System.Math.Round(s.zoneSize / map.CellSize));
            int flat = diameter / 2 + 1;                                     // plateau radius, corners (covers the walls)
            int blend = System.Math.Max(1, (int)System.Math.Round(s.blendWidth / map.CellSize));

            System.Collections.Generic.List<CaveZone> zones = new System.Collections.Generic.List<CaveZone>();
            int count = System.Math.Max(0, s.caveCount);
            for (int i = 0; i < count; i++)
            {
                CaveZone zone = PlaceZone(map, random, zones, diameter, flat, flat + blend);
                if (zone == null) break;
                PlanZone(map, random, zone, s);
                zones.Add(zone);
            }

            foreach (CaveZone zone in zones)
            {
                Flatten(map, zone, flat, blend, terrain);
                DigTrenches(map, zone, s);
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
            float inner = size * 0.5f - 1f;                     // cells further out than this are the solid rim

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

        // 6. One height inside the zone (corners within flat cells of its centre), blending back into
        // the original ground over the next blend cells.
        private static void Flatten(TerrainMap map, CaveZone zone, int flat, int blend, TerrainSettings terrain)
        {
            int cx = zone.CentreCol, cz = zone.CentreRow, outer = flat + blend;

            float sum = 0f; int n = 0;
            for (int z = cz - flat; z <= cz + flat; z++)
                for (int x = cx - flat; x <= cx + flat; x++)
                    if ((x - cx) * (x - cx) + (z - cz) * (z - cz) <= flat * flat) { sum += map.Height(x, z); n++; }
            float plateau = sum / System.Math.Max(1, n);
            if (terrain.levelHeight > 0f)
            {
                int level = (int)System.Math.Round(plateau / terrain.levelHeight);
                level = System.Math.Max(0, System.Math.Min(System.Math.Max(1, terrain.terraceLevels) - 1, level));
                plateau = level * terrain.levelHeight;
            }
            zone.PlateauHeight = plateau;

            for (int z = cz - outer; z <= cz + outer; z++)
                for (int x = cx - outer; x <= cx + outer; x++)
                {
                    float d = (float)System.Math.Sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz));
                    if (d > outer) continue;
                    int i = z * (map.Cols + 1) + x;
                    if (d <= flat) { map.Heights[i] = plateau; continue; }
                    float k = (d - flat) / blend;
                    k = k * k * (3f - 2f * k);                    // smooth step: plateau at the edge, original at the end
                    map.Heights[i] = plateau + (map.Heights[i] - plateau) * k;
                }
        }

        // 7. Every corner of a tunnel cell drops to the floor, so tunnel cells are flat and the solid
        // cells beside them slope up to the plateau (cliffs: the walls). A solid cell left with all
        // four corners on the floor is floor too, and joins the tunnel.
        private static void DigTrenches(TerrainMap map, CaveZone zone, CaveSettings s)
        {
            float floor = zone.PlateauHeight - System.Math.Max(0f, s.roofThickness) - System.Math.Max(0f, s.tunnelHeight);
            zone.FloorHeight = floor;
            int half = zone.Diameter / 2;
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
                    if (map.Height(col, row) != floor || map.Height(col + 1, row) != floor ||
                        map.Height(col, row + 1) != floor || map.Height(col + 1, row + 1) != floor) continue;
                    map.Tunnel[row * map.Cols + col] = true;
                    zone.TunnelCells++;
                }
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
