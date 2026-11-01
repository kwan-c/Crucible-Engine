/*************************************************************************
* \file:         ECSNavigationSystem.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.0
* \brief
*                Bakes the NavGrid from the scene, and draws it.
*
* \details
*                The ECS-to-grid translation lives here, so NavGrid itself
*                never sees a component: this file turns colliders into
*                NavObstacle boxes and the FlowFieldSurface into
*                NavGridSettings.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "ECSNavigationSystem.h"

// STL
#include <algorithm>    // std::max

// Crucible engine
#include "Components/Components.h"            // WorldTransform
#include "Diagnostics/Logger.h"
#include "Graphics/DebugDraw.h"
#include "Navigation/NavigationComponents.h"  // FlowFieldSurface
#include "Physics/PhysicsComponents.h"        // Collider
#include "Systems/PlayMode.h"
#include "Systems/Query.h"
#include "World.h"

namespace Crucible {

    namespace {

        // Lines are drawn this far above the ground so the floor doesn't hide them.
        constexpr float kDrawLift = 0.05f;

        /*!
            \brief
            A collider's world axis-aligned box, the way physics sizes it: the
            shape's local box around m_center, scaled, turned and moved by the
            entity's world matrix.

            \details
            Turning a box makes its AABB bigger than the box itself (a cube
            turned 45 degrees is about 1.4 times as wide). That is the
            conservative footprint Jean David's bake uses on purpose.
        */
        NavObstacle WorldBounds(const Collider& collider, const Math::mat4& world) {

            // Half the shape's size along each local axis.
            Math::vec3 half(0.0f);
            switch (collider.m_type) {
                case ShapeType::Box:     half = collider.m_size * 0.5f; break;
                case ShapeType::Sphere:  half = Math::vec3(collider.m_radius); break;
                // A capsule stands along its local Y. m_height is the full height, ends
                // included, but never less than the two end caps together.
                case ShapeType::Capsule: half = Math::vec3(collider.m_radius, std::max(collider.m_height * 0.5f, collider.m_radius), collider.m_radius); break;
            }

            // Transform all 8 corners and keep the smallest and biggest of each axis.
            // Only corners are needed: a box's furthest points are always corners.
            NavObstacle bounds{ Math::vec3(1e30f), Math::vec3(-1e30f) };
            for (int corner = 0; corner < 8; ++corner) {

                const Math::vec3 sign((corner & 1) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f, (corner & 4) ? 1.0f : -1.0f);
                const Math::vec3 point = Math::vec3(world * Math::vec4(collider.m_center + sign * half, 1.0f));
                bounds.min = glm::min(bounds.min, point);
                bounds.max = glm::max(bounds.max, point);

            }
            return bounds;

        }   // WorldBounds()

    }   // ANONYMOUS NAMESPACE

    ECSNavigationSystem::ECSNavigationSystem(const PlayMode& playMode) : m_playMode(playMode) {}

    /*!
        \brief
        Bakes when Play starts, forgets the bake when the surface is gone, and
        draws the grid while it is baked.
    */
    void ECSNavigationSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {

        // Play bakes on its own: the designer may have moved things since the
        // last Bake click, or never clicked it, and the game must not run on a
        // stale grid. Only Edit -> Playing - Resume carries on with the same one.
        const PlayState state = m_playMode.State();
        if (m_lastState == PlayState::Edit && state == PlayState::Playing) Bake(world);
        m_lastState = state;

        // No surface any more (removed, switched off, or a scene without one):
        // the grid describes something that isn't there, so stop showing it.
        if (m_grid.IsBaked()) {

            bool hasSurface = false;
            world.CreateQuery<const FlowFieldSurface>().ForEach([&](Entity, const FlowFieldSurface&) { hasSurface = true; });
            if (!hasSurface) m_grid = NavGrid{};

        }

        if (m_grid.IsBaked()) DrawGrid();

    }   // ECSNavigationSystem::Update()

    bool ECSNavigationSystem::Bake(World& world) {

        // 1. The surface: its settings, and its entity's position as the centre.
        FlowFieldSurface surface;
        Math::vec3       centre(0.0f);
        int              surfaces = 0;
        world.CreateQuery<const FlowFieldSurface, const WorldTransform>()
            .ForEach([&](Entity, const FlowFieldSurface& found, const WorldTransform& transform) {
                if (surfaces++ == 0) {
                    surface = found;
                    centre  = Math::vec3(transform.matrix[3]);   // a matrix's last column is its position
                }
            });

        if (surfaces == 0) {
            CRU_WARN("Navigation: nothing to bake - add a FlowFieldSurface to an entity first");
            m_grid = NavGrid{};
            return false;
        }
        if (surfaces > 1) {
            CRU_WARN("Navigation: {} FlowFieldSurfaces in the scene - only the first one is used", surfaces);
        }

        // 2. The obstacles: static, solid colliders. Moving things (crates,
        // the player, enemies) are steered around later, not baked in, and
        // triggers block nothing in physics either.
        m_obstacles.clear();
        world.CreateQuery<const Collider, const WorldTransform>()
            .ForEach([&](Entity, const Collider& collider, const WorldTransform& transform) {
                if (collider.m_isTrigger || collider.m_layer != PhysicsLayer::NonMoving || !transform.valid) return;
                m_obstacles.push_back(WorldBounds(collider, transform.matrix));
            });

        // 3. The bake itself. NavGrid logs the result, or why it failed.
        NavGridSettings settings;
        settings.origin            = centre;
        settings.width             = surface.m_width;
        settings.depth             = surface.m_depth;
        settings.cellSize          = surface.m_cellSize;
        settings.bodyRadiusPadding = surface.m_bodyRadiusPadding;
        settings.stepHeight        = surface.m_stepHeight;
        settings.shoulderHeight    = surface.m_shoulderHeight;
        return m_grid.Bake(settings, m_obstacles, m_terrain);

    }   // ECSNavigationSystem::Bake()

    /*!
        \brief
        The grid's outline in blue, and each blocked cell as a red square,
        just above the ground. Unity's NavMesh overlay shows where enemies CAN
        walk; this shows where they can't, because on our maps that is the far
        smaller set (most of a grid is open ground).
    */
    void ECSNavigationSystem::DrawGrid() const {

        const DebugStyle outline{ .color = { 0.3f, 0.8f, 1.0f, 1.0f } };
        const DebugStyle blocked{ .color = { 1.0f, 0.25f, 0.2f, 1.0f } };

        const float cellSize = m_grid.CellSize();
        const float half     = cellSize * 0.5f;

        // The outline: from the first cell's outer corner to the last cell's.
        const Math::vec3 first = m_grid.CellToWorldCenter({ 0, 0 });
        const Math::vec3 last  = m_grid.CellToWorldCenter({ m_grid.Cols() - 1, m_grid.Rows() - 1 });
        const float      y     = m_terrain.GroundHeight(first) + kDrawLift;
        const Math::vec3 a(first.x - half, y, first.z - half);
        const Math::vec3 b(last.x + half,  y, first.z - half);
        const Math::vec3 c(last.x + half,  y, last.z + half);
        const Math::vec3 d(first.x - half, y, last.z + half);
        DebugDraw::Line(a, b, outline);
        DebugDraw::Line(b, c, outline);
        DebugDraw::Line(c, d, outline);
        DebugDraw::Line(d, a, outline);

        // Blocked cells, drawn a little smaller than the cell (80%) so a row of
        // them reads as separate tiles rather than one solid smear.
        const float inset = cellSize * 0.4f;
        for (int row = 0; row < m_grid.Rows(); ++row) {
            for (int col = 0; col < m_grid.Cols(); ++col) {

                if (!m_grid.IsBlocked({ col, row })) continue;

                Math::vec3 centre = m_grid.CellToWorldCenter({ col, row });
                centre.y = m_terrain.GroundHeight(centre) + kDrawLift;
                const Math::vec3 p0(centre.x - inset, centre.y, centre.z - inset);
                const Math::vec3 p1(centre.x + inset, centre.y, centre.z - inset);
                const Math::vec3 p2(centre.x + inset, centre.y, centre.z + inset);
                const Math::vec3 p3(centre.x - inset, centre.y, centre.z + inset);
                DebugDraw::Line(p0, p1, blocked);
                DebugDraw::Line(p1, p2, blocked);
                DebugDraw::Line(p2, p3, blocked);
                DebugDraw::Line(p3, p0, blocked);

            }
        }

    }   // ECSNavigationSystem::DrawGrid()

}   // NAMESPACE CRUCIBLE
