/*************************************************************************
* \file:         PhysicsBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          3 November 2026
* \version       1.1
* \brief         Internal calls behind C# Physics.Raycast
*                (ManagedScripts/Runtime/Physics.cs) and the C#
*                CharacterController class (CharacterController.cs).
*
* \details
*    Bindings only - Physics/ and ECSPhysicsSystem (Hugo's) are unchanged.
*
*    Raycast calls ECSPhysicsSystem::Raycast (the entity version), whose
*    EntityFilter::ignore skips the shooter's body - a CharacterController's
*    hidden capsule included - inside Jolt. (v1.0 re-cast from 1 cm past
*    each hit instead, which never got out of a capsule the ray started in,
*    so a CharacterController's own rays always reported "nothing".)
*
*    CharacterController goes through the component, as gameplay C++ does:
*    SimpleMove / Jump leave a request that ECSPhysicsSystem (PostUpdate,
*    after scripts) uses in this frame's step; IsGrounded / Velocity are
*    what the last step reported. Changing Height, Radius, SlopeLimit or
*    StepOffset makes physics rebuild the capsule.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "Physics/PhysicsComponents.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsEvents.h"
#include "Physics/EntityQueries.h"
#include "ECS/ECSPhysicsSystem.h"
#include "ECS/Systems/EventBus/EventBuses.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <vector>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		// The physics system (SetPhysics, from Core). Main thread only.
		thread_local ECSPhysicsSystem* physics = nullptr;

		bool IsFinite(const ScriptVec3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		// ======================================================================
		// Physics.Raycast
		// ======================================================================

		// How many times a ray may be re-cast past a body whose entity is already
		// destroyed (physics removes it on its next step).
		constexpr int MaxDeadHits = 4;
		// How far past such a hit the next cast starts, in metres.
		constexpr float SkipDistance = 0.01f;

		/*********************************************************************
		 \brief  C# Physics.Raycast. Casts from origin along direction (any
		         length; only its heading is used) up to maxDistance metres.
		         ignoreIndex 0 = ignore nothing.
		 \return 1 with the hit entity, point, normal and distance from the
		         original origin; 0 if nothing was hit, outside Play, or
		         physics is not running.
		 *********************************************************************/
		int32_t ManagedPhysicsRaycast(const ScriptVec3* origin, const ScriptVec3* direction, float maxDistance,
		                              int32_t ignoreIndex, int32_t ignoreGeneration, int32_t* outIndex,
		                              int32_t* outGeneration, ScriptVec3* outPoint, ScriptVec3* outNormal,
		                              float* outDistance) noexcept
		{
			try
			{
				if (!origin || !direction || !outIndex || !outGeneration || !outPoint || !outNormal || !outDistance)
					return 0;
				*outIndex = 0;
				*outGeneration = 0;
				if (!physics || !EntityContext::InPlaySession() || !IsFinite(*origin) || !IsFinite(*direction) ||
				    !(maxDistance > 0.0f))
					return 0;
				const Math::vec3 heading(direction->x, direction->y, direction->z);
				const float length = std::sqrt(heading.x * heading.x + heading.y * heading.y + heading.z * heading.z);
				if (!(length > 1e-6f))
					return 0;
				const Math::vec3 unit = heading / length;

				// The shooter is skipped inside Jolt (all of its body, wherever the ray starts).
				EntityFilter filter;
				if (ignoreIndex != 0)
					filter.ignore = Entity{static_cast<uint32_t>(ignoreIndex), static_cast<uint32_t>(ignoreGeneration)};

				Math::vec3 start(origin->x, origin->y, origin->z);
				float travelled = 0.0f;
				for (int attempt = 0; attempt <= MaxDeadHits; ++attempt)
				{
					const float remaining = maxDistance - travelled;
					EntityHit hit;
					if (!(remaining > 0.0f) || !physics->Raycast(start, unit, hit, remaining, filter))
						return 0;
					Entity alive;
					if (EntityContext::ResolveEntity(static_cast<int32_t>(hit.entity.index),
					                                 static_cast<int32_t>(hit.entity.generation), alive))
					{
						*outIndex = static_cast<int32_t>(hit.entity.index);
						*outGeneration = static_cast<int32_t>(hit.entity.generation);
						*outPoint = {hit.point.x, hit.point.y, hit.point.z};
						*outNormal = {hit.normal.x, hit.normal.y, hit.normal.z};
						*outDistance = travelled + hit.distance;
						return 1;
					}
					// A body whose entity was destroyed this frame (physics drops it next step): go on past it.
					travelled += hit.distance + SkipDistance;
					start = hit.point + unit * SkipDistance;
				}
				return 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// CharacterController
		// ======================================================================

		const CharacterController* ReadController(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Read<CharacterController>(entity);
		}

		CharacterController* WriteController(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Write<CharacterController>(entity);
		}

		/*********************************************************************
		 \brief  C# CharacterController.SimpleMove: walk at this velocity (m/s)
		         until told otherwise. The up part is ignored.
		 *********************************************************************/
		int32_t ManagedCharacterSimpleMove(int32_t index, int32_t generation, const ScriptVec3* velocity) noexcept
		{
			try
			{
				if (!velocity || !IsFinite(*velocity))
					return 0;
				CharacterController* controller = WriteController(index, generation);
				if (!controller)
					return 0;
				controller->SimpleMove(Math::vec3(velocity->x, velocity->y, velocity->z));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# CharacterController.Jump: jump this many metres high if it
		         is on the ground at the next step.
		 *********************************************************************/
		int32_t ManagedCharacterJump(int32_t index, int32_t generation, float height) noexcept
		{
			try
			{
				if (!std::isfinite(height))
					return 0;
				CharacterController* controller = WriteController(index, generation);
				if (!controller)
					return 0;
				controller->Jump(height);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedCharacterIsGrounded(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const CharacterController* controller = out ? ReadController(index, generation) : nullptr;
				if (!controller)
					return 0;
				*out = controller->m_isGrounded ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# CharacterController.Velocity: what it really moved at in the
		         last step, after walls (m/s).
		 *********************************************************************/
		int32_t ManagedCharacterGetVelocity(int32_t index, int32_t generation, ScriptVec3* out) noexcept
		{
			try
			{
				const CharacterController* controller = out ? ReadController(index, generation) : nullptr;
				if (!controller)
					return 0;
				*out = {controller->m_velocity.x, controller->m_velocity.y, controller->m_velocity.z};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <float CharacterController::*Member>
		int32_t GetSetting(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				const CharacterController* controller = out ? ReadController(index, generation) : nullptr;
				if (!controller)
					return 0;
				*out = controller->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Setter for one shape setting. Rejects NaN/infinity and values
		         below Min (whole number; float template arguments need C++20).
		 *********************************************************************/
		template <float CharacterController::*Member, int Min>
		int32_t SetSetting(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value) || *value < static_cast<float>(Min))
					return 0;
				CharacterController* controller = WriteController(index, generation);
				if (!controller)
					return 0;
				controller->*Member = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename Fn>
		void Add(const char* name, Fn* function)
		{
			mono_add_internal_call(name, reinterpret_cast<const void*>(function));
		}
	}

	void SetPhysics(ECSPhysicsSystem* system)
	{
		physics = system;
	}

	// ======================================================================
	// Physics events -> scripts' OnCollision* / OnTrigger*
	// ======================================================================

	namespace
	{
		using ContactEvent = EntityContext::ContactEvent;

		std::vector<ContactEvent> contactEvents; // since scripts last ran (main thread)
		// The listeners; a Subscription only holds a weak link to its bus, so
		// releasing these after the bus is gone is safe.
		Subscription onCollisionEnter, onCollisionExit, onTriggerEnter, onTriggerExit;
	}

	void SetPhysicsEvents(EventBuses* events)
	{
		onCollisionEnter = {};
		onCollisionExit = {};
		onTriggerEnter = {};
		onTriggerExit = {};
		contactEvents.clear();
		if (!events)
			return;
		onCollisionEnter = events->engine.Subscribe<PhysicsEvents::CollisionEnter>(
		    [](const PhysicsEvents::CollisionEnter& e)
		    {
			    ContactEvent event{ContactEvent::Kind::CollisionEnter, e.a, e.b};
			    event.point[0] = e.point.x;
			    event.point[1] = e.point.y;
			    event.point[2] = e.point.z;
			    event.normal[0] = e.normal.x;
			    event.normal[1] = e.normal.y;
			    event.normal[2] = e.normal.z;
			    event.impactSpeed = e.impactSpeed;
			    contactEvents.push_back(event);
		    });
		onCollisionExit = events->engine.Subscribe<PhysicsEvents::CollisionExit>(
		    [](const PhysicsEvents::CollisionExit& e)
		    { contactEvents.push_back({ContactEvent::Kind::CollisionExit, e.a, e.b}); });
		onTriggerEnter = events->engine.Subscribe<PhysicsEvents::TriggerEnter>(
		    [](const PhysicsEvents::TriggerEnter& e)
		    { contactEvents.push_back({ContactEvent::Kind::TriggerEnter, e.trigger, e.other}); });
		onTriggerExit = events->engine.Subscribe<PhysicsEvents::TriggerExit>(
		    [](const PhysicsEvents::TriggerExit& e)
		    { contactEvents.push_back({ContactEvent::Kind::TriggerExit, e.trigger, e.other}); });
	}

	namespace EntityContext
	{
		const std::vector<ContactEvent>& CollisionEvents()
		{
			return contactEvents;
		}

		void ClearCollisionEvents()
		{
			contactEvents.clear();
		}
	}

	void RegisterPhysicsBindings()
	{
		Add("Crucible.Physics::RaycastInternal", &ManagedPhysicsRaycast);

		Add("Crucible.CharacterController::SimpleMoveInternal", &ManagedCharacterSimpleMove);
		Add("Crucible.CharacterController::JumpInternal", &ManagedCharacterJump);
		Add("Crucible.CharacterController::IsGroundedInternal", &ManagedCharacterIsGrounded);
		Add("Crucible.CharacterController::GetVelocityInternal", &ManagedCharacterGetVelocity);
		Add("Crucible.CharacterController::GetHeightInternal", &GetSetting<&CharacterController::m_height>);
		Add("Crucible.CharacterController::SetHeightInternal", &SetSetting<&CharacterController::m_height, 0>);
		Add("Crucible.CharacterController::GetRadiusInternal", &GetSetting<&CharacterController::m_radius>);
		Add("Crucible.CharacterController::SetRadiusInternal", &SetSetting<&CharacterController::m_radius, 0>);
		Add("Crucible.CharacterController::GetSlopeLimitInternal", &GetSetting<&CharacterController::m_slopeLimit>);
		Add("Crucible.CharacterController::SetSlopeLimitInternal", &SetSetting<&CharacterController::m_slopeLimit, 0>);
		Add("Crucible.CharacterController::GetStepOffsetInternal", &GetSetting<&CharacterController::m_stepOffset>);
		Add("Crucible.CharacterController::SetStepOffsetInternal", &SetSetting<&CharacterController::m_stepOffset, 0>);
	}
}
#endif
