/*************************************************************************
* \file:         TerrainTest.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.1
* \brief         Test for the Terrain engine system: builds a small
*                hand-made map and checks it draws, collides and navigates.
*
* \details
*    Put it on the terrain entity (Terrain + MeshRenderer + Collider set to
*    HeightField). On Play it builds:
*      - a smooth hill, gentle enough to walk up (green);
*      - a raised plateau with sheer sides, which are cliffs (grey):
*        the player can't climb them and enemies path round them;
*      - a road strip across the map (tan).
*    then asks navigation to re-bake and logs when the new grid is in.
*
*    Check in Play: the colours show; walking up the hill works; the
*    plateau's sides stop you; the capsules route round the plateau. The
*    log shows the hill-top height (about HillHeight) and the bake count.
*
*    caveTest adds a cave volume (TerrainVolume) over the hill: a flat-topped
*    rock dome with a tunnel straight through it along X, opening at both
*    ends where the dome gets too low. At the box's sides the dome has faded out, so
*    the volume there is exactly the grid's ground: the seam. Check: no
*    crack round the box; the tunnel has a ceiling; two mouths; cave floor
*    in the tunnel colour. The volume has no collision or navigation yet.
*
* \history
*    -v1.0       first version
*    -v1.1       caveTest: a cave volume over the hill
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class TerrainTest : EntityScript
    {
        public int cells = 60;               // cells along each side
        public float cellSize = 2f;          // metres per cell
        public float hillHeight = 7f;        // metres, the walkable hill
        public float plateauHeight = 5f;     // metres, the cliff-sided block
        public float maxWalkableSlope = 35f; // degrees: steeper cells are cliffs (navigation's limit too)
        public bool caveTest = false;        // a rock dome with a tunnel through it over the hill (a TerrainVolume)
        public float voxelSize = 1f;         // metres; must divide cellSize

        private Terrain terrain;
        private int bakeCountAtBuild = -1;
        private bool checkedTop;

        public override void OnCreate()
        {
            terrain = Entity.GetComponent<Terrain>();
            if (terrain == null)
            {
                Log.Error("TerrainTest: this entity has no Terrain component");
                return;
            }

            int corners = cells + 1;
            float half = cells * cellSize * 0.5f;
            float[] heights = new float[corners * corners];
            for (int z = 0; z < corners; ++z)
                for (int x = 0; x < corners; ++x)
                    heights[z * corners + x] = HeightAt(-half + x * cellSize, -half + z * cellSize);

            byte[] regions = new byte[cells * cells];
            float maxRise = cellSize * (float)System.Math.Tan(maxWalkableSlope * System.Math.PI / 180.0);
            for (int z = 0; z < cells; ++z)
                for (int x = 0; x < cells; ++x)
                {
                    float h00 = heights[z * corners + x], h10 = heights[z * corners + x + 1];
                    float h01 = heights[(z + 1) * corners + x], h11 = heights[(z + 1) * corners + x + 1];
                    float rise = System.Math.Max(System.Math.Max(System.Math.Abs(h10 - h00), System.Math.Abs(h01 - h00)),
                                                 System.Math.Max(System.Math.Abs(h11 - h10), System.Math.Abs(h11 - h01)));
                    float centreZ = -half + (z + 0.5f) * cellSize;
                    TerrainRegion region = rise > maxRise ? TerrainRegion.Cliff
                                         : (centreZ > -6f && centreZ < -2f) ? TerrainRegion.Road
                                         : TerrainRegion.Ground;
                    regions[z * cells + x] = (byte)region;
                }

            TerrainVolume volume = caveTest ? CaveVolume(heights, half) : null;
            if (!terrain.Build(cells, cells, cellSize, heights, regions, volume))
            {
                Log.Error("TerrainTest: Terrain.Build refused the grid" + (volume != null ? " or the cave volume" : ""));
                return;
            }
            if (volume != null)
                Log.Info("TerrainTest: cave volume " + volume.CountX + " x " + volume.CountY + " x " + volume.CountZ + " samples of " + voxelSize + " m");

            bakeCountAtBuild = Navigation.BakeCount;
            Navigation.Rebake();
            Log.Info("TerrainTest: built " + cells + " x " + cells + " cells of " + cellSize + " m");
        }

        public override void OnUpdate(float deltaTime)
        {
            // Once the entity's world transform exists (not always on the first frame).
            float top;
            if (!checkedTop && terrain != null && terrain.GroundHeight(30f, 25f, out top))
            {
                Log.Info("TerrainTest: hill top is " + top + " m (expected about " + hillHeight + ")");
                checkedTop = true;
            }

            if (bakeCountAtBuild < 0 || Navigation.BakeCount == bakeCountAtBuild) return;
            Log.Info("TerrainTest: navigation re-baked on the new ground (bake " + Navigation.BakeCount + ")");
            bakeCountAtBuild = -1;
        }

        // The cave volume: from x 4 to 56 m and z 4 to 48 m (on grid corners), y -2 to 24 m. Rock is
        // the higher of the grid's ground and a dome (16 m high out to 13 m from the hill's top, easing
        // to nothing by 22 m); the tunnel is air 8 m wide along X through the hill's middle, from the
        // ground to 7 m above it, so it is roofed under the dome and opens where the dome gets low.
        private TerrainVolume CaveVolume(float[] heights, float half)
        {
            float x0 = 4f, x1 = 56f, z0 = 4f, z1 = 48f, y0 = -2f, y1 = 24f;
            int countX = (int)System.Math.Round((x1 - x0) / voxelSize) + 1;
            int countY = (int)System.Math.Round((y1 - y0) / voxelSize) + 1;
            int countZ = (int)System.Math.Round((z1 - z0) / voxelSize) + 1;
            TerrainVolume volume = new TerrainVolume(new Vector3(x0, y0, z0), voxelSize, countX, countY, countZ);

            for (int z = 0; z < countZ; ++z)
                for (int x = 0; x < countX; ++x)
                {
                    float px = x0 + x * voxelSize, pz = z0 + z * voxelSize;
                    float ground = GridHeight(heights, half, px, pz);
                    float r = (float)System.Math.Sqrt((px - 30f) * (px - 30f) + (pz - 25f) * (pz - 25f));
                    float k = System.Math.Max(0f, System.Math.Min(1f, (22f - r) / 9f));
                    float dome = 16f * k * k * (3f - 2f * k);
                    float top = System.Math.Max(ground, dome);
                    float side = System.Math.Abs(pz - 25f);                         // across the tunnel
                    for (int y = 0; y < countY; ++y)
                    {
                        float py = y0 + y * voxelSize;
                        float rock = top - py;                                       // > 0 below the top
                        // The tunnel: a rounded box, 4 m either side, from the ground to 7 m up.
                        float tunnel = System.Math.Max(side - 4f, System.Math.Max(ground - py, py - (ground + 7f)));
                        volume.Density[volume.Index(x, y, z)] = System.Math.Min(rock, tunnel);
                    }
                }
            return volume;
        }

        // The grid's own ground at (x, z): the same two triangles per cell as the mesh and the collider,
        // so the volume's sides meet the grid exactly.
        private float GridHeight(float[] heights, float half, float x, float z)
        {
            int corners = cells + 1;
            float gx = (x + half) / cellSize, gz = (z + half) / cellSize;
            int cx = System.Math.Min((int)gx, cells - 1), cz = System.Math.Min((int)gz, cells - 1);
            float fx = gx - cx, fz = gz - cz;
            float h00 = heights[cz * corners + cx], h10 = heights[cz * corners + cx + 1];
            float h01 = heights[(cz + 1) * corners + cx], h11 = heights[(cz + 1) * corners + cx + 1];
            return fz >= fx ? h00 + (h11 - h01) * fx + (h01 - h00) * fz
                            : h00 + (h10 - h00) * fx + (h11 - h10) * fz;
        }

        // The map, in the terrain entity's own space.
        private float HeightAt(float x, float z)
        {
            float dx = x - 30f, dz = z - 25f;
            float hill = hillHeight * (float)System.Math.Exp(-(dx * dx + dz * dz) / (2f * 10f * 10f));
            bool onPlateau = x >= -40f && x <= -20f && z >= 10f && z <= 30f;
            return hill + (onPlateau ? plateauHeight : 0f);
        }
    }
}
