/*************************************************************************
* \file:         Collider.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Script access to an entity's Collider component.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's BoxCollider / SphereCollider / CapsuleCollider in one class
*    (the engine has one Collider component with a Shape). Get it with
*    GetComponent<Collider>() or Entity.Collider:
*
*        var col = GetComponent<Collider>();
*        col.IsTrigger = true;               // a pickup: blocks nothing
*        col.Size = new Vector3(2, 1, 2);    // Box: full width, height, depth
*        SetEnabled<Collider>(false);        // no collisions at all
*
*    Which fields matter depends on Shape: Box reads Size; Sphere reads
*    Radius; Capsule reads Radius and Height. Sizes are FULL sizes (a 1m
*    cube is Size 1,1,1), before the Transform's scale - WorldSize etc.
*    give them after it.
*
*    Every change rebuilds the physics body in the same frame's step (a
*    moving body keeps its velocity). Cheap once in a while; don't animate
*    a size every frame on many bodies.
*
*    C++ side: Engine/src/Scripting/Bindings/ColliderBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>A collider's shape. Same values as ShapeType in Physics/PhysicsSystem.h.</summary>
    public enum ColliderShape
    {
        /// <summary>Reads Size.</summary>
        Box = 0,
        /// <summary>Reads Radius.</summary>
        Sphere = 1,
        /// <summary>Reads Radius and Height (total height, rounded ends included).</summary>
        Capsule = 2,
    }

    /// <summary>What a collider collides with. Same values as Physics/PhysicsLayers.h.</summary>
    public enum PhysicsLayer
    {
        /// <summary>The arena: floors, walls, ramps. Two NonMoving bodies never collide.</summary>
        NonMoving = 0,
        /// <summary>Crates and props: anything dynamic that is not a character.</summary>
        Moving = 1,
        /// <summary>The mech.</summary>
        Player = 2,
        /// <summary>The horde.</summary>
        Enemy = 3,
        /// <summary>Bullets as real bodies.</summary>
        Projectile = 4,
    }

    /// <summary>An entity's Collider component. Unity's Box/Sphere/CapsuleCollider.</summary>
    public sealed class Collider
    {
        private readonly EntityIdentity entity;
        internal Collider(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this collider is on.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        /// <summary>Box, Sphere or Capsule.</summary>
        public ColliderShape Shape
        {
            get { int v; return GetShapeInternal(I, G, out v) != 0 ? (ColliderShape)v : ColliderShape.Box; }
            set { SetShapeInternal(I, G, (int)value); }
        }

        /// <summary>Box: full width, height and depth, before scale. Every axis must be above 0.</summary>
        public Vector3 Size
        {
            get { Vector3 v; return GetSizeInternal(I, G, out v) != 0 ? v : new Vector3(1f, 1f, 1f); }
            set { SetSizeInternal(I, G, ref value); }
        }

        /// <summary>Offset of the shape from the entity's position, in local space.</summary>
        public Vector3 Center
        {
            get { Vector3 v; return GetCenterInternal(I, G, out v) != 0 ? v : new Vector3(0f, 0f, 0f); }
            set { SetCenterInternal(I, G, ref value); }
        }

        /// <summary>Sphere / Capsule radius, before scale. Must be above 0.</summary>
        public float Radius
        {
            get { float v; return GetRadiusInternal(I, G, out v) != 0 ? v : 0.5f; }
            set { SetRadiusInternal(I, G, ref value); }
        }

        /// <summary>Capsule: total height, rounded ends included, before scale. Must be above 0.</summary>
        public float Height
        {
            get { float v; return GetHeightInternal(I, G, out v) != 0 ? v : 2f; }
            set { SetHeightInternal(I, G, ref value); }
        }

        /// <summary>0 is ice, 1 is glue.</summary>
        public float Friction
        {
            get { float v; return GetFrictionInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetFrictionInternal(I, G, ref value); }
        }

        /// <summary>0 is a sandbag, 1 is a superball.</summary>
        public float Bounciness
        {
            get { float v; return GetBouncinessInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetBouncinessInternal(I, G, ref value); }
        }

        /// <summary>What it collides with. Anything that moves wants Moving, Player or Enemy.</summary>
        public PhysicsLayer Layer
        {
            get { int v; return GetLayerInternal(I, G, out v) != 0 ? (PhysicsLayer)v : PhysicsLayer.NonMoving; }
            set { SetLayerInternal(I, G, (int)value); }
        }

        /// <summary>
        /// Unity's isTrigger: blocks nothing and calls OnTriggerEnter / OnTriggerExit
        /// instead of OnCollisionEnter / OnCollisionExit.
        /// </summary>
        public bool IsTrigger
        {
            get { int v; return GetIsTriggerInternal(I, G, out v) != 0 && v != 0; }
            set { SetIsTriggerInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>Size after the Transform's scale - what physics really uses.</summary>
        public Vector3 WorldSize
        {
            get { Vector3 s; float r, h; return GetScaledInternal(I, G, out s, out r, out h) != 0 ? s : new Vector3(0f, 0f, 0f); }
        }

        /// <summary>Radius after the Transform's scale.</summary>
        public float WorldRadius
        {
            get { Vector3 s; float r, h; return GetScaledInternal(I, G, out s, out r, out h) != 0 ? r : 0f; }
        }

        /// <summary>Height after the Transform's scale.</summary>
        public float WorldHeight
        {
            get { Vector3 s; float r, h; return GetScaledInternal(I, G, out s, out r, out h) != 0 ? h : 0f; }
        }

        public override string ToString() { return Shape + " collider on " + Entity; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetShapeInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetShapeInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetSizeInternal(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetSizeInternal(int index, int generation, ref Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetCenterInternal(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetCenterInternal(int index, int generation, ref Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetRadiusInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetRadiusInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetHeightInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetHeightInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetFrictionInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetFrictionInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetBouncinessInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetBouncinessInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetLayerInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetLayerInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetIsTriggerInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetIsTriggerInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetScaledInternal(int index, int generation, out Vector3 size, out float radius, out float height);
    }
}
