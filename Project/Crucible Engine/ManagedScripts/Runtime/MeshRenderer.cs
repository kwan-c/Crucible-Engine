/*************************************************************************
* \file:         MeshRenderer.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Script access to an entity's MeshRenderer component.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's MeshRenderer. Get it with GetComponent<MeshRenderer>() or
*    Entity.MeshRenderer:
*
*        GetComponent<MeshRenderer>().Visible = false;   // hide the mesh only
*
*    Visible hides just this entity's mesh: its collider, scripts, lights
*    and children carry on (SetActive(false) switches all of those off).
*    SetEnabled<MeshRenderer>(false) works too. Drawn the same frame.
*
*    C++ side: Engine/src/Scripting/Bindings/ColliderBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>Draws an entity's mesh. Unity's MeshRenderer.</summary>
    public sealed class MeshRenderer
    {
        private readonly EntityIdentity entity;
        internal MeshRenderer(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this renderer is on.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        /// <summary>Is the mesh drawn? Hides only the mesh; everything else keeps running.</summary>
        public bool Visible
        {
            get { int v; return GetVisibleInternal(I, G, out v) != 0 && v != 0; }
            set { SetVisibleInternal(I, G, value ? 1 : 0); }
        }

        public override string ToString() { return "MeshRenderer on " + Entity; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetVisibleInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetVisibleInternal(int index, int generation, int value);
    }
}
