/*************************************************************************
* \file:         TransformBindings.cpp
* \author:       <author>
* \par:          Email: <email>
* \date          30 October 2026
* \version       1.0
* \brief         Internal calls behind the C# Transform class
*                (ManagedScripts/Runtime/Transform.cs).
*
* \details
*    C# passes `out Vector3` / `ref Quaternion` as a pointer to a
*    ScriptVec3 / ScriptQuat (ScriptInterop.h). Getters write through the
*    pointer; setters read from it. Return 1 on success, 0 on failure.
*
*    Every function follows the same shape (see the Position pair):
*      1. check the pointer from C# is not null
*      2. ResolveEntity: in a Play session, and the entity is still alive
*      3. Read<Transform> (getters) or Write<Transform> (setters)
*      4. copy the values, return 1
*    Values are LOCAL space (relative to the parent), like the Inspector.
*
*    TODO: Scale and Rotation are still placeholders that return 0.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Systems/PlayMode.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		// Set by SetWorld() from Core (main thread). thread_local, so a call that
		// arrives on any other thread (e.g. a GC finalizer) sees nullptr and fails.
		thread_local World* world = nullptr;
		thread_local PlayMode* play = nullptr;

		/*********************************************************************
		 \brief  Turns the (index, generation) a script passed in into a live
		         Entity. Fails outside a Play session, before SetWorld(), or if
		         the entity was destroyed (its generation no longer matches,
		         e.g. after a scene reload).
		 *********************************************************************/
		bool ResolveEntity(int32_t index, int32_t generation, Entity& entity)
		{
			if (!world || !play || !play->InSession())
				return false;
			entity = Entity{static_cast<uint32_t>(index), static_cast<uint32_t>(generation)};
			return world->IsAlive(entity);
		}

		/*********************************************************************
		 \brief  true if no component is NaN or infinity. Setters reject those so
		         a script bug cannot corrupt the Transform.
		 *********************************************************************/
		bool IsFinite(const ScriptVec3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		bool IsFinite(const ScriptQuat& q)
		{
			return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
		}

		// ======================================================================
		// Position
		// ======================================================================

		/*********************************************************************
		 \brief  C# Transform.Position (get). Writes the entity's local position
		         into *out.
		 \return 1 on success, 0 if the entity is gone or has no Transform.
		 *********************************************************************/
		int32_t ManagedTransformGetPosition(int32_t index, int32_t generation, ScriptVec3* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !ResolveEntity(index, generation, entity))
					return 0;
				const Transform* transform = world->Read<Transform>(entity); // Read: does not mark it changed
				if (!transform)
					return 0;
				out->x = transform->positions.x;
				out->y = transform->positions.y;
				out->z = transform->positions.z;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Transform.Position (set). Copies *value into the entity's local
		         position.
		 \return 1 on success, 0 if the entity is gone, has no Transform, or the
		         value contains NaN/infinity.
		 *********************************************************************/
		int32_t ManagedTransformSetPosition(int32_t index, int32_t generation,
		                                    const ScriptVec3* value) noexcept
		{
			try
			{
				Entity entity;
				if (!value || !IsFinite(*value) || !ResolveEntity(index, generation, entity))
					return 0;
				Transform* transform = world->Write<Transform>(entity); // Write: marks it changed so
				if (!transform)                                         // the hierarchy recomputes it
					return 0;
				transform->positions = {value->x, value->y, value->z};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Scale
		// ======================================================================
		int32_t ManagedTransformGetScale(int32_t index, int32_t generation, ScriptVec3* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !ResolveEntity(index, generation, entity))
					return 0;
				const Transform* transform = world->Read<Transform>(entity); // Read: does not mark it changed
				if (!transform)
					return 0;
				out->x = transform->scale.x;
				out->y = transform->scale.y;
				out->z = transform->scale.z;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}
		int32_t ManagedTransformSetScale(int32_t index, int32_t generation, const ScriptVec3* value) noexcept
		{
			try
			{
				Entity entity;
				if (!value || !IsFinite(*value) || !ResolveEntity(index, generation, entity))
					return 0;
				Transform* transform = world->Write<Transform>(entity); // Write: marks it changed so
				if (!transform)                                         // the hierarchy recomputes it
					return 0;
				transform->scale = { value->x, value->y, value->z };
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Rotation
		// ======================================================================
		int32_t ManagedTransformGetRotation(int32_t index, int32_t generation, ScriptQuat* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !ResolveEntity(index, generation, entity))
					return 0;
				const Transform* transform = world->Read<Transform>(entity); // Read: does not mark it changed
				if (!transform)
					return 0;
				out->x = transform->rotation.x;
				out->y = transform->rotation.y;
				out->z = transform->rotation.z;
				out->w = transform->rotation.w;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}
		int32_t ManagedTransformSetRotation(int32_t index, int32_t generation,
		                                    const ScriptQuat* value) noexcept
		{
			try
			{
				Entity entity;
				if (!value || !IsFinite(*value) || !ResolveEntity(index, generation, entity))
					return 0;
				Transform* transform = world->Write<Transform>(entity); // Write: marks it changed so
				if (!transform)                                         // the hierarchy recomputes it
					return 0;
				const Math::quat rotation(value->w, value->x, value->y, value->z);   // glm order: w, x, y, z
				if (glm::length(rotation) < 1e-6f)                                  // all-zero is not a rotation
					return 0;
				transform->rotation = glm::normalize(rotation);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// -------------------------------------------
		// RigidBody Members
		// -------------------------------------------

		int32_t ManagedRigidBodyGetVelocity(int32_t index, int32_t generation, ScriptVec3* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !ResolveEntity(index, generation, entity))
					return 0;
				const RigidBody* rigidbody = world->Read<RigidBody>(entity); // Read: does not mark it changed
				if (!rigidbody)
					return 0;
				out->x = rigidbody->velocity.x;
				out->y = rigidbody->velocity.y;
				out->z = rigidbody->velocity.z;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}
		int32_t ManagedRigidBodySetVelocity(int32_t index, int32_t generation, const ScriptVec3* value) noexcept
		{
			try
			{
				Entity entity;
				if (!value || !IsFinite(*value) || !ResolveEntity(index, generation, entity))
					return 0;
				RigidBody* rigidbody = world->Write<RigidBody>(entity); // Write: marks it changed so
				if (!rigidbody)                                         // the hierarchy recomputes it
					return 0;
				rigidbody->velocity = { value->x, value->y, value->z };
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedRigidbodyGetActiveGravity(int32_t index, int32_t generation, int* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !ResolveEntity(index, generation, entity))
					return 0;
				const RigidBody* rigidbody = world->Read<RigidBody>(entity);
				if (!rigidbody)
					return 0;
				*out = rigidbody->useGravity;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedRigidbodySetActiveGravity(int32_t index, int32_t generation, const int* value) noexcept
		{
			try
			{
				Entity entity;
				if (!value || !ResolveEntity(index, generation, entity))
					return 0;
				RigidBody* rigidbody = world->Write<RigidBody>(entity);
				if (!rigidbody)
					return 0;
				rigidbody->useGravity = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}
	}

	void SetWorld(World* newWorld, PlayMode* newPlay)
	{
		world = newWorld;
		play = newPlay;
	}

	void RegisterTransformBindings()
	{
		// Names must match the C# class and method names in Transform.cs exactly.
		
		// Transform Component stuff...
		mono_add_internal_call("Crucible.Transform::GetPosition",
		                       reinterpret_cast<const void*>(ManagedTransformGetPosition));
		mono_add_internal_call("Crucible.Transform::SetPosition",
		                       reinterpret_cast<const void*>(ManagedTransformSetPosition));
		mono_add_internal_call("Crucible.Transform::GetScale",
		                       reinterpret_cast<const void*>(ManagedTransformGetScale));
		mono_add_internal_call("Crucible.Transform::SetScale",
		                       reinterpret_cast<const void*>(ManagedTransformSetScale));
		mono_add_internal_call("Crucible.Transform::GetRotation",
		                       reinterpret_cast<const void*>(ManagedTransformGetRotation));
		mono_add_internal_call("Crucible.Transform::SetRotation",
		                       reinterpret_cast<const void*>(ManagedTransformSetRotation));

		// RigidBody Component stuff...
		mono_add_internal_call("Crucible.RigidBody::GetVelocity",
							   reinterpret_cast<const void*>(ManagedRigidBodyGetVelocity));
		mono_add_internal_call("Crucible.RigidBody::SetVelocity",
							   reinterpret_cast<const void*>(ManagedRigidBodySetVelocity));
		mono_add_internal_call("Crucible.RigidBody::GetActiveGravity",
							   reinterpret_cast<const void*>(ManagedRigidbodyGetActiveGravity));
		mono_add_internal_call("Crucible.RigidBody::SetActiveGravity",
							   reinterpret_cast<const void*>(ManagedRigidbodySetActiveGravity));
	}
}
#endif
