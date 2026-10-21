using System;
using System.Runtime.CompilerServices;

namespace Crucible
{
    // A lifetime-bound identity, not an IsAlive test or a serializable scene ID.
    // Native subsystem bindings must validate owner lifetime before world access.
    public struct EntityIdentity
    {
        public uint Index { get; private set; }
        public uint Generation { get; private set; }
        internal EntityIdentity(uint index, uint generation) { Index = index; Generation = generation; }
        public override string ToString() { return Index + ":" + Generation; }
    }

    public abstract class EntityScript
    {
        private bool bound;
        public EntityIdentity Entity { get; private set; }
        internal void BindEntity(int index, int generation)
        {
            if (bound) throw new InvalidOperationException("An EntityScript instance cannot be rebound.");
            Entity = new EntityIdentity(unchecked((uint)index), unchecked((uint)generation));
            bound = true;
        }
        public virtual void OnCreate() { }
        public virtual void OnUpdate(float deltaTime) { }
        public virtual void OnDestroy() { }
    }

    public static class Log
    {
        public static void Info(string message) { WriteInternal(0, message); }
        public static void Warning(string message) { WriteInternal(1, message); }
        public static void Error(string message) { WriteInternal(2, message); }
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void WriteInternal(int level, string message);
    }
}
