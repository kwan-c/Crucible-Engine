/*************************************************************************
* \file:         CaveGenerator.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          23 November 2026
* \version       1.0
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
*    The result is TerrainMap.Tunnel. This version only plans: the ground's
*    shape is untouched (carving, roofs and entrances are later steps).
*
*    Its own random stream (seed ^ 0x27d4eb2f), so turning caves on or off
*    never changes the terrain's heights.
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
    }

    public static class CaveGenerator
    {
        /// <summary>
        /// Plans the cave systems: marks map.Tunnel and returns the zones placed (fewer than asked
        /// when the map has no room for more).
        /// </summary>
        public static CaveZone[] Plan(TerrainMap map, int seed, CaveSettings s)
        {
            SeededRandom random = new SeededRandom(seed ^ 0x27d4eb2f);
            int diameter = System.Math.Max(8, (int)System.Math.Round(s.zoneSize / map.CellSize));
            int radius = diameter / 2;

            System.Collections.Generic.List<CaveZone> zones = new System.Collections.Generic.List<CaveZone>();
            int count = System.Math.Max(0, s.caveCount);
            for (int i = 0; i < count; i++)
            {
                CaveZone zone = PlaceZone(map, random, zones, diameter, radius);
                if (zone == null) break;
                PlanZone(map, random, zone, s);
                zones.Add(zone);
            }
            return zones.ToArray();
        }

        // A free spot: inside the map with a one-cell margin, and a full zone width plus a quarter
        // away from every other zone. Null after 200 failed tries.
        private static CaveZone PlaceZone(TerrainMap map, SeededRandom random,
                                          System.Collections.Generic.List<CaveZone> placed, int diameter, int radius)
        {
            int lo = radius + 1;
            int hiCol = map.Cols - radius - 1, hiRow = map.Rows - radius - 1;
            if (hiCol <= lo || hiRow <= lo) return null;
            float minDistance = diameter * 1.25f;

            for (int attempt = 0; attempt < 200; attempt++)
            {
                int col = random.Next(lo, hiCol), row = random.Next(lo, hiRow);
                bool clear = true;
                foreach (CaveZone other in placed)
                {
                    float dx = col - other.CentreCol, dz = row - other.CentreRow;
                    if (dx * dx + dz * dz < minDistance * minDistance) { clear = false; break; }
                }
                if (clear) return new CaveZone { CentreCol = col, CentreRow = row, Diameter = diameter };
            }
            return null;
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
