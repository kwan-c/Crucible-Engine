using System.Runtime.CompilerServices;

namespace Crucible
{
    // Bound to this script's entity. Only controls Animator parameters; no
    // movement, physics, input or root-motion APIs are introduced here.
    public sealed class Animator
    {
        private readonly EntityIdentity entity;
        internal Animator(EntityIdentity owner) { entity = owner; }
        public bool SetBool(string name, bool value) { return Set(name, 0, value ? 1 : 0, 0); }
        public bool SetFloat(string name, float value) { return Set(name, 1, 0, value); }
        public bool SetInteger(string name, int value) { return Set(name, 2, value, 0); }
        public bool SetTrigger(string name) { return Set(name, 3, 0, 0); }
        public bool ResetTrigger(string name) { return Set(name, 4, 0, 0); }
        private bool Set(string name, int operation, int integer, float number)
        {
            return SetParameterInternal(unchecked((int)entity.Index), unchecked((int)entity.Generation),
                name, operation, integer, number) != 0;
        }
        // int, rather than native bool, keeps the internal-call ABI explicit.
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetParameterInternal(int index, int generation, string name,
            int operation, int integer, float number);
    }
}
