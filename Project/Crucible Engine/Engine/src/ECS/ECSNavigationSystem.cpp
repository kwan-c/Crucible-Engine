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

        // The area a FlowFieldSurface covers, read off its entity's world matrix.
        struct SurfaceArea {

            Math::vec3 centre = Math::vec3(0.0f);
            float      width  = 0.0f;   // metres along X
            float      depth  = 0.0f;   // metres along Z

        };  // SURFACEAREA STRUCT

        /*!
            \brief
            The position is the centre, and the scale along X and Z is the width
            and depth in metres - so the designer sizes the surface with the
            ordinary scale gizmo, like a box volume.

            \details
            A matrix's first three columns are its X, Y and Z axes, each
            stretched by that axis's scale, so a column's length IS the scale
            whether or not the entity is rotated. The rotation itself is
            ignored: the grid always lines up with the world axes, which is what
            keeps "which cell am I in" a single floor() per axis.
        */
        SurfaceArea AreaOf(const Math::mat4& world) {

            return { Math::vec3(world[3]), glm::length(Math::vec3(world[0])), glm::length(Math::vec3(world[2])) };

        }   // AreaOf()

        // The surface's edge as a bright cyan rectangle at its own height, on top.
        void DrawOutline(const SurfaceArea& area) {

            const DebugStyle outline{ .color = { 0.4f, 0.95f, 1.0f, 1.0f }, .depthTest = false };

            const float      y = area.centre.y + kDrawLift;
            const float      w = area.width * 0.5f;
            const float      d = area.depth * 0.5f;
            const Math::vec3 a(area.centre.x - w, y, area.centre.z - d);
            const Math::vec3 b(area.centre.x + w, y, area.centre.z - d);
            const Math::vec3 c(area.centre.x + w, y, area.centre.z + d);
            const Math::vec3 e(area.centre.x - w, y, area.centre.z + d);
            DebugDraw::Line(a, b, outline);
            DebugDraw::Line(b, c, outline);
            DebugDraw::Line(c, e, outline);
            DebugDraw::Line(e, a, outline);

        }   // DrawOutline()

    }   // ANONYMOUS NAMESPACE

    ECSNavigationSystem::ECSNavigationSystem(const PlayMode& playMode) : m_playMode(playMode) {}

    /*!
        \brief
        Bakes when Play starts, draws the surface's outline, forgets the bake
        when the surface is gone, and draws the grid while it is baked.
    */
    void ECSNavigationSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {

        // Play bakes on its own: the designer may have moved things since the
        // last Bake click, or never clicked it, and the game must not run on a
        // stale grid. Only Edit -> Playing - Resume carries on with the same one.
        const PlayState state = m_playMode.State();
        if (m_lastState == PlayState::Edit && state == PlayState::Playing) Bake(world);
        m_lastState = state;

        // The surface's outline comes straight from its Transform every frame,
        // baked or not, so it follows the scale gizmo live while the designer
        // sizes it - like Unity's volume gizmo. If it no longer matches the
        // grid underneath, that is the cue to bake again.
        bool hasSurface = false;
        world.CreateQuery<const FlowFieldSurface, const WorldTransform>()
            .ForEach([&](Entity, const FlowFieldSurface&, const WorldTransform& transform) {
                if (hasSurface) return;     // only the first surface counts, as in Bake
                hasSurface = true;
                DrawOutline(AreaOf(transform.matrix));
            });

        // No surface any more (removed, switched off, or a scene without one):
        // the grid describes something that isn't there, so stop showing it.
        if (!hasSurface) m_grid = NavGrid{};

        if (m_grid.IsBaked()) DrawGrid();

    }   // ECSNavigationSystem::Update()

    bool ECSNavigationSystem::Bake(World& world) {

        // 1. The surface: its settings, and its entity's position and scale as
        // the area it covers.
        FlowFieldSurface surface;
        SurfaceArea      area;
        int              surfaces = 0;
        world.CreateQuery<const FlowFieldSurface, const WorldTransform>()
            .ForEach([&](Entity, const FlowFieldSurface& found, const WorldTransform& transform) {
                if (surfaces++ == 0) {
                    surface = found;
                    area    = AreaOf(transform.matrix);
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

        // A freshly added surface sits on an entity of scale 1: a 1 x 1 m grid.
        // It still bakes (one cell is a valid grid), but say what to do about it.
        if (area.width < surface.m_cellSize * 2.0f || area.depth < surface.m_cellSize * 2.0f) {
            CRU_WARN("Navigation: the surface only covers {} x {} m - scale its entity so X and Z cover the level (scale = metres)",
                     area.width, area.depth);
        }

        // Until a terrain system exists the ground is flat, at the surface's
        // own height - so a designer sits the surface on the floor, the way
        // Unity's NavMeshSurface sits in the level. World y = 0 would be wrong
        // for any floor that isn't exactly there.
        m_terrain = FlatNavTerrain(area.centre.y);

        // 2. The obstacles: static, solid colliders. "Static" is Unity's rule:
        // a collider with no RigidBody. Anything with one moves (crates, even a
        // kinematic door), so it is steered around later, not baked in - and
        // checking for the RigidBody too catches a moving body someone left on
        // the NonMoving layer. Triggers block nothing in physics either.
        m_obstacles.clear();
        world.CreateQuery<const Collider, const WorldTransform>()
            .ForEach([&](Entity entity, const Collider& collider, const WorldTransform& transform) {
                if (collider.m_isTrigger || collider.m_layer != PhysicsLayer::NonMoving || !transform.valid) return;
                if (world.HasComponent<RigidBody>(entity)) return;
                m_obstacles.push_back(WorldBounds(collider, transform.matrix));
            });

        // 3. The bake itself. NavGrid logs the result, or why it failed.
        NavGridSettings settings;
        settings.origin            = area.centre;
        settings.width             = area.width;
        settings.depth             = area.depth;
        settings.cellSize          = surface.m_cellSize;
        settings.bodyRadiusPadding = surface.m_bodyRadiusPadding;
        settings.stepHeight        = surface.m_stepHeight;
        settings.shoulderHeight    = surface.m_shoulderHeight;
        return m_grid.Bake(settings, m_obstacles, m_terrain);

    }   // ECSNavigationSystem::Bake()

    /*!
        \brief
        The baked grid: its cell lines in blue (not grey, which the editor's
        own world grid already uses), and each blocked cell as a red square,
        just above the ground. The outline is drawn separately, live, by
        Update.

        \details
        Drawn on top of everything (depthTest off), like Unity's NavMesh
        overlay: a floor slightly higher than the surface, or the obstacles
        themselves, would otherwise hide the very cells the designer wants to
        check.
    */
    void ECSNavigationSystem::DrawGrid() const {

        const DebugStyle cells  { .color = { 0.15f, 0.4f, 0.95f, 1.0f }, .depthTest = false };
        const DebugStyle blocked{ .color = { 1.0f, 0.25f, 0.2f, 1.0f },  .depthTest = false };

        const float cellSize = m_grid.CellSize();
        const float half     = cellSize * 0.5f;

        // The grid's corners: from the first cell's outer corner to the last cell's.
        const Math::vec3 first = m_grid.CellToWorldCenter({ 0, 0 });
        const Math::vec3 last  = m_grid.CellToWorldCenter({ m_grid.Cols() - 1, m_grid.Rows() - 1 });
        const float      y     = m_terrain.GroundHeight(first) + kDrawLift;
        const float      minX  = first.x - half;
        const float      maxX  = last.x + half;
        const float      minZ  = first.z - half;
        const float      maxZ  = last.z + half;

        // Cell lines: one long line per column edge and per row edge, not four
        // per cell - 250 x 250 cells is 502 lines this way instead of 250,000.
        for (int col = 1; col < m_grid.Cols(); ++col) {
            const float x = minX + static_cast<float>(col) * cellSize;
            DebugDraw::Line({ x, y, minZ }, { x, y, maxZ }, cells);
        }
        for (int row = 1; row < m_grid.Rows(); ++row) {
            const float z = minZ + static_cast<float>(row) * cellSize;
            DebugDraw::Line({ minX, y, z }, { maxX, y, z }, cells);
        }

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
