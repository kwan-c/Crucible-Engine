/*************************************************************************
* \file:         EntityScript.cs
* \author:       <author>
* \par:          Email: <email>
* \date          30 October 2026
* \version       1.0
* \brief         Base class for gameplay scripts, plus entity identity and
*                logging. Part of Crucible.ScriptCore.dll.
*
* \details
*    To write a script, inherit EntityScript and override the callbacks:
*
*        public sealed class Player : EntityScript
*        {
*            public float speed = 5f;            // shows in the Inspector
*            public override void OnUpdate(float deltaTime) { ... }
*        }
*
*    The engine (EntityScriptSystem / ScriptRuntime) creates the object when
*    Play starts, calls BindEntity, applies Inspector values, then calls
*    OnCreate once and OnUpdate every frame, and OnDestroy on Stop.
*
**************************************************************************/
using System;
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>
    /// Which entity a script belongs to. Index is the entity's slot and
    /// Generation tells apart different entities that reused the same slot.
    /// </summary>
    // A lifetime-bound identity, not an IsAlive test or a serializable scene ID.
    // Native subsystem bindings must validate owner lifetime before world access.
    public struct EntityIdentity
    {
        /// <summary>The entity's slot number.</summary>
        public uint Index { get; private set; }
        /// <summary>Increments each time the slot is reused.</summary>
        public uint Generation { get; private set; }
        internal EntityIdentity(uint index, uint generation)
        {
            Index = index;
            Generation = generation;
        }
        /// <summary>Formats as "index:generation", e.g. "12:3".</summary>
        public override string ToString()
        {
            return "Entity " + Index + ":" + Generation;
        }
    }

    /// <summary>
    /// Base class for every script attached to an entity through a Script
    /// component. Override OnCreate, OnUpdate and OnDestroy.
    /// </summary>
    public abstract class EntityScript
    {
        private bool bound;
        /// <summary>
        /// The entity this script is attached to. Other entities are referenced
        /// with public Entity fields set in the Inspector (see Entity.cs).
        /// </summary>
        public Entity Entity { get; private set; }

        // Entity components (shortcuts for Entity.Transform etc.) --------
        // New components go on Entity (Entity.cs) first, then get a shortcut here.
        public Transform Transform { get { return Entity.Transform; } }

        public RigidBody RigidBody { get { return Entity.RigidBody; } }

        /// <summary>Sets parameters on this entity's Animator controller.</summary>
        public Animator Animator { get { return Entity.Animator; } }

        /// <summary>This entity's AudioSource (needs an AudioSource component).</summary>
        public AudioSource AudioSource { get { return Entity.AudioSource; } }

        /// <summary>This entity's CharacterController (needs a CharacterController component).</summary>
        public CharacterController CharacterController { get { return Entity.CharacterController; } }

        /// <summary>This entity's component or script of type T, or null. See Entity.GetComponent.</summary>
        public T GetComponent<T>() where T : class { return Entity.GetComponent<T>(); }

        /// <summary>Does this entity have a component or script of type T?</summary>
        public bool HasComponent<T>() where T : class { return Entity.HasComponent<T>(); }

        /// <summary>
        /// Destroys an entity (e.g. Destroy(Entity) for this one) at the end of the frame,
        /// or after <paramref name="delay"/> seconds. Same as Entity.Destroy.
        /// </summary>
        public static void Destroy(Entity entity, float delay = 0f) { global::Crucible.Entity.Destroy(entity, delay); }

        /// <summary>
        /// This script on or off (Unity's MonoBehaviour.enabled). While off it gets no OnUpdate
        /// or UI callbacks but keeps its fields; switch it back on from another script.
        /// </summary>
        public bool Enabled
        {
            get { return Entity.IsEnabled<EntityScript>(); }
            set { Entity.SetEnabled<EntityScript>(value); }
        }

        /// <summary>
        /// Called by the engine (ScriptRuntime) right after construction and
        /// before OnCreate. Gives the script its entity. Can only happen once.
        /// </summary>
        internal void BindEntity(int index, int generation)
        {
            if (bound)
                throw new InvalidOperationException("An EntityScript instance cannot be rebound.");
            Entity = new global::Crucible.Entity(unchecked((uint)index), unchecked((uint)generation));
            bound = true;
        }
        /// <summary>Called once when Play starts (or when the script is first enabled).</summary>
        public virtual void OnCreate() {}
        /// <summary>Called every frame while Playing and enabled.</summary>
        /// <param name="deltaTime">Seconds since the previous frame.</param>
        public virtual void OnUpdate(float deltaTime) {}
        /// <summary>Called once when Play stops, the scene changes, or the script/entity is removed.</summary>
        public virtual void OnDestroy() {}

        // --- UI callbacks ---------------------------------------------------------
        // Every running script hears every UI event, once, in the frame it happens and
        // before that frame's OnUpdate (Unity's onClick listeners, without wiring each one).
        // Compare the entity with your own fields: if (button == startButton) ...
        // Polling (UIButton.WasClicked etc.) still works too.

        /// <summary>A UIButton was clicked (pressed and released over it).</summary>
        public virtual void OnButtonClicked(Entity button) {}
        /// <summary>The player changed a UISlider (not when code sets Value).</summary>
        public virtual void OnSliderChanged(Entity slider, float value) {}
        /// <summary>A button whose On Click is Custom was clicked; actionId is its Action Id.</summary>
        public virtual void OnUIAction(string actionId) {}

        /// <summary>
        /// Called by the engine (ScriptRuntime::DispatchUIEvent) with plain values; builds
        /// the Entity and calls the matching virtual above. kind: 0 click, 1 slider, 2 action.
        /// </summary>
        internal void DispatchUIEvent(int kind, int index, int generation, float value, string actionId)
        {
            var source = new global::Crucible.Entity(unchecked((uint)index), unchecked((uint)generation));
            switch (kind)
            {
                case 0: OnButtonClicked(source); break;
                case 1: OnSliderChanged(source, value); break;
                case 2: OnUIAction(actionId ?? string.Empty); break;
            }
        }
    }

    /// <summary>
    /// Writes messages to the Editor Console. (Console.WriteLine only goes to
    /// the process's standard output, which the Editor does not show.)
    /// </summary>
    public static class Log
    {
        /// <summary>Normal message.</summary>
        public static void Info(string message)
        {
            WriteInternal(0, message);
        }
        /// <summary>Warning (shown in yellow).</summary>
        public static void Warning(string message)
        {
            WriteInternal(1, message);
        }
        /// <summary>Error (shown in red).</summary>
        public static void Error(string message)
        {
            WriteInternal(2, message);
        }
        // Implemented in C++: ManagedLog() in Engine/src/Scripting/Bindings/LogBindings.cpp.
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void WriteInternal(int level, string message);
    }
}
