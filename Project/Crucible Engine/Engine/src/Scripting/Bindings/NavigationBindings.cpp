/*************************************************************************
* \file:         NavigationBindings.cpp
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.3
* \brief         Internal calls behind the C# FlowFieldAgent,
*                FlowFieldSurface and Navigation classes
*                (ManagedScripts/Runtime/).
*
* \details
*    FlowFieldAgent: its settings, the four orders (FollowField, MoveTo,
*    Arrive, Stop) and how it's going (HasArrived, RemainingDistance,
*    Velocity). The orders only set fields on the component, exactly as the
*    state graph's nodes do; ECSNavigationSystem carries them out in its
*    Update (the same stage as scripts, so this frame or the next).
*
*    FlowFieldSurface: only the settings that apply live: flood radius and
*    rebuild distance (read on every field rebuild) and the dynamic update
*    interval (v1.2). The rest - cell size, heights, padding, wall cost -
*    are read when the grid is baked at Play, so changing them from a
*    script would do nothing until the next Play.
*
*    Navigation: IsWalkable and IsPathClear on the baked grid, through
*    ECSNavigationSystem::Active(); Rebake (RequestBake) for a script that
*    built new ground mid-Play, and IsBaked to know when it is ready.
*    v1.3: AddGoal / RemoveGoal (extra flow-field goals, e.g. the escort
*    payload; ECSNavigationSystem::AddGoal).
*
*    Same shape as PlayerControllerBindings.cpp: 1 = done, 0 = refused (dead
*    entity, no such component, a bad value); never throws into C#.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/ECSNavigationSystem.h"   // Active(), for the Navigation queries
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
		// FlowFieldAgent's avoidance priority: an int, 0-99 like Unity's
		// (Unity clamps it to that range too, rather than refusing)
		// ======================================================================

		int32_t GetAvoidancePriority(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const FlowFieldAgent* agent = out ? ReadComponent<FlowFieldAgent>(index, generation) : nullptr;
				if (!agent)
					return 0;
				*out = agent->m_avoidancePriority;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t SetAvoidancePriority(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				FlowFieldAgent* agent = WriteComponent<FlowFieldAgent>(index, generation);
				if (!agent)
					return 0;
				agent->m_avoidancePriority = value < 0 ? 0 : (value > 99 ? 99 : value);
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

		// stoppingDistance / slowingDistance: negative = the agent's own setting (C#'s default).
		int32_t ManagedAgentMoveTo(int32_t index, int32_t generation, const ScriptVec3* point, float stoppingDistance,
		                           float slowingDistance) noexcept
		{
			try
			{
				// A NaN destination would turn into a NaN velocity, then a NaN position.
				if (!point || !std::isfinite(point->x) || !std::isfinite(point->y) || !std::isfinite(point->z)
				    || !std::isfinite(stoppingDistance) || !std::isfinite(slowingDistance))
					return 0;
				FlowFieldAgent* agent = WriteComponent<FlowFieldAgent>(index, generation);
				if (!agent)
					return 0;
				agent->MoveTo(Math::vec3(point->x, point->y, point->z), stoppingDistance, slowingDistance);
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

		// targetIndex 0 = C# passed null: back to facing the way it walks.
		int32_t ManagedAgentFaceTarget(int32_t index, int32_t generation, int32_t targetIndex, int32_t targetGeneration) noexcept
		{
			try
			{
				Entity target = kNullEntity;
				if (targetIndex != 0 && !EntityContext::ResolveEntity(targetIndex, targetGeneration, target))
					return 0;
				FlowFieldAgent* agent = WriteComponent<FlowFieldAgent>(index, generation);
				if (!agent)
					return 0;
				agent->FaceTarget(target);
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

		// ======================================================================
		// Navigation (C# static class): questions about the baked grid. No
		// navigation system or no bake yet answers "no" - never walkable, never
		// clear - so a script asking too early plays safe instead of crashing.
		// ======================================================================

		bool IsFinite(const ScriptVec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		int32_t ManagedNavigationIsWalkable(const ScriptVec3* position) noexcept
		{
			try
			{
				const ECSNavigationSystem* navigation = ECSNavigationSystem::Active();
				if (!navigation || !position || !IsFinite(*position))
					return 0;
				return navigation->IsWalkable(Math::vec3(position->x, position->y, position->z)) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		// Navigation.Rebake (Jean David, 10 Nov 2026): bakes again once physics has
		// built the new bodies. 0 with no navigation system or outside Play.
		int32_t ManagedNavigationRebake() noexcept
		{
			try
			{
				ECSNavigationSystem* navigation = ECSNavigationSystem::Active();
				if (!navigation || !EntityContext::InPlaySession())
					return 0;
				navigation->RequestBake();
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedNavigationGetBakeCount() noexcept
		{
			try
			{
				const ECSNavigationSystem* navigation = ECSNavigationSystem::Active();
				return navigation ? static_cast<int32_t>(navigation->BakeCount()) : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		// Navigation.AddGoal / RemoveGoal (Jean David, 28 Nov 2026): the flow field also
		// leads to this entity while it is alive. 0 with no navigation, outside Play,
		// or a dead entity.
		int32_t ManagedNavigationAddGoal(int32_t index, int32_t generation) noexcept
		{
			try
			{
				ECSNavigationSystem* navigation = ECSNavigationSystem::Active();
				Entity entity;
				if (!navigation || !EntityContext::InPlaySession() || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				navigation->AddGoal(entity);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedNavigationRemoveGoal(int32_t index, int32_t generation) noexcept
		{
			try
			{
				ECSNavigationSystem* navigation = ECSNavigationSystem::Active();
				if (!navigation)
					return 0;
				navigation->RemoveGoal(Entity(static_cast<uint32_t>(index), static_cast<uint32_t>(generation)));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedNavigationIsBaked() noexcept
		{
			try
			{
				const ECSNavigationSystem* navigation = ECSNavigationSystem::Active();
				return navigation && navigation->IsBaked() ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedNavigationIsPathClear(const ScriptVec3* from, const ScriptVec3* to) noexcept
		{
			try
			{
				const ECSNavigationSystem* navigation = ECSNavigationSystem::Active();
				if (!navigation || !from || !to || !IsFinite(*from) || !IsFinite(*to))
					return 0;
				return navigation->IsPathClear(Math::vec3(from->x, from->y, from->z), Math::vec3(to->x, to->y, to->z)) ? 1 : 0;
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
		Add("Crucible.FlowFieldAgent::GetAvoidancePriorityInternal", &GetAvoidancePriority);
		Add("Crucible.FlowFieldAgent::SetAvoidancePriorityInternal", &SetAvoidancePriority);
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
		Add("Crucible.FlowFieldAgent::FaceTargetInternal", &ManagedAgentFaceTarget);
		Add("Crucible.FlowFieldAgent::HasArrivedInternal", &ManagedAgentHasArrived);
		Add("Crucible.FlowFieldAgent::GetRemainingDistanceInternal", &GetFloat<FlowFieldAgent, &FlowFieldAgent::m_remainingDistance>);
		Add("Crucible.FlowFieldAgent::GetVelocityInternal", &ManagedAgentGetVelocity);

		// FlowFieldSurface: the settings that apply live
		Add("Crucible.FlowFieldSurface::GetFloodRadiusInternal", &GetFloat<FlowFieldSurface, &FlowFieldSurface::m_floodRadius>);
		Add("Crucible.FlowFieldSurface::SetFloodRadiusInternal", &SetFloat<FlowFieldSurface, &FlowFieldSurface::m_floodRadius>);
		Add("Crucible.FlowFieldSurface::GetRebuildDistanceInternal", &GetFloat<FlowFieldSurface, &FlowFieldSurface::m_rebuildDistance>);
		Add("Crucible.FlowFieldSurface::SetRebuildDistanceInternal", &SetFloat<FlowFieldSurface, &FlowFieldSurface::m_rebuildDistance>);
		Add("Crucible.FlowFieldSurface::GetDynamicUpdateIntervalInternal", &GetFloat<FlowFieldSurface, &FlowFieldSurface::m_dynamicUpdateInterval>);
		Add("Crucible.FlowFieldSurface::SetDynamicUpdateIntervalInternal", &SetFloat<FlowFieldSurface, &FlowFieldSurface::m_dynamicUpdateInterval>);

		// Navigation: grid queries
		Add("Crucible.Navigation::IsWalkableInternal", &ManagedNavigationIsWalkable);
		Add("Crucible.Navigation::IsPathClearInternal", &ManagedNavigationIsPathClear);
		Add("Crucible.Navigation::RebakeInternal", &ManagedNavigationRebake);
		Add("Crucible.Navigation::IsBakedInternal", &ManagedNavigationIsBaked);
		Add("Crucible.Navigation::GetBakeCountInternal", &ManagedNavigationGetBakeCount);
		Add("Crucible.Navigation::AddGoalInternal", &ManagedNavigationAddGoal);
		Add("Crucible.Navigation::RemoveGoalInternal", &ManagedNavigationRemoveGoal);
	}
}
#endif
