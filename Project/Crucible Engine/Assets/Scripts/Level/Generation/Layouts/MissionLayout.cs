/*************************************************************************
* \file:         MissionLayout.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         A mission level's layout numbers in the Inspector: the base
*                every mission's layout script derives from (Unity's
*                SubLevelGenSettings asset, one per mission).
*
* \details
*    Put the mission's layout script (CaptureLayout, later EscortLayout and
*    LiberationLayout) on the Sub Level Generator entity. The generator asks
*    it for the level's MissionPreset (BuildPreset) when it generates.
*    Fields here are the ones every mission has (map, spacing, roads,
*    trees); each mission's script adds its own (its chunks, their sizes,
*    their interiors) in AddChunks. Defaults are Unity M2's / the GDD's.
*    Kept in code (rarely tuned): placement candidates (300) and flatness
*    weight (10); the terrace levels and slope come from the generator.
*    The tree themes by quadrant are TEMPORARY (until level themes).
*    When the Inspector gets list fields, a mission could list its chunk
*    entries here instead of fixed fields in each layout script.
*
* \history
*    -v1.0       new: moved out of MissionPresets.cs into the Inspector
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public abstract class MissionLayout : EntityScript
    {
        // Map and placement.
        public float mapSize = 500f;            // metres, square
        public float edgeMargin = 20f;          // metres kept clear at the map border
        public float minSpacing = 50f;          // metres of gap between chunks (edge to edge): low end
        public float maxSpacing = 50f;          // high end; each level picks its gap in [min, max] from the seed
        public float blendDistance = 24f;       // metres over which a flattened chunk fades into the hills

        // Roads (RoadBuilder).
        public bool roadsAlongRoute = false;    // one route spawn -> 1st -> ... -> extract, instead of a network
        public int extraRoadLinks = 1;          // network only: loops beyond the minimum network
        public float roadWidth = 12f;           // metres
        public float maxRoadSlope = 20f;        // degrees
        public float roadBlend = 8f;            // metres over which a road's edge fades into the terrain
        public float roadSlopeCost = 4f;        // how strongly a route avoids slopes
        public float roadCliffCost = 10f;       // carved roads: how strongly they still avoid cliffs
        public bool carveIfNoWayRound = true;   // no route round the cliffs: carve a ramp instead of skipping
        public int roadSmoothing = 3;           // cells either side averaged when evening out a road

        // Trees (DecorScatter).
        public float treeSpacing = 6f;          // metres: no two trees closer than this
        public float treeDensity = 0.15f;       // 0..1 chance a free spot gets a tree, before clustering
        public float treeClustering = 0.85f;    // 0 = even spread, 1 = only in woods
        public float woodSize = 120f;           // metres across a wood or clearing
        public float treeRoadClearance = 4f;    // metres kept clear beside roads
        public float treeChunkClearance = 6f;   // metres kept clear around chunks
        public float treeStartClearance = 30f;  // metres kept clear around the spawn
        public float treeMaxSlope = 30f;        // degrees
        public float treeMinScale = 0.8f;
        public float treeMaxScale = 1.3f;
        public float themeFavour = 0.7f;        // TEMPORARY quadrants: share of a quadrant's trees in its own theme
        public float themeBlend = 40f;          // TEMPORARY quadrants: metres where neighbouring themes mix

        /// <summary>The mission name this layout is for (the generator's `mission`), e.g. "Capture".</summary>
        public abstract string Mission { get; }

        /// <summary>The level's preset from these Inspector values.</summary>
        public MissionPreset BuildPreset()
        {
            MissionPreset p = new MissionPreset();
            p.Name = Mission;
            p.MapSize = mapSize;
            p.Placement.edgeMargin = edgeMargin;
            p.Placement.minSpacing = minSpacing;
            p.Placement.minSpacingHigh = maxSpacing;
            p.Placement.blendDistance = blendDistance;

            p.Roads.chain = roadsAlongRoute;
            p.Roads.extraLinks = extraRoadLinks;
            p.Roads.roadWidth = roadWidth;
            p.Roads.maxRoadSlope = maxRoadSlope;
            p.Roads.roadBlend = roadBlend;
            p.Roads.slopeCost = roadSlopeCost;
            p.Roads.cliffCost = roadCliffCost;
            p.Roads.carveIfNoWayRound = carveIfNoWayRound;
            p.Roads.smoothing = roadSmoothing;

            p.Decor.spacing = treeSpacing;
            p.Decor.density = treeDensity;
            p.Decor.clusterStrength = treeClustering;
            p.Decor.clusterSize = woodSize;
            p.Decor.roadClearance = treeRoadClearance;
            p.Decor.chunkClearance = treeChunkClearance;
            p.Decor.startClearance = treeStartClearance;
            p.Decor.maxSlope = treeMaxSlope;
            p.Decor.minScale = treeMinScale;
            p.Decor.maxScale = treeMaxScale;
            p.Decor.themeFavour = themeFavour;
            p.Decor.themeBlend = themeBlend;

            AddChunks(p);
            return p;
        }

        /// <summary>The mission's chunks, in placement order, into p.Chunks.</summary>
        protected abstract void AddChunks(MissionPreset p);
    }
}
