/*************************************************************************
* \file:         StateGraphAgent.cs
* \author:       Hugo Low Ren Hao (main author),
*                Jean David Vidal Cruzado (v1.1: target override)
* \par:          Email: low.h@digipen.edu,
*                Email: jeandavidvidal.c@digipen.edu
* \date          5 November 2026
* \version       1.1
* \brief         Script access to an enemy's StateGraphAgent: its brain's
*                variables and the state it's in. Unity's BehaviorGraphAgent.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Each enemy runs its own copy of its .stategraph, with its own copy of
*    the graph's variables (the Variables list in the State Graph window).
*    Scripts read and write them by name, the way Unity's
*    BehaviorGraphAgent does:
*
*        var brain = GetComponent<StateGraphAgent>();
*        brain.SetVariableValue("Home", Transform.Position);   // Vector3
*        brain.SetVariableValue("Alert", 1f);                  // float: note the f
*        float alert;
*        if (brain.GetVariableValue("Alert", out alert)) { ... }
*        if (brain.CurrentState == "Chase") { ... }
*
*    T can be Entity, Vector3, float, int or bool - the variable kinds the
*    editor offers (an entity list can't be set from C# yet). Only
*    variables the graph already has can be set, with the same type: a
*    typo returns false and logs a warning once, instead of quietly making
*    a new variable no node reads.
*
*    Setting works from OnCreate: the enemy's copy is made on demand.
*    TEMPORARY: the engine sets "Target" to the nearest player every frame
*    until perception exists, so setting Target yourself doesn't stick yet.
*    To point one enemy at something else (a mission objective), use
*    SetTargetOverride(entity): its Target is that entity every frame while
*    it exists; ClearTargetOverride() goes back to the nearest player.
*
* \history
*    -v1.0       first version
*    -v1.1       SetTargetOverride / ClearTargetOverride (Jean David)
*
*    C++ side: Engine/src/Scripting/Bindings/StateGraphBindings.cpp.
*
**************************************************************************/
using System;
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>An enemy's brain (Unity's BehaviorGraphAgent): its state graph's variables and current state.</summary>
    public sealed class StateGraphAgent
    {
        private readonly EntityIdentity entity;
        internal StateGraphAgent(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this agent is on.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        /// <summary>The name of the state it's in; empty before its first frame or without a graph.</summary>
        public string CurrentState
        {
            get { return GetCurrentStateInternal(I, G) ?? string.Empty; }
        }

        /// <summary>
        /// Sets one of the graph's variables. False (and a warning, once) if the graph has no
        /// variable by that name or it holds a different type. T: Entity, Vector3, float, int, bool.
        /// </summary>
        public bool SetVariableValue<T>(string name, T value)
        {
            if (string.IsNullOrEmpty(name))
                return false;
            object boxed = value;
            Type type = typeof(T);
            if (type == typeof(float)) return SetFloatInternal(I, G, name, (float)boxed) != 0;
            if (type == typeof(int)) return SetIntInternal(I, G, name, (int)boxed) != 0;
            if (type == typeof(bool)) return SetBoolInternal(I, G, name, (bool)boxed ? 1 : 0) != 0;
            if (type == typeof(Vector3))
            {
                Vector3 v = (Vector3)boxed;
                return SetVector3Internal(I, G, name, ref v) != 0;
            }
            if (type == typeof(Entity))
            {
                Entity e = boxed as Entity;
                return SetEntityInternal(I, G, name, e != null ? (int)e.Index : 0, e != null ? (int)e.Generation : 0) != 0;
            }
            throw new ArgumentException("SetVariableValue<" + type.Name + ">: use Entity, Vector3, float, int or bool.");
        }

        /// <summary>
        /// Reads one of the graph's variables. False (value = default) if there is none by that
        /// name or it holds a different type. An Entity variable holding nothing reads as null.
        /// </summary>
        public bool GetVariableValue<T>(string name, out T value)
        {
            value = default(T);
            if (string.IsNullOrEmpty(name))
                return false;
            Type type = typeof(T);
            if (type == typeof(float))
            {
                float v;
                if (GetFloatInternal(I, G, name, out v) == 0) return false;
                value = (T)(object)v;
                return true;
            }
            if (type == typeof(int))
            {
                int v;
                if (GetIntInternal(I, G, name, out v) == 0) return false;
                value = (T)(object)v;
                return true;
            }
            if (type == typeof(bool))
            {
                int v;
                if (GetBoolInternal(I, G, name, out v) == 0) return false;
                value = (T)(object)(v != 0);
                return true;
            }
            if (type == typeof(Vector3))
            {
                Vector3 v;
                if (GetVector3Internal(I, G, name, out v) == 0) return false;
                value = (T)(object)v;
                return true;
            }
            if (type == typeof(Entity))
            {
                int index, generation;
                if (GetEntityInternal(I, G, name, out index, out generation) == 0) return false;
                value = (T)(object)(index != 0 ? new Entity(unchecked((uint)index), unchecked((uint)generation)) : null);
                return true;
            }
            throw new ArgumentException("GetVariableValue<" + type.Name + ">: use Entity, Vector3, float, int or bool.");
        }

        /// <summary>
        /// This enemy's "Target" becomes `target` (instead of the nearest player) every frame while
        /// it exists; null = back to the nearest player. False outside Play or without a graph.
        /// </summary>
        public bool SetTargetOverride(Entity target)
        {
            return SetTargetOverrideInternal(I, G, target != null ? (int)target.Index : 0, target != null ? (int)target.Generation : 0) != 0;
        }

        /// <summary>Back to the nearest player.</summary>
        public bool ClearTargetOverride() { return SetTargetOverride(null); }

        public override string ToString() { return "StateGraphAgent on " + Entity; }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFloatInternal(int index, int generation, string name, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetIntInternal(int index, int generation, string name, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetBoolInternal(int index, int generation, string name, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetVector3Internal(int index, int generation, string name, ref Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetEntityInternal(int index, int generation, string name, int valueIndex, int valueGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFloatInternal(int index, int generation, string name, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetIntInternal(int index, int generation, string name, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBoolInternal(int index, int generation, string name, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetVector3Internal(int index, int generation, string name, out Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetEntityInternal(int index, int generation, string name, out int valueIndex, out int valueGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern string GetCurrentStateInternal(int index, int generation);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetTargetOverrideInternal(int index, int generation, int targetIndex, int targetGeneration);
    }
}
