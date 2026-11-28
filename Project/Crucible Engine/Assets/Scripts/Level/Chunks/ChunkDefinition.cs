/*************************************************************************
* \file:         ChunkDefinition.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          26 November 2026
* \version       1.1
* \brief         A placed chunk's label: its role in the mission, its order
*                (Capture's 1st, 2nd, 3rd) and its footprint. Holds no
*                gameplay; the objectives on the chunk will.
*
* \details
*    Ported from the Unity prototype (Level/Chunks/ChunkDefinition.cs). Put
*    it on a chunk prefab's root. Unlike Unity, the size isn't set on the
*    prefab: SubLevelGenerator takes it from the mission's preset
*    (MissionPresets) and calls Setup once the chunk's scripts are
*    attached (a frame after spawning), so one table holds every mission
*    number. Setup also stretches the
*    prefab's "Footprint" child (a 1 m Disc or Plane) to the footprint, so
*    the chunk shows its real size.
*
*    Children the generator looks for:
*      Footprint     optional: the 1 m mesh stretched to the footprint
*      PlayerStart   on the spawn chunk: where the player is put
*
*    Interior settings, path connectors and roaming blockers are later port
*    steps (roads, waves).
*
* \history
*    -v1.0       ported from Unity: role, order, footprint, Contains
*    -v1.1       comment: Setup comes a frame after spawning
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class ChunkDefinition : EntityScript
    {
        /// <summary>The chunk's role in its mission (MissionPresets.Spawn, Extract, Capture, ...).</summary>
        public string Role { get; private set; }

        /// <summary>Capture's 1st, 2nd, 3rd (1-based); 0 for chunks with no order.</summary>
        public int Order { get; private set; }

        /// <summary>The footprint in metres, centred on this entity.</summary>
        public ChunkFootprint Footprint { get; private set; }

        /// <summary>Called by SubLevelGenerator once the chunk's scripts are attached (a frame after spawning).</summary>
        public void Setup(string role, int order, ChunkFootprint footprint)
        {
            Role = role;
            Order = order;
            Footprint = footprint;

            Entity shape = Entity.FindChild("Footprint");
            if (shape != null)
            {
                Vector3 scale = shape.Transform.Scale;
                shape.Transform.Scale = new Vector3(footprint.Width, scale.Y, footprint.Depth);
            }
        }

        /// <summary>Inside the footprint (box or circle); height is ignored. Chunks are never rotated.</summary>
        public bool Contains(Vector3 worldPosition)
        {
            Vector3 centre = Entity.Transform.WorldPosition;
            return Footprint.Inside(worldPosition.X - centre.X, worldPosition.Z - centre.Z, 0f);
        }
    }
}
