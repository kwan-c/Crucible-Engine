/*************************************************************************
* \file:         PhysicsBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          3 November 2026
* \version       1.2
* \brief         Internal calls behind C# Physics.Raycast and
*                Physics.Explode (ManagedScripts/Runtime/Physics.cs),
*                RigidBody.AddExplosionForce and RigidBody.FreezeRotation
*                (RigidBody.cs), and the C# CharacterController class
*                (CharacterController.cs).
*
* \history
*    -v1.2       (Hugo Low Ren Hao) Physics.Explode, RigidBody.AddExplosionForce
*                and RigidBody.FreezeRotation, for dynamic enemies: knockback,
*                explosions and toppling over on death; Physics.Raycast with
*                a layer mask, Physics.SphereCast, Physics.CheckSphere and
*                CharacterController.Center, for the enemies' position checks
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
#include "ECS/Components/Components.h"   // RigidBody, RigidBodyConstraints
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
		// A C# layer mask (one bit per PhysicsLayer, Unity's int layerMask) as the
		// engine's LayerMask. Built with With() one layer at a time because
		// LayerMask deliberately has no "from raw bits" constructor; bits for
		// layers that don't exist are dropped.
		LayerMask MaskFromBits(uint32_t bits)
		{
			if (bits == 0xFFFFFFFFu)
				return LayerMask::All();   // C# LayerMask.All: means the same, so it skips corpses too
			LayerMask mask;
			for (uint32_t layer = 0; layer < static_cast<uint32_t>(kPhysicsLayerCount); ++layer)
				if ((bits >> layer) & 1u)
					mask = mask.With(static_cast<PhysicsLayer>(layer));
			return mask;
		}

		// The C# side's ignore entity and layer mask as an EntityFilter.
		EntityFilter MakeFilter(uint32_t layerMask, int32_t ignoreIndex, int32_t ignoreGeneration)
		{
			EntityFilter filter;
			filter.layers = MaskFromBits(layerMask);
			if (ignoreIndex != 0)
				filter.ignore = Entity{static_cast<uint32_t>(ignoreIndex), static_cast<uint32_t>(ignoreGeneration)};
			return filter;
		}

		int32_t ManagedPhysicsRaycastMasked(const ScriptVec3* origin, const ScriptVec3* direction, float maxDistance,
		                                    uint32_t layerMask, int32_t ignoreIndex, int32_t ignoreGeneration,
		                                    int32_t* outIndex, int32_t* outGeneration, ScriptVec3* outPoint,
		                                    ScriptVec3* outNormal, float* outDistance) noexcept;

		// Xavier's original: every layer. Forwards to the masked version below
		// (added by Hugo, 7 Nov 2026), which is his code with the mask added.
		int32_t ManagedPhysicsRaycast(const ScriptVec3* origin, const ScriptVec3* direction, float maxDistance,
		                              int32_t ignoreIndex, int32_t ignoreGeneration, int32_t* outIndex,
		                              int32_t* outGeneration, ScriptVec3* outPoint, ScriptVec3* outNormal,
		                              float* outDistance) noexcept
		{
			return ManagedPhysicsRaycastMasked(origin, direction, maxDistance, ~0u, ignoreIndex, ignoreGeneration,
			                                   outIndex, outGeneration, outPoint, outNormal, outDistance);
		}

		/*********************************************************************
		 \brief  C# Physics.Raycast with a layer mask: only bodies on those
		         layers are hit (Unity's layerMask argument).
		 *********************************************************************/
		int32_t ManagedPhysicsRaycastMasked(const ScriptVec3* origin, const ScriptVec3* direction, float maxDistance,
		                                    uint32_t layerMask, int32_t ignoreIndex, int32_t ignoreGeneration,
		                                    int32_t* outIndex, int32_t* outGeneration, ScriptVec3* outPoint,
		                                    ScriptVec3* outNormal, float* outDistance) noexcept
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
				const EntityFilter filter = MakeFilter(layerMask, ignoreIndex, ignoreGeneration);

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
		// Physics.SphereCast / Physics.CheckSphere (added by Hugo, 7 Nov 2026)
		// ======================================================================

		/*********************************************************************
		 \brief  C# Physics.SphereCast: a ball of `radius` swept from origin
		         along direction, up to maxDistance; the nearest body it hits
		         on the mask's layers. A body the ball already overlaps at
		         the start is hit at distance 0 (Jolt's rule; Unity skips
		         those - C# callers check Distance if they need Unity's).
		 \return 1 with the hit; 0 if nothing (or a body whose entity was
		         destroyed this frame) was hit, outside Play, or bad input.
		 *********************************************************************/
		int32_t ManagedPhysicsSphereCast(const ScriptVec3* origin, float radius, const ScriptVec3* direction,
		                                 float maxDistance, uint32_t layerMask, int32_t ignoreIndex,
		                                 int32_t ignoreGeneration, int32_t* outIndex, int32_t* outGeneration,
		                                 ScriptVec3* outPoint, ScriptVec3* outNormal, float* outDistance) noexcept
		{
			try
			{
				if (!origin || !direction || !outIndex || !outGeneration || !outPoint || !outNormal || !outDistance)
					return 0;
				*outIndex = 0;
				*outGeneration = 0;
				if (!physics || !EntityContext::InPlaySession() || !IsFinite(*origin) || !IsFinite(*direction) ||
				    !(maxDistance > 0.0f) || !(radius > 0.0f) || !std::isfinite(radius))
					return 0;
				const Math::vec3 heading(direction->x, direction->y, direction->z);
				const float length = std::sqrt(heading.x * heading.x + heading.y * heading.y + heading.z * heading.z);
				if (!(length > 1e-6f))
					return 0;

				EntityHit hit;
				if (!physics->SphereCast(Math::vec3(origin->x, origin->y, origin->z), radius, heading / length, hit,
				                         std::isfinite(maxDistance) ? maxDistance : 1.0e6f,
				                         MakeFilter(layerMask, ignoreIndex, ignoreGeneration)))
					return 0;

				// Unlike Raycast, no re-cast past a destroyed body: a ball can't be
				// restarted from a point the way a ray can. It is gone next step.
				Entity alive;
				if (!EntityContext::ResolveEntity(static_cast<int32_t>(hit.entity.index),
				                                  static_cast<int32_t>(hit.entity.generation), alive))
					return 0;
				*outIndex = static_cast<int32_t>(hit.entity.index);
				*outGeneration = static_cast<int32_t>(hit.entity.generation);
				*outPoint = {hit.point.x, hit.point.y, hit.point.z};
				*outNormal = {hit.normal.x, hit.normal.y, hit.normal.z};
				*outDistance = hit.distance;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Physics.CheckSphere (Unity's): does anything on the
		         mask's layers overlap a ball at center right now?
		 \return 1 yes; 0 no, outside Play, or bad input.
		 *********************************************************************/
		int32_t ManagedPhysicsCheckSphere(const ScriptVec3* center, float radius, uint32_t layerMask,
		                                  int32_t ignoreIndex, int32_t ignoreGeneration) noexcept
		{
			try
			{
				if (!center || !physics || !EntityContext::InPlaySession() || !IsFinite(*center) ||
				    !(radius > 0.0f) || !std::isfinite(radius))
					return 0;

				// Reused between calls (main thread only, like `physics`), so a
				// planner asking about dozens of spots doesn't allocate each time.
				thread_local std::vector<Entity> overlaps;
				overlaps.clear();
				physics->OverlapSphere(Math::vec3(center->x, center->y, center->z), radius, overlaps,
				                       MakeFilter(layerMask, ignoreIndex, ignoreGeneration));

				// Bodies of entities destroyed this frame are still in physics until its next step.
				for (const Entity& entity : overlaps)
				{
					Entity alive;
					if (EntityContext::ResolveEntity(static_cast<int32_t>(entity.index),
					                                 static_cast<int32_t>(entity.generation), alive))
						return 1;
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

		/*********************************************************************
		 \brief  C# CharacterController.Center (added by Hugo, 7 Nov 2026):
		         the capsule's offset from the entity's position, in metres
		         (not scaled). Read-only for now: enemies aim at it.
		 *********************************************************************/
		int32_t ManagedCharacterGetCenter(int32_t index, int32_t generation, ScriptVec3* out) noexcept
		{
			try
			{
				const CharacterController* controller = out ? ReadController(index, generation) : nullptr;
				if (!controller)
					return 0;
				*out = {controller->m_center.x, controller->m_center.y, controller->m_center.z};
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

		// ======================================================================
		// Explosions and rotation locks, for dynamic enemies (Hugo, v1.2).
		// Explode / AddExplosionForce are ECSPhysicsSystem's; both push as an
		// impulse - a one-off kick, the mode Explode already used.
		// ======================================================================

		/*********************************************************************
		 \brief  C# RigidBody.AddExplosionForce (Unity's arguments). Pushes one
		         dynamic body away from the blast, fading to nothing at the
		         radius; upwardsModifier throws it up.
		 \return 1 if it pushed the body; 0 if not (not dynamic, outside the
		         radius, outside Play, a bad number).
		 *********************************************************************/
		int32_t ManagedRigidBodyAddExplosionForce(int32_t index, int32_t generation, float force,
		                                          const ScriptVec3* position, float radius, float upwardsModifier) noexcept
		{
			try
			{
				if (!position || !physics || !EntityContext::InPlaySession() || !IsFinite(*position) ||
				    !std::isfinite(force) || !std::isfinite(radius) || radius < 0.0f || !std::isfinite(upwardsModifier))
					return 0;
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				return physics->AddExplosionForce(entity, force, Math::vec3(position->x, position->y, position->z),
				                                  radius, upwardsModifier, ForceMode::Impulse) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Physics.Explode: every dynamic body in the sphere is pushed
		         away (ECSPhysicsSystem::Explode).
		 \return How many entities the blast reached, characters and static
		         things included; 0 outside Play or for a bad number.
		 *********************************************************************/
		int32_t ManagedPhysicsExplode(const ScriptVec3* position, float radius, float force, float upwardsModifier) noexcept
		{
			try
			{
				if (!position || !physics || !EntityContext::InPlaySession() || !IsFinite(*position) ||
				    !(radius > 0.0f) || !std::isfinite(radius) || !std::isfinite(force) || !std::isfinite(upwardsModifier))
					return 0;
				Explosion explosion;
				explosion.position        = Math::vec3(position->x, position->y, position->z);
				explosion.radius          = radius;
				explosion.force           = force;
				explosion.upwardsModifier = upwardsModifier;
				explosion.mode            = ForceMode::Impulse;
				std::vector<Entity> reached;
				physics->Explode(explosion, reached);
				return static_cast<int32_t>(reached.size());
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# RigidBody.FreezeRotation (Unity's freezeRotation): all
		         three rotation locks at once. Physics rebuilds the body with
		         the new locks next frame, keeping its velocity.
		 *********************************************************************/
		int32_t ManagedRigidBodyGetFreezeRotation(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const RigidBody* body = EntityContext::GetWorld()->Read<RigidBody>(entity);
				if (!body)
					return 0;
				const RigidBodyConstraints& c = body->constraints;
				*out = (c.freezeRotationX && c.freezeRotationY && c.freezeRotationZ) ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedRigidBodySetFreezeRotation(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				RigidBody* body = EntityContext::GetWorld()->Write<RigidBody>(entity);
				if (!body)
					return 0;
				body->constraints.freezeRotationX = body->constraints.freezeRotationY = body->constraints.freezeRotationZ = value != 0;
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
		Add("Crucible.Physics::RaycastMaskedInternal", &ManagedPhysicsRaycastMasked);
		Add("Crucible.Physics::SphereCastInternal", &ManagedPhysicsSphereCast);
		Add("Crucible.Physics::CheckSphereInternal", &ManagedPhysicsCheckSphere);
		Add("Crucible.Physics::ExplodeInternal", &ManagedPhysicsExplode);
		Add("Crucible.RigidBody::AddExplosionForceInternal", &ManagedRigidBodyAddExplosionForce);
		Add("Crucible.RigidBody::GetFreezeRotationInternal", &ManagedRigidBodyGetFreezeRotation);
		Add("Crucible.RigidBody::SetFreezeRotationInternal", &ManagedRigidBodySetFreezeRotation);

		Add("Crucible.CharacterController::SimpleMoveInternal", &ManagedCharacterSimpleMove);
		Add("Crucible.CharacterController::JumpInternal", &ManagedCharacterJump);
		Add("Crucible.CharacterController::IsGroundedInternal", &ManagedCharacterIsGrounded);
		Add("Crucible.CharacterController::GetVelocityInternal", &ManagedCharacterGetVelocity);
		Add("Crucible.CharacterController::GetCenterInternal", &ManagedCharacterGetCenter);
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
