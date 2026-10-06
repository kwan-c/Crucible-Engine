/*************************************************************************
  \file       StressMoverSystem.cpp
  \brief      TEMPORARY - load testing only. See StressMoverSystem.h.
*************************************************************************/
#include "Graphics/StressMoverSystem.h"

#include "ECS/World.h"

#include <algorithm>
#include <cmath>

namespace Crucible {

    void StressMoverSystem::Describe(SystemDesc& desc) const {

        desc.Uses<MoverQuery>()                      // writes Transform, reads StressMover
            .Affinity(SystemAffinity::MainThread);

    }   // StressMoverSystem::Describe()

    void StressMoverSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {

        if (!m_query) { m_query.emplace(world.CreateQuery<Transform, const StressMover>()); }

        m_time += std::max(world.DeltaTime(), 0.0f);
        const float time = m_time;

        // Writing Transform marks it changed, so HierarchyTransformSystem recomputes the
        // entity's world matrix and RenderExtractSystem re-sends it - the path being measured.
        m_query->ForEach([time](Entity, Transform& transform, const StressMover& mover) {
            const float angle = time * mover.speed + mover.phase;
            transform.positions = mover.origin + Math::vec3(std::sin(angle) * mover.radius, 0.0f, std::cos(angle) * mover.radius);
        });

    }   // StressMoverSystem::Update()

}   // NAMESPACE CRUCIBLE
