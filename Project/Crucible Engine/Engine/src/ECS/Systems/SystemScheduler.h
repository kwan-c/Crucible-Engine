/*************************************************************************
* \file:         SystemScheduler.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          25 September 2026
* \version       1.0
* \brief
*                Declares BuildSystemSchedule(): turns each registered
*                system's SystemDesc into an execution plan - per stage, a
*                dependency-respecting order split into batches of systems
*                that could theoretically run at the same time.
*
* \details
*                Pure planning code: no World, no Query, no threads. The
*                scheduler decides WHAT runs and WHEN; ECSSystemManager
*                executes the plan (serially for now), and a future job
*                system would decide HOW a batch runs across threads.
*
*                Per stage:
*                  1. RunAfter/RunBefore become graph edges (same stage), are
*                     already satisfied (consistent with stage order), or are
*                     errors (contradict stage order / self-dependency).
*                     A missing required dependency (RunAfter/RunBefore) is
*                     an error; a missing optional one (...IfPresent) is
*                     logged and ignored.
*                  2. Kahn's topological sort; among ready systems the lower
*                     priority value runs first, then registration order.
*                  3. A dependency cycle is reported as an error naming the
*                     systems in the cycle.
*                  4. The order is cut into contiguous batches: a new batch
*                     starts when the next system conflicts with, or depends
*                     on, a system already in the current batch. Flattening
*                     the batches gives back the exact serial order.
*
* \history
*    -v1.0       initial dependency DAG, topological order and batching
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <string>
#include <typeindex>
#include <vector>

#include "IECSSystems.h"

namespace Crucible
{
	/*************************************************************************
	 \brief  One registered system as the scheduler sees it.
	 *************************************************************************/
	struct SystemNode
	{
		SystemDesc      desc;   // resolved Stage()/Priority()/Describe()
		std::type_index type;   // identity used by RunAfter/RunBefore
		std::string     name;   // for error messages and LogSchedule()
	};

	/*************************************************************************
	 \brief  Systems that don't conflict or depend on each other, so they
			 could theoretically run at the same time.
	 \note   Indices are registration indices, in serial execution order.
	 *************************************************************************/
	struct ExecutionBatch
	{
		std::vector<std::size_t> systems;
	};

	/*************************************************************************
	 \brief  The batches of one stage, in execution order.
	 *************************************************************************/
	struct StageSchedule
	{
		SystemStage stage = SystemStage::Update;
		std::vector<ExecutionBatch> batches;
	};

	/*************************************************************************
	 \brief  The full execution plan for every registered system.
	 *************************************************************************/
	struct SystemSchedule
	{
		std::vector<StageSchedule> stages;     // non-empty stages only, in stage order
		std::vector<std::size_t>   serialOrder; // every system in execution order (flattened batches)
		std::vector<SystemDesc>    descs;       // resolved description per registration index
		std::vector<std::string>   names;       // display name per registration index
	};

	/*************************************************************************
	 \brief  Builds the execution plan for a set of systems.
	 \param[in] nodes One entry per system, indexed by registration order.
	 \return The schedule.
	 \note   Throws std::logic_error on a dependency cycle, a
			 self-dependency, a dependency that contradicts stage order, or
			 a required dependency on an unregistered system.
	 *************************************************************************/
	SystemSchedule BuildSystemSchedule(const std::vector<SystemNode>& nodes);

	/*************************************************************************
	 \brief  Display name of a stage, e.g. "PostUpdate".
	 *************************************************************************/
	const char* StageName(SystemStage stage) noexcept;
}
