/*************************************************************************
* \file:         World.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.8
* \brief
*                composes EntityManager + ComponentRegistry into the single
*                API systems and gameplay code actually call
*
* \details
*                World is the single front-facing entry point for the
*                ECS. It owns an EntityManager (entity ids/liveness, see
*                ECSCore.h), a ComponentRegistry (per-type ComponentInfo,
*                see ComponentRegistry.h) and an ArchetypeRegistry (the
*                archetype table, see ArchetypeRegistry.h). Gameplay
*                code and systems (see Systems/IECSSystems.h and
*                ECSPhysicsSystem.h for an example system) should only
*                ever talk to World - it hides the archetype-move
*                bookkeeping in MoveEntity() (see World.cpp) behind
*                simple Add/Remove/Get/Has component calls.
*
*                Guarantees:
*                  - Structural changes are all-or-nothing: if an
*                    allocation or component constructor throws, the
*                    entity (and the whole batch, for CreateEntities) is
*                    left exactly as before.
*                  - Every mutable component access marks the component
*                    changed (GetComponent/Write/non-const queries); use
*                    Read<T>() or a const World for pure reads.
*                  - Lifecycle-hook exceptions are caught, logged and
*                    counted (HookFailureCount()); they never leave a
*                    structural change half-done.
*                  - Public entry points validate their input in every
*                    build configuration.
*
* \history
*    -v1.0       skeleton
*    -v1.1       added member-level comments for readability, no logic changes
*    -v1.2       AddComponent/RemoveComponent now throw std::invalid_argument
*                on a dead/stale Entity in every build config, instead of
*                relying solely on an assert that NDEBUG strips out
*    -v1.3       hook symmetry (CreateEntity(s) fire add hooks, DestroyEntity
*                fires remove hooks), DestroyAllEntities, EmplaceComponent,
*                SetDeltaTime/DeltaTime, queries stamp change ticks, new
*                components relocated straight into their slot; all existing
*                public names and signatures unchanged
*    -v1.4       private DestroyEntityImpl (silent removal of rolled-back
*                spawns); documented that lifecycle hooks must not throw
*    -v1.5       exception-safe creation/migration (CreateEntities is
*                all-or-nothing), Read/Write + const GetComponent and
*                stamping mutable GetComponent, signature validation,
*                catch-log-continue hook policy with HookFailureCount(),
*                declared-access and structural-change enforcement for
*                running systems; existing public signatures unchanged
*    -v1.6       mandatory base components (Transform, WorldTransform,
*                HierarchyNode) on every entity; Unity-style hierarchy API
*                (SetParent, DetachChildren, sibling order, in World.cpp;
*                world transforms are read from WorldTransform); DestroyEntity destroys
*                descendants; RemoveComponent only takes RemovableComponent
*    -v1.7       private ReadNodeRow (one lookup per node for
*                HierarchyTransformSystem); per-call access checks
*                Debug-only (see CRUCIBLE_ECS_ACCESS_CHECKS)
*    -v1.8       GLM math: Detail::SafeNormalize, ComposeTRS from glm
*                mat4_cast + glm::scale; DecomposeTRS wraps
*                glm::decompose; no negative scales (mirrored keep-world
*                reparents are refused); shared unlink/placement helpers
*
**************************************************************************/
#pragma once

#include "EngineExport.h"

//std libs
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

//header files
#include "Core/ECSCore.h"          // Entity, EntityManager
#include "Components/ComponentRegistry.h" // ComponentRegistry
#include "Components/ComponentSignature.h"
#include "Components/Components.h"        // Transform, WorldTransform, HierarchyNode (the mandatory base)
#include "Archetype/ArchetypeRegistry.h"
#include "Systems/Query.h"
#include "Systems/SystemAccess.h"


namespace Crucible
{
	class CommandBuffer;

	/*************************************************************************
	 \brief  Why World::TrySetParent() refused a change (Ok = applied or
			 nothing to do).
	 *************************************************************************/
	enum class ParentResult : std::uint8_t
	{
		Ok,
		DeadChild,     // child is dead/stale/null
		DeadParent,    // parent is non-null but dead/stale
		SelfParent,    // child == parent
		WouldCycle,    // parent is one of child's descendants
		UnsupportedTransform,  // keep-world requires an invertible representable parent frame
		InvalidSocketBinding
	};

	/*************************************************************************
	 \brief  Human-readable reason for a ParentResult (for logs/exceptions).
	 *************************************************************************/
	ENGINE_API const char* ToString(ParentResult result) noexcept;

	/*************************************************************************
	 \brief  Crucible's mandatory base scene components. World ORs this into
			 every creation signature (CreateEntity, CreateEntities,
			 CommandBuffer spawns), so each entity is born in its final
			 archetype with all three, default-valued:
			   Transform      position 0, identity rotation, scale 1
			   WorldTransform identity matrix, valid = false
			   HierarchyNode  all links null, childCount 0 (a root)
	 \note   Must agree with the Mandatory<T> specialisations in
			 Components.h - checked for every registered component below.
	 *************************************************************************/
	inline constexpr ComponentSignature kMandatoryComponents =
		MakeSignature<Transform, WorldTransform, HierarchyNode>();

	namespace Detail
	{
		// GLM conventions used below: matrices are column-major (m[column][row], m[3] is the
		// translation), glm::quat's constructor takes (w, x, y, z), and v.length() is the
		// component count - glm::length(v) is the Euclidean length.

		/*********************************************************************
		 \brief  Unit form of q, or identity when q is zero or non-finite.
		 \details A rotation must be unit length or mat4_cast() turns it into a
				  scaled/sheared matrix; a zero or NaN one would poison every
				  descendant. Already-unit quaternions (the common case) are
				  returned unchanged, without a square root.
		 *********************************************************************/
		inline Math::quat SafeNormalize(const Math::quat& q) noexcept
		{
			const float n2 = glm::dot(q, q);
			if (n2 > 0.999999f && n2 < 1.000001f) return q;
			if (!(n2 > 1e-12f) || !std::isfinite(n2)) return Math::quat(1.f, 0.f, 0.f, 0.f);   // identity (w first)
			return glm::normalize(q);
		}

		/*********************************************************************
		 \brief  translation * rotation * scale as one matrix.
		 \details The rotation matrix gets the translation as its last column,
				  then glm::scale() scales the three axis columns - the same
				  result as translate * mat4_cast * scale without the two 4x4
				  multiplies. The rotation is normalised first (see
				  SafeNormalize).
		 *********************************************************************/
		inline Math::mat4 ComposeTRS(const Math::vec3& translation, const Math::quat& rotation, const Math::vec3& scale) noexcept
		{
			Math::mat4 m = glm::mat4_cast(SafeNormalize(rotation));
			m[3] = Math::vec4(translation, 1.f);
			return glm::scale(m, scale);
		}

		/*********************************************************************
		 \brief  Splits an affine matrix into translation * rotation * scale,
				 with a positive scale on every axis (glm::decompose inside).
		 \param[in]  m           The matrix (e.g. a world or local matrix);
								 its bottom row is ignored (treated as affine).
		 \param[out] translation Exact translation of m.
		 \param[out] rotation    Unit rotation, the canonical one (w >= 0) of
								 q / -q.
		 \param[out] scale       Per-axis scale, all positive.
		 \return false (outputs untouched) when m has a non-finite value, an
				 (almost) zero or collapsed scale axis (not invertible), or is
				 mirrored - Crucible does not produce negative scales.
		 \details Without shear the result is exact: ComposeTRS() of it gives m
				  back. Shear policy (glm::decompose's Gram-Schmidt): a rotated
				  child under a non-uniformly scaled parent has shear, which
				  T/R/S can't store. Then the translation is still exact, the
				  rotation keeps the X axis direction and the XY plane, and each
				  scale is the axis' projection onto that rotation. This is an
				  axis-preserving approximation (not in general the closest
				  rotation or T*R*S): recomposing it does not reproduce the
				  sheared matrix exactly.
		 \note   Prefer this to calling glm::decompose() directly: that one
				 lets NaNs through, writes its outputs even when it fails,
				 flips all three scales negative for a mirrored matrix, and
				 rejects small but valid scales (its singularity test is an
				 absolute |det| < FLT_EPSILON, e.g. a uniform scale under
				 0.005). This wrapper validates first and hands glm a basis
				 with unit-length axes, then scales the result back.
		 *********************************************************************/
		ENGINE_API bool DecomposeTRS(const Math::mat4& m, Math::vec3& translation, Math::quat& rotation,
			Math::vec3& scale) noexcept;

		// Whether the Mandatory<T> trait and kMandatoryComponents say the same thing about T.
		template <typename T>
		constexpr bool MandatoryTraitMatchesSignature() noexcept
		{
			return Mandatory<T>::value == kMandatoryComponents[ComponentTypeId<T>()];
		}
	}

	// Two-way consistency check over EVERY registered component: a type marked
	// Mandatory<T> but missing from kMandatoryComponents fails here, and so does
	// a bit in kMandatoryComponents whose type isn't marked Mandatory. The count
	// check makes sure the list really covers every ComponentKind, so no id can
	// escape the comparison.
#define CRUCIBLE_MANDATORY_MATCHES(Type) && Detail::MandatoryTraitMatchesSignature<Type>()
#define CRUCIBLE_COUNT_COMPONENT(Type) + 1u
	static_assert(true CRUCIBLE_COMPONENT_LIST(CRUCIBLE_MANDATORY_MATCHES),
		"kMandatoryComponents and Mandatory<T> must agree for every component (see Components.h)");
	static_assert((0u CRUCIBLE_COMPONENT_LIST(CRUCIBLE_COUNT_COMPONENT)) == kMaxComponents,
		"CRUCIBLE_COMPONENT_LIST must list every ComponentKind for the mandatory check to be complete");
#undef CRUCIBLE_COUNT_COMPONENT
#undef CRUCIBLE_MANDATORY_MATCHES

	/*************************************************************************
	 \brief  Runtime form of Mandatory<T>, for any path that removes by
			 ComponentId (editor, serialization): such a path must refuse
			 ids this returns true for.
	 *************************************************************************/
	constexpr bool IsMandatoryComponent(ComponentId id) noexcept
	{
		return id < kMaxComponents && kMandatoryComponents[id];
	}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif

	/*************************************************************************
	 \brief
		Composes EntityManager + ComponentRegistry + ArchetypeRegistry into
		the single API systems and gameplay code actually call.

	 \details
		World is the single front-facing entry point for the ECS. It hides
		the archetype-move bookkeeping in MoveEntity() behind simple
		Add/Remove/Get/Has component calls; gameplay code and systems should
		only ever talk to World, never to EntityManager/Archetype/
		ArchetypeRegistry directly (their mutators are private to World).

	 \note
		Non-copyable: owns all ECS storage (every entity, archetype and
		component column), so copying would require deep-cloning the entire
		ECS state, which is not supported.
	 *************************************************************************/
	class ENGINE_API World
	{
	public:

		/*********************************************************************
		 \brief  Constructs an empty world and registers every known
				 component type.
		 *********************************************************************/
		World();
		~World() = default;

		// World owns ECS storage, so copying it is not allowed
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		/*********************************************************************
		 \brief  Advances the tick every write this frame is stamped with.
				 Call once per frame, before systems update.
		 *********************************************************************/
		void AdvanceTick() { ++m_currentTick; }
		std::uint64_t CurrentTick() const { return m_currentTick; }

		/*********************************************************************
		 \brief  Sets this frame's delta time (seconds). Core calls this once
				 per frame, before systems update.
		 *********************************************************************/
		void SetDeltaTime(float dt) noexcept { m_deltaTime = dt; }

		/*********************************************************************
		 \brief  This frame's delta time in seconds, as set by SetDeltaTime().
		 *********************************************************************/
		float DeltaTime() const noexcept { return m_deltaTime; }

		/*********************************************************************
		 \brief  Registers component type T with this world's registry.
		 \tparam T The component type; must satisfy RegisteredComponent.
		 \return The ComponentId T was registered under.
		 *********************************************************************/
		template <typename T>
		ComponentId RegisterComponent() { return m_components.Register<T>(); }

		/*********************************************************************
		 \brief  This world's component registry.
		 \return Reference to the ComponentRegistry.
		 \note  Read-only in practice: registration is private to World.
		 *********************************************************************/
		ComponentRegistry& Components() { return m_components; }

		/*********************************************************************
		 \brief  This world's archetype registry.
		 \return Reference to the ArchetypeRegistry.
		 \note  Read-only in practice: archetype creation and every
				structural change are private to World.
		 *********************************************************************/
		ArchetypeRegistry& Archetypes() { return m_archetypes; }

		/*********************************************************************
		 \brief  Creates a new entity with only the mandatory base
				 components (Transform, WorldTransform, HierarchyNode - see
				 kMandatoryComponents), default-valued: a root at the origin.
		 \return A handle to the new entity.
		 *********************************************************************/
		Entity CreateEntity();

		/*********************************************************************
		 \brief
			Creates a new entity, placed directly into the archetype for a
			given component signature.

		 \param[in]
			signature The components the new entity should start with. The
			mandatory base components (kMandatoryComponents) are always
			added to it, so passing them is allowed but never needed.
		 \return
			A handle to the new entity, placed directly in the archetype for
			signature | kMandatoryComponents. Every one of its components is
			default-constructed.

		 \note
			Fires OnComponentAdded for every component the entity starts
			with. Throws std::invalid_argument if the signature names an
			unregistered component. If construction fails, no entity is
			created and no slot is leaked.
		 *********************************************************************/
		Entity CreateEntity(const ComponentSignature& signature);

		/*********************************************************************
		 \brief
			Creates count entities in one archetype, reserving storage once.

		 \param[in] count     Number of entities to create.
		 \param[in] signature Component set every new entity starts with
							  (plus kMandatoryComponents, always).
		 \return The created entity handles.

		 \note
			All-or-nothing: if any entity fails to build, every entity
			created by this call is removed again and no hooks have fired.
			Hooks fire per entity once the whole batch exists.
		 *********************************************************************/
		std::vector<Entity> CreateEntities(uint32_t count, const ComponentSignature& signature = ComponentSignature{});

		/*********************************************************************
		 \brief  Destroys an entity AND all its descendants (Unity's
				 Object.Destroy semantics), freeing their slots for reuse.
		 \param[in] entity The entity to destroy.
		 \details Children are destroyed before their parents. To keep the
				  children, call DetachChildren(entity) first. Every destroy
				  path (this, CommandBuffer::DestroyEntity, DestroyAllEntities,
				  a rolled-back spawn) destroys the descendants too, so no
				  entity is ever left with a dead parent or turned into an
				  orphan root.
		 \note  A no-op (not an error) if entity is already dead or stale.
				Fires OnEntityDestroyed, then OnComponentRemoved for every
				component each entity still owns, before tearing storage down.
		 *********************************************************************/
		void DestroyEntity(Entity entity);

		/*********************************************************************
		 \brief
			Destroys every live entity through DestroyEntity(), so every
			removal hook fires.

		 \note
			Call before tearing down anything those hooks release resources
			into (e.g. the renderer). Throws if called during Query iteration.
		 *********************************************************************/
		void DestroyAllEntities();

		/*********************************************************************
		 \brief  Whether an entity handle is still alive.
		 \param[in] e The handle to check.
		 \return true if e refers to a currently-alive entity.
		 *********************************************************************/
		bool IsAlive(Entity e)const { return m_entities.IsAlive(e); }

		/*********************************************************************
		 \brief  The exact component set an entity currently has.
		 \param[in] entity The entity to query.
		 \return Its archetype's signature, or an empty signature if entity
				 is dead/stale.
		 *********************************************************************/
		ComponentSignature GetSignature(Entity entity)const;

		/*********************************************************************
		 \brief  Whether an entity currently has a given component type.
		 \tparam T The component type to check for.
		 \param[in] entity The entity to check.
		 \return true if entity is alive and its archetype contains T.
		 *********************************************************************/
		template <typename T>
		bool HasComponent(Entity entity)const;

		/*********************************************************************
		 \brief  Fetches a mutable pointer to an entity's component and
				 marks it changed (same model as a non-const Query).
		 \tparam T The component type to fetch.
		 \param[in] entity The entity to query.
		 \return Pointer to the component, or nullptr if entity is dead/
				 stale or does not have T.
		 \note  Use Read<T>() (or a const World) when you only read, so
				change-filtered systems don't see false changes.
		 *********************************************************************/
		template <typename T>
		T* GetComponent(Entity entity);

		/*********************************************************************
		 \brief  Read-only access to an entity's component; never marks it
				 changed.
		 \return Pointer to the component, or nullptr if entity is dead/
				 stale or does not have T.
		 *********************************************************************/
		template <typename T>
		const T* GetComponent(Entity entity) const;

		/*********************************************************************
		 \brief  Explicit read-only access (same as the const GetComponent).
		 *********************************************************************/
		template <typename T>
		const T* Read(Entity entity) const { return GetComponent<T>(entity); }

		/*********************************************************************
		 \brief  Explicit write access: returns a mutable pointer and marks
				 the component changed at the current tick.
		 \return Pointer to the component, or nullptr if entity is dead/
				 stale or does not have T.
		 *********************************************************************/
		template <typename T>
		T* Write(Entity entity) { return GetComponent<T>(entity); }

		/*********************************************************************
		 \brief
			Adds (or overwrites) a component on an entity.

		 \tparam T
			The component type to add.
		 \param[in]
			entity The entity to modify.
		 \param[in]
			value The value to give the component; defaults to T{}.
		 \return
			Reference to the entity's (new or existing) T instance.

		 \details
			If entity already has a T, overwrites it in place with no
			archetype move. Otherwise moves the entity to the archetype for
			its signature plus T and relocates value straight into its new
			column slot (one move - no default-construct-then-assign).
			Every entity has the mandatory components, so
			AddComponent<Transform>/<WorldTransform> is always an in-place
			overwrite (no hook, no archetype move).

		 \note
			Throws std::invalid_argument if entity is dead or stale, and
			std::logic_error if called while a Query is iterating. If the
			move fails, the entity is left unchanged. HierarchyNode is
			refused at compile time - use SetParent.
		 *********************************************************************/
		template <typename T> T& AddComponent(Entity entity, T value = T{});

		/*********************************************************************
		 \brief
			Constructs a T from args and adds it (or overwrites an existing
			T), exactly like AddComponent(entity, T(args...)).

		 \tparam T    The component type to add.
		 \tparam Args Constructor argument types for T.
		 \return Reference to the entity's T instance.
		 *********************************************************************/
		template <typename T, typename... Args> T& EmplaceComponent(Entity entity, Args&&... args);

		/*********************************************************************
		 \brief
			Removes a component from an entity, if it has one.

		 \tparam T
			The component type to remove.
		 \param[in]
			entity The entity to modify.

		 \details
			A no-op if entity does not currently have a T. Otherwise fires
			OnComponentRemoved, then moves the entity to the archetype for
			its signature minus T. Every allocation happens before the hook
			fires, so a failure leaves both the entity and the hook state
			untouched.

		 \note
			Throws std::invalid_argument if entity is dead or stale, and
			std::logic_error if called while a Query is iterating. The
			mandatory base components (Transform, WorldTransform,
			HierarchyNode) are rejected at compile time
			(RemovableComponent); they only go away with DestroyEntity.
		 *********************************************************************/
		template <RemovableComponent T> void RemoveComponent(Entity entity);

		/*********************************************************************
		 \brief
			Registers a callback fired for every entity that gains a T,
			AFTER the component's value has been written.

		 \details
			Fires for AddComponent/EmplaceComponent (when T is newly added),
			CreateEntity/CreateEntities (including the mandatory base
			components, with their default values) and CommandBuffer spawns.
			Overwriting an existing T does not fire it.

			For a CommandBuffer spawn the hook fires once per component,
			after the WHOLE buffer has replayed, so it sees the final value:
			the one given to EntitySpawn::With<T>() (or the default), after
			any WithParent() has already solved the local Transform.

		 \note
			Runs under the iteration guard: hooks may read, but must queue
			structural changes through a CommandBuffer. Registering a hook
			during Query iteration or hook dispatch throws std::logic_error.
		 \warning
			Hook failure policy: an exception thrown by a hook is caught,
			logged and counted (HookFailureCount()); the remaining hooks
			still run and the structural change always completes. Side
			effects a hook made before throwing are NOT undone, and a
			remove/destroy hook may later run for a component whose add
			hook failed - hooks must tolerate that.
		 *********************************************************************/
		template <typename T>
		void OnComponentAdded(std::function<void(World&, Entity, T&)> callback);

		/*********************************************************************
		 \brief
			Registers a callback fired for every entity about to lose a T,
			BEFORE the component is torn down (so the callback can still
			read it - e.g. release a GPU handle it holds).

		 \details
			Fires on RemoveComponent AND on DestroyEntity, so resource-owning
			hooks always pair up with their add.
		 \warning
			Same failure policy as OnComponentAdded().
		 *********************************************************************/
		template <typename T>
		void OnComponentRemoved(std::function<void(World&, Entity, T&)> callback);

		/*********************************************************************
		 \brief
			Registers a callback fired for every entity about to be
			destroyed, BEFORE its component data is torn down.
		 \warning
			Same failure policy as OnComponentAdded().
		 *********************************************************************/
		void OnEntityDestroyed(std::function<void(World&, Entity)> callback);

		/*********************************************************************
		 \brief  How many lifecycle-hook invocations have thrown (and been
				 caught) in this world so far.
		 *********************************************************************/
		std::uint64_t HookFailureCount() const noexcept { return m_hookFailures; }

		// ---------------------------------------------------------------------
		// Hierarchy - Unity Transform semantics (SetParent, DetachChildren,
		// sibling order, root). World is the only writer
		// of HierarchyNode links: every public write path refuses the
		// component at compile time, so both ends of every link always agree.
		// Every entity has a HierarchyNode (mandatory), so these never add a
		// component or move an entity between archetypes.
		// Defined in World.cpp.
		// ---------------------------------------------------------------------

		/*********************************************************************
		 \brief  Unity's Transform.SetParent(parent, worldPositionStays):
				 child becomes the LAST child of parent; kNullEntity makes it
				 a root.
		 \param worldPositionStays true (Unity's default): the child keeps
				 its world position/rotation/scale - its local Transform is
				 recomputed. false: its local Transform is kept, so it moves
				 with the new parent.
		 \return Ok, or why nothing was changed. UnsupportedTransform (only
				 with worldPositionStays) when the new parent's world
				 transform, or the child's current parent chain, has a zero or
				 non-finite scale, or when the old and new parents differ in
				 handedness (keeping the pose would need a negative scale):
				 there is no local Transform that keeps the world transform.
				 Use worldPositionStays = false then.
		 \details The world matrix is kept exactly whenever T/R/S can store
				  it. The solve works on the child's frame WITHOUT its own
				  scale, so a zero local scale is carried over unchanged
				  (the local scale is never recovered from a matrix). Crucible
				  never creates a negative scale. Shear can't be stored: see
				  Detail::DecomposeTRS for the policy (position exact).
				  Strong guarantee: every step that can throw or fail
				  (structural check, reading and solving transforms) happens
				  before any link or Transform is modified.
		 \note   Edits links on several entities, so structural rules apply:
				 throws std::logic_error during Query iteration or inside a
				 declared system's Update - use CommandBuffer::SetParent there.
		 *********************************************************************/
		ParentResult TrySetParent(Entity child, Entity parent, bool worldPositionStays = true);

		/*********************************************************************
		 \brief  TrySetParent() that throws std::invalid_argument when the
				 change is refused (cycle, self-parent, dead entity,
				 unsupported transform).
		 *********************************************************************/
		void SetParent(Entity child, Entity parent, bool worldPositionStays = true);

		/*********************************************************************
		 \brief  Unity's Transform.DetachChildren(): every child becomes a
				 root and keeps its world transform. Same rules as SetParent.
		 \note   All-or-nothing: if any child can't keep its world transform
				 (parent has a zero or negative scale - see TrySetParent), throws
				 std::invalid_argument and nothing is detached.
		 *********************************************************************/
		void DetachChildren(Entity parent);

		/*********************************************************************
		 \brief  Unity's Transform.parent.
		 \return The parent, or kNullEntity for a root or a dead entity.
		 *********************************************************************/
		Entity GetParent(Entity entity) const;
		// Fresh local transforms plus the current CPU animation pose, including sockets.
		bool TryGetWorldMatrix(Entity entity, Math::mat4& matrix) const;
		bool TryGetParentFrame(Entity entity, Math::mat4& matrix) const;

		/*********************************************************************
		 \brief  Unity's Transform.childCount (0 for a dead entity).
		 *********************************************************************/
		std::uint32_t ChildCount(Entity entity) const;

		/*********************************************************************
		 \brief  Unity's Transform.GetChild(index).
		 \return The index-th child, or kNullEntity if out of range.
		 \note   Walks from whichever end is closer: O(min(index, count-1-index)).
				 To visit every child use ForEachChild (O(count) overall).
		 *********************************************************************/
		Entity GetChild(Entity parent, std::uint32_t index) const;

		/*********************************************************************
		 \brief  Unity's Transform.root: the topmost ancestor (entity itself
				 if it has no parent; kNullEntity if entity is dead).
		 *********************************************************************/
		Entity GetRoot(Entity entity) const;

		/*********************************************************************
		 \brief  Unity's Transform.IsChildOf: true if entity IS ancestor, or
				 is anywhere below it.
		 *********************************************************************/
		bool IsChildOf(Entity entity, Entity ancestor) const;

		/*********************************************************************
		 \brief  Strict version of IsChildOf: ancestor is entity's parent,
				 grandparent, ... (never entity itself).
		 *********************************************************************/
		bool IsAncestorOf(Entity ancestor, Entity entity) const;

		/*********************************************************************
		 \brief  Unity's Transform.GetSiblingIndex. Roots have no sibling
				 list and return 0.
		 \note   Walks both directions at once: O(min(index, count-1-index)).
		 *********************************************************************/
		std::uint32_t GetSiblingIndex(Entity entity) const;

		/*********************************************************************
		 \brief  Unity's Transform.SetSiblingIndex: moves entity to index
				 among its siblings (clamped to the end). A no-op for roots.
		 \note   Same structural rules as SetParent.
		 *********************************************************************/
		void SetSiblingIndex(Entity entity, std::uint32_t index);

		/*********************************************************************
		 \brief  Unity's SetAsFirstSibling / SetAsLastSibling.
		*********************************************************************/
		void SetAsFirstSibling(Entity entity);
		void SetAsLastSibling(Entity entity);

		/*********************************************************************
		 \brief  Calls fn(child) for each direct child, first to last (Unity:
				 foreach (Transform child in transform)).
		 \note   Don't reparent or destroy this entity's children inside fn.
		*********************************************************************/
		template <typename Fn>
		void ForEachChild(Entity parent, Fn&& fn) const;

		/*********************************************************************
		 \brief  Appends root and all its descendants to out, parents before
				 children. Nothing is appended for a dead root.
		 \note   O(subtree). out doubles as the work list; reuse the same
				 vector across calls (clear() keeps its capacity) to avoid
				 reallocating.
		*********************************************************************/
		void CollectSubtree(Entity root, std::vector<Entity>& out) const;

		/*********************************************************************
		 \brief
			Builds a Query over a fixed set of required component types.

		 \tparam T
			The component types the query requires and will iterate.
			`const T` is read-only access; non-const T is write access and
			gets its change tick stamped on iteration.
		 \return
			A Query<T...> whose matching-archetype list has already been
			built (Rebuild() is called before returning).

		 \note
			Build once (e.g. on a system's first Update) and keep it - the
			query only re-scans archetypes when a new one has been created.
			A Query object belongs to one system; it must not be iterated by
			two threads at once.
		 *********************************************************************/
		template<typename ...T>
		Query<T... > CreateQuery();

		/*********************************************************************
		 \brief  Fluent builder for a query with Exclude<...>().
		 *********************************************************************/
		template<typename ...T>
		QueryBuilder<T...> BuildQuery() { return QueryBuilder<T...>(m_archetypes, &m_currentTick); }

		/*********************************************************************
		 \brief  Tick T was last written at for entity, or 0 if never (or if
				 entity is dead / doesn't have T).
		 *********************************************************************/
		template <typename T>
		std::uint64_t LastChanged(Entity entity) const;

		/*********************************************************************
		 \brief  Stamps T's change tick for entity with the current tick.
		 \note  Throws std::invalid_argument for a dead/stale entity and
				std::logic_error if the entity has no T.
		 *********************************************************************/
		template <typename T>
		void MarkChanged(Entity entity);

	private:
		friend class CommandBuffer;
		friend class HierarchyTransformSystem;   // reads NodeRow: one lookup per node per frame

		// Everything HierarchyTransformSystem needs about one node, from ONE
		// entity lookup (the public Read/LastChanged/Write path costs one each).
		struct NodeRow
		{
			const Transform* local = nullptr;
			const HierarchyNode* links = nullptr;
			const SocketAttachment* socket = nullptr;
			WorldTransform* world = nullptr;
			std::uint64_t localTick = 0;          // LastChanged<Transform>
			std::uint64_t linksTick = 0;          // LastChanged<HierarchyNode>
			std::uint64_t* worldTick = nullptr;   // stamp this when world is rewritten
		};
		// false for a dead/stale entity. Every live entity has all three
		// (mandatory base components), so there is no Contains() check.
		bool ReadNodeRow(Entity entity, NodeRow& out) noexcept;

		using ComponentHookFn = std::function<void(World&, Entity, void*)>;

		EntityRecord& RequireMutableEntity(Entity entity, const char* operation);
		void RequireStructuralChange(const char* operation) const;

		Entity CreateEntityImpl(const ComponentSignature& signature, bool fireAddHooks);
		// Takes an entity slot and adds its default row to archetype, stamped now, no hooks.
		// Strong guarantee: on a throw the slot is given back and archetype is unchanged.
		Entity PlaceNewEntity(Archetype& archetype);
		void NotifyComponentsAdded(Entity entity);

		// fireHooks == false removes an entity whose add hooks never ran (a
		// rolled-back spawn or bulk create), so no remove/destroy hook may run.
		void DestroyEntityImpl(Entity entity, bool fireHooks);

		/*********************************************************************
		 \brief
			Relocates an entity's row from its current archetype into
			destination, carrying over every shared component.

		 \details
			Reserves every destination array first and builds the
			destination's new default columns before moving anything, so if
			anything throws the entity is still intact in its source
			archetype. addedValue (for addedId) is relocated straight into
			its slot.
		 \note
			Throws std::logic_error (before anything moves) if destination
			lacks a mandatory base component. Removal paths must check
			earlier (before firing remove hooks); RemoveComponent does so at
			compile time.
		 *********************************************************************/
		void MoveEntity(EntityRecord& record, Entity entity, Archetype& destination,
			ComponentId addedId = kInvalidComponentId, void* addedValue = nullptr);

		template <typename T> T& OverwriteComponent(EntityRecord& record, ComponentId id, T&& value);
		template <typename T> T& InsertComponent(EntityRecord& record, Entity entity, ComponentId id, T&& value);

		// Hook dispatch: every hook is isolated (see the failure policy on OnComponentAdded).
		void FireAddHooks(ComponentId id, Entity entity, void* component) noexcept;
		void FireRemoveHooks(ComponentId id, Entity entity, void* component) noexcept;
		void FireDestroyHooks(Entity entity) noexcept;
		void FireAddHooksForRow(Archetype& archetype, uint32_t row, Entity entity) noexcept;
		void FireRemoveHooksForRow(Archetype& archetype, uint32_t row, Entity entity) noexcept;
		void ReportHookFailure(const char* hookKind, ComponentId id, Entity entity, const char* what) noexcept;

		// Hierarchy internals (World.cpp). HierarchyLinks() is the only write path to a
		// HierarchyNode; it does NOT stamp - StampReparented() marks the one entity whose
		// parent changed (sibling/child-list edits move nothing).
		HierarchyNode& HierarchyLinks(Entity entity);
		void StampReparented(Entity entity);
		void DetachFromParent(Entity child);
		// Takes child out of parent's child list (neighbours' sibling links, parent's
		// first/last child and count); child's own links are left for the caller.
		void UnlinkFromSiblings(const HierarchyNode& child, HierarchyNode& parent);
		void InsertChildAt(Entity child, Entity parent, std::uint32_t index);   // index >= count appends
		void DestroyDescendants(Entity root, bool fireHooks);

		// Unchecked reads for internal walks: one record lookup, no declared-access check
		// (public queries check once per call via CheckHierarchyRead). Null if dead.
		const HierarchyNode* NodeOf(Entity entity) const;
		const Transform* LocalOf(Entity entity) const;
		void CheckHierarchyRead(const char* operation) const;

		// The index-th child, walking from whichever end is closer; kNullEntity past the end.
		Entity ChildAt(const HierarchyNode& parent, std::uint32_t index) const;
		// Position among its siblings (0 for a root), walking both directions at once.
		std::uint32_t SiblingIndexOf(const HierarchyNode& node) const;
		// True if target is from or one of its ancestors. With worldOut, also returns from's
		// world matrix from the same walk (valid only when false is returned).
		bool ReachesUp(Entity from, Entity target, Math::mat4* worldOut) const;

		// The local Transform that keeps a child's world transform when it moves from
		// oldParentWorld to the parent whose inverse world matrix is *newParentInverse
		// (nullptr = root). Reads only; UnsupportedTransform if there is none.
		ParentResult SolveKeepWorld(const Transform& local, const Math::mat4& oldParentWorld,
			const Math::mat4* newParentInverse, Transform& out) const;

		// parent world x local (T*R*S) computed now in one pass up the parents;
		// identity for a dead entity or kNullEntity. Used by keep-world reparenting.
		Math::mat4 ComputeWorldMatrix(Entity entity) const;
		void InstallHierarchyHook();

		EntityManager     m_entities;
		ComponentRegistry m_components;
		ArchetypeRegistry m_archetypes;
		std::uint64_t     m_currentTick = 1;
		float             m_deltaTime = 0.f;
		std::uint64_t     m_hookFailures = 0;

		std::array<std::vector<ComponentHookFn>, kMaxComponents> m_onAddHooks;
		std::array<std::vector<ComponentHookFn>, kMaxComponents> m_onRemoveHooks;
		std::vector<std::function<void(World&, Entity)>> m_onDestroyHooks;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

	template <typename T>
	void World::OnComponentAdded(std::function<void(World&, Entity, T&)> callback)
	{
		if (m_archetypes.IsIterating())
			throw std::logic_error("World::OnComponentAdded: register hooks outside queries and hook dispatch");

		m_onAddHooks[ComponentTypeId<T>()].push_back(
			[cb = std::move(callback)](World& world, Entity entity, void* component)
			{
				cb(world, entity, *static_cast<T*>(component));
			});
	}

	template <typename T>
	void World::OnComponentRemoved(std::function<void(World&, Entity, T&)> callback)
	{
		if (m_archetypes.IsIterating())
			throw std::logic_error("World::OnComponentRemoved: register hooks outside queries and hook dispatch");

		m_onRemoveHooks[ComponentTypeId<T>()].push_back(
			[cb = std::move(callback)](World& world, Entity entity, void* component)
			{
				cb(world, entity, *static_cast<T*>(component));
			});
	}

	inline bool World::ReadNodeRow(Entity entity, NodeRow& out) noexcept
	{
		if (!m_entities.IsAlive(entity)) return false;
		const EntityRecord& record = m_entities.Record(entity);
		Archetype& a = *record.archetype;
		const std::uint32_t row = record.row;
		ComponentColumn& local = a.GetColumn(ComponentTypeId<Transform>());
		ComponentColumn& links = a.GetColumn(ComponentTypeId<HierarchyNode>());
		ComponentColumn& world = a.GetColumn(ComponentTypeId<WorldTransform>());
		out.local = static_cast<const Transform*>(local.At(row));
		out.links = static_cast<const HierarchyNode*>(links.At(row));
		out.world = static_cast<WorldTransform*>(world.At(row));
		out.socket = a.Contains(ComponentTypeId<SocketAttachment>())
			? static_cast<const SocketAttachment*>(a.GetColumn(ComponentTypeId<SocketAttachment>()).At(row)) : nullptr;
		out.localTick = local.ChangedTicksData()[row];
		out.linksTick = links.ChangedTicksData()[row];
		out.worldTick = world.ChangedTicksData() + row;
		return true;
	}

	template <typename T>
	bool World::HasComponent(Entity e)const
	{
		if (!m_entities.IsAlive(e)) return false;
		return m_entities.Record(e).archetype->Contains(ComponentTypeId<T>());
	}

	template <typename T>
	T* World::GetComponent(Entity e)
	{
		static_assert(!WorldManaged<T>::value, "This component is managed by World (e.g. HierarchyNode): use Read<T>() and World's API to change it");
		constexpr ComponentId id = ComponentTypeId<T>();
		CheckDeclaredAccess(ComponentSignature{}, MakeSignature<T>(), "writes through World::GetComponent/Write and");

		if (!m_entities.IsAlive(e)) return nullptr;
		const EntityRecord& record = m_entities.Record(e);
		if (!record.archetype->Contains(id)) return nullptr;

		// Handing out a mutable pointer counts as a write.
		record.archetype->GetColumn(id).Touch(record.row, m_currentTick);
		return static_cast<T*>(record.archetype->GetComponentPtr(id, record.row));
	}

	template <typename T>
	const T* World::GetComponent(Entity e) const
	{
		constexpr ComponentId id = ComponentTypeId<T>();
		CheckDeclaredAccess(MakeSignature<T>(), ComponentSignature{}, "reads through World::Read/GetComponent and");

		if (!m_entities.IsAlive(e)) return nullptr;
		const EntityRecord& record = m_entities.Record(e);
		if (!record.archetype->Contains(id)) return nullptr;
		return static_cast<const T*>(static_cast<const Archetype*>(record.archetype)->GetColumn(id).At(record.row));
	}

	template <typename Fn>
	void World::ForEachChild(Entity parent, Fn&& fn) const
	{
		CheckHierarchyRead("reads through World::ForEachChild and");   // once, not per child
		const HierarchyNode* node = NodeOf(parent);
		if (!node) return;   // dead/stale parent: nothing to visit (a live one always has a node)
		for (Entity c = node->firstChild; c.IsValid(); )
		{
			const Entity next = NodeOf(c)->nextSibling;   // read before fn runs
			fn(c);
			c = next;
		}
	}

	template <typename T>
	std::uint64_t World::LastChanged(Entity entity) const
	{
		if (!m_entities.IsAlive(entity)) return 0;
		const EntityRecord& record = m_entities.Record(entity);
		constexpr ComponentId id = ComponentTypeId<T>();
		if (!record.archetype->Contains(id)) return 0;
		return static_cast<const Archetype*>(record.archetype)->GetColumn(id).LastChanged(record.row);
	}

	template <typename T>
	void World::MarkChanged(Entity entity)
	{
		CheckDeclaredAccess(ComponentSignature{}, MakeSignature<T>(), "marks changed via World::MarkChanged and");

		if (!m_entities.IsAlive(entity))
			throw std::invalid_argument("World::MarkChanged called with a dead/stale Entity");

		const EntityRecord& record = m_entities.Record(entity);
		constexpr ComponentId id = ComponentTypeId<T>();
		if (!record.archetype->Contains(id))
			throw std::logic_error("World::MarkChanged: the entity does not have this component");

		record.archetype->GetColumn(id).Touch(record.row, m_currentTick);
	}

	template<typename T>
	inline T& World::AddComponent(Entity entity, T value)
	{
		static_assert(!WorldManaged<T>::value, "This component is managed by World (e.g. HierarchyNode): use SetParent (kNullEntity to detach)");
		EntityRecord& record = RequireMutableEntity(entity, "AddComponent");
		constexpr ComponentId id = ComponentTypeId<T>();

		if (record.archetype->Contains(id)) return OverwriteComponent<T>(record, id, std::move(value));
		return InsertComponent<T>(record, entity, id, std::move(value));
	}

	template <typename T, typename... Args>
	inline T& World::EmplaceComponent(Entity entity, Args&&... args)
	{
		return AddComponent<T>(entity, T(std::forward<Args>(args)...));
	}

	template<RemovableComponent T>
	inline void World::RemoveComponent(Entity entity)
	{
		EntityRecord& record = RequireMutableEntity(entity, "RemoveComponent");
		constexpr ComponentId id = ComponentTypeId<T>();

		if (!record.archetype->Contains(id)) return;

		// Allocate everything the move needs first: once the hook has fired, the removal must not fail.
		Archetype* destination = m_archetypes.GetRemoveDestination(*record.archetype, id);
		destination->ReserveForAppend();

		FireRemoveHooks(id, entity, record.archetype->GetComponentPtr(id, record.row));
		MoveEntity(record, entity, *destination);
	}

	template <typename T>
	T& World::OverwriteComponent(EntityRecord& record, ComponentId id, T&& value)
	{
		T& existing = *static_cast<T*>(record.archetype->GetComponentPtr(id, record.row));
		existing = std::move(value);
		record.archetype->GetColumn(id).Touch(record.row, m_currentTick);
		return existing;
	}

	template <typename T>
	T& World::InsertComponent(EntityRecord& record, Entity entity, ComponentId id, T&& value)
	{
		Archetype* destination = m_archetypes.GetAddDestination(*record.archetype, id);
		MoveEntity(record, entity, *destination, id, &value);   // value is relocated straight into its slot

		T& slot = *static_cast<T*>(destination->GetComponentPtr(id, record.row));
		destination->GetColumn(id).Touch(record.row, m_currentTick);
		FireAddHooks(id, entity, &slot);
		return slot;
	}

	template<typename... T>
	Query<T...>World::CreateQuery()
	{
		QueryDescription description;
		description.required = MakeSignature<std::remove_const_t<T>...>();
		Query<T...>query(std::move(description), m_archetypes, &m_currentTick);
		query.Rebuild();
		return query;
	}
}
