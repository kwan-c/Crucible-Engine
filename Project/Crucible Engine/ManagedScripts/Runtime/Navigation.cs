/*************************************************************************
* \file:         Navigation.cs
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          7 November 2026
* \version       1.0
* \brief         Questions about where enemies can go, answered from the
*                FlowFieldSurface's baked grid. Unity's NavMesh static
*                class (SamplePosition, Raycast). Part of
*                Crucible.ScriptCore.dll.
*
* \details
*    For AI that picks its own spots - firing positions, slots round the
*    player, a place to retreat to - before telling a FlowFieldAgent to go:
*
*        if (Navigation.IsWalkable(spot) && Navigation.IsPathClear(Position, spot))
*            agent.MoveTo(spot, 0.3f);
*
*    Top-down only: heights are ignored, so pair it with a ray down to find
*    the ground at the spot. Both answer false while nothing is baked (in
*    Edit before a Bake, or with no FlowFieldSurface in the scene).
*
*    Unlike Unity's NavMesh.Raycast, which returns true when the way is
*    BLOCKED, IsPathClear returns true when it is clear - read as it's named.
*
*    C++ side: Engine/src/Scripting/Bindings/NavigationBindings.cpp, which
*    asks ECSNavigationSystem::Active().
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>Where enemies can stand and walk, from the baked flow-field grid (Unity's NavMesh queries).</summary>
    public static class Navigation
    {
        /// <summary>
        /// Could an enemy stand here? Inside the surface, no obstacle on the cell, not a terrain
        /// wall. Height is ignored. False while nothing is baked.
        /// </summary>
        public static bool IsWalkable(Vector3 position) { return IsWalkableInternal(ref position) != 0; }

        /// <summary>
        /// Could an enemy walk straight from "from" to "to" without crossing a blocked cell or a
        /// wall? Blocked cells right at the start are allowed, so an enemy shoved into one can
        /// still walk out. False while nothing is baked.
        /// </summary>
        public static bool IsPathClear(Vector3 from, Vector3 to) { return IsPathClearInternal(ref from, ref to) != 0; }

        /// <summary>
        /// Bakes the grid again once physics has caught up (a frame or two), for a script
        /// that just built or changed the ground mid-Play: call it after Terrain.Build.
        /// The old grid is used until then: wait for BakeCount to change to know the new one is in.
        /// </summary>
        /// <returns>False outside Play or with no navigation.</returns>
        public static bool Rebake() { return RebakeInternal() != 0; }

        /// <summary>True once a grid has been baked this Play (by Play itself or a Rebake).</summary>
        public static bool IsBaked { get { return IsBakedInternal() != 0; } }

        /// <summary>How many bakes have finished so far. Read it before Rebake(); once it changes, the new grid is in.</summary>
        public static int BakeCount { get { return GetBakeCountInternal(); } }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int IsWalkableInternal(ref Vector3 position);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int RebakeInternal();
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int IsBakedInternal();
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBakeCountInternal();
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int IsPathClearInternal(ref Vector3 from, ref Vector3 to);
    }
}
