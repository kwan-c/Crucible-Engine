/*************************************************************************
* \file:         ComponentKind.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.4
* \brief
*                Declares the fixed ComponentKind enum (one entry per
*                engine component type) and the CRUCIBLE_COMPONENT /
*                CRUCIBLE_COMPONENT_LIST macros used to bind each C++
*                component struct to its ComponentKind/name.
*
* \details
*                This is THE file to edit when adding a new component
*                type: add an enum value here (before MAXCOMPONENT),
*                add it to CRUCIBLE_COMPONENT_LIST, define the actual
*                component struct in Components.h, and bind it there
*                with CRUCIBLE_COMPONENT(Type, KindValue). Everything
*                else (ComponentTypeId.h, ComponentInfo.h,
*                ComponentRegistry.cpp's RegisterAll) picks the new
*                type up automatically via the macro list.
*
* \history
*    -v1.0       initial fixed component-kind enum + binding macros
*    -v1.1       HierarchyNode and WorldTransform kinds; WorldManaged and
*                Mandatory traits, RemovableComponent concept
*    -v1.2       EntityName kind (17); WorldValidated trait
*    -v1.3       ToggleableComponent concept, RequiresComponents trait
*                (per-component on/off state and its dependencies)
*    -v1.4       NetworkIdentity, NetInterpolation, PlayerInput kinds
*                (33-35); NotToggleable and RuntimeOnly traits
*
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace Crucible
{

	/*************************************************************************
	 \brief
		Fixed enumeration giving every engine component type a stable
		numeric id.

	 \details
		The numeric value of each enumerator IS the component's ComponentId
		and its bit position in a ComponentSignature. MAXCOMPONENT is a
		sentinel one past the last real entry, used to size fixed-length
		arrays such as ComponentSignature's bitset and ComponentRegistry's
		storage.

	 \note
		This is the file to edit when adding a new component type: add an
		enumerator here (before MAXCOMPONENT) and add it to
		CRUCIBLE_COMPONENT_LIST below.
	 *************************************************************************/
	//fixed enum id for each component
	enum class ComponentKind :uint32_t
	{
		Transform = 0,
		RigidBody = 1,
		MeshRenderer = 2,
		AudioSource = 3,
		Collider = 4,
		CharacterController = 5,
		HierarchyNode = 6,    // parent / child / sibling links
		WorldTransform = 7,   // computed by HierarchyTransformSystem
		Light = 8,            // Graphics: point/spot/directional light
		Skybox = 9,           // Graphics: scene sky + ambient + exposure
		AudioListener = 10,   // Audio: where the ears are (Audio/AudioComponents.h)
		Camera = 11,          // Graphics: the game camera (struct CameraComponent)
		PlayerController = 12,   // Player: keyboard / gamepad -> CharacterController, local player only (Player/PlayerComponents.h)
		CameraOrbit = 13,        // Player: mouse / right stick -> third-person camera pivot (Player/PlayerComponents.h)
		ParticleEmitter = 14,    // Graphics: a particle effect (sparks, fire, smoke...) - Components.h
		MaterialParams = 15,     // Graphics: per-object material tweaks (glow strength...) - Components.h
		StressMover = 16,        // TEMPORARY load-test helper: walks an entity in a circle - Components.h
		EntityName = 17,         // every entity's display name (mandatory) - Components.h
		UICanvas = 18,
		UIRect = 19,
		UIImage = 20,
		UIButton = 21,
		UIText = 22,
		UISlider = 23,           // UI: a draggable value (UI/UIComponents.h)
		Water = 24,
		AnimatedModel = 25,
		SocketAttachment = 26,
		ScriptComponent = 27,
		PrefabInstance = 28,     // Editor/Scene: links an entity to the .cprefab it was placed from (Scene/Prefab.h)
		ConstantForce = 29,      // Physics: a steady push set in the Inspector (Physics/PhysicsComponents.h)
		PhysicsJoint = 30,       // Physics: hinge, fixed, ball, character or spring joint between two bodies (Physics/PhysicsComponents.h)
		WorldText = 31,          // Graphics: text placed in the 3D world by its Transform (Text/WorldText.h)
		Vine = 32,               // Graphics: procedural vines that grow from the entity and wrap one of its children - Components.h
		NetworkIdentity = 33,    // Network: who this entity is on every peer (Network/NetComponents.h)
		NetInterpolation = 34,   // Network: a client's recent snapshots of a remote entity (Network/NetComponents.h)
		PlayerInput = 35,        // Network/Player: one tick of a player's intent (Network/NetComponents.h)
		FlowFieldSurface = 36,   // Navigation: where enemies can walk, Unity's NavMeshSurface (Navigation/NavigationComponents.h)
		FlowFieldAgent = 37,     // Navigation: one enemy's legs, Unity's NavMeshAgent (Navigation/NavigationComponents.h)
		StateGraphAgent = 38,    // AI: one enemy's brain, runs a .stategraph (AI/AIComponents.h)

		MAXCOMPONENT = 39
	};


	/*************************************************************************
	 \brief
		Primary template for per-component-type metadata; deliberately left
		undefined.

	 \details
		Only explicit specialisations produced by CRUCIBLE_COMPONENT are
		valid to use. A type with no specialisation fails the
		RegisteredComponent concept at compile time.
	 *************************************************************************/
	template <typename T>
	struct ComponentTraits;

	/*************************************************************************
	 \brief
		True for components whose fields only World may write, because they
		hold invariants that span several entities (e.g. HierarchyNode
		links).
	 \details
		Public mutable access paths (World::GetComponent/Write/AddComponent/
		EmplaceComponent, non-const Query, CommandBuffer::AddComponent and
		EntitySpawn::With) static_assert against it. Specialised next to the
		component's definition in Components.h.
	 *************************************************************************/
	template <typename T>
	struct WorldManaged : std::false_type {};

	/*************************************************************************
	 \brief
		True for components anyone may read, but whose values World must
		check before they are stored (EntityName: the naming rules in
		World.h).
	 \details
		Written only through World calls that check the value -
		World::SetEntityName and AddComponent/EmplaceComponent (also
		reached from CommandBuffer, scene loading and undo). Mutable
		pointers are refused at compile time: World::GetComponent/Write and
		non-const Query static_assert against it, and AddComponent,
		EmplaceComponent and the OnComponentAdded/Removed hooks give only a
		const reference. Specialised in Components.h.
	 *************************************************************************/
	template <typename T>
	struct WorldValidated : std::false_type {};

	/*************************************************************************
	 \brief
		True for Crucible's mandatory base scene components (Transform,
		WorldTransform, HierarchyNode): every entity World creates has them
		from birth until it is destroyed.
	 \details
		Specialised next to the component definitions in Components.h. World
		combines them into every creation signature (kMandatoryComponents in
		World.h, which is checked against this trait for every component),
		and the typed removal paths (World::RemoveComponent,
		CommandBuffer::RemoveComponent) only accept RemovableComponent types.
	 *************************************************************************/
	template <typename T>
	struct Mandatory : std::false_type {};

	/*************************************************************************
	 \brief  A component that may be removed from a live entity (i.e. not
			 one of the mandatory base components).
	 *************************************************************************/
	template <typename T>
	concept RemovableComponent = !Mandatory<T>::value;

	/*************************************************************************
	 \brief
		True for components the ECS must never switch off although they are
		neither mandatory nor World-managed: identity-like components such
		as NetworkIdentity, which would silently drop out of every default
		query if they were off.
	 \details
		Excluded from ToggleableComponent, so World::SetComponentEnabled and
		the other on/off calls refuse them at compile time and their columns
		keep no on/off bytes. Specialised next to the component's definition
		(the network components: Network/NetComponents.h).
	 *************************************************************************/
	template <typename T>
	struct NotToggleable : std::false_type {};

	/*************************************************************************
	 \brief
		True for components that exist only while the game runs and are never
		written to a file: scenes, prefabs, the Play snapshot, undo steps and
		copy/paste all leave them out.
	 \details
		EntityIO's kSkipInSceneFile skips them, and so does everything built
		on it, so Stop never brings back a stale value either. The editor
		also keeps them out of Add Component, prefab overrides and undo.
		Specialised next to the component's definition
		(Network/NetComponents.h).
	 *************************************************************************/
	template <typename T>
	struct RuntimeOnly : std::false_type {};

	/*************************************************************************
	 \brief
		A component the ECS can switch off without removing it (Unity's
		Behaviour.enabled): every component except the mandatory base ones,
		World-managed ones and NotToggleable ones.
	 \details
		The ECS keeps the on/off state itself, next to the component's data
		(one byte per entity in the component's column), so a new component
		type gets it with nothing to write. It is independent of any flag a
		component has of its own (MeshRenderer::visible, Light::enabled...):
		those keep their meaning and are never changed by it. See
		World::SetComponentEnabled.
	 *************************************************************************/
	template <typename T>
	concept ToggleableComponent = !Mandatory<T>::value && !WorldManaged<T>::value && !NotToggleable<T>::value;

	/*************************************************************************
	 \brief  A list of component types (used by RequiresComponents).
	 *************************************************************************/
	template <typename... Components>
	struct ComponentTypeList {};

	/*************************************************************************
	 \brief
		The other components on the SAME entity that T needs in order to
		run (e.g. RigidBody needs a Collider). Specialised in Components.h:
			template <> struct RequiresComponents<RigidBody>
			{ using type = ComponentTypeList<Collider>; };
	 \details
		Only affects the on/off state: while any of them (or anything they
		require in turn) is switched off, T counts as off too - not
		"active" - and comes back by itself when they are on again. T's own
		on/off choice is untouched. A requirement the entity doesn't have
		blocks nothing. Cycles are rejected when the World is built.
	 *************************************************************************/
	template <typename T>
	struct RequiresComponents { using type = ComponentTypeList<>; };


/*************************************************************************
 \brief
	Binds a component struct to its ComponentKind and a display name.
 \param Type
	The component struct being bound (e.g. Transform).
 \param KindValue
	The matching ComponentKind enumerator name (e.g. Transform).

 \details
	Expands to an explicit specialisation of ComponentTraits<Type> with
	a `kind` and a `name` (stringised from Type). Used once per
	component struct in Components.h, right after its definition.
*************************************************************************/
#define CRUCIBLE_COMPONENT(Type, KindValue)                         \
    template<>                                                       \
	struct ComponentTraits<Type>                                      \
	{                                                                  \
		static constexpr ComponentKind kind = ComponentKind::KindValue; \
		static constexpr std::string_view name = #Type;                  \
	};

/*************************************************************************
  \brief
	 X-macro listing every registered component type, in registration
	 order.
  \param X
	 A macro/function-like name invoked once per component type, as
	 X(Type).
  \details
	 Expanding this with X = a per-type action is how
	 ComponentRegistry::RegisterAll() registers every component type
	 without a hand-maintained list of Register<T>() calls - add a new
	 component here (matching its ComponentKind entry above) and
	 RegisterAll() picks it up automatically.
*************************************************************************/
#define CRUCIBLE_COMPONENT_LIST(X)                                             \
 X(Transform)                                                                  \
 X(RigidBody)                                                                  \
 X(MeshRenderer)                                                               \
 X(AudioSource)                                                                \
 X(Collider)                                                                   \
 X(CharacterController)                                                        \
 X(HierarchyNode)                                                              \
 X(WorldTransform)                                                             \
 X(Light)                                                                      \
 X(Skybox)                                                                     \
 X(AudioListener)                                                              \
 X(CameraComponent)                                                            \
 X(PlayerController)                                                           \
 X(CameraOrbit)                                                                \
 X(ParticleEmitter)                                                            \
 X(MaterialParams)                                                             \
 X(StressMover)                                                                \
 X(EntityName)                                                                 \
 X(UICanvas)																   \
 X(UIRect)																	   \
 X(UIImage)																	   \
 X(UIButton)																   \
 X(UIText)																	   \
 X(UISlider)														           \
 X(Water)                                                                      \
 X(AnimatedModel)                                                              \
 X(SocketAttachment)                                                           \
 X(ScriptComponent)                                                            \
 X(Vine)                                                                       \
 X(PrefabInstance)                                                             \
 X(ConstantForce)                                                              \
 X(PhysicsJoint)                                                               \
 X(WorldText)                                                                  \
 X(NetworkIdentity)                                                            \
 X(NetInterpolation)                                                           \
 X(PlayerInput)                                                                \
 X(FlowFieldSurface)                                                           \
 X(FlowFieldAgent)                                                             \
 X(StateGraphAgent)                                                            \

}
