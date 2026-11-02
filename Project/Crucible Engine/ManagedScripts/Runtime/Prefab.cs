/*************************************************************************
* \file:         Prefab.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         A .cprefab asset that scripts can spawn copies of.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Declare a public field and drag a .cprefab from the Asset Browser onto
*    it in the Inspector (Unity: `public GameObject bulletPrefab;`):
*
*        public Prefab bulletPrefab;
*        ...
*        Entity bullet = bulletPrefab.Instantiate(position, rotation);
*        bullet.GetComponent<Bullet>().speed = 40f;   // fields can be set at once
*        Entity.Destroy(bullet, 3f);                  // gone after 3 seconds
*
*    The new entities exist immediately; scripts on them get OnCreate on the
*    next frame. An unassigned field, or a prefab that was deleted, is null.
*
*    Created by the engine only: native code writes `assetId` into a new
*    object without running a constructor - keep that field name.
*
*    C++ side: Engine/src/Scripting/Bindings/PrefabBindings.cpp.
*
**************************************************************************/
using System;
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>
    /// A prefab asset to spawn with Instantiate. Assign it in the Inspector.
    /// </summary>
    public sealed class Prefab
    {
        // Written by native code (ScriptRuntime.cpp, WriteFields). Do not rename.
#pragma warning disable 0649 // assigned from C++, which the compiler cannot see
        private ulong assetId;
#pragma warning restore 0649

        private Prefab() {}

        // The guid crosses to C++ as two ints (no `long` in internal calls).
        private int IdLow { get { return unchecked((int)(uint)assetId); } }
        private int IdHigh { get { return unchecked((int)(uint)(assetId >> 32)); } }

        /// <summary>The prefab's file name, e.g. "Bullet".</summary>
        public string Name
        {
            get { return GetNameInternal(IdLow, IdHigh) ?? string.Empty; }
        }

        /// <summary>
        /// Spawns a copy as a root entity, where the prefab was saved.
        /// Returns its root entity, or null if it could not be spawned.
        /// </summary>
        public Entity Instantiate()
        {
            return Spawn(null);
        }

        /// <summary>Spawns a copy as a root entity at a position and rotation.</summary>
        public Entity Instantiate(Vector3 position, Quaternion rotation)
        {
            Entity root = Spawn(null);
            if (root != null)
            {
                root.Transform.Position = position;
                root.Transform.Rotation = rotation;
            }
            return root;
        }

        /// <summary>
        /// Spawns a copy as a child of <paramref name="parent"/>, keeping the prefab's
        /// saved transform as its local offset from the parent.
        /// </summary>
        public Entity Instantiate(Entity parent)
        {
            if (parent == null)
                throw new ArgumentNullException("parent");
            return Spawn(parent);
        }

        public override string ToString()
        {
            return "Prefab " + Name;
        }

        private Entity Spawn(Entity parent)
        {
            int index, generation;
            int parentIndex = parent != null ? (int)parent.Index : 0;
            int parentGeneration = parent != null ? (int)parent.Generation : 0;
            if (InstantiateInternal(IdLow, IdHigh, parentIndex, parentGeneration, out index, out generation) == 0)
                return null;
            return new Entity(unchecked((uint)index), unchecked((uint)generation));
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int InstantiateInternal(int idLow, int idHigh, int parentIndex, int parentGeneration,
                                                      out int index, out int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetNameInternal(int idLow, int idHigh);
    }
}
