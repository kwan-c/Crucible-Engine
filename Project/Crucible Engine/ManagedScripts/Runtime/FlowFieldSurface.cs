/*************************************************************************
* \file:         FlowFieldSurface.cs
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.1
* \brief         Script access to a FlowFieldSurface: the ground enemies
*                navigate. Unity's NavMeshSurface. Part of
*                Crucible.ScriptCore.dll.
*
* \details
*    Only the settings read each time the field is rebuilt are here:
*
*        var surface = Entity.Find("Ground").GetComponent<FlowFieldSurface>();
*        surface.FloodRadius = 0f;      // boss fight: arrows across the whole level
*
*    A change shows at the next rebuild, after the player has moved
*    RebuildDistance. DynamicUpdateInterval (v1.1) applies at once. Cell
*    size, heights and padding are baked once at Play, so they're set in
*    the Inspector only.
*
*    C++ side: Engine/src/Scripting/Bindings/NavigationBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>The ground enemies navigate (Unity's NavMeshSurface): how far the field reaches and how often it updates.</summary>
    public sealed class FlowFieldSurface
    {
        private readonly EntityIdentity entity;
        internal FlowFieldSurface(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this surface is on.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        /// <summary>Metres round the player that get arrows; 0 = the whole surface. Beyond it enemies head straight in.</summary>
        public float FloodRadius
        {
            get { float v; return GetFloodRadiusInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetFloodRadiusInternal(I, G, ref value); }
        }

        /// <summary>Metres the player must move before the field is rebuilt. Smaller = fresher arrows, more work.</summary>
        public float RebuildDistance
        {
            get { float v; return GetRebuildDistanceInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetRebuildDistanceInternal(I, G, ref value); }
        }

        /// <summary>Seconds between checks of the DynamicObstacles while Playing; 0 = every frame. A moved obstacle's cells follow it after at most this long.</summary>
        public float DynamicUpdateInterval
        {
            get { float v; return GetDynamicUpdateIntervalInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetDynamicUpdateIntervalInternal(I, G, ref value); }
        }

        public override string ToString() { return "FlowFieldSurface on " + Entity; }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFloodRadiusInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFloodRadiusInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetRebuildDistanceInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetRebuildDistanceInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetDynamicUpdateIntervalInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetDynamicUpdateIntervalInternal(int index, int generation, ref float value);
    }
}
