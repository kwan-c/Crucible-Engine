/*************************************************************************
* \file:         Animator.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \author:       Hugo Low Ren Hao (v1.1: Speed, for hit stop)
* \par:          Email: low.h@digipen.edu
* \date          30 October 2026
* \version       1.1
* \brief         C# access to an entity's Animator controller parameters,
*                and its playback Speed. Part of Crucible.ScriptCore.dll.
*
* \details
*    Use it from a script as `Animator.SetBool("Running", true);`.
*    Each call goes to C++ (ManagedAnimatorParameter in Bindings/AnimatorBindings.cpp),
*    which queues the change for the animation system's next update.
*    Every setter returns false if the entity has no animator, the
*    parameter does not exist, or it has a different type.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>
    /// Sets parameters on the Animator controller of the script's entity.
    /// Get it through EntityScript.Animator.
    /// </summary>
    // Bound to this script's entity. Only controls Animator parameters; no
    // movement, physics, input or root-motion APIs are introduced here.
    public sealed class Animator
    {
        private readonly EntityIdentity entity;
        internal Animator(EntityIdentity owner)
        {
            entity = owner;
        }
        /// <summary>Sets a Bool parameter. Returns false if it could not be set.</summary>
        public bool SetBool(string name, bool value)
        {
            return Set(name, 0, value ? 1 : 0, 0);
        }
        /// <summary>Sets a Float parameter. Returns false if it could not be set.</summary>
        public bool SetFloat(string name, float value)
        {
            return Set(name, 1, 0, value);
        }
        /// <summary>Sets an Integer parameter. Returns false if it could not be set.</summary>
        public bool SetInteger(string name, int value)
        {
            return Set(name, 2, value, 0);
        }
        /// <summary>Fires a Trigger parameter. Returns false if it could not be set.</summary>
        public bool SetTrigger(string name)
        {
            return Set(name, 3, 0, 0);
        }
        /// <summary>Clears a Trigger that has not been consumed yet.</summary>
        public bool ResetTrigger(string name)
        {
            return Set(name, 4, 0, 0);
        }
        /// <summary>
        /// Playback speed (Unity's animator.speed): 1 normal, 0 holds the current pose (hit stop),
        /// 2 double speed. Reads 1 if the entity has no animated model; setting one does nothing.
        /// Unlike the parameters, it takes effect straight away (not queued).
        /// </summary>
        public float Speed
        {
            get { float v; return GetSpeedInternal(unchecked((int)entity.Index), unchecked((int)entity.Generation), out v) != 0 ? v : 1f; }
            set { SetSpeedInternal(unchecked((int)entity.Index), unchecked((int)entity.Generation), ref value); }
        }
        // Packs the call for C++. `operation` matches ScriptAnimatorOperation:
        // 0 Bool, 1 Float, 2 Integer, 3 Trigger, 4 ResetTrigger.
        private bool Set(string name, int operation, int integer, float number)
        {
            return SetParameterInternal(unchecked((int)entity.Index), unchecked((int)entity.Generation), name,
                                        operation, integer, number) != 0;
        }
        // Implemented in C++: ManagedAnimatorParameter() in Engine/src/Scripting/Bindings/AnimatorBindings.cpp.
        // int, rather than native bool, keeps the internal-call ABI explicit.
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetParameterInternal(int index, int generation, string name, int operation,
                                                       int integer, float number);
        // Implemented in C++: ManagedAnimatorGetSpeed / ManagedAnimatorSetSpeed, same file.
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetSpeedInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetSpeedInternal(int index, int generation, ref float value);
    }
}
