/*************************************************************************
* \file:         HierarchyTransformSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 September 2026
* \version       1.0
* \brief
*                Computes WorldTransform = parent world x local (T*R*S)
*                for every entity, parents before children, and only for
*                subtrees where something changed.
*
* \details
*                Runs FIRST in PreRender: after every Update and PostUpdate
*                system (physics included) AND after those stages' command
*                replays and event deliveries, so spawns, reparents and
*                listener edits made anywhere before rendering are in this
*                frame's matrices. PreRender systems that read WorldTransform
*                should declare RunAfter<HierarchyTransformSystem>() (its
*                lowest-possible Priority() already puts it first). Changes
*                made later - by PreRender systems' own commands - show up
*                next frame (a spawn has valid == false until then), so
*                PreRender is for reading/extracting only. Every entity has a
*                Transform, WorldTransform and HierarchyNode (mandatory base
*                components), so every node's world matrix is cached - a
*                node missing one of them is a broken invariant and throws.
*
*                A node is recomputed when any of these is true:
*                  - its parent was recomputed this run,
*                  - its WorldTransform was never computed (valid == false),
*                  - its Transform changed, or it was reparented (only the
*                    moved entity's HierarchyNode is stamped - not its old
*                    or new parent, nor its siblings), at or after the
*                    tick of this system's previous run.
*                ">=" (not ">") is deliberate: a change made later in the
*                same tick, after this system ran, would otherwise be
*                missed forever. The cost is recomputing a one-off change
*                twice.
*
* \history
*    -v1.0       initial top-down propagation with change ticks
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "EngineExport.h"
#include "Components/Components.h"
#include "Systems/IECSSystems.h"
#include "Systems/Query.h"

namespace Crucible
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251 4275)   // STL members; IECSSystem is a header-only interface
#endif
	/*************************************************************************
	 \brief
		Engine system that fills every entity's WorldTransform from its
		local Transform and its parent's world matrix.

	 \details
		Walks each tree from its root (an entity with no parent),
		depth-first, pushing children after their parent is final - so no
		depth sorting is needed. Clean subtrees cost only link reads; only
		recomputed nodes are written (and stamped), so a renderer can
		upload just the matrices that moved.
	 *************************************************************************/
	class ENGINE_API HierarchyTransformSystem final : public IECSSystem
	{
	public:
		/*********************************************************************
		 \brief  PreRender: after the PostUpdate command/event flush, before
				 anything that consumes matrices.
		 *********************************************************************/
		SystemStage Stage() const override { return SystemStage::PreRender; }

		/*********************************************************************
		 \brief  Lowest possible priority: first among PreRender systems.
		 *********************************************************************/
		int Priority() const override { return (std::numeric_limits<int>::min)(); }

		/*********************************************************************
		 \brief  Reads Transform + HierarchyNode, writes WorldTransform, runs
				 after ECSPhysicsSystem when it is registered.
		 *********************************************************************/
		void Describe(SystemDesc& desc) const override;

		/*********************************************************************
		 \brief  Recomputes the world matrix of every dirty node (see the
				 file header for the dirty rules).
		 \note   Throws std::logic_error if the hierarchy links are corrupted
				 (a cycle) or an entity lacks a mandatory base component.
		 *********************************************************************/
		void Update(World& world, SystemContext& ctx) override;

		void Shutdown() noexcept override;

		/*********************************************************************
		 \brief  Diagnostics / tests: how many WorldTransforms the last
				 Update() wrote.
		 *********************************************************************/
		std::uint32_t LastRecomputedCount() const noexcept { return m_lastRecomputed; }

		/*********************************************************************
		 \brief  Forces every node to be recomputed on the next Update().
		 *********************************************************************/
		void InvalidateAll() noexcept { m_lastRunTick = 0; }

	private:
		// Every entity has a Transform, so this visits every entity (roots are
		// picked out by their HierarchyNode). const: iterating stamps nothing.
		using NodeQuery = Query<const Transform>;

		struct Frame
		{
			Entity entity;
			Math::mat4 parentWorld;
			bool parentRecomputed;
		};

		std::optional<NodeQuery> m_nodes;
		std::vector<Frame> m_stack;           // reused every frame
		std::uint64_t m_lastRunTick = 0;      // 0 = recompute everything
		std::uint32_t m_lastRecomputed = 0;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
