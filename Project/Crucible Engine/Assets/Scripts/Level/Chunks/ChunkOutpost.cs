/*************************************************************************
* \file:         ChunkOutpost.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         The outpost the level generator builds inside a circular
*                chunk: which pieces may go in each kind of spot, and how
*                many. Put it on the chunk prefab's root, next to
*                ChunkDefinition.
*
* \details
*    Ported from the Unity prototype (Level/Chunks/ChunkOutpost.cs). Slots
*    (OutpostPlanner sorts the chunk's generated interior into them):
*      Centre    one piece in the middle of the hold area (a landmark)
*      Wall Edge next to walls (cover such as low walls, rocks)
*      High      on top of blocks (vantage pieces)
*      Flat      open ground (buildings, pillars, trees)
*      Camp      flat ground in the hold area (unused by Capture)
*    The Inspector can't show lists, so each slot has three piece fields
*    (A, B, C) and a min / max count. Empty fields are skipped; the same
*    prefab in two fields is twice as likely to be picked (Unity's weights).
*    Unity's counts are the defaults. Pieces need their own collider and
*    the StaticObstacle tag to block enemies (the generator doesn't tag).
*
*    SubLevelGenerator reads this once the chunk's scripts are attached
*    (a frame after it spawns the chunk), plans the pieces and spawns them
*    under the chunk, then re-bakes navigation.
*
* \history
*    -v1.0       ported from Unity; three piece fields per slot (no lists)
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class ChunkOutpost : EntityScript
    {
        public Prefab centreA, centreB, centreC;
        public int centreMin = 1, centreMax = 1;
        public Prefab wallEdgeA, wallEdgeB, wallEdgeC;
        public int wallEdgeMin = 4, wallEdgeMax = 8;
        public Prefab highA, highB, highC;
        public int highMin = 1, highMax = 3;
        public Prefab flatA, flatB, flatC;
        public int flatMin = 3, flatMax = 6;
        public Prefab campA, campB, campC;
        public int campMin = 0, campMax = 0;
        public float spacing = 6f;          // metres between pieces
        public float laneClearance = 2f;    // metres kept clear either side of each entry lane
        public float minScale = 0.9f;
        public float maxScale = 1.2f;

        /// <summary>The rules for OutpostPlanner: an empty field has weight 0.</summary>
        public OutpostSettings ToSettings()
        {
            OutpostSettings s = new OutpostSettings();
            s.centre = Slot(centreA, centreB, centreC, centreMin, centreMax);
            s.wallEdge = Slot(wallEdgeA, wallEdgeB, wallEdgeC, wallEdgeMin, wallEdgeMax);
            s.high = Slot(highA, highB, highC, highMin, highMax);
            s.flat = Slot(flatA, flatB, flatC, flatMin, flatMax);
            s.camp = Slot(campA, campB, campC, campMin, campMax);
            s.spacing = System.Math.Max(0.5f, spacing);
            s.laneClearance = System.Math.Max(0f, laneClearance);
            s.minScale = System.Math.Max(0.01f, System.Math.Min(minScale, maxScale));
            s.maxScale = System.Math.Max(s.minScale, System.Math.Max(minScale, maxScale));
            return s;
        }

        /// <summary>The prefab of piece `index` (0 = A, 1 = B, 2 = C) in a slot, or null.</summary>
        public Prefab Piece(OutpostSlot slot, int index)
        {
            Prefab a, b, c;
            switch (slot)
            {
                case OutpostSlot.Centre: a = centreA; b = centreB; c = centreC; break;
                case OutpostSlot.WallEdge: a = wallEdgeA; b = wallEdgeB; c = wallEdgeC; break;
                case OutpostSlot.High: a = highA; b = highB; c = highC; break;
                case OutpostSlot.Camp: a = campA; b = campB; c = campC; break;
                default: a = flatA; b = flatB; c = flatC; break;
            }
            return index == 0 ? a : index == 1 ? b : index == 2 ? c : null;
        }

        private static OutpostSlotSettings Slot(Prefab a, Prefab b, Prefab c, int min, int max)
        {
            return new OutpostSlotSettings
            {
                weights = new[] { a != null ? 1f : 0f, b != null ? 1f : 0f, c != null ? 1f : 0f },
                min = min,
                max = max,
            };
        }
    }
}
