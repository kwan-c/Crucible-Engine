/*************************************************************************
* \file:         Entity.cs
* \author:       <author>
* \par:          Email: <email>
* \date          1 November 2026
* \version       1.0
* \brief         A reference to an entity in the scene, with access to its
*                components. Part of Crucible.ScriptCore.dll.
*
* \details
*    Every script has one for its own entity (EntityScript.Entity). To
*    reference ANOTHER entity, declare a public field and drag the entity
*    onto it in the Inspector, like Unity's `public GameObject door;`:
*
*        public Entity door;                       // assigned in the Inspector
*        public override void OnUpdate(float dt)
*        {
*            if (door != null && door.IsAlive)
*                door.Transform.Position += new Vector3(0, dt, 0);
*        }
*
*    An unassigned field, or one whose entity was deleted, is null when
*    Play starts. If the entity is destroyed during Play the reference
*    stays non-null but IsAlive turns false and component calls do nothing.
*
*    Created by the engine only: EntityScript.BindEntity, and
*    ScriptRuntime (native code writes `index` and `generation` directly
*    into a new object, without running a constructor - keep those field
*    names, and keep everything else lazily created).
*
*    C++ side: Engine/src/Scripting/Bindings/EntityBindings.cpp.
*
**************************************************************************/
using System;
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>
    /// A reference to an entity: its Transform, RigidBody, Animator and name.
    /// </summary>
    public sealed class Entity
    {
        // Written by native code (ScriptRuntime.cpp, WriteFields). Do not rename.
        private uint index;
        private uint generation;

        // Component wrappers, created on first use.
        private Transform transform;
        private RigidBody rigidBody;
        private Animator animator;
        private AudioSource audioSource;
        private CharacterController characterController;
        private Camera camera;
        private CameraOrbit cameraOrbit;
        private Light light;
        private ParticleEmitter particleEmitter;

        internal Entity(uint index, uint generation)
        {
            this.index = index;
            this.generation = generation;
        }

        /// <summary>The entity's slot number.</summary>
        public uint Index { get { return index; } }
        /// <summary>Increments each time the slot is reused.</summary>
        public uint Generation { get { return generation; } }
        /// <summary>The (index, generation) pair as a value type.</summary>
        public EntityIdentity Identity { get { return new EntityIdentity(index, generation); } }

        /// <summary>This entity's position, rotation and scale.</summary>
        public Transform Transform { get { return transform ?? (transform = new Transform(Identity)); } }
        /// <summary>This entity's physics body.</summary>
        public RigidBody RigidBody { get { return rigidBody ?? (rigidBody = new RigidBody(Identity)); } }
        /// <summary>Sets parameters on this entity's Animator controller.</summary>
        public Animator Animator { get { return animator ?? (animator = new Animator(Identity)); } }
        /// <summary>This entity's AudioSource (needs an AudioSource component).</summary>
        public AudioSource AudioSource { get { return audioSource ?? (audioSource = new AudioSource(Identity)); } }
        /// <summary>This entity's CharacterController (needs a CharacterController component).</summary>
        public CharacterController CharacterController { get { return characterController ?? (characterController = new CharacterController(Identity)); } }
        /// <summary>This entity's Camera (needs a Camera component).</summary>
        public Camera Camera { get { return camera ?? (camera = new Camera(Identity)); } }
        /// <summary>This entity's CameraOrbit (needs a CameraOrbit component).</summary>
        public CameraOrbit CameraOrbit { get { return cameraOrbit ?? (cameraOrbit = new CameraOrbit(Identity)); } }
        /// <summary>This entity's Light (needs a Light component).</summary>
        public Light Light { get { return light ?? (light = new Light(Identity)); } }
        /// <summary>This entity's ParticleEmitter (needs a ParticleEmitter component).</summary>
        public ParticleEmitter ParticleEmitter { get { return particleEmitter ?? (particleEmitter = new ParticleEmitter(Identity)); } }

        /// <summary>False once the entity has been destroyed (and outside Play).</summary>
        public bool IsAlive
        {
            get { int alive; return IsAliveInternal((int)index, (int)generation, out alive) != 0 && alive != 0; }
        }

        /// <summary>The name shown in the Hierarchy; empty if it has none or is gone.</summary>
        public string Name
        {
            get { return GetNameInternal((int)index, (int)generation) ?? string.Empty; }
        }

        // --- finding entities and components -------------------------------------- //

        /// <summary>
        /// An entity whose Hierarchy name is exactly <paramref name="name"/>, or null.
        /// If several share the name, any one of them may be returned. It searches every
        /// entity, so call it in OnCreate and keep the result rather than every frame.
        /// </summary>
        public static Entity Find(string name)
        {
            if (string.IsNullOrEmpty(name))
                return null;
            int foundIndex, foundGeneration;
            return FindInternal(name, out foundIndex, out foundGeneration) != 0
                ? new Entity(unchecked((uint)foundIndex), unchecked((uint)foundGeneration))
                : null;
        }

        /// <summary>
        /// A child, grandchild, ... of this entity named exactly <paramref name="name"/>, or null.
        /// This is how to reach the parts of a spawned prefab: bullet.FindChild("Trail").
        /// </summary>
        public Entity FindChild(string name)
        {
            if (string.IsNullOrEmpty(name))
                return null;
            int foundIndex, foundGeneration;
            return FindChildInternal((int)index, (int)generation, name, out foundIndex, out foundGeneration) != 0
                ? new Entity(unchecked((uint)foundIndex), unchecked((uint)foundGeneration))
                : null;
        }

        /// <summary>
        /// Destroys an entity and its children at the end of this frame, or after
        /// <paramref name="delay"/> seconds of Play time. Until then it still works normally.
        /// Scripts on it get OnDestroy first. Null or already-destroyed entities are ignored.
        /// </summary>
        public static void Destroy(Entity entity, float delay = 0f)
        {
            if (!ReferenceEquals(entity, null))
                DestroyInternal((int)entity.index, (int)entity.generation, delay);
        }

        /// <summary>
        /// The entity's component or script of type T, or null if it has none.
        /// T can be Transform, RigidBody, Animator, AudioSource, CharacterController, Camera, CameraOrbit, Light, ParticleEmitter, UIText, UIImage, UISlider, UIButton, UIRect, UICanvas, or a script class (e.g. GetComponent&lt;Door&gt;()).
        /// A script found during OnCreate may not have run its own OnCreate yet.
        /// </summary>
        public T GetComponent<T>() where T : class
        {
            Type type = typeof(T);
            if (type == typeof(Transform))
                return Has(ComponentKind.Transform) ? (T)(object)Transform : null;
            if (type == typeof(RigidBody))
                return Has(ComponentKind.RigidBody) ? (T)(object)RigidBody : null;
            if (type == typeof(Animator))
                return Has(ComponentKind.Animator) ? (T)(object)Animator : null;
            if (type == typeof(AudioSource))
                return Has(ComponentKind.AudioSource) ? (T)(object)AudioSource : null;
            if (type == typeof(CharacterController))
                return Has(ComponentKind.CharacterController) ? (T)(object)CharacterController : null;
            if (type == typeof(Camera))
                return Has(ComponentKind.Camera) ? (T)(object)Camera : null;
            if (type == typeof(CameraOrbit))
                return Has(ComponentKind.CameraOrbit) ? (T)(object)CameraOrbit : null;
            if (type == typeof(Light))
                return Has(ComponentKind.Light) ? (T)(object)Light : null;
            if (type == typeof(ParticleEmitter))
                return Has(ComponentKind.ParticleEmitter) ? (T)(object)ParticleEmitter : null;
            if (type == typeof(UIText))   return Has(ComponentKind.UIText)   ? (T)(object)new UIText(Identity)   : null;
            if (type == typeof(UIImage))  return Has(ComponentKind.UIImage)  ? (T)(object)new UIImage(Identity)  : null;
            if (type == typeof(UISlider)) return Has(ComponentKind.UISlider) ? (T)(object)new UISlider(Identity) : null;
            if (type == typeof(UIButton)) return Has(ComponentKind.UIButton) ? (T)(object)new UIButton(Identity) : null;
            if (type == typeof(UIRect))   return Has(ComponentKind.UIRect)   ? (T)(object)new UIRect(Identity)   : null;
            if (type == typeof(UICanvas)) return Has(ComponentKind.UICanvas) ? (T)(object)new UICanvas(Identity) : null;
            if (typeof(EntityScript).IsAssignableFrom(type))
                return GetScriptInternal((int)index, (int)generation) as T;
            throw new ArgumentException("GetComponent<" + type.Name +
                                        ">: use Transform, RigidBody, Animator, AudioSource, CharacterController, Camera, CameraOrbit, Light, ParticleEmitter, a UI component or a script class.");
        }

        /// <summary>Does the entity have a component or script of type T? See GetComponent.</summary>
        public bool HasComponent<T>() where T : class
        {
            return GetComponent<T>() != null;
        }

        // Native component kinds. Must match ScriptComponentKind in Bindings/EntityBindings.cpp.
        private enum ComponentKind
        {
            Transform = 0,
            RigidBody = 1,
            Animator = 2,
            Script = 3,
            AudioSource = 4,
            CharacterController = 5,
            Camera = 6,
            CameraOrbit = 7,
            Light = 8,
            ParticleEmitter = 9,
            UIText = 10,
            UIImage = 11,
            UISlider = 12,
            UIButton = 13,
            UIRect = 14,
            UICanvas = 15,
        }

        private bool Has(ComponentKind kind)
        {
            int has;
            return HasComponentInternal((int)index, (int)generation, (int)kind, out has) != 0 && has != 0;
        }

        // --- equality --------------------------------------------------------------- //

        /// <summary>True if both refer to the same entity.</summary>
        public override bool Equals(object other)
        {
            var entity = other as Entity;
            return !ReferenceEquals(entity, null) && entity.index == index && entity.generation == generation;
        }

        public override int GetHashCode()
        {
            return unchecked((int)(index * 397u ^ generation));
        }

        /// <summary>Same entity? (Two Entity objects can refer to one entity.)</summary>
        public static bool operator ==(Entity a, Entity b)
        {
            if (ReferenceEquals(a, b))
                return true;
            if (ReferenceEquals(a, null) || ReferenceEquals(b, null))
                return false;
            return a.index == b.index && a.generation == b.generation;
        }

        public static bool operator !=(Entity a, Entity b)
        {
            return !(a == b);
        }

        /// <summary>The entity's name and id, e.g. "Door (Entity 12:3)".</summary>
        public override string ToString()
        {
            string name = Name;
            string id = "Entity " + index + ":" + generation;
            return name.Length > 0 ? name + " (" + id + ")" : id;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsAliveInternal(int index, int generation, out int alive);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetNameInternal(int index, int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int FindInternal(string name, out int index, out int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int HasComponentInternal(int index, int generation, int kind, out int has);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern EntityScript GetScriptInternal(int index, int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int FindChildInternal(int index, int generation, string name, out int childIndex,
                                                    out int childGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int DestroyInternal(int index, int generation, float delay);
    }
}
