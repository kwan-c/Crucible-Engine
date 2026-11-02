/*************************************************************************
* \file:         MeshRenderer.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.1
* \brief         Script access to an entity's MeshRenderer and
*                MaterialParams components. Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's MeshRenderer. Get it with GetComponent<MeshRenderer>() or
*    Entity.MeshRenderer:
*
*        var mesh = GetComponent<MeshRenderer>();
*        mesh.Visible = false;               // hide the mesh only
*        mesh.Material = damagedLook;        // a public Material field
*        mesh.Model = brokenModel;           // a public Model field
*        mesh.Mesh = Mesh.Sphere;            // a built-in mesh (clears Model)
*        mesh.SetMaterial(2, glowingVisor);  // one of the model's material slots
*
*    Visible hides just this entity's mesh: its collider, scripts, lights
*    and children carry on (SetActive(false) switches all of those off).
*    SetEnabled<MeshRenderer>(false) works too. Every change is drawn the
*    same frame.
*
*    Material is the material for the whole object (null = the default
*    material, or a model's own imported ones); SetMaterial(slot, m)
*    overrides one model slot and wins over Material. Changing Model clears
*    the slot overrides, and an AnimatedModel on the entity starts over.
*    Only the material the object USES changes - the .mat itself, and other
*    objects using it, are untouched.
*
*    MaterialParams (a separate component, added in the Inspector) gives each
*    object its own glow strength without its own material:
*
*        GetComponent<MaterialParams>().EmissiveScale = charged ? 4f : 1f;
*
*    C++ side: Engine/src/Scripting/Bindings/MeshRendererBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>Draws an entity's mesh or model. Unity's MeshRenderer.</summary>
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

        /// <summary>
        /// The material for the whole object. null = the default material (a plain mesh) or the
        /// model's own imported materials. A slot set with SetMaterial wins over it.
        /// </summary>
        public Material Material
        {
            get { int low, high; return GetMaterialInternal(I, G, out low, out high) != 0 ? Material.FromId(low, high) : null; }
            set { SetMaterialInternal(I, G, value != null ? value.IdLow : 0, value != null ? value.IdHigh : 0); }
        }

        /// <summary>
        /// The model drawn (null = none). Setting it clears the plain mesh and the slot
        /// overrides (as the Inspector does); an AnimatedModel on the entity starts over.
        /// </summary>
        public Model Model
        {
            get { int low, high; return GetModelInternal(I, G, out low, out high) != 0 ? Model.FromId(low, high) : null; }
            set { SetModelInternal(I, G, value != null ? value.IdLow : 0, value != null ? value.IdHigh : 0); }
        }

        /// <summary>
        /// The plain mesh drawn when there is no Model: a built-in one (Mesh.Cube ...), or null.
        /// Setting it clears Model and the slot overrides. Only built-in meshes can be set.
        /// </summary>
        public Mesh Mesh
        {
            get { int low, high; return GetMeshInternal(I, G, out low, out high) != 0 ? Mesh.FromId(low, high) : null; }
            set { SetMeshInternal(I, G, value != null ? value.IdLow : 0, value != null ? value.IdHigh : 0); }
        }

        /// <summary>How many material slots the model has (0 without a model).</summary>
        public int MaterialCount
        {
            get { int v; return GetMaterialCountInternal(I, G, out v) != 0 ? v : 0; }
        }

        /// <summary>The override on one of the model's material slots, or null (the model's own / Material).</summary>
        public Material GetMaterial(int slot)
        {
            int low, high;
            return GetSlotMaterialInternal(I, G, slot, out low, out high) != 0 ? Material.FromId(low, high) : null;
        }

        /// <summary>
        /// Overrides one of the model's material slots (0 .. MaterialCount - 1); null goes back
        /// to the model's own / Material. False without a model or for a slot it doesn't have.
        /// </summary>
        public bool SetMaterial(int slot, Material material)
        {
            return SetSlotMaterialInternal(I, G, slot, material != null ? material.IdLow : 0,
                                           material != null ? material.IdHigh : 0) != 0;
        }

        public override string ToString() { return "MeshRenderer on " + Entity; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetVisibleInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetVisibleInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMaterialInternal(int index, int generation, out int low, out int high);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetMaterialInternal(int index, int generation, int low, int high);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetModelInternal(int index, int generation, out int low, out int high);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetModelInternal(int index, int generation, int low, int high);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMeshInternal(int index, int generation, out int low, out int high);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetMeshInternal(int index, int generation, int low, int high);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMaterialCountInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetSlotMaterialInternal(int index, int generation, int slot, out int low, out int high);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetSlotMaterialInternal(int index, int generation, int slot, int low, int high);
    }

    /// <summary>
    /// Per-object material numbers (the MaterialParams component - add it in the Inspector).
    /// Objects sharing a material can differ here and still draw together.
    /// </summary>
    public sealed class MaterialParams
    {
        private readonly EntityIdentity entity;
        internal MaterialParams(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity these are on.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        /// <summary>
        /// Multiplies the material's emissive (glow): 0 = off, 1 = as the material says, more =
        /// brighter. For "charged / ready to fire" pulses. Not negative.
        /// </summary>
        public float EmissiveScale
        {
            get { float v; return GetEmissiveScaleInternal(I, G, out v) != 0 ? v : 1f; }
            set { SetEmissiveScaleInternal(I, G, ref value); }
        }

        /// <summary>Three spare numbers passed to the shader as they are, for effects to come.</summary>
        public Vector3 Custom
        {
            get { Vector3 v; return GetCustomInternal(I, G, out v) != 0 ? v : new Vector3(0f, 0f, 0f); }
            set { SetCustomInternal(I, G, ref value); }
        }

        public override string ToString() { return "MaterialParams on " + Entity; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetEmissiveScaleInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetEmissiveScaleInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetCustomInternal(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetCustomInternal(int index, int generation, ref Vector3 value);
    }
}
