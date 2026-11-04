using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>
    /// An entity's position, rotation and scale. Position / Rotation / Scale are LOCAL
    /// (relative to the parent, like the Inspector); the World* properties are in world space.
    /// Directions follow the engine's convention: Forward is local -Z (as for the camera and lights).
    /// </summary>
    public sealed class Transform
    {
        private readonly EntityIdentity entity;
        internal Transform(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        // --- local (relative to the parent) ------------------------------------ //

        /// <summary>Position relative to the parent (world position for a root).</summary>
        public Vector3 Position
        {
            get { Vector3 v;    return GetPosition(I, G, out v) != 0 ? v : Vector3.Zero; }
            set { SetPosition(I, G, ref value); }
        }

        /// <summary>Scale relative to the parent.</summary>
        public Vector3 Scale
        {
            get { Vector3 v;    return GetScale(I, G, out v) != 0 ? v : Vector3.One; }
            set { SetScale(I, G, ref value); }
        }

        /// <summary>Rotation relative to the parent.</summary>
        public Quaternion Rotation
        {
            get { Quaternion v; return GetRotation(I, G, out v) != 0 ? v : Quaternion.Identity; }
            set { SetRotation(I, G, ref value); }
        }

        // --- world space ---------------------------------------------------------- //

        /// <summary>
        /// Position in world space (Unity's transform.position). Up to date even if
        /// something moved earlier this frame. Setting it moves the entity there,
        /// whatever its parent.
        /// </summary>
        public Vector3 WorldPosition
        {
            get { Vector3 v;    return GetWorldPosition(I, G, out v) != 0 ? v : Vector3.Zero; }
            set { SetWorldPosition(I, G, ref value); }
        }

        /// <summary>Rotation in world space (Unity's transform.rotation).</summary>
        public Quaternion WorldRotation
        {
            get { Quaternion v; return GetWorldRotation(I, G, out v) != 0 ? v : Quaternion.Identity; }
            set { SetWorldRotation(I, G, ref value); }
        }

        /// <summary>The way the entity faces, in world space (its local -Z). Length 1.</summary>
        public Vector3 Forward { get { return WorldRotation.Forward; } }
        /// <summary>The entity's right, in world space (its local +X). Length 1.</summary>
        public Vector3 Right   { get { return WorldRotation.Right; } }
        /// <summary>The entity's up, in world space (its local +Y). Length 1.</summary>
        public Vector3 Up      { get { return WorldRotation.Up; } }

        /// <summary>Turns the entity so Forward points at <paramref name="target"/> (a world position).</summary>
        public void LookAt(Vector3 target) { LookAt(target, Vector3.Up); }

        /// <summary>Turns the entity so Forward points at <paramref name="target"/>, keeping its top towards <paramref name="worldUp"/>.</summary>
        public void LookAt(Vector3 target, Vector3 worldUp)
        {
            Vector3 direction = target - WorldPosition;
            if (direction.Length > 1e-6f)
                WorldRotation = Quaternion.LookRotation(direction, worldUp);
        }

        /// <summary>Turns the entity so Forward points at another entity.</summary>
        public void LookAt(Entity target)
        {
            if (target != null)
                LookAt(target.Transform.WorldPosition);
        }

        // --- parent and children --------------------------------------------------- //

        /// <summary>
        /// The parent entity, or null for a root. Setting it is SetParent(value, true):
        /// the entity stays where it is in the world.
        /// </summary>
        public Entity Parent
        {
            get
            {
                int index, generation;
                return GetParentInternal(I, G, out index, out generation) != 0
                    ? new Entity(unchecked((uint)index), unchecked((uint)generation))
                    : null;
            }
            set { SetParent(value, true); }
        }

        /// <summary>
        /// Makes this entity the last child of <paramref name="parent"/> (null = a root).
        /// worldPositionStays true keeps it where it is in the world; false keeps its local
        /// values, so it jumps to the same offset from the new parent. Returns false (and logs
        /// why) if the change is refused, e.g. it would make a loop.
        /// </summary>
        public bool SetParent(Entity parent, bool worldPositionStays = true)
        {
            return SetParentInternal(I, G, parent != null ? (int)parent.Index : 0,
                                     parent != null ? (int)parent.Generation : 0, worldPositionStays ? 1 : 0) != 0;
        }

        /// <summary>How many direct children it has.</summary>
        public int ChildCount
        {
            get { int v; return GetChildCountInternal(I, G, out v) != 0 ? v : 0; }
        }

        /// <summary>The direct child at <paramref name="index"/> (Hierarchy order), or null if out of range.</summary>
        public Entity GetChild(int index)
        {
            int childIndex, childGeneration;
            return GetChildInternal(I, G, index, out childIndex, out childGeneration) != 0
                ? new Entity(unchecked((uint)childIndex), unchecked((uint)childGeneration))
                : null;
        }

        // --- internal calls: ComponentBindings.cpp (local) and HierarchyBindings.cpp ---- //

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetPosition(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetPosition(int index, int generation, ref Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetScale(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetScale(int index, int generation, ref Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetRotation(int index, int generation, out Quaternion value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetRotation(int index, int generation, ref Quaternion value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetWorldPosition(int index, int generation, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetWorldPosition(int index, int generation, ref Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetWorldRotation(int index, int generation, out Quaternion value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetWorldRotation(int index, int generation, ref Quaternion value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetParentInternal(int index, int generation, out int parentIndex, out int parentGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetParentInternal(int index, int generation, int parentIndex, int parentGeneration,
                                                    int worldPositionStays);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetChildCountInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetChildInternal(int index, int generation, int childIndex, out int outIndex,
                                                   out int outGeneration);
    }
}
