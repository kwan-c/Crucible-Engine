/*************************************************************************
* \file:         SystemScheduler.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          25 September 2026
* \version       1.0
* \brief
*                Implementation of BuildSystemSchedule(). See
*                SystemScheduler.h for the rules it applies.
*
* \history
*    -v1.0       initial dependency DAG, topological order and batching
*    -v1.1       required dependencies fail the schedule when missing;
*                optional ones are logged and ignored
*
**************************************************************************/

#include "SystemScheduler.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <Diagnostics/Logger.h>

namespace Crucible
{
	namespace
	{
		// Directed graph over registration indices: edge u -> v means "u runs before v".
		struct DependencyGraph
		{
			std::vector<std::vector<std::size_t>> successors;
			std::vector<std::vector<std::size_t>> predecessors;

			explicit DependencyGraph(std::size_t count) : successors(count), predecessors(count) {}

			void AddEdge(std::size_t before, std::size_t after)
			{
				// Two systems may declare the same constraint from both sides.
				if (std::find(successors[before].begin(), successors[before].end(), after) != successors[before].end()) return;
				successors[before].push_back(after);
				predecessors[after].push_back(before);
			}

			bool HasEdge(std::size_t before, std::size_t after) const
			{
				return std::find(successors[before].begin(), successors[before].end(), after) != successors[before].end();
			}
		};

		// Adds one declared dependency, or rejects it if it can never be satisfied.
		void ResolveDependency(const std::vector<SystemNode>& nodes, std::size_t self, std::size_t other,
			bool selfRunsAfterOther, DependencyGraph& graph)
		{
			const char* relation = selfRunsAfterOther ? "RunAfter" : "RunBefore";

			if (other == self)
			{
				throw std::logic_error("ECS scheduler: system '" + nodes[self].name + "' declares " + relation + " on itself");
			}

			const SystemStage selfStage = nodes[self].desc.stage;
			const SystemStage otherStage = nodes[other].desc.stage;

			// Same stage: a real ordering edge in this stage's graph.
			if (selfStage == otherStage)
			{
				if (selfRunsAfterOther) graph.AddEdge(other, self);
				else graph.AddEdge(self, other);
				return;
			}

			// Different stages: stage order already decides - fine if it agrees, an error if it contradicts.
			const bool otherStageIsEarlier = otherStage < selfStage;
			if (otherStageIsEarlier == selfRunsAfterOther) return;

			throw std::logic_error("ECS scheduler: system '" + nodes[self].name + "' (stage " + StageName(selfStage) +
				") declares " + relation + " '" + nodes[other].name + "' (stage " + StageName(otherStage) +
				"), which contradicts stage order");
		}

		// Walks leftover predecessors after a failed topological sort to name one actual cycle.
		std::string DescribeCycle(const std::vector<SystemNode>& nodes, const DependencyGraph& graph,
			const std::vector<std::size_t>& remainingInDegree, std::size_t start)
		{
			// Every leftover node has at least one leftover predecessor, so this walk must revisit a node.
			std::vector<std::size_t> path;
			std::size_t current = start;
			while (std::find(path.begin(), path.end(), current) == path.end())
			{
				path.push_back(current);
				for (std::size_t predecessor : graph.predecessors[current])
				{
					if (remainingInDegree[predecessor] > 0) { current = predecessor; break; }
				}
			}

			// The walk went backwards (against "runs before"), so reverse the looped part.
			std::vector<std::size_t> cycle(std::find(path.begin(), path.end(), current), path.end());
			std::reverse(cycle.begin(), cycle.end());

			std::string text;
			for (std::size_t index : cycle) text += nodes[index].name + " -> ";
			return text + nodes[cycle.front()].name;
		}

		// Kahn's algorithm for one stage; returns the members in execution order.
		std::vector<std::size_t> SortStage(const std::vector<SystemNode>& nodes, const DependencyGraph& graph,
			const std::vector<std::size_t>& members, SystemStage stage)
		{
			std::vector<std::size_t> inDegree(nodes.size(), 0);
			for (std::size_t member : members) inDegree[member] = graph.predecessors[member].size();

			// Ready systems ordered by (priority, registration index): lower priority value runs first.
			std::set<std::pair<int, std::size_t>> ready;
			for (std::size_t member : members)
			{
				if (inDegree[member] == 0) ready.emplace(nodes[member].desc.priority, member);
			}

			std::vector<std::size_t> order;
			order.reserve(members.size());
			while (!ready.empty())
			{
				const std::size_t next = ready.begin()->second;
				ready.erase(ready.begin());
				order.push_back(next);

				// Releasing a system may make the systems waiting on it ready.
				for (std::size_t successor : graph.successors[next])
				{
					if (--inDegree[successor] == 0) ready.emplace(nodes[successor].desc.priority, successor);
				}
			}

			// Detect a dependency cycle if not every system could be emitted.
			if (order.size() != members.size())
			{
				std::size_t start = members.front();
				for (std::size_t member : members)
				{
					if (inDegree[member] > 0) { start = member; break; }
				}
				throw std::logic_error(std::string("ECS scheduler: dependency cycle in stage ") + StageName(stage) +
					": " + DescribeCycle(nodes, graph, inDegree, start));
			}
			return order;
		}

		// Cuts a stage's order into contiguous batches of mutually compatible systems.
		std::vector<ExecutionBatch> BuildBatches(const std::vector<SystemNode>& nodes, const DependencyGraph& graph,
			const std::vector<std::size_t>& order)
		{
			std::vector<ExecutionBatch> batches;
			for (std::size_t system : order)
			{
				bool startNewBatch = batches.empty();
				if (!startNewBatch)
				{
					// A system can't share a batch with anything it conflicts with or depends on.
					for (std::size_t member : batches.back().systems)
					{
						if (SystemsConflict(nodes[member].desc.access, nodes[system].desc.access) || graph.HasEdge(member, system))
						{
							startNewBatch = true;
							break;
						}
					}
				}

				if (startNewBatch) batches.emplace_back();
				batches.back().systems.push_back(system);
			}
			return batches;
		}
	}

	const char* StageName(SystemStage stage) noexcept
	{
		switch (stage)
		{
		case SystemStage::PreUpdate:  return "PreUpdate";
		case SystemStage::Update:     return "Update";
		case SystemStage::PostUpdate: return "PostUpdate";
		case SystemStage::PreRender:  return "PreRender";
		default:                      return "Unknown";
		}
	}

	SystemSchedule BuildSystemSchedule(const std::vector<SystemNode>& nodes)
	{
		std::unordered_map<std::type_index, std::size_t> indexByType;
		for (std::size_t i = 0; i < nodes.size(); ++i)
		{
			// A system outside every real stage would otherwise never run.
			if (nodes[i].desc.stage >= SystemStage::Count)
			{
				throw std::logic_error("ECS scheduler: system '" + nodes[i].name + "' has an invalid stage");
			}
			indexByType.emplace(nodes[i].type, i);
		}

		// Build dependency edges; cross-stage constraints are checked against stage order.
		DependencyGraph graph(nodes.size());
		for (std::size_t self = 0; self < nodes.size(); ++self)
		{
			const auto resolveAll = [&](const std::vector<SystemDependency>& targets, bool selfRunsAfterOther)
				{
					for (const SystemDependency& target : targets)
					{
						const auto found = indexByType.find(target.type);
						if (found == indexByType.end())
						{
							// A required dependency that isn't registered makes the schedule invalid.
							if (target.required)
							{
								throw std::logic_error("ECS scheduler: system '" + nodes[self].name + "' requires '" +
									target.type.name() + "' (" + (selfRunsAfterOther ? "RunAfter" : "RunBefore") +
									"), which is not registered - register it or use " +
									(selfRunsAfterOther ? "RunAfterIfPresent" : "RunBeforeIfPresent"));
							}

							// Optional dependency: the other system simply isn't part of this build.
							CRU_INFO("ECS scheduler: '{}' has an optional dependency on unregistered system '{}' - ignored",
								nodes[self].name, target.type.name());
							continue;
						}
						ResolveDependency(nodes, self, found->second, selfRunsAfterOther, graph);
					}
				};

			// Exclusive already means "touches everything"; listed reads/writes are just documentation.
			const SystemAccess& access = nodes[self].desc.access;
			if (access.exclusive && (access.reads.any() || access.writes.any()))
			{
				CRU_WARN("ECS scheduler: '{}' is Exclusive() and also lists reads/writes - Exclusive() wins", nodes[self].name);
			}
			resolveAll(nodes[self].desc.runAfter, true);
			resolveAll(nodes[self].desc.runBefore, false);
		}

		SystemSchedule schedule;
		schedule.descs.reserve(nodes.size());
		schedule.names.reserve(nodes.size());
		for (const SystemNode& node : nodes)
		{
			schedule.descs.push_back(node.desc);
			schedule.names.push_back(node.name);
		}

		// Stages are sorted and batched independently; stage order itself never changes.
		for (std::size_t stageValue = 0; stageValue < static_cast<std::size_t>(SystemStage::Count); ++stageValue)
		{
			const SystemStage stage = static_cast<SystemStage>(stageValue);

			std::vector<std::size_t> members;
			for (std::size_t i = 0; i < nodes.size(); ++i)
			{
				if (nodes[i].desc.stage == stage) members.push_back(i);
			}
			if (members.empty()) continue;

			StageSchedule stageSchedule;
			stageSchedule.stage = stage;
			stageSchedule.batches = BuildBatches(nodes, graph, SortStage(nodes, graph, members, stage));

			for (const ExecutionBatch& batch : stageSchedule.batches)
			{
				schedule.serialOrder.insert(schedule.serialOrder.end(), batch.systems.begin(), batch.systems.end());
			}
			schedule.stages.push_back(std::move(stageSchedule));
		}

		return schedule;
	}
}
