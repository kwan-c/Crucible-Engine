/*************************************************************************
* \file:         EntityScript.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
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

        /// <summary>Every component or script of type T on this entity. See Entity.GetComponents.</summary>
        public T[] GetComponents<T>() where T : class { return Entity.GetComponents<T>(); }

        /// <summary>Does this entity have a component or script of type T?</summary>
        public bool HasComponent<T>() where T : class { return Entity.HasComponent<T>(); }

        /// <summary>
        /// Destroys an entity (e.g. Destroy(Entity) for this one) at the end of the frame,
        /// or after <paramref name="delay"/> seconds. Same as Entity.Destroy.
        /// </summary>
        public static void Destroy(Entity entity, float delay = 0f) { global::Crucible.Entity.Destroy(entity, delay); }

        /// <summary>
        /// Keeps a root entity (e.g. DontDestroyOnLoad(Entity) for this one) alive through
        /// scene loads. Same as Scene.DontDestroyOnLoad.
        /// </summary>
        public static bool DontDestroyOnLoad(Entity entity) { return global::Crucible.Scene.DontDestroyOnLoad(entity); }

        /// <summary>
        /// This script on or off (Unity's MonoBehaviour.enabled). While off it gets no OnUpdate
        /// or callbacks but keeps its fields; switch it back on from another script. Only this
        /// script: the entity's other scripts carry on. Applies from the next frame.
        /// (Entity.SetEnabled&lt;EntityScript&gt;(false) / SetActive(false) pause ALL of the
        /// entity's scripts without changing this.)
        /// </summary>
        public bool Enabled
        {
            get { int on; return GetEnabledInternal(this, (int)Entity.Index, (int)Entity.Generation, out on) != 0 && on != 0; }
            set { SetEnabledInternal(this, (int)Entity.Index, (int)Entity.Generation, value ? 1 : 0); }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetEnabledInternal(EntityScript script, int index, int generation, out int enabled);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetEnabledInternal(EntityScript script, int index, int generation, int enabled);

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

        // --- physics callbacks ----------------------------------------------------
        // Called on the scripts of BOTH entities involved (Unity's rule), at the start of
        // the frame after the physics step that saw it, before OnUpdate. The entity may
        // have been destroyed since: check other.IsAlive before using it.
        // Needs a Collider (and RigidBody / CharacterController) on the entities; a
        // Collider with Is Trigger ticked gives trigger events instead of collisions.

        /// <summary>This entity started touching another solid collider.</summary>
        public virtual void OnCollisionEnter(Collision collision) {}
        /// <summary>It stopped touching it (or the other was destroyed). Only Other is set.</summary>
        public virtual void OnCollisionExit(Collision collision) {}
        /// <summary>Something entered this trigger - or this entered another's trigger.</summary>
        public virtual void OnTriggerEnter(Entity other) {}
        /// <summary>Something left this trigger - or this left another's trigger.</summary>
        public virtual void OnTriggerExit(Entity other) {}

        // --- network callbacks ------------------------------------------------------
        // Multiplayer (see Network.cs). Called at the start of the frame the event
        // happened in, before OnUpdate, in the order the events arrived. Every running
        // script hears them, except a command aimed at one entity (see OnNetworkCommand).
        // A script that starts later does not hear earlier events: read Network.Peers,
        // Network.IsReady etc. in OnCreate.

        /// <summary>
        /// This machine is in a multiplayer session: the host is listening, or this client
        /// has joined and received the host's world (Network.IsReady is true from here on).
        /// Never called in a single-player (offline) game.
        /// </summary>
        public virtual void OnNetworkStarted() {}
        /// <summary>
        /// The session is over: Play stopped, the host left, the connection was lost - or a
        /// join failed (then OnNetworkStarted never ran). <paramref name="reason"/> says which.
        /// </summary>
        public virtual void OnNetworkStopped(string reason) {}
        /// <summary>
        /// Another player is in the session (on a client: also once for everyone who was
        /// already there when it joined). Never called for this machine's own player.
        /// </summary>
        public virtual void OnPeerJoined(int peer) {}
        /// <summary>
        /// Another player has finished loading and is in the game. On the host this is the
        /// moment to give that player what it needs (its character, its score...).
        /// </summary>
        public virtual void OnPeerReady(int peer) {}
        /// <summary>Another player left or lost the connection.</summary>
        public virtual void OnPeerLeft(int peer) {}
        /// <summary>
        /// The authority only (host, or a single-player game): somebody asked for something
        /// with Network.SendCommand. A command sent with a target reaches only the scripts on
        /// that entity; one without reaches every script, so check <paramref name="kind"/>.
        /// <paramref name="sender"/> is who asked (0 = this machine itself) and cannot be
        /// faked; everything in <paramref name="data"/> can: read it, check data.Ok, then
        /// check that this player may ask for this before acting. See NetReader.cs.
        /// </summary>
        public virtual void OnNetworkCommand(int sender, ushort kind, NetReader data) {}
        /// <summary>
        /// Every machine: the authority announced that something happened, with
        /// Network.SendEvent - the place for what each player should see or hear (an effect,
        /// a sound, a HUD message). An event sent with a target reaches only the scripts on
        /// that entity; one without reaches every script, so check <paramref name="kind"/>.
        /// It comes from the host, so it can be trusted more than a command - still check
        /// data.Ok before using what you read.
        /// </summary>
        public virtual void OnNetworkEvent(ushort kind, NetReader data) {}

        /// <summary>
        /// Called by the engine (ScriptRuntime::DispatchNetworkEvent) with plain values.
        /// kind: 0 started, 1 stopped, 2 peer joined, 3 peer ready, 4 peer left, 5 command, 6 event
        /// (ScriptNetEvent::Kind in Bindings/NetworkContext.h - keep the two in step).
        /// </summary>
        internal void DispatchNetworkEvent(int kind, int peer, int code, int serial, string text)
        {
            switch (kind)
            {
                case 0: OnNetworkStarted(); break;
                case 1: OnNetworkStopped(text ?? string.Empty); break;
                case 2: OnPeerJoined(peer); break;
                case 3: OnPeerReady(peer); break;
                case 4: OnPeerLeft(peer); break;
                case 5:
                {
                    // Every script gets its own reader over the same bytes.
                    byte[] payload = Network.PayloadOf(serial);
                    OnNetworkCommand(peer, unchecked((ushort)code), new NetReader(payload, payload.Length));
                    break;
                }
                case 6:
                {
                    byte[] payload = Network.PayloadOf(serial);
                    OnNetworkEvent(unchecked((ushort)code), new NetReader(payload, payload.Length));
                    break;
                }
            }
        }

        /// <summary>
        /// Called by the engine (ScriptRuntime::DispatchPhysicsEvent) with plain values.
        /// kind: 0 collision enter, 1 collision exit, 2 trigger enter, 3 trigger exit.
        /// The normal points from this entity toward the other.
        /// </summary>
        internal void DispatchPhysicsEvent(int kind, int index, int generation, float px, float py, float pz,
                                           float nx, float ny, float nz, float impactSpeed)
        {
            var other = new global::Crucible.Entity(unchecked((uint)index), unchecked((uint)generation));
            switch (kind)
            {
                case 0: OnCollisionEnter(new Collision(other, new Vector3(px, py, pz), new Vector3(nx, ny, nz), impactSpeed)); break;
                case 1: OnCollisionExit(new Collision(other, Vector3.Zero, Vector3.Zero, 0f)); break;
                case 2: OnTriggerEnter(other); break;
                case 3: OnTriggerExit(other); break;
            }
        }

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
