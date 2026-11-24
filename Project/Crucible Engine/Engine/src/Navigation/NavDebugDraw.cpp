/*************************************************************************
* \file:         NavDebugDraw.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          17 November 2026
* \version       1.4
* \brief         See NavDebugDraw.h.
*
* \history
*    -v1.0       first version
*    -v1.1       raised layers (Walkable tops) drawn as green tiles at their
*                own height; blocked ground drawn at its node's height
*    -v1.2       links between different surfaces: climbs as white lines,
*                drops as magenta arrows (down)
*    -v1.3       flow arrows on every layer, at its own height
*    -v1.4       cave floors drawn like raised layers; a cave cell has no
*                ground tile
*
**************************************************************************/
#include "NavDebugDraw.h"

// Crucible engine
#include "Graphics/DebugDraw.h"

namespace Crucible::NavDebugDraw {

    namespace {

        // Lines are drawn this far above the ground so the floor doesn't hide them.
        constexpr float kDrawLift = 0.05f;

        // A flat square tile, `inset` from the centre each way.
        void Square(const Math::vec3& centre, float inset, const DebugStyle& style) {

            const Math::vec3 p0(centre.x - inset, centre.y, centre.z - inset);
            const Math::vec3 p1(centre.x + inset, centre.y, centre.z - inset);
            const Math::vec3 p2(centre.x + inset, centre.y, centre.z + inset);
            const Math::vec3 p3(centre.x - inset, centre.y, centre.z + inset);
            DebugDraw::Line(p0, p1, style);
            DebugDraw::Line(p1, p2, style);
            DebugDraw::Line(p2, p3, style);
            DebugDraw::Line(p3, p0, style);

        }   // Square()

    }   // ANONYMOUS NAMESPACE

    bool WantsGrid()   { return DebugDraw::ShouldDraw(DebugCategory::NavGrid); }
    bool WantsArrows() { return DebugDraw::ShouldDraw(DebugCategory::FlowField); }

    void Outline(const NavArea& area) {

        const DebugStyle outline{ .color = { 0.4f, 0.95f, 1.0f, 1.0f }, .depthTest = false, .category = DebugCategory::NavGrid };

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

    }   // Outline()

    void Grid(const NavGrid& grid, const NavTerrain& ground) {

        const DebugStyle cells  { .color = { 0.15f, 0.4f, 0.95f, 1.0f }, .depthTest = false, .category = DebugCategory::NavGrid };
        const DebugStyle blocked{ .color = { 1.0f, 0.25f, 0.2f, 1.0f },  .depthTest = false, .category = DebugCategory::NavGrid };
        const DebugStyle raised { .color = { 0.3f, 0.95f, 0.45f, 1.0f }, .depthTest = false, .category = DebugCategory::NavGrid };
        const DebugStyle climb  { .color = { 0.95f, 0.95f, 0.95f, 1.0f }, .depthTest = false, .category = DebugCategory::NavGrid };
        const DebugStyle drop   { .color = { 1.0f, 0.3f, 0.9f, 1.0f },   .depthTest = false, .category = DebugCategory::NavGrid };

        const float cellSize = grid.CellSize();
        const float half     = cellSize * 0.5f;

        // The grid's corners: from the first cell's outer corner to the last cell's.
        const Math::vec3 first = grid.CellToWorldCenter({ 0, 0 });
        const Math::vec3 last  = grid.CellToWorldCenter({ grid.Cols() - 1, grid.Rows() - 1 });
        const float      y     = ground.GroundHeight(first) + kDrawLift;
        const float      minX  = first.x - half;
        const float      maxX  = last.x + half;
        const float      minZ  = first.z - half;
        const float      maxZ  = last.z + half;

        // Cell lines: one long line per column edge and per row edge, not four
        // per cell - 250 x 250 cells is 502 lines this way instead of 250,000.
        for (int col = 1; col < grid.Cols(); ++col) {
            const float x = minX + static_cast<float>(col) * cellSize;
            DebugDraw::Line({ x, y, minZ }, { x, y, maxZ }, cells);
        }
        for (int row = 1; row < grid.Rows(); ++row) {
            const float z = minZ + static_cast<float>(row) * cellSize;
            DebugDraw::Line({ minX, y, z }, { maxX, y, z }, cells);
        }

        // Blocked cells and raised layers, drawn a little smaller than the
        // cell (80%) so a row of them reads as separate tiles rather than one
        // solid smear.
        const float inset = cellSize * 0.4f;
        for (int row = 0; row < grid.Rows(); ++row) {
            for (int col = 0; col < grid.Cols(); ++col) {

                const NavCell cell{ col, row };
                const int     groundNode = grid.GroundNode(cell);

                // The ground: red where it is blocked, or where it is not a
                // layer at all (a Walkable box sits on it). A cave cell has
                // floors instead, drawn below.
                const bool cave = grid.LayerCount(cell) > 0 && grid.IsCaveNode(grid.NodeIndex(cell, 0));
                if (!cave && grid.IsBlocked(cell)) {
                    Math::vec3 centre = grid.CellToWorldCenter(cell);
                    centre.y = (groundNode >= 0 ? grid.NodeHeight(groundNode) : ground.GroundHeight(centre)) + kDrawLift;
                    Square(centre, inset, blocked);
                }

                // Raised layers (Walkable tops) and cave floors: green where
                // open, red where blocked, at their own height, so a bridge
                // reads as a second floor of tiles over the grid.
                for (int layer = 0; layer < grid.LayerCount(cell); ++layer) {
                    const int node = grid.NodeIndex(cell, layer);
                    if (node == groundNode) continue;
                    Math::vec3 centre = grid.CellToWorldCenter(cell);
                    centre.y = grid.NodeHeight(node) + kDrawLift;
                    Square(centre, inset, grid.IsNodeBlocked(node) ? blocked : raised);
                }

                // Links between different surfaces (along one surface they are
                // everywhere, so not drawn): a climb, both ways, as a white
                // line between the two layers (drawn once); a drop, one way,
                // as a magenta arrow down to where it lands.
                for (int layer = 0; layer < grid.LayerCount(cell); ++layer) {
                    const int node = grid.NodeIndex(cell, layer);
                    for (int dir = 0; dir < kNavDirections; ++dir) {
                        const int other = grid.LinkedNode(node, dir);
                        if (other < 0 || grid.IsSameSurface(node, other)) continue;
                        Math::vec3 from = grid.CellToWorldCenter(cell);
                        Math::vec3 to   = grid.CellToWorldCenter(grid.NodeCell(other));
                        from.y = grid.NodeHeight(node)  + kDrawLift * 2.0f;
                        to.y   = grid.NodeHeight(other) + kDrawLift * 2.0f;
                        const bool twoWay = grid.LinkedNode(other, kNavOpposite[dir]) == node;
                        if (!twoWay)           DebugDraw::Arrow(from, to, drop);
                        else if (node < other) DebugDraw::Line(from, to, climb);
                    }
                }

            }
        }

    }   // Grid()

    void Arrows(const NavGrid& grid, const FlowField& field) {

        const DebugStyle arrow{ .color = { 1.0f, 0.85f, 0.2f, 1.0f }, .depthTest = false, .category = DebugCategory::FlowField };

        // Each arrow spans 70% of a cell, so neighbouring arrows never touch.
        const float halfLength = grid.CellSize() * 0.35f;

        // One per node that has a direction, at its own layer's height: up
        // the ramp, along the deck, and on the ground under it.
        for (int node = 0; node < grid.NodeCount(); ++node) {

            const Math::vec3 direction = field.DirectionAtNode(node);
            if (direction.x == 0.0f && direction.z == 0.0f) continue;

            Math::vec3 centre = grid.CellToWorldCenter(grid.NodeCell(node));
            centre.y = grid.NodeHeight(node) + kDrawLift * 2.0f;   // a little above the grid lines
            DebugDraw::Arrow(centre - direction * halfLength, centre + direction * halfLength, arrow);

        }

    }   // Arrows()

}   // NAMESPACE CRUCIBLE::NAVDEBUGDRAW
