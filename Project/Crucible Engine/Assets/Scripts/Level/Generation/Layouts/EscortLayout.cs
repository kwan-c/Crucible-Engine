/*************************************************************************
* \file:         EscortLayout.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Escort's level layout in the Inspector: its chunks and their
*                sizes (plus the shared map / road / tree numbers from
*                MissionLayout).
*
* \details
*    On the Sub Level Generator entity of an Escort scene (mission
*    "Escort"). Defaults are Unity M2's (SubLevelGen_Escort) and the GDD's:
*      map 500 m, edge 20 m, spacing 60-90 m (GDD; the least gap between
*      any two chunks, picked per level), roads along the route;
*      spawn (placed first) and extract (farthest from the spawn): 24 m
*      boxes; 3 to 4 checkpoints (the count picked per level from the
*      seed, as Unity did): circles of checkpointRadius (Unity's 40 m
*      Chunk_Escort), landmarks only (no interior), numbered 1st / 2nd /
*      ... as a route from the spawn.
*    The roads join spawn -> 1st -> ... -> last -> extract; the generator
*    turns them into the escort route (EscortRoute). The payload runs
*    from the 1st checkpoint to the last; then the extract opens.
*
* \history
*    -v1.0       new: Unity's SubLevelGen_Escort numbers; checkpointChunk
*                (the checkpoint prefab, Chunk Escort)
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class EscortLayout : MissionLayout
    {
        // Chunks.
        public Prefab checkpointChunk;          // the checkpoint prefab (Chunk Escort)
        public int minCheckpoints = 3;          // each level has between min and max checkpoints (from the seed)
        public int maxCheckpoints = 4;
        public float checkpointRadius = 20f;    // metres (Unity's Chunk_Escort: 40 m across)
        public float spawnSize = 24f;           // metres, square pad
        public float extractSize = 24f;         // metres, square pad

        public EscortLayout()
        {
            minSpacing = 60f;                   // GDD: spacing range 60 - 90
            maxSpacing = 90f;
            roadsAlongRoute = true;             // one road spawn -> checkpoints -> extract: the payload's route
        }

        public override string Mission { get { return MissionPresets.Escort; } }

        /// <summary>This level's checkpoint count: in [min, max], picked from the seed (Unity's missionChunkMin / Max).</summary>
        public static int CheckpointCount(int seed, int min, int max)
        {
            int lo = System.Math.Max(0, System.Math.Min(min, max)), hi = System.Math.Max(0, System.Math.Max(min, max));
            return new SeededRandom(seed ^ 0x6a09e667).Next(lo, hi + 1);
        }

        public override Prefab PrefabFor(string role)
        {
            return role == MissionPresets.Checkpoint ? checkpointChunk : base.PrefabFor(role);
        }

        protected override void AddChunks(MissionPreset p, int seed)
        {
            float d = checkpointRadius * 2f;
            int count = CheckpointCount(seed, minCheckpoints, maxCheckpoints);
            p.Chunks.Add(new ChunkRequest { Role = MissionPresets.Spawn, Footprint = new ChunkFootprint(ChunkShape.Box, spawnSize, spawnSize), Start = true });
            for (int i = 0; i < count; i++)
                p.Chunks.Add(new ChunkRequest { Role = MissionPresets.Checkpoint, Order = i + 1, Footprint = new ChunkFootprint(ChunkShape.Circle, d, d) });
            p.Chunks.Add(new ChunkRequest { Role = MissionPresets.Extract, Footprint = new ChunkFootprint(ChunkShape.Box, extractSize, extractSize), End = true });
        }
    }
}
