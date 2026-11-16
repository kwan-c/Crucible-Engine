/*************************************************************************
* \file:         TerrainTest.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
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

            if (!terrain.Build(cells, cells, cellSize, heights, regions))
            {
                Log.Error("TerrainTest: Terrain.Build refused the grid");
                return;
            }

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
