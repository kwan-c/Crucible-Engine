/*************************************************************************
* \file:         SpawnSources.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.1
* \brief         Where a wave's enemies appear: the spawn-source interface
*                and the ring around a moving or fixed centre.
*
* \details
*    Unity's spawn points were components (SpawnPoint, RoamingSpawner,
*    ChunkRingSpawner); an objective made a ring with new GameObject. The
*    engine's scripts can't create entities, so a source is a plain object:
*      ISpawnSource   TryGetPose: a spot and facing for the next enemy, or
*                     false when it has none this time
*      SpawnRing      Unity's SpawnRing.TryPick: random spots on a circle
*                     round a centre, kept only where an enemy can stand
*                     (isOpen: Navigation.IsWalkable in the game), facing
*                     the centre; height from groundHeight (the terrain)
*      RingSource     a ring whose centre is asked for each spawn: the
*                     roaming ring follows a player, a Capture ring sits
*                     on its area, Escort's will follow the payload
*    Hand-placed points (Liberation's tents) are an entity script
*    (SpawnPoint.cs) implementing ISpawnSource.
*
* \history
*    -v1.0       ported from Unity (SpawnRing, RoamingSpawner, ChunkRingSpawner)
*    -v1.1       RingSource.LastCentre (for the director's ground ray)
*
**************************************************************************/
using System;

namespace Crucible.@Scripts
{
    public interface ISpawnSource
    {
        bool TryGetPose(Random random, out Vector3 position, out Quaternion rotation);
    }

    public static class SpawnRing
    {
        // A spot on the circle `radius` round `centre` where isOpen holds (null = anywhere), at
        // groundHeight(x, z) (null = the centre's height) plus lift, facing the centre.
        // `attempts` random angles are tried before giving up.
        public static bool TryPick(Vector3 centre, float radius, int attempts, Random random,
                                   Func<Vector3, bool> isOpen, Func<float, float, float> groundHeight, float lift,
                                   out Vector3 position, out Quaternion rotation)
        {
            for (int i = 0; i < Math.Max(1, attempts); i++)
            {
                double angle = random.NextDouble() * Math.PI * 2.0;
                float x = centre.X + (float)Math.Cos(angle) * radius;
                float z = centre.Z + (float)Math.Sin(angle) * radius;
                float y = groundHeight != null ? groundHeight(x, z) : centre.Y;
                if (float.IsNaN(y)) continue;                    // off the map
                var candidate = new Vector3(x, y, z);
                if (isOpen != null && !isOpen(candidate)) continue;
                position = new Vector3(x, y + lift, z);
                var toCentre = new Vector3(centre.X - x, 0f, centre.Z - z);
                rotation = Quaternion.LookRotation(toCentre);
                return true;
            }
            position = Vector3.Zero;
            rotation = Quaternion.Identity;
            return false;
        }
    }

    public sealed class RingSource : ISpawnSource
    {
        public Func<Vector3?> Centre;               // asked per spawn; null result = no centre right now
        public float Radius = 30f;
        public int Attempts = 12;
        public float Lift = 0.5f;                   // above the ground, so a spawn never starts inside it
        public Func<Vector3, bool> IsOpen;          // null = everywhere
        public Func<float, float, float> GroundHeight;
        public Vector3 LastCentre { get; private set; }   // the centre of the latest spawn (a ground ray starts above it)

        public RingSource(Func<Vector3?> centre, float radius) { Centre = centre; Radius = Math.Max(1f, radius); }

        public bool TryGetPose(Random random, out Vector3 position, out Quaternion rotation)
        {
            Vector3? c = Centre != null ? Centre() : null;
            if (!c.HasValue) { position = Vector3.Zero; rotation = Quaternion.Identity; return false; }
            LastCentre = c.Value;
            return SpawnRing.TryPick(c.Value, Radius, Attempts, random, IsOpen, GroundHeight, Lift, out position, out rotation);
        }
    }
}
