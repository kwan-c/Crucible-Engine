/*************************************************************************
* \file:         ColliderBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the C# Collider and MeshRenderer
*                classes (ManagedScripts/Runtime/Collider.cs,
*                MeshRenderer.cs).
*
* \details
*    Bindings only: Collider (Physics/PhysicsComponents.h, Hugo's) and
*    MeshRenderer (Components.h) are unchanged.
*
*    Collider: ECSPhysicsSystem compares every Collider with the body it
*    built each frame and rebuilds the body when a setting changed (keeping
*    a dynamic body's velocity), so a change made in OnUpdate takes effect
*    in that frame's physics step. Sizes are FULL sizes and are multiplied
*    by the Transform's scale, as in the Inspector.
*
*    MeshRenderer: RenderExtractSystem reads `visible` every frame.
*
*    Vectors cross as ScriptVec3, enums and bools as int.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Physics/PhysicsComponents.h"
#include "Physics/PhysicsLayers.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		// C# ColliderShape and PhysicsLayer (Collider.cs) pass these numbers.
		static_assert(static_cast<int>(ShapeType::Box) == 0 && static_cast<int>(ShapeType::Sphere) == 1 &&
		                  static_cast<int>(ShapeType::Capsule) == 2,
		              "ShapeType changed - update ColliderShape in ManagedScripts/Runtime/Collider.cs");
		static_assert(static_cast<int>(PhysicsLayer::NonMoving) == 0 && static_cast<int>(PhysicsLayer::Moving) == 1 &&
		                  static_cast<int>(PhysicsLayer::Player) == 2 && static_cast<int>(PhysicsLayer::Enemy) == 3 &&
		                  static_cast<int>(PhysicsLayer::Projectile) == 4 && static_cast<int>(PhysicsLayer::Count) == 5,
		              "PhysicsLayer changed - update PhysicsLayer in ManagedScripts/Runtime/Collider.cs");

		/*********************************************************************
		 \brief  The live entity's T for reading, or null.
		 *********************************************************************/
		template <typename T>
		const T* ReadComponent(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Read<T>(entity);
		}

		/*********************************************************************
		 \brief  The live entity's T for writing (marks it changed), or null.
		 *********************************************************************/
		template <typename T>
		T* WriteComponent(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Write<T>(entity);
		}

		bool Finite(const ScriptVec3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		// ======================================================================
		// Collider floats. Minimum: radius / height must be above 0; friction
		// and bounciness not negative.
		// ======================================================================

		template <float Collider::*Member>
		int32_t GetFloat(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				const Collider* collider = out ? ReadComponent<Collider>(index, generation) : nullptr;
				if (!collider)
					return 0;
				*out = collider->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <float Collider::*Member, bool Positive>
		int32_t SetFloat(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value) || *value < 0.0f || (Positive && *value == 0.0f))
					return 0;
				// Unchanged values are not written, so the body is not rebuilt for nothing.
				const Collider* current = ReadComponent<Collider>(index, generation);
				if (!current)
					return 0;
				if (current->*Member == *value)
					return 1;
				Collider* collider = WriteComponent<Collider>(index, generation);
				if (!collider)
					return 0;
				collider->*Member = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Collider vectors: size (full size, every axis above 0) and center
		// ======================================================================

		template <Math::vec3 Collider::*Member>
		int32_t GetVector(int32_t index, int32_t generation, ScriptVec3* out) noexcept
		{
			try
			{
				const Collider* collider = out ? ReadComponent<Collider>(index, generation) : nullptr;
				if (!collider)
					return 0;
				const Math::vec3& v = collider->*Member;
				*out = {v.x, v.y, v.z};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <Math::vec3 Collider::*Member, bool Positive>
		int32_t SetVector(int32_t index, int32_t generation, const ScriptVec3* value) noexcept
		{
			try
			{
				if (!value || !Finite(*value))
					return 0;
				if constexpr (Positive)
				{
					if (value->x <= 0.0f || value->y <= 0.0f || value->z <= 0.0f)
						return 0;
				}
				const Collider* current = ReadComponent<Collider>(index, generation);
				if (!current)
					return 0;
				const Math::vec3 v(value->x, value->y, value->z);
				if (current->*Member == v)
					return 1;
				Collider* collider = WriteComponent<Collider>(index, generation);
				if (!collider)
					return 0;
				collider->*Member = v;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Collider shape, layer, isTrigger
		// ======================================================================

		int32_t ManagedColliderGetShape(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const Collider* collider = out ? ReadComponent<Collider>(index, generation) : nullptr;
				if (!collider)
					return 0;
				*out = static_cast<int32_t>(collider->m_type);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedColliderSetShape(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				if (value < 0 || value > static_cast<int32_t>(ShapeType::Capsule))
					return 0;
				const Collider* current = ReadComponent<Collider>(index, generation);
				if (!current)
					return 0;
				if (static_cast<int32_t>(current->m_type) == value)
					return 1;
				Collider* collider = WriteComponent<Collider>(index, generation);
				if (!collider)
					return 0;
				collider->m_type = static_cast<ShapeType>(value);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedColliderGetLayer(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const Collider* collider = out ? ReadComponent<Collider>(index, generation) : nullptr;
				if (!collider)
					return 0;
				*out = static_cast<int32_t>(collider->m_layer);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedColliderSetLayer(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				if (value < 0 || value >= static_cast<int32_t>(PhysicsLayer::Count))
					return 0;
				const Collider* current = ReadComponent<Collider>(index, generation);
				if (!current)
					return 0;
				if (static_cast<int32_t>(current->m_layer) == value)
					return 1;
				Collider* collider = WriteComponent<Collider>(index, generation);
				if (!collider)
					return 0;
				collider->m_layer = static_cast<PhysicsLayer>(value);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedColliderGetIsTrigger(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const Collider* collider = out ? ReadComponent<Collider>(index, generation) : nullptr;
				if (!collider)
					return 0;
				*out = collider->m_isTrigger ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedColliderSetIsTrigger(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				const Collider* current = ReadComponent<Collider>(index, generation);
				if (!current)
					return 0;
				if (current->m_isTrigger == (value != 0))
					return 1;
				Collider* collider = WriteComponent<Collider>(index, generation);
				if (!collider)
					return 0;
				collider->m_isTrigger = value != 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Collider.WorldSize / WorldRadius / WorldHeight: the
		         collider as physics builds it, after the Transform's scale
		         (ScaledCollider - the same numbers debug draw shows).
		 *********************************************************************/
		int32_t ManagedColliderGetScaled(int32_t index, int32_t generation, ScriptVec3* outSize, float* outRadius,
		                                 float* outHeight) noexcept
		{
			try
			{
				if (!outSize || !outRadius || !outHeight)
					return 0;
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const World& world = *EntityContext::GetWorld();
				const Collider* collider = world.Read<Collider>(entity);
				const Transform* transform = world.Read<Transform>(entity);
				if (!collider || !transform)
					return 0;
				const Collider scaled = ScaledCollider(*collider, transform->scale);
				*outSize = {scaled.m_size.x, scaled.m_size.y, scaled.m_size.z};
				*outRadius = scaled.m_radius;
				*outHeight = scaled.m_height;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// MeshRenderer
		// ======================================================================

		int32_t ManagedMeshRendererGetVisible(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const MeshRenderer* renderer = out ? ReadComponent<MeshRenderer>(index, generation) : nullptr;
				if (!renderer)
					return 0;
				*out = renderer->visible ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedMeshRendererSetVisible(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				MeshRenderer* renderer = WriteComponent<MeshRenderer>(index, generation);
				if (!renderer)
					return 0;
				renderer->visible = value != 0;
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

	void RegisterColliderBindings()
	{
		Add("Crucible.Collider::GetShapeInternal", &ManagedColliderGetShape);
		Add("Crucible.Collider::SetShapeInternal", &ManagedColliderSetShape);
		Add("Crucible.Collider::GetSizeInternal", &GetVector<&Collider::m_size>);
		Add("Crucible.Collider::SetSizeInternal", &SetVector<&Collider::m_size, true>);
		Add("Crucible.Collider::GetCenterInternal", &GetVector<&Collider::m_center>);
		Add("Crucible.Collider::SetCenterInternal", &SetVector<&Collider::m_center, false>);
		Add("Crucible.Collider::GetRadiusInternal", &GetFloat<&Collider::m_radius>);
		Add("Crucible.Collider::SetRadiusInternal", &SetFloat<&Collider::m_radius, true>);
		Add("Crucible.Collider::GetHeightInternal", &GetFloat<&Collider::m_height>);
		Add("Crucible.Collider::SetHeightInternal", &SetFloat<&Collider::m_height, true>);
		Add("Crucible.Collider::GetFrictionInternal", &GetFloat<&Collider::m_friction>);
		Add("Crucible.Collider::SetFrictionInternal", &SetFloat<&Collider::m_friction, false>);
		Add("Crucible.Collider::GetBouncinessInternal", &GetFloat<&Collider::m_bounciness>);
		Add("Crucible.Collider::SetBouncinessInternal", &SetFloat<&Collider::m_bounciness, false>);
		Add("Crucible.Collider::GetLayerInternal", &ManagedColliderGetLayer);
		Add("Crucible.Collider::SetLayerInternal", &ManagedColliderSetLayer);
		Add("Crucible.Collider::GetIsTriggerInternal", &ManagedColliderGetIsTrigger);
		Add("Crucible.Collider::SetIsTriggerInternal", &ManagedColliderSetIsTrigger);
		Add("Crucible.Collider::GetScaledInternal", &ManagedColliderGetScaled);

		Add("Crucible.MeshRenderer::GetVisibleInternal", &ManagedMeshRendererGetVisible);
		Add("Crucible.MeshRenderer::SetVisibleInternal", &ManagedMeshRendererSetVisible);
	}
}
#endif
