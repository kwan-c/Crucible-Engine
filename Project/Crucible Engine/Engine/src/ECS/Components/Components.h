/*************************************************************************
* \file:         Components.h
* \author : Abhishek Akilandeswari Patchamuthu
* \par : Email : a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.2
* \brief
* Defines the actual per - entity component structs(plain
	* data, no logic) and binds each one to its
	* ComponentKind via CRUCIBLE_COMPONENT(see
		* ComponentKind.h).
	*
	*\details
	* This is the second(and last) file to touch when
	* adding a new component type - after adding the enum
	* value / list entry in ComponentKind.h, define the
	* struct here and bind it with CRUCIBLE_COMPONENT(Type,
		*KindValue) at the bottom.ComponentRegistry::RegisterAll()
	* (ComponentRegistry.cpp) then picks it up
	* automatically.Systems(e.g.ECSPhysicsSystem.h / .cpp)
	* read / write these structs through
	* World::GetComponent<T> / AddComponent<T>(see World.h).
	*
	*\history
	* -v1.0       initial Transform / MeshRenderer / Audio / RigidBody components
	* -v1.1       Transform rotation/scale; WorldTransform and HierarchyNode;
	*             mandatory base components (Transform, WorldTransform,
	*             HierarchyNode)
	* -v1.2       Audio renamed to AudioSource (clashed with class Crucible::Audio)
	*
	*
	**************************************************************************/
#pragma once

#include <cstdint>

#include"ComponentKind.h"
#include"Math/Math.h"
#include"Physics/PhysicsComponents.h"
#include"../Core/ECSCore.h"   // Entity (HierarchyNode links)

namespace Crucible
{
	/*************************************************************************
	 \brief  Position of an entity in local/world space.
	 *************************************************************************/
	struct Transform
	{
		Math::vec3  positions{0.f,0.f,0.f};								// local: relative to the parent (or the world, for a root)
		Math::quat  rotation = Math::quat(Math::vec3(0.f, 0.f, 0.f));
		Math::vec3  scale{ 1.f,1.f,1.f };
	};

	/*************************************************************************
	 \brief  The entity's final transform in world space.
	 \note   Written ONLY by HierarchyTransformSystem, first in PreRender
			 (after every earlier stage's commands and events). Read it from
			 PreRender systems that RunAfter<HierarchyTransformSystem>(), or
			 after the frame's systems (the renderer); edit Transform instead.
			 valid == false means "not computed yet" (e.g. spawned during
			 PreRender) - skip it.
	 *************************************************************************/
	struct WorldTransform
	{
		Math::mat4 matrix = Math::mat4(1.0f);
		bool valid = false;   // false until the propagation system has computed it
	};

	/*************************************************************************
	 \brief  An entity's place in the scene tree, as intrusive links.
	 \details
		Children form a doubly linked list through prevSibling/nextSibling,
		so attach/detach are O(1) and nothing is heap-allocated. Every
		entity has one (it is a mandatory base component); a root with no
		children simply has all links null.
	 \note   Read-only outside World: every public write path refuses it at
			 compile time (see WorldManaged). Use World::SetParent/
			 DetachChildren/SetSiblingIndex, which keep both ends of every
			 link consistent. (Named HierarchyNode because the editor already
			 has a Crucible::Hierarchy panel class.)
			 Its change tick (LastChanged / change filters) means "this
			 entity was reparented": SetSiblingIndex and edits to a parent's
			 or siblings' links don't stamp it, because nothing moves.
	 *************************************************************************/
	struct HierarchyNode
	{
		Entity parent{};
		Entity firstChild{};
		Entity lastChild{};
		Entity prevSibling{};
		Entity nextSibling{};
		std::uint32_t childCount = 0;
	};

	/*************************************************************************
	 \brief
		Data needed to draw an entity's mesh.
	 \details
		Purely descriptive - no dependency on a specific renderer beyond the
		Math types.
	 *************************************************************************/
	struct MeshRenderer
	{
		bool visible = true;
		Math::vec4 colour{ 0.f,0.f,0.f,0.f };
	};
	
	/*************************************************************************
	 \brief  Playback state for an entity's audio source.
	 \note   Named AudioSource (Unity's name), not Audio: the audio system
	         class is Crucible::Audio, and one namespace cannot hold both.
	 *************************************************************************/
	struct AudioSource
	{
		bool play = false;
		bool loop = false;
	};

	/*************************************************************************
	 \brief
		Linear motion state used by ECSPhysicsSystem to integrate Transform.
	 *************************************************************************/
	struct RigidBody
	{
	    Math::vec3 velocity{ 0.f,0.f,0.f };
		float mass = 1.f;
	};

	// Bind each component above to its ComponentKind (see ComponentKind.h)
	// and give ComponentRegistry a compile-time name for it.
	CRUCIBLE_COMPONENT(Transform, Transform);
	CRUCIBLE_COMPONENT(RigidBody, RigidBody);
	CRUCIBLE_COMPONENT(MeshRenderer, MeshRenderer);
	CRUCIBLE_COMPONENT(AudioSource, AudioSource);
	CRUCIBLE_COMPONENT(HierarchyNode, HierarchyNode);
	CRUCIBLE_COMPONENT(WorldTransform, WorldTransform);

	// Links span several entities: only World may write them.
	template <> struct WorldManaged<HierarchyNode> : std::true_type {};

	// Mandatory base scene components: every entity has them for its whole
	// life (see World::CreateEntity and kMandatoryComponents in World.h).
	template <> struct Mandatory<Transform> : std::true_type {};
	template <> struct Mandatory<WorldTransform> : std::true_type {};
	template <> struct Mandatory<HierarchyNode> : std::true_type {};


}