/*************************************************************************
* \file:         HierarchyTransformSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 September 2026
* \version       1.0
* \brief         Implementation - see HierarchyTransformSystem.h.
*
* \history
*    -v1.0       initial top-down propagation with change ticks
*
**************************************************************************/
#include "HierarchyTransformSystem.h"

#include <stdexcept>

#include "ECSPhysicsSystem.h"
#include "World.h"

namespace Crucible
{
	namespace
	{
		constexpr std::size_t kMaxStack = 1u << 20;   // corrupted (cyclic) links guard

		[[noreturn]] void MissingBaseComponent()
		{
			throw std::logic_error("HierarchyTransformSystem: entity lacks Transform/WorldTransform/HierarchyNode - the mandatory-component invariant is broken");
		}
	}

	void HierarchyTransformSystem::Describe(SystemDesc& desc) const
	{
		desc.Uses<NodeQuery>()
			.Reads<Transform, HierarchyNode>()
			.Writes<WorldTransform>()
			.RunAfterIfPresent<ECSPhysicsSystem>()   // physics (if registered) moves Transforms first
			.Affinity(SystemAffinity::Worker);
	}

	void HierarchyTransformSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx)
	{
		if (!m_nodes) m_nodes.emplace(world.CreateQuery<const Transform>());

		const std::uint64_t since = m_lastRunTick;
		const auto changed = [&](std::uint64_t tick) { return since == 0 || tick >= since; };
		m_lastRecomputed = 0;
		m_stack.clear();

		m_nodes->ForEach([&](Entity e, const Transform&)
			{
				const HierarchyNode* rootLinks = world.Read<HierarchyNode>(e);
				if (!rootLinks) MissingBaseComponent();
				if (rootLinks->parent.IsValid()) return;   // not a root: reached through its parent

				m_stack.push_back({ e, Math::mat4::fromIdentity(), false });
				while (!m_stack.empty())
				{
					if (m_stack.size() > kMaxStack) throw std::logic_error("HierarchyTransformSystem: hierarchy links are corrupted (cycle?)");
					const Frame frame = m_stack.back();
					m_stack.pop_back();

					const Transform* local = world.Read<Transform>(frame.entity);
					const HierarchyNode* links = world.Read<HierarchyNode>(frame.entity);
					const WorldTransform* cached = world.Read<WorldTransform>(frame.entity);
					if (!local || !links || !cached) MissingBaseComponent();

					const bool dirty = frame.parentRecomputed
						|| !cached->valid
						|| changed(world.LastChanged<Transform>(frame.entity))
						|| changed(world.LastChanged<HierarchyNode>(frame.entity));

					Math::mat4 worldMatrix = cached->matrix;   // clean: reuse, children still need it
					if (dirty)
					{
						worldMatrix = frame.parentWorld * Detail::ComposeTRS(local->positions, local->rotation, local->scale);
						WorldTransform* out = world.Write<WorldTransform>(frame.entity);
						out->matrix = worldMatrix;
						out->valid = true;
						++m_lastRecomputed;
					}

					// Push last child first so children pop (and run) in order.
					for (Entity c = links->lastChild; c.IsValid(); )
					{
						const HierarchyNode* childLinks = world.Read<HierarchyNode>(c);
						if (!childLinks) MissingBaseComponent();
						m_stack.push_back({ c, worldMatrix, dirty });
						c = childLinks->prevSibling;
					}
				}
			});

		m_lastRunTick = world.CurrentTick();
	}

	void HierarchyTransformSystem::Shutdown() noexcept
	{
		m_nodes.reset();
		m_stack.clear();
		m_stack.shrink_to_fit();
	}
}
