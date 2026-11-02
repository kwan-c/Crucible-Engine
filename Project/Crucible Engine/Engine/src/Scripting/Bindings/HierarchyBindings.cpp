/*************************************************************************
* \file:         HierarchyBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the world-space and parent/child parts
*                of the C# Transform class (ManagedScripts/Runtime/Transform.cs).
*
* \details
*    Kept apart from ComponentBindings.cpp (local position / rotation /
*    scale, RigidBody). World values come from World::TryGetWorldMatrix,
*    which is worked out from the current local transforms - not from
*    WorldTransform, which is only refreshed in PreRender - so a script that
*    moved something earlier this frame reads the new value.
*
*    Setting a world value converts it into the parent's space with
*    World::TryGetParentFrame and writes the LOCAL Transform, so the parent
*    link (and any socket mount) is respected.
*
*    Parent / child calls wrap World's Unity-style hierarchy API
*    (TrySetParent, GetParent, ChildCount, GetChild). The script system is
*    Exclusive, so reparenting directly is allowed.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		World& TheWorld()
		{
			return *EntityContext::GetWorld(); // only after ResolveEntity succeeded
		}

		bool IsFinite(const ScriptVec3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		bool IsFinite(const ScriptQuat& q)
		{
			return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
		}

		// ======================================================================
		// World position / rotation
		// ======================================================================

		/*********************************************************************
		 \brief  C# Transform.WorldPosition (get).
		 *********************************************************************/
		int32_t ManagedTransformGetWorldPosition(int32_t index, int32_t generation, ScriptVec3* out) noexcept
		{
			try
			{
				Entity entity;
				Math::mat4 world;
				if (!out || !EntityContext::ResolveEntity(index, generation, entity) ||
				    !TheWorld().TryGetWorldMatrix(entity, world))
					return 0;
				*out = {world[3].x, world[3].y, world[3].z};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Transform.WorldPosition (set). Converts into the parent's
		         space and writes the local position.
		 *********************************************************************/
		int32_t ManagedTransformSetWorldPosition(int32_t index, int32_t generation, const ScriptVec3* value) noexcept
		{
			try
			{
				Entity entity;
				Math::mat4 parent;
				if (!value || !IsFinite(*value) || !EntityContext::ResolveEntity(index, generation, entity) ||
				    !TheWorld().TryGetParentFrame(entity, parent))
					return 0;
				const Math::vec4 local = glm::inverse(parent) * Math::vec4(value->x, value->y, value->z, 1.0f);
				if (!std::isfinite(local.x) || !std::isfinite(local.y) || !std::isfinite(local.z))
					return 0; // parent scaled to zero
				Transform* transform = TheWorld().Write<Transform>(entity);
				if (!transform)
					return 0;
				transform->positions = {local.x, local.y, local.z};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Transform.WorldRotation (get).
		 *********************************************************************/
		int32_t ManagedTransformGetWorldRotation(int32_t index, int32_t generation, ScriptQuat* out) noexcept
		{
			try
			{
				Entity entity;
				Math::mat4 world;
				Math::vec3 translation, scale;
				Math::quat rotation;
				if (!out || !EntityContext::ResolveEntity(index, generation, entity) ||
				    !TheWorld().TryGetWorldMatrix(entity, world) ||
				    !Detail::DecomposeTRS(world, translation, rotation, scale))
					return 0;
				*out = {rotation.x, rotation.y, rotation.z, rotation.w}; // copy by name: glm is (w, x, y, z)
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Transform.WorldRotation (set). Removes the parent's world
		         rotation and writes the local rotation.
		 *********************************************************************/
		int32_t ManagedTransformSetWorldRotation(int32_t index, int32_t generation, const ScriptQuat* value) noexcept
		{
			try
			{
				Entity entity;
				Math::mat4 parent;
				Math::vec3 translation, scale;
				Math::quat parentRotation;
				if (!value || !IsFinite(*value) || !EntityContext::ResolveEntity(index, generation, entity) ||
				    !TheWorld().TryGetParentFrame(entity, parent) ||
				    !Detail::DecomposeTRS(parent, translation, parentRotation, scale))
					return 0;
				const Math::quat world(value->w, value->x, value->y, value->z); // glm order: w, x, y, z
				if (glm::length(world) < 1e-6f)
					return 0;
				Transform* transform = TheWorld().Write<Transform>(entity);
				if (!transform)
					return 0;
				transform->rotation = glm::normalize(glm::inverse(parentRotation) * glm::normalize(world));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Parent / children
		// ======================================================================

		/*********************************************************************
		 \brief  C# Transform.Parent (get).
		 \return 1 with the parent, 0 for a root (or a dead entity).
		 *********************************************************************/
		int32_t ManagedTransformGetParent(int32_t index, int32_t generation, int32_t* outIndex,
		                                  int32_t* outGeneration) noexcept
		{
			try
			{
				Entity entity;
				if (!outIndex || !outGeneration || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const Entity parent = TheWorld().GetParent(entity);
				if (!parent.IsValid())
					return 0;
				*outIndex = static_cast<int32_t>(parent.index);
				*outGeneration = static_cast<int32_t>(parent.generation);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Transform.SetParent(parent, worldPositionStays). parentIndex
		         0 makes it a root. Logs why if the World refuses (a cycle, a
		         dead parent, a zero-scaled parent...).
		 *********************************************************************/
		int32_t ManagedTransformSetParent(int32_t index, int32_t generation, int32_t parentIndex,
		                                  int32_t parentGeneration, int32_t worldPositionStays) noexcept
		{
			try
			{
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				Entity parent = kNullEntity;
				if (parentIndex != 0 && !EntityContext::ResolveEntity(parentIndex, parentGeneration, parent))
					return 0;
				const ParentResult result = TheWorld().TrySetParent(entity, parent, worldPositionStays != 0);
				if (result != ParentResult::Ok)
				{
					CRU_USER_WARN("[Script] Transform.SetParent refused for entity {}: {}", entity.index, ToString(result));
					return 0;
				}
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Transform.ChildCount.
		 *********************************************************************/
		int32_t ManagedTransformGetChildCount(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				*out = static_cast<int32_t>(TheWorld().ChildCount(entity));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Transform.GetChild(i): the i-th child in Hierarchy order.
		 \return 0 if i is out of range.
		 *********************************************************************/
		int32_t ManagedTransformGetChild(int32_t index, int32_t generation, int32_t childIndex, int32_t* outIndex,
		                                 int32_t* outGeneration) noexcept
		{
			try
			{
				Entity entity;
				if (!outIndex || !outGeneration || childIndex < 0 ||
				    !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const Entity child = TheWorld().GetChild(entity, static_cast<uint32_t>(childIndex));
				if (!child.IsValid())
					return 0;
				*outIndex = static_cast<int32_t>(child.index);
				*outGeneration = static_cast<int32_t>(child.generation);
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

	void RegisterHierarchyBindings()
	{
		Add("Crucible.Transform::GetWorldPosition", &ManagedTransformGetWorldPosition);
		Add("Crucible.Transform::SetWorldPosition", &ManagedTransformSetWorldPosition);
		Add("Crucible.Transform::GetWorldRotation", &ManagedTransformGetWorldRotation);
		Add("Crucible.Transform::SetWorldRotation", &ManagedTransformSetWorldRotation);
		Add("Crucible.Transform::GetParentInternal", &ManagedTransformGetParent);
		Add("Crucible.Transform::SetParentInternal", &ManagedTransformSetParent);
		Add("Crucible.Transform::GetChildCountInternal", &ManagedTransformGetChildCount);
		Add("Crucible.Transform::GetChildInternal", &ManagedTransformGetChild);
	}
}
#endif
