/*************************************************************************
* \file:         HierarchyTransformSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 September 2026
* \version       1.1
* \brief         Implementation - see HierarchyTransformSystem.h.
*
* \history
*    -v1.0       initial top-down propagation with change ticks
*    -v1.1       one lookup per node, parent matrix by pointer, root frame
*                kept off the stack; access checked once per Update
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
		if (!m_nodes) m_nodes.emplace(world.CreateQuery<const Transform, const HierarchyNode>());

		// The per-node reads below go through World::ReadNodeRow, so check the
		// declared access once here instead of on every read.
		CheckDeclaredAccess(MakeSignature<Transform, HierarchyNode>(), MakeSignature<WorldTransform>(),
			"HierarchyTransformSystem reads/writes");

		const std::uint64_t since = m_lastRunTick;
		const std::uint64_t now = world.CurrentTick();
		const auto changed = [since](std::uint64_t tick) { return since == 0 || tick >= since; };
		m_lastRecomputed = 0;
		m_stack.clear();

		m_nodes->ForEach([&](Entity e, const Transform&, const HierarchyNode& rootLinks)
			{
				if (rootLinks.parent.IsValid()) return;   // not a root: reached through its parent

				// Depth-first, each node read once: after a node, push its next sibling
				// (same parent frame), then its first child (this node's frame), so a
				// child's whole subtree runs before its next sibling.
				// The root's frame stays in a register; only descendants use the stack.
				Frame frame{ e, nullptr, false };
				for (;;)
				{
					World::NodeRow node;
					if (!world.ReadNodeRow(frame.entity, node)) MissingBaseComponent();

					const bool dirty = frame.parentRecomputed
						|| !node.world->valid
						|| changed(node.localTick)
						|| changed(node.linksTick);

					if (dirty)
					{
						const Math::mat4 local = Detail::ComposeTRS(node.local->positions, node.local->rotation, node.local->scale);
						node.world->matrix = frame.parentWorld ? *frame.parentWorld * local : local;
						node.world->valid = true;
						*node.worldTick = now;   // same stamp World::Write would give
						++m_lastRecomputed;
					}

					if (frame.entity != e && node.links->nextSibling.IsValid())
						m_stack.push_back({ node.links->nextSibling, frame.parentWorld, frame.parentRecomputed });
					if (node.links->firstChild.IsValid())
						m_stack.push_back({ node.links->firstChild, &node.world->matrix, dirty });   // clean or fresh: children read it in place

					if (m_stack.empty()) break;
					if (m_stack.size() > kMaxStack) throw std::logic_error("HierarchyTransformSystem: hierarchy links are corrupted (cycle?)");
					frame = m_stack.back();
					m_stack.pop_back();
				}
			});

		m_lastRunTick = now;
	}

	void HierarchyTransformSystem::Shutdown() noexcept
	{
		m_nodes.reset();
		m_stack.clear();
		m_stack.shrink_to_fit();
	}
}
