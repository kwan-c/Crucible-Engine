/*************************************************************************
* \file:         Entity.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
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
*    Tags (Unity's GameObject.tag): Tag, CompareTag, FindWithTag,
*    FindAllWithTag, IsTagDefined. Tags come from the project's tag list
*    (Editor: Inspector > Tag > Manage Tags..., saved in
*    Assets/Config/Tags.json) and are case-sensitive.
*
*    C++ side: Engine/src/Scripting/Bindings/EntityBindings.cpp, and
*    Bindings/TagBindings.cpp for the tag calls.
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
        private Collider collider;
        private MeshRenderer meshRenderer;
        private PlayerController playerController;
        private WorldText worldText;
        private MaterialParams materialParams;
        private FlowFieldAgent flowFieldAgent;
        private FlowFieldSurface flowFieldSurface;
        private StateGraphAgent stateGraphAgent;

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
        /// <summary>This entity's Collider (needs a Collider component).</summary>
        public Collider Collider { get { return collider ?? (collider = new Collider(Identity)); } }
        /// <summary>This entity's MeshRenderer (needs a MeshRenderer component).</summary>
        public MeshRenderer MeshRenderer { get { return meshRenderer ?? (meshRenderer = new MeshRenderer(Identity)); } }
        /// <summary>This entity's PlayerController: move speed, jump, facing (keys: InputSettings).</summary>
        public PlayerController PlayerController { get { return playerController ?? (playerController = new PlayerController(Identity)); } }
        /// <summary>This entity's WorldText: text in the 3D world (needs a WorldText component).</summary>
        public WorldText WorldText { get { return worldText ?? (worldText = new WorldText(Identity)); } }
        /// <summary>This entity's MaterialParams: per-object glow (needs a MaterialParams component).</summary>
        public MaterialParams MaterialParams { get { return materialParams ?? (materialParams = new MaterialParams(Identity)); } }
        /// <summary>This entity's FlowFieldAgent: enemy movement - MoveTo, Arrive, FollowField (needs a FlowFieldAgent component).</summary>
        public FlowFieldAgent FlowFieldAgent { get { return flowFieldAgent ?? (flowFieldAgent = new FlowFieldAgent(Identity)); } }
        /// <summary>This entity's FlowFieldSurface: the ground enemies navigate (needs a FlowFieldSurface component).</summary>
        public FlowFieldSurface FlowFieldSurface { get { return flowFieldSurface ?? (flowFieldSurface = new FlowFieldSurface(Identity)); } }
        /// <summary>This entity's StateGraphAgent: its brain's variables and state (needs a StateGraphAgent component).</summary>
        public StateGraphAgent StateGraphAgent { get { return stateGraphAgent ?? (stateGraphAgent = new StateGraphAgent(Identity)); } }

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

        // --- tags ------------------------------------------------------------------ //

        /// <summary>The tag every entity starts with.</summary>
        public const string Untagged = "Untagged";

        // SetTagInternal results. Must match TagBindings.cpp.
        private const int TagSet = 0;
        private const int TagDeadEntity = 1;
        private const int TagUndefined = 2;

        /// <summary>
        /// The entity's tag ("Untagged" by default; empty once it is gone). Setting it to a tag
        /// the project does not define throws an ArgumentException. Setting it on a destroyed
        /// entity does nothing. A tag set during Play is restored by Stop.
        /// </summary>
        public string Tag
        {
            get { return GetTagInternal((int)index, (int)generation) ?? string.Empty; }
            set
            {
                if (value == null)
                    throw new ArgumentNullException("value", "An entity's tag can't be null - use Entity.Untagged.");
                int result = SetTagInternal((int)index, (int)generation, value);
                if (result == TagUndefined)
                    throw new ArgumentException("Tag '" + value + "' is not defined. Add it in the Editor: " +
                                                "Inspector > Tag > Manage Tags... (tags are case-sensitive).", "value");
                if (result != TagSet && result != TagDeadEntity)
                    throw new InvalidOperationException("Could not set the tag of " + ToString() + ".");
            }
        }

        /// <summary>
        /// True if the entity is alive and its tag is exactly <paramref name="tag"/>
        /// (case-sensitive). Like Unity's CompareTag.
        /// </summary>
        public bool CompareTag(string tag)
        {
            if (tag == null)
                return false;
            int result;
            return CompareTagInternal((int)index, (int)generation, tag, out result) != 0 && result != 0;
        }

        /// <summary>
        /// An entity whose tag is exactly <paramref name="tag"/>, or null. If several have it,
        /// the same one is returned while the scene is unchanged, but which one is unspecified.
        /// It searches every entity: call it in OnCreate and keep the result.
        /// </summary>
        public static Entity FindWithTag(string tag)
        {
            if (string.IsNullOrEmpty(tag))
                return null;
            int foundIndex, foundGeneration;
            return FindWithTagInternal(tag, out foundIndex, out foundGeneration) != 0
                ? new Entity(unchecked((uint)foundIndex), unchecked((uint)foundGeneration))
                : null;
        }

        /// <summary>Every entity whose tag is exactly <paramref name="tag"/> (empty if none).</summary>
        public static Entity[] FindAllWithTag(string tag)
        {
            if (string.IsNullOrEmpty(tag))
                return new Entity[0];
            int[] pairs = FindAllWithTagInternal(tag);
            if (pairs == null)
                return new Entity[0];
            Entity[] found = new Entity[pairs.Length / 2];
            for (int i = 0; i < found.Length; ++i)
                found[i] = new Entity(unchecked((uint)pairs[i * 2]), unchecked((uint)pairs[i * 2 + 1]));
            return found;
        }

        /// <summary>Whether the project's tag list has <paramref name="tag"/> (case-sensitive).</summary>
        public static bool IsTagDefined(string tag)
        {
            return !string.IsNullOrEmpty(tag) && IsTagDefinedInternal(tag) != 0;
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
        /// Every component or script of type T on the entity, in Inspector order (empty if none).
        /// Mostly for scripts: an entity can have several, including several of one class, and
        /// GetComponents&lt;EntityScript&gt;() returns all of them. Engine components exist at most
        /// once per entity, so for those it is GetComponent&lt;T&gt;() as an array of 0 or 1.
        /// </summary>
        public T[] GetComponents<T>() where T : class
        {
            if (typeof(EntityScript).IsAssignableFrom(typeof(T)))
            {
                object[] scripts = GetScriptsInternal((int)index, (int)generation);
                if (scripts == null)
                    return new T[0];
                var matches = new System.Collections.Generic.List<T>(scripts.Length);
                foreach (object script in scripts)
                {
                    T match = script as T;
                    if (match != null)
                        matches.Add(match);
                }
                return matches.ToArray();
            }
            T component = GetComponent<T>();
            return component != null ? new T[] { component } : new T[0];
        }

        /// <summary>
        /// The entity's component or script of type T, or null if it has none.
        /// T can be Transform, RigidBody, Animator, AudioSource, CharacterController, Camera, CameraOrbit, Light, ParticleEmitter, Collider, MeshRenderer, PlayerController, WorldText, MaterialParams, FlowFieldAgent, FlowFieldSurface, StateGraphAgent, UIText, UIImage, UISlider, UIButton, UIRect, UICanvas, or a script class (e.g. GetComponent&lt;Door&gt;()).
        /// With several scripts on the entity: the first (in Inspector order) that is a T - a
        /// base class or EntityScript itself matches any script that derives from it.
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
            if (type == typeof(Collider))
                return Has(ComponentKind.Collider) ? (T)(object)Collider : null;
            if (type == typeof(MeshRenderer))
                return Has(ComponentKind.MeshRenderer) ? (T)(object)MeshRenderer : null;
            if (type == typeof(PlayerController))
                return Has(ComponentKind.PlayerController) ? (T)(object)PlayerController : null;
            if (type == typeof(WorldText))
                return Has(ComponentKind.WorldText) ? (T)(object)WorldText : null;
            if (type == typeof(MaterialParams))
                return Has(ComponentKind.MaterialParams) ? (T)(object)MaterialParams : null;
            if (type == typeof(FlowFieldAgent))
                return Has(ComponentKind.FlowFieldAgent) ? (T)(object)FlowFieldAgent : null;
            if (type == typeof(FlowFieldSurface))
                return Has(ComponentKind.FlowFieldSurface) ? (T)(object)FlowFieldSurface : null;
            if (type == typeof(StateGraphAgent))
                return Has(ComponentKind.StateGraphAgent) ? (T)(object)StateGraphAgent : null;
            if (type == typeof(UIText))   return Has(ComponentKind.UIText)   ? (T)(object)new UIText(Identity)   : null;
            if (type == typeof(UIImage))  return Has(ComponentKind.UIImage)  ? (T)(object)new UIImage(Identity)  : null;
            if (type == typeof(UISlider)) return Has(ComponentKind.UISlider) ? (T)(object)new UISlider(Identity) : null;
            if (type == typeof(UIButton)) return Has(ComponentKind.UIButton) ? (T)(object)new UIButton(Identity) : null;
            if (type == typeof(UIRect))   return Has(ComponentKind.UIRect)   ? (T)(object)new UIRect(Identity)   : null;
            if (type == typeof(UICanvas)) return Has(ComponentKind.UICanvas) ? (T)(object)new UICanvas(Identity) : null;
            if (typeof(EntityScript).IsAssignableFrom(type))
            {
                object[] scripts = GetScriptsInternal((int)index, (int)generation);
                if (scripts != null)
                    foreach (object script in scripts)
                    {
                        T match = script as T;
                        if (match != null)
                            return match;
                    }
                return null;
            }
            throw new ArgumentException("GetComponent<" + type.Name +
                                        ">: use Transform, RigidBody, Animator, AudioSource, CharacterController, Camera, CameraOrbit, Light, ParticleEmitter, Collider, MeshRenderer, PlayerController, WorldText, MaterialParams, FlowFieldAgent, FlowFieldSurface, StateGraphAgent, a UI component or a script class.");
        }

        /// <summary>Does the entity have a component or script of type T? See GetComponent.</summary>
        public bool HasComponent<T>() where T : class
        {
            return GetComponent<T>() != null;
        }

        // --- switching on and off ------------------------------------------------- //

        /// <summary>
        /// Switches one component on or off (Unity's .enabled). Its data is kept; while off,
        /// it does nothing (not drawn, no physics, silent, script paused...). T is any type
        /// GetComponent accepts. A script class switches that script (the first one of that
        /// class, as GetComponent finds it) - the same as its Enabled; EntityScript itself
        /// switches ALL of the entity's scripts at once. Transform can't be switched off.
        /// Returns false if the entity has no such component.
        /// </summary>
        public bool SetEnabled<T>(bool enabled) where T : class
        {
            if (IsOneScript(typeof(T)))
            {
                EntityScript script = GetComponent<T>() as EntityScript;
                if (script == null)
                    return false;
                script.Enabled = enabled;
                return true;
            }
            return SetComponentEnabledInternal((int)index, (int)generation, KindOf(typeof(T)), enabled ? 1 : 0) != 0;
        }

        /// <summary>
        /// Is the component switched on (its own choice)? False if the entity has none. A script
        /// class reads that script's Enabled; EntityScript reads the switch for all scripts.
        /// </summary>
        public bool IsEnabled<T>() where T : class
        {
            if (IsOneScript(typeof(T)))
            {
                EntityScript script = GetComponent<T>() as EntityScript;
                return script != null && script.Enabled;
            }
            int on;
            return IsComponentEnabledInternal((int)index, (int)generation, KindOf(typeof(T)), out on) != 0 && on != 0;
        }

        // A particular script class (not EntityScript itself, which means every script).
        private static bool IsOneScript(Type type)
        {
            return type != typeof(EntityScript) && typeof(EntityScript).IsAssignableFrom(type);
        }

        /// <summary>
        /// Unity's SetActive: false switches off every component on this entity and all its
        /// children (remembering which), true switches exactly those back on - anything you
        /// had switched off yourself stays off. On your own entity this pauses your script too.
        /// </summary>
        public void SetActive(bool active)
        {
            SetActiveInternal((int)index, (int)generation, active ? 1 : 0);
        }

        /// <summary>Is this entity itself active - SetActive(false) not called on it? (Unity's activeSelf.)</summary>
        public bool IsActiveSelf
        {
            get { int v; return IsActiveInternal((int)index, (int)generation, 1, out v) != 0 && v != 0; }
        }

        /// <summary>Active, and so are all its parents (Unity's activeInHierarchy).</summary>
        public bool IsActive
        {
            get { int v; return IsActiveInternal((int)index, (int)generation, 0, out v) != 0 && v != 0; }
        }

        // The ComponentKind number for a GetComponent type; throws for anything else.
        private static int KindOf(Type type)
        {
            if (type == typeof(Transform)) return (int)ComponentKind.Transform;
            if (type == typeof(RigidBody)) return (int)ComponentKind.RigidBody;
            if (type == typeof(Animator)) return (int)ComponentKind.Animator;
            if (type == typeof(AudioSource)) return (int)ComponentKind.AudioSource;
            if (type == typeof(CharacterController)) return (int)ComponentKind.CharacterController;
            if (type == typeof(Camera)) return (int)ComponentKind.Camera;
            if (type == typeof(CameraOrbit)) return (int)ComponentKind.CameraOrbit;
            if (type == typeof(Light)) return (int)ComponentKind.Light;
            if (type == typeof(ParticleEmitter)) return (int)ComponentKind.ParticleEmitter;
            if (type == typeof(Collider)) return (int)ComponentKind.Collider;
            if (type == typeof(MeshRenderer)) return (int)ComponentKind.MeshRenderer;
            if (type == typeof(PlayerController)) return (int)ComponentKind.PlayerController;
            if (type == typeof(WorldText)) return (int)ComponentKind.WorldText;
            if (type == typeof(MaterialParams)) return (int)ComponentKind.MaterialParams;
            if (type == typeof(FlowFieldAgent)) return (int)ComponentKind.FlowFieldAgent;
            if (type == typeof(FlowFieldSurface)) return (int)ComponentKind.FlowFieldSurface;
            if (type == typeof(StateGraphAgent)) return (int)ComponentKind.StateGraphAgent;
            if (type == typeof(UIText)) return (int)ComponentKind.UIText;
            if (type == typeof(UIImage)) return (int)ComponentKind.UIImage;
            if (type == typeof(UISlider)) return (int)ComponentKind.UISlider;
            if (type == typeof(UIButton)) return (int)ComponentKind.UIButton;
            if (type == typeof(UIRect)) return (int)ComponentKind.UIRect;
            if (type == typeof(UICanvas)) return (int)ComponentKind.UICanvas;
            if (typeof(EntityScript).IsAssignableFrom(type)) return (int)ComponentKind.Script;
            throw new ArgumentException(type.Name + ": use a type GetComponent accepts.");
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
            Collider = 16,
            MeshRenderer = 17,
            PlayerController = 18,
            WorldText = 19,
            MaterialParams = 20,
            FlowFieldAgent = 21,
            FlowFieldSurface = 22,
            StateGraphAgent = 23,
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
        private static extern object[] GetScriptsInternal(int index, int generation);   // every running script, Inspector order
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int FindChildInternal(int index, int generation, string name, out int childIndex,
                                                    out int childGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int DestroyInternal(int index, int generation, float delay);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetComponentEnabledInternal(int index, int generation, int kind, int enabled);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsComponentEnabledInternal(int index, int generation, int kind, out int enabled);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetActiveInternal(int index, int generation, int active);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsActiveInternal(int index, int generation, int self, out int active);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetTagInternal(int index, int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetTagInternal(int index, int generation, string tag);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int CompareTagInternal(int index, int generation, string tag, out int result);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int FindWithTagInternal(string tag, out int index, out int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int[] FindAllWithTagInternal(string tag);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsTagDefinedInternal(string tag);
    }
}
