/*************************************************************************
* \file:         Physics.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.1
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
using System.Runtime.InteropServices;   // StructLayout, NativeHit

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

        /// <summary>Casts a Ray (e.g. from Camera.ScreenPointToRay). See the overload above.</summary>
        public static bool Raycast(Ray ray, out RaycastHit hit, float maxDistance = float.PositiveInfinity,
                                   Entity ignore = null)
        {
            return Raycast(ray.Origin, ray.Direction, out hit, maxDistance, ignore);
        }

        /// <summary>Does a Ray hit anything?</summary>
        public static bool Raycast(Ray ray, float maxDistance = float.PositiveInfinity, Entity ignore = null)
        {
            RaycastHit hit;
            return Raycast(ray.Origin, ray.Direction, out hit, maxDistance, ignore);
        }

        // --- Added by Hugo (7 Nov 2026): layer masks, SphereCast, CheckSphere ---------- //
        // Unity's signatures, plus `ignore` last as in Xavier's Raycast. layerMask is a
        // LayerMask.GetMask(...) value; only bodies on those layers are hit, so e.g. a
        // walls-only ray looks straight past enemies standing in front of a wall.

        /// <summary>Raycast that only hits bodies on the layers in layerMask (Unity's layerMask argument).</summary>
        public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance,
                                   int layerMask, Entity ignore = null)
        {
            hit = new RaycastHit();
            int index, generation;
            Vector3 point, normal;
            float distance;
            if (RaycastMaskedInternal(ref origin, ref direction, maxDistance, unchecked((uint)layerMask),
                                      IgnoreIndex(ignore), IgnoreGeneration(ignore),
                                      out index, out generation, out point, out normal, out distance) == 0)
                return false;
            hit = MakeHit(index, generation, point, normal, distance);
            return true;
        }

        /// <summary>
        /// Sweeps a ball of radius from origin along direction and reports the first body it
        /// hits (Unity's Physics.SphereCast) - "would something this wide fit through?".
        /// Unlike Unity, a body the ball already overlaps at the start IS reported, at
        /// Distance 0; skip those if you want Unity's behaviour.
        /// </summary>
        public static bool SphereCast(Vector3 origin, float radius, Vector3 direction, out RaycastHit hit,
                                      float maxDistance = float.PositiveInfinity, int layerMask = LayerMask.All,
                                      Entity ignore = null)
        {
            hit = new RaycastHit();
            int index, generation;
            Vector3 point, normal;
            float distance;
            if (SphereCastInternal(ref origin, radius, ref direction, maxDistance, unchecked((uint)layerMask),
                                   IgnoreIndex(ignore), IgnoreGeneration(ignore),
                                   out index, out generation, out point, out normal, out distance) == 0)
                return false;
            hit = MakeHit(index, generation, point, normal, distance);
            return true;
        }

        /// <summary>Is anything on layerMask's layers inside a ball at position right now? (Unity's Physics.CheckSphere.)</summary>
        public static bool CheckSphere(Vector3 position, float radius, int layerMask = LayerMask.All, Entity ignore = null)
        {
            return CheckSphereInternal(ref position, radius, unchecked((uint)layerMask),
                                       IgnoreIndex(ignore), IgnoreGeneration(ignore)) != 0;
        }

        // --- non-allocating queries (Jean David, 10 Nov 2026) ------------------- //
        // Unity's *NonAlloc forms: pass your own results array (keep it between
        // calls) and get back how many were found, nearest-first for rays. Entities
        // destroyed this frame are left out. Only during Play, like Raycast.

        /// <summary>Every entity whose collider overlaps a ball. Unity's Physics.OverlapSphereNonAlloc.</summary>
        /// <returns>How many entries of results were filled.</returns>
        public static int OverlapSphereNonAlloc(Vector3 position, float radius, Entity[] results,
                                                int layerMask = LayerMask.All, Entity ignore = null)
        {
            if (results == null || results.Length == 0) return 0;
            int[] pairs = PairScratch(results.Length);
            int count = OverlapSphereInternal(ref position, radius, unchecked((uint)layerMask),
                                              IgnoreIndex(ignore), IgnoreGeneration(ignore), pairs, results.Length);
            FillEntities(pairs, count, results);
            return count;
        }

        /// <summary>Every entity whose collider overlaps a box (halfExtents = half its size). Unity's Physics.OverlapBoxNonAlloc.</summary>
        /// <returns>How many entries of results were filled.</returns>
        public static int OverlapBoxNonAlloc(Vector3 center, Vector3 halfExtents, Entity[] results, Quaternion orientation,
                                             int layerMask = LayerMask.All, Entity ignore = null)
        {
            if (results == null || results.Length == 0) return 0;
            int[] pairs = PairScratch(results.Length);
            int count = OverlapBoxInternal(ref center, ref halfExtents, ref orientation, unchecked((uint)layerMask),
                                           IgnoreIndex(ignore), IgnoreGeneration(ignore), pairs, results.Length);
            FillEntities(pairs, count, results);
            return count;
        }

        /// <summary>Every entity whose collider overlaps a capsule between two points. Unity's Physics.OverlapCapsuleNonAlloc.</summary>
        /// <returns>How many entries of results were filled.</returns>
        public static int OverlapCapsuleNonAlloc(Vector3 point0, Vector3 point1, float radius, Entity[] results,
                                                 int layerMask = LayerMask.All, Entity ignore = null)
        {
            if (results == null || results.Length == 0) return 0;
            int[] pairs = PairScratch(results.Length);
            int count = OverlapCapsuleInternal(ref point0, ref point1, radius, unchecked((uint)layerMask),
                                               IgnoreIndex(ignore), IgnoreGeneration(ignore), pairs, results.Length);
            FillEntities(pairs, count, results);
            return count;
        }

        /// <summary>Every collider a ray passes through, nearest first. Unity's Physics.RaycastNonAlloc.</summary>
        /// <returns>How many entries of results were filled.</returns>
        public static int RaycastNonAlloc(Vector3 origin, Vector3 direction, RaycastHit[] results,
                                          float maxDistance = float.PositiveInfinity, int layerMask = LayerMask.All,
                                          Entity ignore = null)
        {
            if (results == null || results.Length == 0) return 0;
            if (hitScratch == null || hitScratch.Length < results.Length) hitScratch = new NativeHit[results.Length];
            int count = RaycastAllInternal(ref origin, ref direction, maxDistance, unchecked((uint)layerMask),
                                           IgnoreIndex(ignore), IgnoreGeneration(ignore), hitScratch, results.Length);
            for (int i = 0; i < count; ++i)
            {
                NativeHit h = hitScratch[i];
                results[i] = MakeHit(h.Index, h.Generation, h.Point, h.Normal, h.Distance);
            }
            return count;
        }

        /// <summary>
        /// Sweeps a capsule (between point1 and point2) along direction and reports the first
        /// collider it hits. Unity's Physics.CapsuleCast. A collider it already overlaps at
        /// the start is hit at distance 0, as SphereCast does.
        /// </summary>
        public static bool CapsuleCast(Vector3 point1, Vector3 point2, float radius, Vector3 direction, out RaycastHit hit,
                                       float maxDistance = float.PositiveInfinity, int layerMask = LayerMask.All,
                                       Entity ignore = null)
        {
            hit = new RaycastHit();
            int index, generation;
            Vector3 point, normal;
            float distance;
            if (CapsuleCastInternal(ref point1, ref point2, radius, ref direction, maxDistance, unchecked((uint)layerMask),
                                    IgnoreIndex(ignore), IgnoreGeneration(ignore),
                                    out index, out generation, out point, out normal, out distance) == 0)
                return false;
            hit = MakeHit(index, generation, point, normal, distance);
            return true;
        }

        // One hit as the C++ side writes it. Must match ScriptHit in PhysicsBindings.cpp.
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeHit
        {
            public int Index;
            public int Generation;
            public Vector3 Point;
            public Vector3 Normal;
            public float Distance;
        }

        // Reused between calls (scripts run on one thread), so a query every frame makes no garbage.
        private static int[] pairScratch;
        private static NativeHit[] hitScratch;

        static int[] PairScratch(int count)
        {
            if (pairScratch == null || pairScratch.Length < count * 2) pairScratch = new int[count * 2];
            return pairScratch;
        }

        static void FillEntities(int[] pairs, int count, Entity[] results)
        {
            for (int i = 0; i < count; ++i)
                results[i] = new Entity(unchecked((uint)pairs[i * 2]), unchecked((uint)pairs[i * 2 + 1]));
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int OverlapSphereInternal(ref Vector3 position, float radius, uint layerMask, int ignoreIndex,
                                                        int ignoreGeneration, int[] pairs, int maxCount);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int OverlapBoxInternal(ref Vector3 center, ref Vector3 halfExtents, ref Quaternion orientation,
                                                     uint layerMask, int ignoreIndex, int ignoreGeneration, int[] pairs, int maxCount);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int OverlapCapsuleInternal(ref Vector3 point0, ref Vector3 point1, float radius, uint layerMask,
                                                         int ignoreIndex, int ignoreGeneration, int[] pairs, int maxCount);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int RaycastAllInternal(ref Vector3 origin, ref Vector3 direction, float maxDistance, uint layerMask,
                                                     int ignoreIndex, int ignoreGeneration, NativeHit[] hits, int maxCount);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int CapsuleCastInternal(ref Vector3 point1, ref Vector3 point2, float radius, ref Vector3 direction,
                                                      float maxDistance, uint layerMask, int ignoreIndex, int ignoreGeneration,
                                                      out int index, out int generation, out Vector3 point, out Vector3 normal,
                                                      out float distance);

        static int IgnoreIndex(Entity ignore) { return ignore != null ? (int)ignore.Index : 0; }
        static int IgnoreGeneration(Entity ignore) { return ignore != null ? (int)ignore.Generation : 0; }

        static RaycastHit MakeHit(int index, int generation, Vector3 point, Vector3 normal, float distance)
        {
            RaycastHit hit = new RaycastHit();
            hit.Entity = new Entity(unchecked((uint)index), unchecked((uint)generation));
            hit.Point = point;
            hit.Normal = normal;
            hit.Distance = distance;
            return hit;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int RaycastMaskedInternal(ref Vector3 origin, ref Vector3 direction, float maxDistance,
                                                        uint layerMask, int ignoreIndex, int ignoreGeneration,
                                                        out int index, out int generation, out Vector3 point,
                                                        out Vector3 normal, out float distance);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SphereCastInternal(ref Vector3 origin, float radius, ref Vector3 direction,
                                                     float maxDistance, uint layerMask, int ignoreIndex,
                                                     int ignoreGeneration, out int index, out int generation,
                                                     out Vector3 point, out Vector3 normal, out float distance);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int CheckSphereInternal(ref Vector3 position, float radius, uint layerMask,
                                                      int ignoreIndex, int ignoreGeneration);

        /// <summary>
        /// An explosion at position: every dynamic body within radius is pushed away (as
        /// RigidBody.AddExplosionForce), full force at the centre fading to nothing at the
        /// edge; upwardsModifier throws them up. Returns how many entities it reached -
        /// characters and static things too, which aren't moved. Damage is up to the caller.
        /// </summary>
        public static int Explode(Vector3 position, float radius, float force, float upwardsModifier = 0f)
        {
            return ExplodeInternal(ref position, radius, force, upwardsModifier);
        }

        // Added by Hugo (7 Nov 2026), C++ side in PhysicsBindings.cpp.
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int ExplodeInternal(ref Vector3 position, float radius, float force, float upwardsModifier);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int RaycastInternal(ref Vector3 origin, ref Vector3 direction, float maxDistance,
                                                  int ignoreIndex, int ignoreGeneration, out int index,
                                                  out int generation, out Vector3 point, out Vector3 normal,
                                                  out float distance);
    }
}
