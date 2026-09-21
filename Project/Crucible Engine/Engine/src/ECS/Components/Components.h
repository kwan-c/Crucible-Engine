/*************************************************************************
* \file:         Components.h
* \author : Abhishek Akilandeswari Patchamuthu
* \par : Email : a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
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
	*
	*
	**************************************************************************/
#pragma once

#include <cstdint>

#include"ComponentKind.h"
#include"Math/Math.h"

namespace Crucible
{
	/*************************************************************************
	 \brief  Position of an entity in local/world space.
	 *************************************************************************/
	struct Transform
	{
		Math::vec3  positions{0.f,0.f,0.f};
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
	 *************************************************************************/
	struct Audio
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
	CRUCIBLE_COMPONENT(Audio, Audio);
	

}