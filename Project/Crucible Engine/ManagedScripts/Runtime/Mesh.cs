/*************************************************************************
* \file:         Mesh.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief         The engine's built-in meshes (Mesh.Cube, Mesh.Sphere ...),
*                for MeshRenderer.Mesh. Part of Crucible.ScriptCore.dll.
*
* \details
*        GetComponent<MeshRenderer>().Mesh = Mesh.Sphere;
*
*    Code only: these are not files, so there is no `public Mesh` field to
*    pick in the Inspector (yet). Their ids are fixed hashes of the
*    primitive's name (ResourceManager::BuiltinId), the same ids scenes
*    already save for a MeshRenderer showing a built-in mesh.
*
*    Setting MeshRenderer.Mesh clears its Model (a model always wins over a
*    mesh), as the Inspector's Mesh picker does.
*
*    C++ side: Engine/src/Scripting/Bindings/MeshRendererBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>A mesh: one of the engine's built-in primitives. Unity's Mesh (for MeshFilter.mesh).</summary>
    public sealed class Mesh
    {
        private readonly ulong assetId;
        private Mesh(ulong id) { assetId = id; }

        internal int IdLow { get { return unchecked((int)(uint)assetId); } }
        internal int IdHigh { get { return unchecked((int)(uint)(assetId >> 32)); } }

        internal static Mesh FromId(int low, int high)
        {
            ulong id = ((ulong)(uint)high << 32) | (uint)low;
            return id == 0 ? null : new Mesh(id);
        }

        // The built-in mesh by its renderer name, or null if there is none (or no renderer yet).
        private static Mesh Builtin(string name)
        {
            int low, high;
            return GetBuiltinInternal(name, out low, out high) != 0 ? FromId(low, high) : null;
        }

        /// <summary>A 1 m cube.</summary>
        public static Mesh Cube { get { return Builtin("Cube"); } }
        public static Mesh Sphere { get { return Builtin("Sphere"); } }
        public static Mesh Capsule { get { return Builtin("Capsule"); } }
        public static Mesh Cylinder { get { return Builtin("Cylinder"); } }
        public static Mesh Cone { get { return Builtin("Cone"); } }
        public static Mesh Pyramid { get { return Builtin("Pyramid"); } }
        /// <summary>A flat square, facing up.</summary>
        public static Mesh Plane { get { return Builtin("Plane"); } }
        /// <summary>A flat circle, facing up.</summary>
        public static Mesh Disc { get { return Builtin("Disc"); } }

        /// <summary>"Cube", "Sphere", ... (or a model file's path, for a mesh set from one in the Inspector).</summary>
        public string Name { get { return GetNameInternal(IdLow, IdHigh) ?? string.Empty; } }

        public override bool Equals(object other)
        {
            var mesh = other as Mesh;
            return !ReferenceEquals(mesh, null) && mesh.assetId == assetId;
        }
        public override int GetHashCode() { return assetId.GetHashCode(); }
        public static bool operator ==(Mesh a, Mesh b)
        {
            if (ReferenceEquals(a, b)) return true;
            if (ReferenceEquals(a, null) || ReferenceEquals(b, null)) return false;
            return a.assetId == b.assetId;
        }
        public static bool operator !=(Mesh a, Mesh b) { return !(a == b); }
        public override string ToString() { return "Mesh " + Name; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetBuiltinInternal(string name, out int low, out int high);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetNameInternal(int idLow, int idHigh);
    }
}
