/*************************************************************************
* \file:         CaveGenerator.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          23 November 2026
* \version       2.0
* \brief         A cave system as plain data: an optional generator feature
*                that raises a rocky mountain and carves rooms and tunnels
*                through it and under it, as a cave volume. No engine calls,
*                so it can be tested alone.
*
* \details
*    The terrain's heightfield holds one ground per spot, so the caves live
*    in a TerrainVolume: a box of rock and air the engine meshes with
*    marching cubes, in place of the heightfield cells under it. Like Deep
*    Rock Galactic, rooms are strung together by winding tunnels; like
*    Minecraft, the cave mouths are wherever a tunnel breaks the surface.
*    One cave system per map:
*      1. Place: the box (zoneSize, whole cells) on the flattest of 16 free
*         spots (least height difference inside it).
*      2. Base: the box's ground is levelled to the terrace level nearest
*         its average, blending into the land around over blendWidth. It
*         is the ground round the mountain and the upper rooms' level.
*      3. Mountain: rises mountainHeight (plus noise) over the middle, its
*         outline warped by noise, easing to the base well inside the box
*         - so the box's sides are the levelled ground exactly and meet the
*         heightfield without a crack.
*      4. Rooms: rooms chambers, each three overlapping squashed spheres
*         with a flat floor; deepRooms of them 12-22 m under the base, the
*         rest at ground level inside the mountain (with 14 m of mountain
*         over them). Kept apart unless on different levels.
*      5. Tunnels: the shortest network joining the rooms (a climb counts
*         three times its length), plus two loops. Each is a winding worm
*         tunnelHeight tall with a flat floor; its climb is spread evenly
*         along it, and it swings out sideways when the climb would be
*         steeper than kMaxSlope.
*      6. Mouths: entrances tunnels from the ground-level rooms straight
*         out through the mountainside, ending on the base outside.
*      7. Walls: 3D noise moves every cave wall by up to wallNoise.
*      8. Checks: a flood fill through the air from the sky; rooms it
*         misses are counted (should be none).
*      9. Heightfield: the cells under the box take the mountain's top, so
*         until the volume collides (a later step) the mountain is solid
*         ground you can walk on and enemies path round.
*    Its own random streams (seed ^ 0x27d4eb2f and noise seeds), so the
*    ground outside the box is never touched.
*
* \history
*    -v1.0       tunnel plan (cellular automata), plateau, trenches
*    -v1.1-1.2   rock hill with tunnels at ground level and entrances
*    -v2.0       a cave volume: mountain, rooms on two levels, worm tunnels,
*                mouths, noisy walls (replaces the heightfield caves)
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    // What the cave feature needs, as plain data (SubLevelGenerator fills it in).
    public sealed class CaveSettings
    {
        public float zoneSize = 200f;           // metres across the cave's box (whole cells)
        public float voxelSize = 2f;            // metres per sample; must divide the cell size
        public float blendWidth = 12f;          // metres over which the levelled base blends into the land around
        public float mountainHeight = 34f;      // metres the mountain rises over the base in its middle
        public int rooms = 8;                   // chambers in the system
        public int deepRooms = 4;               // of those, under the ground; the rest inside the mountain
        public float tunnelHeight = 8f;         // metres from a tunnel's floor to its ceiling
        public int entrances = 3;               // tunnels out of the mountainside
        public float wallNoise = 2.5f;          // metres the walls are pushed in or out
    }

    // The cave system made, for the log.
    public sealed class CaveSystem
    {
        public float CentreX, CentreZ;          // metres, in the map's own space
        public float BaseHeight, TopHeight;     // the ground round the mountain; the mountain's highest point
        public float HeightRange;               // the ground's height difference inside the box before levelling
        public int Rooms, DeepRooms, Tunnels, Entrances;
        public float SteepestDegrees;           // the steepest stretch of any tunnel
        public int UnreachableRooms;            // rooms no air path from outside reaches (should be 0)
        public int Samples;                     // in the volume
    }

    public static class CaveGenerator
    {
        private const int kCandidates = 16;         // free spots compared; the flattest wins
        private const float kMaxSlope = 30f;        // degrees: the steepest a tunnel may climb
        private const float kDeepTop = -12f, kDeepBottom = -22f;   // metres from the base, deep rooms' centres
        private const float kUpperLow = 5f, kUpperHigh = 8f;       // metres from the base, ground-level rooms' centres
        private const float kRockOverRoom = 14f;    // metres of mountain over a ground-level room's centre
        private const float kRoomSpacing = 26f;     // metres apart across, unless on different levels
        private const float kLevelGap = 14f;        // metres apart in height that counts as a different level
        private const float kStep = 1.5f;           // metres between carves along a tunnel

        // The box and its fields while one system is generated.
        private sealed class Work
        {
            public TerrainMap Map;
            public TerrainVolume Volume;
            public float[] Air;                     // per sample: distance-like, below 0 = carved
            public float[] Top;                     // per column (x fastest, then z): the rock's top
            public int Col0, Row0, Cells;           // the box's first cell and size in cells
            public float MinX, MinZ, Size, CentreX, CentreZ, Base;
        }

        private sealed class Room { public float X, Y, Z, Radius; public bool Deep; }

        /// <summary>
        /// Adds a cave system to the map: levels and raises map.Heights over its box, and sets map.Volume.
        /// Null (and the map unchanged) when the box doesn't fit on the map or its voxels don't fit its cells.
        /// </summary>
        public static CaveSystem Generate(TerrainMap map, int seed, CaveSettings s, TerrainSettings terrain)
        {
            int cells = System.Math.Max(4, (int)System.Math.Round(s.zoneSize / map.CellSize));
            int blend = System.Math.Max(1, (int)System.Math.Round(s.blendWidth / map.CellSize));
            float perCell = map.CellSize / s.voxelSize;
            if (s.voxelSize <= 0f || System.Math.Abs(perCell - System.Math.Round(perCell)) > 1e-3f || perCell < 1f) return null;

            SeededRandom random = new SeededRandom(seed ^ 0x27d4eb2f);
            CaveSystem result = new CaveSystem();
            Work w = new Work { Map = map, Cells = cells };
            if (!Place(map, random, cells, blend, w, result)) return null;

            Level(map, w, blend, terrain, result);
            ShapeMountain(w, s, seed, result);

            // The box: from below the deepest room to above the mountain's top.
            float bottom = w.Base + kDeepBottom - 16f, top = result.TopHeight + 4f;
            int countXZ = (int)System.Math.Round(cells * perCell) + 1;
            int countY = (int)System.Math.Ceiling((top - bottom) / s.voxelSize) + 1;
            w.Volume = new TerrainVolume(new Vector3(w.MinX, bottom, w.MinZ), s.voxelSize, countXZ, countY, countXZ);
            w.Air = new float[w.Volume.Density.Length];
            for (int i = 0; i < w.Air.Length; i++) w.Air[i] = float.MaxValue;

            List<Room> rooms = PlaceRooms(w, random, s);
            result.Rooms = rooms.Count;
            foreach (Room r in rooms) if (r.Deep) result.DeepRooms++;
            foreach (Room r in rooms) CarveRoom(w, random, r);

            float radius = TunnelRadius(s);
            foreach (int[] link in Links(rooms))
            {
                float steepest = CarveWorm(w, random, Point(rooms[link[0]]), Point(rooms[link[1]]), radius);
                result.SteepestDegrees = System.Math.Max(result.SteepestDegrees, steepest);
                result.Tunnels++;
            }
            result.Entrances = CarveMouths(w, random, rooms, s, radius);

            FillDensity(w, s, seed);
            result.UnreachableRooms = CountUnreachable(w, rooms);
            result.Samples = w.Volume.Density.Length;

            RaiseHeightfield(w, blend, terrain);
            map.Volume = w.Volume;
            return result;
        }

        // A tunnel's carve radius: its floor sits 0.6 of the radius below the centre, so the height is 1.6 radii.
        private static float TunnelRadius(CaveSettings s) { return System.Math.Max(1f, s.tunnelHeight / 1.6f); }

        private static float[] Point(Room r) { return new float[] { r.X, r.Y, r.Z }; }

        // ---------------------------------------------------------------- 1. place

        private static bool Place(TerrainMap map, SeededRandom random, int cells, int blend, Work w, CaveSystem result)
        {
            int lo = blend + 1, hiCol = map.Cols - cells - blend - 1, hiRow = map.Rows - cells - blend - 1;
            if (hiCol < lo || hiRow < lo) return false;
            int bestCol = -1, bestRow = -1;
            float bestRange = float.MaxValue;
            for (int k = 0; k < kCandidates; k++)
            {
                int col = random.Next(lo, hiCol + 1), row = random.Next(lo, hiRow + 1);
                float low = float.MaxValue, high = float.MinValue;
                for (int z = row; z <= row + cells; z++)
                    for (int x = col; x <= col + cells; x++)
                    {
                        float h = map.Height(x, z);
                        if (h < low) low = h;
                        if (h > high) high = h;
                    }
                if (high - low < bestRange) { bestRange = high - low; bestCol = col; bestRow = row; }
            }
            w.Col0 = bestCol; w.Row0 = bestRow;
            w.MinX = map.MinX + bestCol * map.CellSize; w.MinZ = map.MinZ + bestRow * map.CellSize;
            w.Size = cells * map.CellSize;
            w.CentreX = w.MinX + w.Size * 0.5f; w.CentreZ = w.MinZ + w.Size * 0.5f;
            result.CentreX = w.CentreX; result.CentreZ = w.CentreZ; result.HeightRange = bestRange;
            return true;
        }

        // ---------------------------------------------------------------- 2. base

        // The box's corners to one terrace level (the nearest to their average); corners within blend
        // cells of the box ease back to the original ground.
        private static void Level(TerrainMap map, Work w, int blend, TerrainSettings terrain, CaveSystem result)
        {
            float sum = 0f; int n = 0;
            for (int z = w.Row0; z <= w.Row0 + w.Cells; z++)
                for (int x = w.Col0; x <= w.Col0 + w.Cells; x++) { sum += map.Height(x, z); n++; }
            float level = sum / n;
            if (terrain.levelHeight > 0f)
            {
                int step = (int)System.Math.Round(level / terrain.levelHeight);
                step = System.Math.Max(0, System.Math.Min(System.Math.Max(1, terrain.terraceLevels) - 1, step));
                level = step * terrain.levelHeight;
            }
            w.Base = level;
            result.BaseHeight = level;

            for (int z = w.Row0 - blend; z <= w.Row0 + w.Cells + blend; z++)
                for (int x = w.Col0 - blend; x <= w.Col0 + w.Cells + blend; x++)
                {
                    int out_ = System.Math.Max(System.Math.Max(w.Col0 - x, x - (w.Col0 + w.Cells)),
                                               System.Math.Max(w.Row0 - z, z - (w.Row0 + w.Cells)));   // cells outside the box
                    int i = z * (map.Cols + 1) + x;
                    if (out_ <= 0) { map.Heights[i] = level; continue; }
                    float k = (float)out_ / (blend + 1);
                    k = k * k * (3f - 2f * k);
                    map.Heights[i] = level + (map.Heights[i] - level) * k;
                }
        }

        // ---------------------------------------------------------------- 3. mountain

        // The rock's top per sample column: the base, plus the mountain over the middle. The outline is a
        // circle (radius 38% of the box) pushed in and out by noise (up to 8% of the box), rising over the
        // outer half of that radius so its sides are steep enough for the mouths to read as openings, and the mountain is gone a few
        // metres inside the box's sides, so they stay the levelled base.
        private static void ShapeMountain(Work w, CaveSettings s, int seed, CaveSystem result)
        {
            SeededNoise outline = new SeededNoise(seed ^ 0x1b56c4e9), bumps = new SeededNoise(seed ^ 0x165667b1);
            int count = (int)System.Math.Round(w.Size / s.voxelSize) + 1;
            w.Top = new float[count * count];
            float radius = w.Size * 0.38f, warp = w.Size * 0.08f, clear = w.Size * 0.5f - 6f;
            result.TopHeight = w.Base;
            for (int z = 0; z < count; z++)
                for (int x = 0; x < count; x++)
                {
                    float px = w.MinX + x * s.voxelSize, pz = w.MinZ + z * s.voxelSize;
                    w.Top[z * count + x] = MountainTop(w, s, outline, bumps, px, pz, radius, warp, clear);
                    result.TopHeight = System.Math.Max(result.TopHeight, w.Top[z * count + x]);
                }
        }

        private static float MountainTop(Work w, CaveSettings s, SeededNoise outline, SeededNoise bumps,
                                         float px, float pz, float radius, float warp, float clear)
        {
            float dx = px - w.CentreX, dz = pz - w.CentreZ;
            float d = (float)System.Math.Sqrt(dx * dx + dz * dz);
            if (System.Math.Abs(dx) >= clear || System.Math.Abs(dz) >= clear) return w.Base;
            float push = (outline.Layered(px / 80f, pz / 80f, 3, 0.5f, 2f) - 0.5f) * 2f * warp;
            float k = System.Math.Max(0f, System.Math.Min(1f, (radius - d - push) / (radius * 0.5f)));   // rises over the outer half
            float rise = k * k * (3f - 2f * k);
            float edge = System.Math.Min(1f, System.Math.Min(clear - System.Math.Abs(dx), clear - System.Math.Abs(dz)) / 6f);  // gone at the clear line
            float bump = (bumps.Layered(px / 16f, pz / 16f, 3, 0.5f, 2f) - 0.5f) * 14f;
            return w.Base + System.Math.Max(0f, rise * edge * (s.mountainHeight + bump));
        }

        // The rock's top at a point inside the box (nearest sample column).
        private static float TopAt(Work w, float px, float pz)
        {
            int count = (int)System.Math.Round(w.Size / w.Volume.VoxelSize) + 1;
            int x = System.Math.Max(0, System.Math.Min(count - 1, (int)System.Math.Round((px - w.MinX) / w.Volume.VoxelSize)));
            int z = System.Math.Max(0, System.Math.Min(count - 1, (int)System.Math.Round((pz - w.MinZ) / w.Volume.VoxelSize)));
            return w.Top[z * count + x];
        }

        // ---------------------------------------------------------------- 4. rooms

        private static List<Room> PlaceRooms(Work w, SeededRandom random, CaveSettings s)
        {
            List<Room> rooms = new List<Room>();
            int total = System.Math.Max(0, s.rooms), deep = System.Math.Max(0, System.Math.Min(total, s.deepRooms));
            float spreadDeep = w.Size * 0.22f;          // centres within this of the middle
            float spreadUpper = w.Size * 0.16f;         // ground-level rooms nearer it, under the mountain's top
            for (int i = 0; i < total; i++)
            {
                bool isDeep = i >= total - deep;
                for (int attempt = 0; attempt < 400; attempt++)
                {
                    double angle = random.NextDouble() * 2.0 * System.Math.PI;
                    float distance = random.Range(0f, isDeep ? spreadDeep : spreadUpper);
                    Room r = new Room
                    {
                        X = w.CentreX + (float)System.Math.Cos(angle) * distance,
                        Z = w.CentreZ + (float)System.Math.Sin(angle) * distance,
                        Y = w.Base + (isDeep ? random.Range(kDeepBottom, kDeepTop) : random.Range(kUpperLow, kUpperHigh)),
                        Radius = random.Range(9f, 13f),
                        Deep = isDeep
                    };
                    if (!isDeep && TopAt(w, r.X, r.Z) < r.Y + kRockOverRoom) continue;
                    bool apart = true;
                    foreach (Room o in rooms)
                    {
                        float ax = r.X - o.X, az = r.Z - o.Z;
                        if (ax * ax + az * az < kRoomSpacing * kRoomSpacing && System.Math.Abs(r.Y - o.Y) < kLevelGap) { apart = false; break; }
                    }
                    if (apart) { rooms.Add(r); break; }
                }
            }
            return rooms;
        }

        // Three overlapping squashed spheres around the centre, each with a flat floor.
        private static void CarveRoom(Work w, SeededRandom random, Room r)
        {
            for (int k = 0; k < 3; k++)
            {
                float ox = random.Range(-r.Radius * 0.6f, r.Radius * 0.6f), oz = random.Range(-r.Radius * 0.6f, r.Radius * 0.6f);
                float across = r.Radius * random.Range(0.7f, 1f);
                CarveEllipsoid(w, r.X + ox, r.Y, r.Z + oz, across, r.Radius * 0.55f, r.Radius * 0.35f);
            }
        }

        // Air inside the ellipsoid, above its floor (floorDrop below the centre). Never within two
        // samples of the box's sides or bottom, so they stay the levelled ground.
        private static void CarveEllipsoid(Work w, float cx, float cy, float cz, float across, float up, float floorDrop)
        {
            TerrainVolume v = w.Volume;
            float size = v.VoxelSize;
            int x0 = System.Math.Max(2, (int)System.Math.Floor((cx - across - v.Origin.X) / size));
            int x1 = System.Math.Min(v.CountX - 3, (int)System.Math.Ceiling((cx + across - v.Origin.X) / size));
            int z0 = System.Math.Max(2, (int)System.Math.Floor((cz - across - v.Origin.Z) / size));
            int z1 = System.Math.Min(v.CountZ - 3, (int)System.Math.Ceiling((cz + across - v.Origin.Z) / size));
            int y0 = System.Math.Max(2, (int)System.Math.Floor((cy - up - v.Origin.Y) / size));
            int y1 = System.Math.Min(v.CountY - 1, (int)System.Math.Ceiling((cy + up - v.Origin.Y) / size));
            float smaller = System.Math.Min(across, up), floor = cy - floorDrop;
            for (int y = y0; y <= y1; y++)
                for (int z = z0; z <= z1; z++)
                    for (int x = x0; x <= x1; x++)
                    {
                        float px = v.Origin.X + x * size, py = v.Origin.Y + y * size, pz = v.Origin.Z + z * size;
                        float ex = (px - cx) / across, ey = (py - cy) / up, ez = (pz - cz) / across;
                        float d = ((float)System.Math.Sqrt(ex * ex + ey * ey + ez * ez) - 1f) * smaller;
                        d = System.Math.Max(d, floor - py);
                        int i = v.Index(x, y, z);
                        if (d < w.Air[i]) w.Air[i] = d;
                    }
        }

        // ---------------------------------------------------------------- 5. tunnels

        // The shortest network joining the rooms (Prim's), where a climb counts three times its length,
        // plus the two shortest links left over, so there are loops.
        private static List<int[]> Links(List<Room> rooms)
        {
            List<int[]> links = new List<int[]>();
            int n = rooms.Count;
            if (n < 2) return links;
            System.Func<int, int, float> cost = (a, b) =>
            {
                float dx = rooms[a].X - rooms[b].X, dz = rooms[a].Z - rooms[b].Z;
                return (float)System.Math.Sqrt(dx * dx + dz * dz) + 3f * System.Math.Abs(rooms[a].Y - rooms[b].Y);
            };
            bool[] joined = new bool[n];
            joined[0] = true;
            for (int added = 1; added < n; added++)
            {
                int bestA = -1, bestB = -1; float best = float.MaxValue;
                for (int a = 0; a < n; a++)
                    if (joined[a])
                        for (int b = 0; b < n; b++)
                            if (!joined[b] && cost(a, b) < best) { best = cost(a, b); bestA = a; bestB = b; }
                joined[bestB] = true;
                links.Add(new int[] { bestA, bestB });
            }
            for (int loop = 0; loop < 2; loop++)
            {
                int bestA = -1, bestB = -1; float best = float.MaxValue;
                for (int a = 0; a < n; a++)
                    for (int b = a + 1; b < n; b++)
                    {
                        bool taken = false;
                        foreach (int[] l in links) if ((l[0] == a && l[1] == b) || (l[0] == b && l[1] == a)) { taken = true; break; }
                        if (!taken && cost(a, b) < best) { best = cost(a, b); bestA = a; bestB = b; }
                    }
                if (bestA >= 0) links.Add(new int[] { bestA, bestB });
            }
            return links;
        }

        // A winding tunnel from a to b: a sideways wave (and, when the climb is too steep for the
        // distance, a wide swing out) across the straight line; the climb spread evenly along the path's
        // run. Carves every kStep metres. Returns the steepest stretch, in degrees.
        private static float CarveWorm(Work w, SeededRandom random, float[] a, float[] b, float radius)
        {
            float dx = b[0] - a[0], dz = b[2] - a[2];
            float flat = (float)System.Math.Sqrt(dx * dx + dz * dz), rise = System.Math.Abs(b[1] - a[1]);
            float sideX = flat > 1e-3f ? -dz / flat : 1f, sideZ = flat > 1e-3f ? dx / flat : 0f;
            float need = rise / (float)System.Math.Tan(kMaxSlope * System.Math.PI / 180.0);   // run a walkable climb needs
            float swing = flat >= need ? 0f : (float)System.Math.Sqrt(System.Math.Max(0f, need * need / 4f - flat * flat / 4f)) * 1.15f;
            float phase = random.Range(0f, 6.28f), wave = random.Range(5f, 10f);

            const int points = 200;
            float[] px = new float[points], pz = new float[points], run = new float[points];
            for (int i = 0; i < points; i++)
            {
                float t = (float)i / (points - 1);
                float off = ((float)System.Math.Sin(t * System.Math.PI * 2 + phase) * wave + swing * 2f) * (float)System.Math.Sin(t * System.Math.PI);
                px[i] = a[0] + dx * t + sideX * off;
                pz[i] = a[2] + dz * t + sideZ * off;
                if (i > 0) run[i] = run[i - 1] + (float)System.Math.Sqrt((px[i] - px[i - 1]) * (px[i] - px[i - 1]) + (pz[i] - pz[i - 1]) * (pz[i] - pz[i - 1]));
            }
            float total = System.Math.Max(1e-3f, run[points - 1]);
            float steepest = (float)(System.Math.Atan(rise / total) * 180.0 / System.Math.PI);   // even climb: the same everywhere

            int seg = 0;
            for (float d = 0f; d <= total; d += kStep)
            {
                while (seg < points - 2 && run[seg + 1] < d) seg++;
                float span = System.Math.Max(1e-4f, run[seg + 1] - run[seg]);
                float f = System.Math.Max(0f, System.Math.Min(1f, (d - run[seg]) / span));
                float cx = px[seg] + (px[seg + 1] - px[seg]) * f, cz = pz[seg] + (pz[seg + 1] - pz[seg]) * f;
                float cy = a[1] + (b[1] - a[1]) * (d / total);
                CarveEllipsoid(w, cx, cy, cz, radius, radius, radius * 0.6f);
            }
            return steepest;
        }

        // ---------------------------------------------------------------- 6. mouths

        // From up to `entrances` ground-level rooms (the outermost first), straight away from the
        // mountain's middle until past its foot, ending on the base 10 m further out (inside the box).
        private static int CarveMouths(Work w, SeededRandom random, List<Room> rooms, CaveSettings s, float radius)
        {
            List<Room> upper = new List<Room>();
            foreach (Room r in rooms) if (!r.Deep) upper.Add(r);
            upper.Sort((p, q) => Distance(w, q).CompareTo(Distance(w, p)));
            int made = 0;
            float limit = w.Size * 0.5f - 4f;
            foreach (Room r in upper)
            {
                if (made >= System.Math.Max(0, s.entrances)) break;
                float dx = r.X - w.CentreX, dz = r.Z - w.CentreZ, len = (float)System.Math.Sqrt(dx * dx + dz * dz);
                if (len < 1e-3f) { dx = 1f; dz = 0f; len = 1f; }
                dx /= len; dz /= len;
                float ex = r.X, ez = r.Z;
                for (float t = 1f; t < w.Size; t += 1f)
                {
                    float qx = r.X + dx * t, qz = r.Z + dz * t;
                    bool inside = System.Math.Abs(qx - w.CentreX) < limit && System.Math.Abs(qz - w.CentreZ) < limit;
                    if (!inside) break;
                    ex = qx; ez = qz;
                    if (TopAt(w, qx, qz) < r.Y + 2f)
                    {
                        float further = System.Math.Min(10f, limit - System.Math.Max(System.Math.Abs(qx - w.CentreX), System.Math.Abs(qz - w.CentreZ)));
                        ex = qx + dx * further; ez = qz + dz * further;
                        break;
                    }
                }
                CarveWorm(w, random, Point(r), new float[] { ex, w.Base + radius * 0.6f, ez }, radius);
                made++;
            }
            return made;
        }

        private static float Distance(Work w, Room r)
        {
            float dx = r.X - w.CentreX, dz = r.Z - w.CentreZ;
            return (float)System.Math.Sqrt(dx * dx + dz * dz);
        }

        // ---------------------------------------------------------------- 7. density

        // Rock below the top, minus the carved air, its walls moved by 3D noise; the bottom layer always
        // rock. Near the box's sides (two samples) nothing was carved, so there it is exactly the base.
        private static void FillDensity(Work w, CaveSettings s, int seed)
        {
            TerrainVolume v = w.Volume;
            SeededNoise walls = new SeededNoise(seed ^ 0x2f9be6cd);
            int count = v.CountX;
            float reach = System.Math.Max(0f, s.wallNoise) + v.VoxelSize;
            for (int y = 0; y < v.CountY; y++)
                for (int z = 0; z < v.CountZ; z++)
                    for (int x = 0; x < v.CountX; x++)
                    {
                        int i = v.Index(x, y, z);
                        float py = v.Origin.Y + y * v.VoxelSize;
                        float rock = w.Top[z * count + x] - py;
                        float air = w.Air[i];
                        if (air < reach)
                        {
                            float px = v.Origin.X + x * v.VoxelSize, pz = v.Origin.Z + z * v.VoxelSize;
                            air += (walls.Layered3(px / 14f, py / 14f, pz / 14f, 3, 0.5f, 2f) - 0.5f) * 2f * s.wallNoise;
                        }
                        float d = System.Math.Min(rock, air);
                        if (y == 0) d = System.Math.Max(d, v.VoxelSize);
                        v.Density[i] = d;
                    }
        }

        // ---------------------------------------------------------------- 8. checks

        // A flood fill through the air (six neighbours) from the top layer: a room is cut off when it
        // reaches no sample within half the room's radius of its centre.
        private static int CountUnreachable(Work w, List<Room> rooms)
        {
            TerrainVolume v = w.Volume;
            bool[] seen = new bool[v.Density.Length];
            Stack<int> open = new Stack<int>();
            for (int z = 0; z < v.CountZ; z++)
                for (int x = 0; x < v.CountX; x++)
                {
                    int i = v.Index(x, v.CountY - 1, z);
                    if (v.Density[i] < 0f) { seen[i] = true; open.Push(i); }
                }
            int layer = v.CountX * v.CountZ;
            while (open.Count > 0)
            {
                int i = open.Pop();
                int x = i % v.CountX, z = (i / v.CountX) % v.CountZ, y = i / layer;
                for (int n = 0; n < 6; n++)
                {
                    int nx = x + (n == 0 ? -1 : n == 1 ? 1 : 0), ny = y + (n == 2 ? -1 : n == 3 ? 1 : 0), nz = z + (n == 4 ? -1 : n == 5 ? 1 : 0);
                    if (nx < 0 || ny < 0 || nz < 0 || nx >= v.CountX || ny >= v.CountY || nz >= v.CountZ) continue;
                    int j = v.Index(nx, ny, nz);
                    if (seen[j] || v.Density[j] >= 0f) continue;
                    seen[j] = true; open.Push(j);
                }
            }
            int cut = 0;
            foreach (Room r in rooms)
            {
                int cx = (int)System.Math.Round((r.X - v.Origin.X) / v.VoxelSize);
                int cy = (int)System.Math.Round((r.Y - v.Origin.Y) / v.VoxelSize);
                int cz = (int)System.Math.Round((r.Z - v.Origin.Z) / v.VoxelSize);
                int reach = System.Math.Max(1, (int)(r.Radius * 0.5f / v.VoxelSize));
                bool reached = false;
                for (int y = System.Math.Max(0, cy - reach); y <= System.Math.Min(v.CountY - 1, cy + reach) && !reached; y++)
                    for (int z = System.Math.Max(0, cz - reach); z <= System.Math.Min(v.CountZ - 1, cz + reach) && !reached; z++)
                        for (int x = System.Math.Max(0, cx - reach); x <= System.Math.Min(v.CountX - 1, cx + reach) && !reached; x++)
                            reached = seen[v.Index(x, y, z)];
                if (!reached) cut++;
            }
            return cut;
        }

        // ---------------------------------------------------------------- 9. heightfield

        // The cells under the box take the rock's top (the volume draws them; until it collides, this is
        // the ground physics and navigation use), and walkability follows - there and in the blend ring.
        private static void RaiseHeightfield(Work w, int blend, TerrainSettings terrain)
        {
            TerrainMap map = w.Map;
            for (int z = w.Row0; z <= w.Row0 + w.Cells; z++)
                for (int x = w.Col0; x <= w.Col0 + w.Cells; x++)
                    map.Heights[z * (map.Cols + 1) + x] = TopAt(w, map.MinX + x * map.CellSize, map.MinZ + z * map.CellSize);
            for (int row = w.Row0 - blend - 1; row <= w.Row0 + w.Cells + blend; row++)
                for (int col = w.Col0 - blend - 1; col <= w.Col0 + w.Cells + blend; col++)
                {
                    if (col < 0 || row < 0 || col >= map.Cols || row >= map.Rows) continue;
                    map.Walkable[row * map.Cols + col] = TerrainGenerator.IsWalkable(map.Height(col, row), map.Height(col + 1, row),
                        map.Height(col, row + 1), map.Height(col + 1, row + 1), map.CellSize, terrain.maxWalkableSlope);
                }
        }
    }
}
