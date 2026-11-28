/*************************************************************************
* \file:         CaptureLayout.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.2
* \brief         Capture's level layout in the Inspector: its chunks, their
*                sizes and the areas' generated interiors (plus the shared
*                map / road / tree numbers from MissionLayout).
*
* \details
*    On the Sub Level Generator entity of a Capture scene (mission
*    "Capture"). Defaults are the GDD's and Unity M2's, the same numbers the
*    level used before they moved here:
*      map 500 m, edge 20 m, spacing 50-70 m (the least gap between any
*      two chunks, picked per level), roads along the route;
*      spawn (placed first, flattest in the middle third) and extract
*      (farthest from the spawn): 24 m boxes (Unity's start chunk);
*      `areas` capture areas: circles of areaRadius (GDD 30 m), numbered
*      1st / 2nd / 3rd as a route from the spawn, each with Unity's KOTH
*      interior (walls, steps, an open hold area, lanes from the roads).
*    The hold rules are on MissionController; the waves on the Spawn
*    Director; the outpost pieces on the Chunk Capture prefab.
*
* \history
*    -v1.0       new: Capture's numbers moved out of MissionPresets.cs
*    -v1.1       AddChunks takes the seed (unused here)
*    -v1.2       areaChunk: the capture area prefab (was the generator's
*                captureChunk)
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class CaptureLayout : MissionLayout
    {
        // Chunks.
        public Prefab areaChunk;                // the capture area prefab (Chunk Capture)
        public int areas = 3;                   // capture areas, cleared in order
        public float areaRadius = 30f;          // metres (GDD)
        public float spawnSize = 24f;           // metres, square pad
        public float extractSize = 24f;         // metres, square pad

        // Each area's generated interior (ChunkInterior; Unity's KOTH numbers).
        public float featureSize = 20f;         // metres across a typical block or dip
        public float stepHeight = 3f;           // metres per step: one step = cover, stacked = walls
        public float maxHeight = 6f;            // metres: the tallest the interior rises
        public float sharpness = 0.85f;         // 0 = smooth slopes, 1 = sheer steps
        public float holdArea = 0.3f;           // fraction of the radius kept open in the middle
        public float holdCover = 0.5f;          // 0..1: light cover in the hold area (one step at most)
        public float laneWidth = 8f;            // metres: lanes from each road entrance stay at base level
        public float edgeFade = 0.15f;          // fraction of the radius over which it fades to base

        public CaptureLayout()
        {
            maxSpacing = 70f;                   // GDD: spacing range 70 - 50
            roadsAlongRoute = true;             // roads follow the objective route
        }

        public override string Mission { get { return MissionPresets.Capture; } }

        public override Prefab PrefabFor(string role)
        {
            return role == MissionPresets.Capture ? areaChunk : base.PrefabFor(role);
        }

        protected override void AddChunks(MissionPreset p, int seed)
        {
            float d = areaRadius * 2f;
            p.Chunks.Add(new ChunkRequest { Role = MissionPresets.Spawn, Footprint = new ChunkFootprint(ChunkShape.Box, spawnSize, spawnSize), Start = true });
            for (int i = 0; i < areas; i++)
                p.Chunks.Add(new ChunkRequest { Role = MissionPresets.Capture, Order = i + 1, Footprint = new ChunkFootprint(ChunkShape.Circle, d, d), Interior = Interior() });
            p.Chunks.Add(new ChunkRequest { Role = MissionPresets.Extract, Footprint = new ChunkFootprint(ChunkShape.Box, extractSize, extractSize), End = true });
        }

        private InteriorSettings Interior()
        {
            return new InteriorSettings
            {
                featureSize = featureSize, stepHeight = stepHeight, maxHeight = maxHeight, sharpness = sharpness,
                holdArea = holdArea, holdCover = holdCover, laneWidth = laneWidth, edgeFade = edgeFade,
            };
        }
    }
}
