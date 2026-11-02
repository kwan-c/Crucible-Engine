/*************************************************************************
* \file:         Physics.cs
* \author:       <author>
* \par:          Email: <email>
* \date          2 November 2026
* \version       1.0
* \brief         Physics queries for scripts: Physics.Raycast and RaycastHit.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's Physics.Raycast:
*
*        RaycastHit hit;
*        if (Physics.Raycast(muzzle, aimDirection, out hit, 100f, Entity))
*            hit.Entity.GetComponent<Enemy>()?.TakeDamage(10);
*
*    Pass the shooter as `ignore` so a ray starting inside it does not hit
*    itself. Rays hit Colliders (bodies), not CharacterControllers. Only
*    works during Play (physics only runs then); otherwise it hits nothing.
*
*    C++ side: Engine/src/Scripting/Bindings/PhysicsBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>What a Physics.Raycast hit. Unity's RaycastHit.</summary>
    public struct RaycastHit
    {
        /// <summary>The entity whose collider was hit.</summary>
        public Entity Entity { get; internal set; }
        /// <summary>Where the ray hit, in world space.</summary>
        public Vector3 Point { get; internal set; }
        /// <summary>Which way the surface faces where it was hit.</summary>
        public Vector3 Normal { get; internal set; }
        /// <summary>How far from the ray's origin the hit was, in metres.</summary>
        public float Distance { get; internal set; }

        public override string ToString()
        {
            return "Hit " + Entity + " at " + Point + " (" + Distance + " m)";
        }
    }

    /// <summary>Physics queries.</summary>
    public static class Physics
    {
        /// <summary>
        /// Casts a ray and reports the first collider it hits.
        /// </summary>
        /// <param name="origin">Where the ray starts, in world space.</param>
        /// <param name="direction">Which way it goes (any length).</param>
        /// <param name="hit">Filled in when it returns true.</param>
        /// <param name="maxDistance">How far to look, in metres.</param>
        /// <param name="ignore">An entity to see through, usually the shooter. Null = none.</param>
        public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit,
                                   float maxDistance = float.PositiveInfinity, Entity ignore = null)
        {
            hit = new RaycastHit();
            int index, generation;
            Vector3 point, normal;
            float distance;
            int ignoreIndex = ignore != null ? (int)ignore.Index : 0;
            int ignoreGeneration = ignore != null ? (int)ignore.Generation : 0;
            if (RaycastInternal(ref origin, ref direction, maxDistance, ignoreIndex, ignoreGeneration,
                                out index, out generation, out point, out normal, out distance) == 0)
                return false;
            hit.Entity = new Entity(unchecked((uint)index), unchecked((uint)generation));
            hit.Point = point;
            hit.Normal = normal;
            hit.Distance = distance;
            return true;
        }

        /// <summary>Does a ray hit anything? Same as above without the hit details.</summary>
        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance = float.PositiveInfinity,
                                   Entity ignore = null)
        {
            RaycastHit hit;
            return Raycast(origin, direction, out hit, maxDistance, ignore);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int RaycastInternal(ref Vector3 origin, ref Vector3 direction, float maxDistance,
                                                  int ignoreIndex, int ignoreGeneration, out int index,
                                                  out int generation, out Vector3 point, out Vector3 normal,
                                                  out float distance);
    }
}
