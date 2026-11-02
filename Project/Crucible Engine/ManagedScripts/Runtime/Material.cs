/*************************************************************************
* \file:         Material.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief         Material and Model: references to a .mat file and a model
*                file, for public script fields set in the Inspector.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*        public Material damagedLook;     // drag a .mat onto it in the Inspector
*        public Model brokenModel;        // drag a model file onto it
*        ...
*        GetComponent<MeshRenderer>().Material = damagedLook;
*
*    Saved as the asset's guid, so renaming or moving the file keeps the
*    link. A material that only exists inside a model (its imported
*    materials) can't be picked - make a .mat for it.
*
*    `assetId` is written by native code (ScriptRuntime.cpp), which makes
*    the object without running a constructor - keep that field name.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>A material file (.mat). Assign it in the Inspector.</summary>
    public sealed class Material
    {
        // Written by native code (ScriptRuntime.cpp, WriteFields). Do not rename.
#pragma warning disable 0649 // assigned from C++, which the compiler cannot see
        private ulong assetId;
#pragma warning restore 0649

        private Material() {}

        // The guid crosses to C++ as two ints (no `long` in internal calls).
        internal int IdLow { get { return unchecked((int)(uint)assetId); } }
        internal int IdHigh { get { return unchecked((int)(uint)(assetId >> 32)); } }

        internal static Material FromId(int low, int high)
        {
            ulong id = ((ulong)(uint)high << 32) | (uint)low;
            if (id == 0)
                return null;
            var material = new Material();
            material.assetId = id;
            return material;
        }

        /// <summary>The file name, e.g. "Rust.mat".</summary>
        public string Name { get { return GetNameInternal(IdLow, IdHigh) ?? string.Empty; } }

        public override bool Equals(object other)
        {
            var material = other as Material;
            return !ReferenceEquals(material, null) && material.assetId == assetId;
        }
        public override int GetHashCode() { return assetId.GetHashCode(); }
        public static bool operator ==(Material a, Material b)
        {
            if (ReferenceEquals(a, b)) return true;
            if (ReferenceEquals(a, null) || ReferenceEquals(b, null)) return false;
            return a.assetId == b.assetId;
        }
        public static bool operator !=(Material a, Material b) { return !(a == b); }
        public override string ToString() { return "Material " + Name; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetNameInternal(int idLow, int idHigh);
    }

    /// <summary>A model file (.gltf / .glb / .fbx ...). Assign it in the Inspector.</summary>
    public sealed class Model
    {
        // Written by native code (ScriptRuntime.cpp, WriteFields). Do not rename.
#pragma warning disable 0649 // assigned from C++, which the compiler cannot see
        private ulong assetId;
#pragma warning restore 0649

        private Model() {}

        internal int IdLow { get { return unchecked((int)(uint)assetId); } }
        internal int IdHigh { get { return unchecked((int)(uint)(assetId >> 32)); } }

        internal static Model FromId(int low, int high)
        {
            ulong id = ((ulong)(uint)high << 32) | (uint)low;
            if (id == 0)
                return null;
            var model = new Model();
            model.assetId = id;
            return model;
        }

        /// <summary>The file name, e.g. "Mech.glb".</summary>
        public string Name { get { return GetNameInternal(IdLow, IdHigh) ?? string.Empty; } }

        public override bool Equals(object other)
        {
            var model = other as Model;
            return !ReferenceEquals(model, null) && model.assetId == assetId;
        }
        public override int GetHashCode() { return assetId.GetHashCode(); }
        public static bool operator ==(Model a, Model b)
        {
            if (ReferenceEquals(a, b)) return true;
            if (ReferenceEquals(a, null) || ReferenceEquals(b, null)) return false;
            return a.assetId == b.assetId;
        }
        public static bool operator !=(Model a, Model b) { return !(a == b); }
        public override string ToString() { return "Model " + Name; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetNameInternal(int idLow, int idHigh);
    }
}
