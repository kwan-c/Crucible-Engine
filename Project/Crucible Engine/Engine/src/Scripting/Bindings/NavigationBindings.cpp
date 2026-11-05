/*************************************************************************
* \file:         NavigationBindings.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.0
* \brief         Internal calls behind the C# FlowFieldAgent and
*                FlowFieldSurface classes (ManagedScripts/Runtime/).
*
* \details
*    FlowFieldAgent: its settings, the four orders (FollowField, MoveTo,
*    Arrive, Stop) and how it's going (HasArrived, RemainingDistance,
*    Velocity). The orders only set fields on the component, exactly as the
*    state graph's nodes do; ECSNavigationSystem carries them out in its
*    Update (the same stage as scripts, so this frame or the next).
*
*    FlowFieldSurface: only the two settings read on every field rebuild
*    (flood radius, rebuild distance). The rest - cell size, heights,
*    padding, wall cost - are read when the grid is baked at Play, so
*    changing them from a script would do nothing until the next Play.
*
*    Same shape as PlayerControllerBindings.cpp: 1 = done, 0 = refused (dead
*    entity, no such component, a bad value); never throws into C#.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "Navigation/NavigationComponents.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;   // ScriptVec3: how C# passes a Vector3

		// Read does not mark the component changed; Write does. Getters use Read.
		template <typename C>
		const C* ReadComponent(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Read<C>(entity);
		}

		template <typename C>
		C* WriteComponent(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Write<C>(entity);
		}

		// ======================================================================
		// Floats, on either component: finite and not negative (every one is a
		// speed, a distance or a weight, and a negative one breaks the maths)
		// ======================================================================

		template <typename C, float C::*Member>
		int32_t GetFloat(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				const C* component = out ? ReadComponent<C>(index, generation) : nullptr;
				if (!component)
					return 0;
				*out = component->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename C, float C::*Member>
		int32_t SetFloat(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value) || *value < 0.0f)
					return 0;
				C* component = WriteComponent<C>(index, generation);
				if (!component)
					return 0;
				component->*Member = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Agent orders: the same calls the state graph's nodes make
		// ======================================================================

		int32_t ManagedAgentFollowField(int32_t index, int32_t generation) noexcept
		{
			try
			{
				FlowFieldAgent* agent = WriteComponent<FlowFieldAgent>(index, generation);
				if (!agent)
					return 0;
				agent->FollowField();
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedAgentMoveTo(int32_t index, int32_t generation, const ScriptVec3* point) noexcept
		{
			try
			{
				// A NaN destination would turn into a NaN velocity, then a NaN position.
				if (!point || !std::isfinite(point->x) || !std::isfinite(point->y) || !std::isfinite(point->z))
					return 0;
				FlowFieldAgent* agent = WriteComponent<FlowFieldAgent>(index, generation);
				if (!agent)
					return 0;
				agent->MoveTo(Math::vec3(point->x, point->y, point->z));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// targetIndex 0 = C# passed null: the agent is told to Arrive at nothing,
		// so it stands still - the same as a target that has died.
		int32_t ManagedAgentArrive(int32_t index, int32_t generation, int32_t targetIndex, int32_t targetGeneration) noexcept
		{
			try
			{
				Entity target = kNullEntity;
				if (targetIndex != 0 && !EntityContext::ResolveEntity(targetIndex, targetGeneration, target))
					return 0;
				FlowFieldAgent* agent = WriteComponent<FlowFieldAgent>(index, generation);
				if (!agent)
					return 0;
				agent->Arrive(target);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedAgentStop(int32_t index, int32_t generation) noexcept
		{
			try
			{
				FlowFieldAgent* agent = WriteComponent<FlowFieldAgent>(index, generation);
				if (!agent)
					return 0;
				agent->Stop();
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Agent progress: written by ECSNavigationSystem every frame
		// ======================================================================

		int32_t ManagedAgentHasArrived(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const FlowFieldAgent* agent = out ? ReadComponent<FlowFieldAgent>(index, generation) : nullptr;
				if (!agent)
					return 0;
				*out = agent->HasArrived() ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedAgentGetVelocity(int32_t index, int32_t generation, ScriptVec3* out) noexcept
		{
			try
			{
				const FlowFieldAgent* agent = out ? ReadComponent<FlowFieldAgent>(index, generation) : nullptr;
				if (!agent)
					return 0;
				out->x = agent->m_velocity.x;
				out->y = agent->m_velocity.y;
				out->z = agent->m_velocity.z;
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

	void RegisterNavigationBindings()
	{
		// FlowFieldAgent settings
		Add("Crucible.FlowFieldAgent::GetSpeedInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_speed>);
		Add("Crucible.FlowFieldAgent::SetSpeedInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_speed>);
		Add("Crucible.FlowFieldAgent::GetAccelerationInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_acceleration>);
		Add("Crucible.FlowFieldAgent::SetAccelerationInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_acceleration>);
		Add("Crucible.FlowFieldAgent::GetAngularSpeedInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_angularSpeed>);
		Add("Crucible.FlowFieldAgent::SetAngularSpeedInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_angularSpeed>);
		Add("Crucible.FlowFieldAgent::GetBaseOffsetInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_baseOffset>);
		Add("Crucible.FlowFieldAgent::SetBaseOffsetInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_baseOffset>);
		Add("Crucible.FlowFieldAgent::GetStoppingDistanceInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_stoppingDistance>);
		Add("Crucible.FlowFieldAgent::SetStoppingDistanceInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_stoppingDistance>);
		Add("Crucible.FlowFieldAgent::GetSlowingDistanceInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_slowingDistance>);
		Add("Crucible.FlowFieldAgent::SetSlowingDistanceInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_slowingDistance>);
		Add("Crucible.FlowFieldAgent::GetNeighbourRadiusInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_neighbourRadius>);
		Add("Crucible.FlowFieldAgent::SetNeighbourRadiusInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_neighbourRadius>);
		Add("Crucible.FlowFieldAgent::GetSeparationRadiusInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_separationRadius>);
		Add("Crucible.FlowFieldAgent::SetSeparationRadiusInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_separationRadius>);
		Add("Crucible.FlowFieldAgent::GetSeparationWeightInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_separationWeight>);
		Add("Crucible.FlowFieldAgent::SetSeparationWeightInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_separationWeight>);
		Add("Crucible.FlowFieldAgent::GetAlignmentWeightInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_alignmentWeight>);
		Add("Crucible.FlowFieldAgent::SetAlignmentWeightInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_alignmentWeight>);
		Add("Crucible.FlowFieldAgent::GetRadiusInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_radius>);
		Add("Crucible.FlowFieldAgent::SetRadiusInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_radius>);
		Add("Crucible.FlowFieldAgent::GetAvoidanceDistanceInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_avoidanceDistance>);
		Add("Crucible.FlowFieldAgent::SetAvoidanceDistanceInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_avoidanceDistance>);
		Add("Crucible.FlowFieldAgent::GetAvoidanceWeightInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_avoidanceWeight>);
		Add("Crucible.FlowFieldAgent::SetAvoidanceWeightInternal", &SetFloat<FlowFieldAgent, &FlowFieldAgent::m_avoidanceWeight>);

		// FlowFieldAgent orders and progress
		Add("Crucible.FlowFieldAgent::FollowFieldInternal", &ManagedAgentFollowField);
		Add("Crucible.FlowFieldAgent::MoveToInternal", &ManagedAgentMoveTo);
		Add("Crucible.FlowFieldAgent::ArriveInternal", &ManagedAgentArrive);
		Add("Crucible.FlowFieldAgent::StopInternal", &ManagedAgentStop);
		Add("Crucible.FlowFieldAgent::HasArrivedInternal", &ManagedAgentHasArrived);
		Add("Crucible.FlowFieldAgent::GetRemainingDistanceInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_remainingDistance>);
		Add("Crucible.FlowFieldAgent::GetVelocityInternal", &ManagedAgentGetVelocity);

		// FlowFieldSurface: the settings read on every field rebuild
		Add("Crucible.FlowFieldSurface::GetFloodRadiusInternal", &GetFloat<FlowFieldSurface, &FlowFieldSurface::m_floodRadius>);
		Add("Crucible.FlowFieldSurface::SetFloodRadiusInternal", &SetFloat<FlowFieldSurface, &FlowFieldSurface::m_floodRadius>);
		Add("Crucible.FlowFieldSurface::GetRebuildDistanceInternal", &GetFloat<FlowFieldSurface, &FlowFieldSurface::m_rebuildDistance>);
		Add("Crucible.FlowFieldSurface::SetRebuildDistanceInternal", &SetFloat<FlowFieldSurface, &FlowFieldSurface::m_rebuildDistance>);
	}
}
#endif
