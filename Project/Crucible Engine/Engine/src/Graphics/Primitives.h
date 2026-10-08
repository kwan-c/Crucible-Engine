#pragma once
/*************************************************************************
* \file:         Primitives.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Procedural whitebox primitives (Cube, Plane, Disc,
*                Cylinder, Cone, Pyramid, Sphere, Capsule).
*
* \details
*                Everything here builds a CPU-side MeshData (Vertex + index
*                list) for Renderer::CreateMesh(), the same path the
*                built-in cube goes through. These exist so designers can
*                block out levels before a real asset pipeline (.glb
*                import) exists.
*
*                Conventions used throughout this file:
*                  - Y is up. A "cap" or "disc" lying flat sits in the XZ
*                    plane.
*                  - Round shapes (cylinder/cone/sphere/capsule/disc) take
*                    a segment count and use ANALYTIC normals (computed
*                    from the shape's geometry, not averaged), so shading
*                    stays correct at any resolution. Flat-faced shapes
*                    (cube, pyramid) duplicate vertices per face instead.
*                  - Any seam where a flat normal meets a curved one (e.g.
*                    a cylinder's end cap vs its side) duplicates the
*                    vertex, once per side of the seam, so the two faces
*                    don't blend normals.
*                  - Winding: every triangle here is CCW when viewed from
*                    the direction its normal points (standard right-hand-
*                    rule convention), calibrated directly against
*                    MakeCube()'s own (already-correct, already-rendering)
*                    winding - see the "Lower/Upper ring" pattern below.
*                  - Tangent (xyz = direction of increasing U, w =
*                    bitangent handedness sign; see Vertex's own doc
*                    comment in Vulkan/RenderLayouts.h) is derived
*                    analytically per shape from its own UV parametrization,
*                    the same way normals are. A flat cap's UV is polar (so
*                    a UV-gradient-matched tangent would be discontinuous
*                    at its center); those caps instead use a fixed, still-
*                    orthonormal +X tangent, noted at each call site -
*                    adequate given caps are rarely the focus of a normal
*                    map in a whitebox tool.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "RenderTypes.h"

namespace Crucible::Primitives {

    namespace Detail {
        inline constexpr float kTwoPi = 6.28318530717958647692f;

        // Builds one Vertex from its five components; exists purely so every MakeXxx() function
        // below reads as a flat list of vertex data instead of repeating aggregate-init braces.
        inline Vertex MakeVertex(glm::vec3 pos, glm::vec3 normal, glm::vec2 uv, glm::vec3 color,
            glm::vec4 tangent) {
            return Vertex{ pos, normal, uv, color, tangent };
        }

        // Flat-cap convention (see file header): a fixed orthonormal tangent, independent of the
        // cap's actual (polar) UV gradient. Valid for both +Y and -Y caps - see Primitives.h's
        // design notes for the two concrete cross-product checks this was calibrated against.
        inline constexpr glm::vec4 kFlatCapTangent{ 1.0f, 0.0f, 0.0f, 1.0f };

        // Appends the two triangles of a "ring strip" quad between two rings at the same pair of
        // columns (c, c+1). `lower`/`upper` are vertex indices already pushed into mesh.vertices;
        // "lower" must be the ring with the smaller world-space Y (this is what the winding was
        // verified against - see the cylinder-side / sphere-band derivation in the design notes).
        // Works unmodified for doubly-curved bands (sphere/capsule) too, since the rule only
        // depends on relative Y, not on the two rings sharing a radius.
        inline void AddRingStrip(std::vector<uint32_t>& idx,
            uint32_t lowerC, uint32_t upperC, uint32_t lowerC1, uint32_t upperC1) {
            idx.insert(idx.end(), { lowerC, upperC, lowerC1,   lowerC1, upperC, upperC1 });
        }
    }

    /*************************************************************************
     \brief
        Builds a unit cube: one duplicated quad (4 verts) per face, so each
        face gets a true flat normal AND a real per-face UV.

     \param[in] color Vertex color tint applied to every vertex.
     \return A CPU-side MeshData ready for Renderer::CreateMesh().

     \details
        The old shared-corner cube approximated normals via corner
        direction and never had real UVs at all - vec2(0) everywhere -
        which made it untexturable; this version fixes both, following the
        same per-face vertex-duplication style as MakePyramid(). Each
        face's four corners (c0..c3) are wound so (c0,c1,c2)+(c0,c2,c3)
        faces outward along `normal` (verified per-face via
        cross(c1-c0, c2-c0) against the original hand-authored cube's own
        winding), with u = c0->c3, v = c0->c1 (so tangent = dP/du =
        normalize(c3-c0), matching every face's hand-derived w = -1 - see
        the design notes for the cross-product check per face).
     *************************************************************************/
    inline MeshData MakeCube(glm::vec3 color = { 0.8f, 0.8f, 0.8f }) {
        struct Face { glm::vec3 c0, c1, c2, c3, normal; glm::vec4 tangent; };
        const Face faces[6] = {
            // +Y (top)
            { {-0.5f,0.5f,-0.5f}, {-0.5f,0.5f,0.5f}, {0.5f,0.5f,0.5f}, {0.5f,0.5f,-0.5f},
              {0,1,0}, {1,0,0,-1} },
            // -Y (bottom)
            { {-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,0.5f}, {-0.5f,-0.5f,0.5f},
              {0,-1,0}, {0,0,1,-1} },
            // -Z (back)
            { {-0.5f,-0.5f,-0.5f}, {-0.5f,0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {0.5f,-0.5f,-0.5f},
              {0,0,-1}, {1,0,0,-1} },
            // +Z (front)
            { {-0.5f,-0.5f,0.5f}, {0.5f,-0.5f,0.5f}, {0.5f,0.5f,0.5f}, {-0.5f,0.5f,0.5f},
              {0,0,1}, {0,1,0,-1} },
            // -X (left)
            { {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,0.5f}, {-0.5f,0.5f,0.5f}, {-0.5f,0.5f,-0.5f},
              {-1,0,0}, {0,1,0,-1} },
            // +X (right)
            { {0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {0.5f,0.5f,0.5f}, {0.5f,-0.5f,0.5f},
              {1,0,0}, {0,0,1,-1} },
        };

        MeshData mesh;
        mesh.vertices.reserve(24);
        mesh.indices.reserve(36);
        for (const Face& f : faces) {
            const uint32_t i0 = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(Detail::MakeVertex(f.c0, f.normal, { 0, 0 }, color, f.tangent));
            mesh.vertices.push_back(Detail::MakeVertex(f.c1, f.normal, { 0, 1 }, color, f.tangent));
            mesh.vertices.push_back(Detail::MakeVertex(f.c2, f.normal, { 1, 1 }, color, f.tangent));
            mesh.vertices.push_back(Detail::MakeVertex(f.c3, f.normal, { 1, 0 }, color, f.tangent));
            mesh.indices.insert(mesh.indices.end(), { i0, i0 + 1, i0 + 2,   i0, i0 + 2, i0 + 3 });
        }
        return mesh;
    }

    /*************************************************************************
     \brief
        Builds a flat, optionally-subdivided grid in the XZ plane, normal
        +Y.

     \param[in] width          Grid width along X.
     \param[in] depth          Grid depth along Z.
     \param[in] widthSegments  Number of subdivisions along X (clamped >= 1).
     \param[in] depthSegments  Number of subdivisions along Z (clamped >= 1).
     \param[in] color          Vertex color tint applied to every vertex.
     \return A CPU-side MeshData ready for Renderer::CreateMesh().

     \details
        dP/du = +X regardless of position on a flat linear grid; see the
        file header for the tangent w-sign convention.
     *************************************************************************/
    inline MeshData MakePlane(float width = 1.0f, float depth = 1.0f,
        uint32_t widthSegments = 1, uint32_t depthSegments = 1,
        glm::vec3 color = { 0.8f, 0.8f, 0.8f }) {
        widthSegments = std::max(widthSegments, 1u);
        depthSegments = std::max(depthSegments, 1u);

        constexpr glm::vec4 kTangent{ 1.0f, 0.0f, 0.0f, -1.0f };

        MeshData mesh;
        const uint32_t cols = widthSegments + 1;
        const uint32_t rows = depthSegments + 1;
        mesh.vertices.reserve(size_t{ cols } * rows);

        for (uint32_t row = 0; row < rows; ++row) {
            const float v = static_cast<float>(row) / static_cast<float>(depthSegments);
            const float z = (v - 0.5f) * depth;
            for (uint32_t col = 0; col < cols; ++col) {
                const float u = static_cast<float>(col) / static_cast<float>(widthSegments);
                const float x = (u - 0.5f) * width;
                mesh.vertices.push_back(Detail::MakeVertex({ x, 0.0f, z }, { 0.0f, 1.0f, 0.0f }, { u, v }, color, kTangent));
            }
        }

        mesh.indices.reserve(size_t{ widthSegments } * depthSegments * 6);
        for (uint32_t row = 0; row < depthSegments; ++row) {
            for (uint32_t col = 0; col < widthSegments; ++col) {
                const uint32_t i0 = row * cols + col;
                const uint32_t i1 = i0 + 1;
                const uint32_t i2 = i0 + cols;
                const uint32_t i3 = i2 + 1;
                mesh.indices.insert(mesh.indices.end(), { i0, i2, i1,  i1, i2, i3 });
            }
        }
        return mesh;
    }

    /*************************************************************************
     \brief
        Builds a flat circle in the XZ plane, normal +Y: a centre vertex
        plus a ring, fan-triangulated.

     \param[in] radius   Disc radius.
     \param[in] segments Number of ring segments (clamped >= 3).
     \param[in] color    Vertex color tint applied to every vertex.
     \return A CPU-side MeshData ready for Renderer::CreateMesh().
     *************************************************************************/
    inline MeshData MakeDisc(float radius = 0.5f, uint32_t segments = 16,
        glm::vec3 color = { 0.8f, 0.8f, 0.8f }) {
        segments = std::max(segments, 3u);

        MeshData mesh;
        mesh.vertices.reserve(segments + 2);
        const uint32_t centre = 0;
        mesh.vertices.push_back(Detail::MakeVertex({ 0, 0, 0 }, { 0, 1, 0 }, { 0.5f, 0.5f }, color, Detail::kFlatCapTangent));

        for (uint32_t i = 0; i <= segments; ++i) {
            const float phi = (static_cast<float>(i) / static_cast<float>(segments)) * Detail::kTwoPi;
            const float x = radius * std::cos(phi);
            const float z = radius * std::sin(phi);
            const glm::vec2 uv{ 0.5f + 0.5f * std::cos(phi), 0.5f + 0.5f * std::sin(phi) };
            mesh.vertices.push_back(Detail::MakeVertex({ x, 0.0f, z }, { 0, 1, 0 }, uv, color, Detail::kFlatCapTangent));
        }

        mesh.indices.reserve(size_t{ segments } * 3);
        for (uint32_t i = 0; i < segments; ++i) {
            const uint32_t ringI = centre + 1 + i;
            const uint32_t ringJ = centre + 1 + i + 1;
            // Fan facing +Y: (centre, ring[i+1], ring[i]) - see winding note at top of file.
            mesh.indices.insert(mesh.indices.end(), { centre, ringJ, ringI });
        }
        return mesh;
    }

    /*************************************************************************
     \brief
        Builds a cylinder centred at the origin, extending +-height/2 along
        Y.

     \param[in] radius         Cylinder radius.
     \param[in] height         Cylinder height along Y.
     \param[in] radialSegments Number of radial segments (clamped >= 3).
     \param[in] color          Vertex color tint applied to every vertex.
     \return A CPU-side MeshData ready for Renderer::CreateMesh().

     \details
        The side uses analytic radial normals/tangents; the two caps are
        separate flat rings (duplicated verts at the seam) so a cap's +-Y
        normal never blends with the side's radial one.
     *************************************************************************/
    inline MeshData MakeCylinder(float radius = 0.5f, float height = 1.0f, uint32_t radialSegments = 16,
        glm::vec3 color = { 0.8f, 0.8f, 0.8f }) {
        radialSegments = std::max(radialSegments, 3u);
        const float halfH = height * 0.5f;

        MeshData mesh;

        // --- Side (analytic radial normal + azimuthal tangent, shared between the two side rings) ---
        std::vector<uint32_t> bottomRing, topRing;
        bottomRing.reserve(radialSegments + 1);
        topRing.reserve(radialSegments + 1);
        for (uint32_t i = 0; i <= radialSegments; ++i) {
            const float phi = (static_cast<float>(i) / static_cast<float>(radialSegments)) * Detail::kTwoPi;
            const glm::vec3 n{ std::cos(phi), 0.0f, std::sin(phi) };
            // dP/dphi = (-sin, 0, cos); v runs 1(bottom)->0(top) i.e. dP/dv points -Y, and
            // cross(n, tangent) == (0,-1,0) exactly (both are unit already) - handedness w=+1.
            const glm::vec4 tangent{ -std::sin(phi), 0.0f, std::cos(phi), 1.0f };
            const float u = static_cast<float>(i) / static_cast<float>(radialSegments);
            const glm::vec3 posBottom{ radius * n.x, -halfH, radius * n.z };
            const glm::vec3 posTop{ radius * n.x,  halfH, radius * n.z };

            bottomRing.push_back(static_cast<uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(Detail::MakeVertex(posBottom, n, { u, 1.0f }, color, tangent));
            topRing.push_back(static_cast<uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(Detail::MakeVertex(posTop, n, { u, 0.0f }, color, tangent));
        }
        for (uint32_t i = 0; i < radialSegments; ++i) {
            // Lower = bottom ring (smaller Y), Upper = top ring - see AddRingStrip's contract.
            Detail::AddRingStrip(mesh.indices, bottomRing[i], topRing[i], bottomRing[i + 1], topRing[i + 1]);
        }

        // --- Bottom cap (-Y, flat, own vertices so the normal doesn't blend with the side) ---
        {
            const uint32_t centre = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(Detail::MakeVertex({ 0, -halfH, 0 }, { 0, -1, 0 }, { 0.5f, 0.5f }, color, Detail::kFlatCapTangent));
            const uint32_t ringStart = static_cast<uint32_t>(mesh.vertices.size());
            for (uint32_t i = 0; i <= radialSegments; ++i) {
                const float phi = (static_cast<float>(i) / static_cast<float>(radialSegments)) * Detail::kTwoPi;
                const glm::vec3 pos{ radius * std::cos(phi), -halfH, radius * std::sin(phi) };
                const glm::vec2 uv{ 0.5f + 0.5f * std::cos(phi), 0.5f + 0.5f * std::sin(phi) };
                mesh.vertices.push_back(Detail::MakeVertex(pos, { 0, -1, 0 }, uv, color, Detail::kFlatCapTangent));
            }
            for (uint32_t i = 0; i < radialSegments; ++i) {
                // Fan facing -Y: (centre, ring[i], ring[i+1]).
                mesh.indices.insert(mesh.indices.end(), { centre, ringStart + i, ringStart + i + 1 });
            }
        }

        // --- Top cap (+Y) ---
        {
            const uint32_t centre = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(Detail::MakeVertex({ 0, halfH, 0 }, { 0, 1, 0 }, { 0.5f, 0.5f }, color, Detail::kFlatCapTangent));
            const uint32_t ringStart = static_cast<uint32_t>(mesh.vertices.size());
            for (uint32_t i = 0; i <= radialSegments; ++i) {
                const float phi = (static_cast<float>(i) / static_cast<float>(radialSegments)) * Detail::kTwoPi;
                const glm::vec3 pos{ radius * std::cos(phi), halfH, radius * std::sin(phi) };
                const glm::vec2 uv{ 0.5f + 0.5f * std::cos(phi), 0.5f + 0.5f * std::sin(phi) };
                mesh.vertices.push_back(Detail::MakeVertex(pos, { 0, 1, 0 }, uv, color, Detail::kFlatCapTangent));
            }
            for (uint32_t i = 0; i < radialSegments; ++i) {
                // Fan facing +Y: (centre, ring[i+1], ring[i]).
                mesh.indices.insert(mesh.indices.end(), { centre, ringStart + i + 1, ringStart + i });
            }
        }
        return mesh;
    }

    /*************************************************************************
     \brief
        Builds a cone: base circle at y = -height/2 (-Y cap), apex at
        y = +height/2.

     \param[in] radius         Base radius.
     \param[in] height         Cone height along Y.
     \param[in] radialSegments Number of radial segments (clamped >= 3).
     \param[in] color          Vertex color tint applied to every vertex.
     \return A CPU-side MeshData ready for Renderer::CreateMesh().

     \details
        The apex is duplicated once per radial wedge so each side triangle
        gets the correct smoothly-varying analytic normal/tangent right up
        to the tip (no averaged/singular normal at the point).
     *************************************************************************/
    inline MeshData MakeCone(float radius = 0.5f, float height = 1.0f, uint32_t radialSegments = 16,
        glm::vec3 color = { 0.8f, 0.8f, 0.8f }) {
        radialSegments = std::max(radialSegments, 3u);
        const float halfH = height * 0.5f;

        MeshData mesh;

        // Side surface normal at azimuth phi (independent of position along the slant for a
        // right circular cone): n = normalize(cos(phi) * height, radius, sin(phi) * height).
        auto sideNormal = [&](float phi) {
            return glm::normalize(glm::vec3{ std::cos(phi) * height, radius, std::sin(phi) * height });
        };

        std::vector<uint32_t> baseRing, apexRing;
        baseRing.reserve(radialSegments + 1);
        apexRing.reserve(radialSegments + 1);
        for (uint32_t i = 0; i <= radialSegments; ++i) {
            const float phi = (static_cast<float>(i) / static_cast<float>(radialSegments)) * Detail::kTwoPi;
            const glm::vec3 n = sideNormal(phi);
            // Tangent is still the pure azimuthal direction (-sin,0,cos) - the slant only changes
            // the normal, not the direction of increasing U around the cone. v runs 1 (base) -> 0
            // (apex), same convention as the cylinder, so bitangent = dP/dv must point from apex
            // toward base (NOT "up the slant toward the apex" - that's -dP/dv, since v decreases
            // going that way; an earlier draft of this derivation mixed the two up). With that,
            // cross(n, tangent) already points apex-to-base, matching the cylinder's w=+1 exactly
            // (confirmed by a finite-difference check against the actual generated vertices).
            const glm::vec4 tangent{ -std::sin(phi), 0.0f, std::cos(phi), 1.0f };
            const float u = static_cast<float>(i) / static_cast<float>(radialSegments);
            const glm::vec3 basePos{ radius * std::cos(phi), -halfH, radius * std::sin(phi) };
            const glm::vec3 apexPos{ 0.0f, halfH, 0.0f };

            baseRing.push_back(static_cast<uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(Detail::MakeVertex(basePos, n, { u, 1.0f }, color, tangent));
            apexRing.push_back(static_cast<uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(Detail::MakeVertex(apexPos, n, { u, 0.0f }, color, tangent));
        }
        for (uint32_t i = 0; i < radialSegments; ++i) {
            // Same (Lower, Upper, Lower+1) triangle as the cylinder side; the second triangle of
            // the strip is skipped since the apex is a single point, not a second ring.
            mesh.indices.insert(mesh.indices.end(), { baseRing[i], apexRing[i], baseRing[i + 1] });
        }

        // --- Base cap (-Y) ---
        {
            const uint32_t centre = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(Detail::MakeVertex({ 0, -halfH, 0 }, { 0, -1, 0 }, { 0.5f, 0.5f }, color, Detail::kFlatCapTangent));
            const uint32_t ringStart = static_cast<uint32_t>(mesh.vertices.size());
            for (uint32_t i = 0; i <= radialSegments; ++i) {
                const float phi = (static_cast<float>(i) / static_cast<float>(radialSegments)) * Detail::kTwoPi;
                const glm::vec3 pos{ radius * std::cos(phi), -halfH, radius * std::sin(phi) };
                const glm::vec2 uv{ 0.5f + 0.5f * std::cos(phi), 0.5f + 0.5f * std::sin(phi) };
                mesh.vertices.push_back(Detail::MakeVertex(pos, { 0, -1, 0 }, uv, color, Detail::kFlatCapTangent));
            }
            for (uint32_t i = 0; i < radialSegments; ++i) {
                mesh.indices.insert(mesh.indices.end(), { centre, ringStart + i, ringStart + i + 1 });
            }
        }
        return mesh;
    }

    /*************************************************************************
     \brief
        Builds a simple flat-faced 4-sided (square-base) pyramid.

     \param[in] baseSize Side length of the square base.
     \param[in] height   Pyramid height along Y.
     \param[in] color    Vertex color tint applied to every vertex.
     \return A CPU-side MeshData ready for Renderer::CreateMesh().

     \details
        Unlike the round shapes this is deliberately faceted (like the
        cube): each of the 5 faces gets its own duplicated vertices and a
        proper cross-product face normal, no "resolution" parameter
        needed.
     *************************************************************************/
    inline MeshData MakePyramid(float baseSize = 1.0f, float height = 1.0f,
        glm::vec3 color = { 0.8f, 0.8f, 0.8f }) {
        const float h = baseSize * 0.5f;
        const float halfHeight = height * 0.5f;

        // Base corners, consistently ordered going around the square.
        const glm::vec3 b0{ -h, -halfHeight, -h };
        const glm::vec3 b1{ h, -halfHeight, -h };
        const glm::vec3 b2{ h, -halfHeight,  h };
        const glm::vec3 b3{ -h, -halfHeight,  h };
        const glm::vec3 apex{ 0.0f, halfHeight, 0.0f };

        MeshData mesh;
        mesh.vertices.reserve(4 + 4 * 3);

        // --- Base (-Y); u = b0->b1 (+X), v = b0->b3 (+Z): dP/du=+X, cross(normal,tangent)=+Z
        // matches dP/dv=+Z exactly, so w=+1 (independently derived from the cap-fan convention
        // above, which used an arbitrary fixed tangent - this one instead follows the base's own
        // real grid UV, since a 4-corner quad isn't polar like the round caps are).
        const uint32_t base0 = static_cast<uint32_t>(mesh.vertices.size());
        const glm::vec4 baseTangent{ 1.0f, 0.0f, 0.0f, 1.0f };
        mesh.vertices.push_back(Detail::MakeVertex(b0, { 0, -1, 0 }, { 0, 0 }, color, baseTangent));
        mesh.vertices.push_back(Detail::MakeVertex(b1, { 0, -1, 0 }, { 1, 0 }, color, baseTangent));
        mesh.vertices.push_back(Detail::MakeVertex(b2, { 0, -1, 0 }, { 1, 1 }, color, baseTangent));
        mesh.vertices.push_back(Detail::MakeVertex(b3, { 0, -1, 0 }, { 0, 1 }, color, baseTangent));
        // Split along the b0-b2 diagonal (NOT b1-b3) so the two triangles share that edge and
        // tile the square exactly once - a mismatched diagonal here would double up one strip of
        // the base while leaving the other uncovered.
        mesh.indices.insert(mesh.indices.end(), {
            base0 + 0, base0 + 1, base0 + 2,
            base0 + 0, base0 + 2, base0 + 3
            });

        // --- Sides (one flat-shaded triangle each, own normal/tangent + duplicated verts) ---
        const glm::vec3 corners[4] = { b0, b1, b2, b3 };
        for (int i = 0; i < 4; ++i) {
            const glm::vec3& cA = corners[i];
            const glm::vec3& cB = corners[(i + 1) % 4];
            // Verified winding: (cA, apex, cB) -> cross(apex - cA, cB - cA) points outward.
            const glm::vec3 normal = glm::normalize(glm::cross(apex - cA, cB - cA));
            // UV: cA=(0,0), apex=(0.5,1), cB=(1,0) - since cA and cB share v=0, the standard
            // tangent-from-UV solve reduces to T = normalize(cB-cA) directly (no apex term
            // survives); handedness w=-1 confirmed by a concrete cross-product check against the
            // "up the face toward apex" bitangent direction (see design notes).
            const glm::vec3 tangentDir = glm::normalize(cB - cA);
            const glm::vec4 tangent{ tangentDir, -1.0f };
            const uint32_t idx = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(Detail::MakeVertex(cA, normal, { 0, 0 }, color, tangent));
            mesh.vertices.push_back(Detail::MakeVertex(apex, normal, { 0.5f, 1 }, color, tangent));
            mesh.vertices.push_back(Detail::MakeVertex(cB, normal, { 1, 0 }, color, tangent));
            mesh.indices.insert(mesh.indices.end(), { idx, idx + 1, idx + 2 });
        }
        return mesh;
    }

    /*************************************************************************
     \brief
        Builds a standard UV sphere.

     \param[in] longitudeSegments Number of longitude segments (clamped >= 3).
     \param[in] latitudeSegments  Number of latitude segments (clamped >= 2).
     \param[in] radius            Sphere radius.
     \param[in] color             Vertex color tint applied to every vertex.
     \return A CPU-side MeshData ready for Renderer::CreateMesh().

     \details
        Poles collapse to a single position per row (still get a correct
        analytic normal/tangent each); the ring-strip formula naturally
        produces a couple of harmless zero-area triangles there instead of
        needing a special-cased fan.
     *************************************************************************/
    inline MeshData MakeSphere(float radius = 0.5f, uint32_t longitudeSegments = 16, uint32_t latitudeSegments = 8,
        glm::vec3 color = { 0.8f, 0.8f, 0.8f }) {
        longitudeSegments = std::max(longitudeSegments, 3u);
        latitudeSegments = std::max(latitudeSegments, 2u);

        MeshData mesh;
        mesh.vertices.reserve(size_t{ longitudeSegments + 1 } * (latitudeSegments + 1));

        std::vector<std::vector<uint32_t>> rows(latitudeSegments + 1);
        for (uint32_t lat = 0; lat <= latitudeSegments; ++lat) {
            const float theta = (static_cast<float>(lat) / static_cast<float>(latitudeSegments)) * 3.14159265358979323846f;
            const float y = radius * std::cos(theta);
            const float ringRadius = radius * std::sin(theta);
            rows[lat].reserve(longitudeSegments + 1);
            for (uint32_t lon = 0; lon <= longitudeSegments; ++lon) {
                const float phi = (static_cast<float>(lon) / static_cast<float>(longitudeSegments)) * Detail::kTwoPi;
                const glm::vec3 pos{ ringRadius * std::cos(phi), y, ringRadius * std::sin(phi) };
                const glm::vec3 n = (radius > 0.0f) ? (pos / radius) : glm::vec3{ 0, 1, 0 };
                // Same azimuthal-tangent / w=+1 derivation as the cylinder side (v also runs
                // "north pole -> south pole" i.e. decreasing Y as v increases, identically).
                const glm::vec4 tangent{ -std::sin(phi), 0.0f, std::cos(phi), 1.0f };
                const glm::vec2 uv{ static_cast<float>(lon) / static_cast<float>(longitudeSegments),
                                     static_cast<float>(lat) / static_cast<float>(latitudeSegments) };
                rows[lat].push_back(static_cast<uint32_t>(mesh.vertices.size()));
                mesh.vertices.push_back(Detail::MakeVertex(pos, n, uv, color, tangent));
            }
        }

        for (uint32_t lat = 0; lat < latitudeSegments; ++lat) {
            // Row `lat` has smaller theta -> larger Y -> it is the "Upper" ring; row `lat+1` is
            // "Lower". AddRingStrip wants (lower, upper) per column.
            for (uint32_t lon = 0; lon < longitudeSegments; ++lon) {
                Detail::AddRingStrip(mesh.indices,
                    rows[lat + 1][lon], rows[lat][lon],
                    rows[lat + 1][lon + 1], rows[lat][lon + 1]);
            }
        }
        return mesh;
    }

    /*************************************************************************
     \brief
        Builds a capsule: a cylindrical body with two hemispherical caps.

     \param[in] radius         Capsule radius.
     \param[in] cylinderHeight Height of the straight cylindrical body
                                (excludes the two hemispherical caps).
     \param[in] radialSegments Number of radial segments (clamped >= 3).
     \param[in] capSegments    Number of latitude segments per hemisphere
                                cap (clamped >= 1).
     \param[in] color          Vertex color tint applied to every vertex.
     \return A CPU-side MeshData ready for Renderer::CreateMesh().

     \details
        Built as a single latitude sweep (top hemisphere, then bottom
        hemisphere) so the middle "cylinder" band falls out for free: a
        hemisphere's own equator row already has a purely radial normal,
        so connecting the top hemisphere's equator ring straight to the
        bottom hemisphere's equator ring reproduces an exact cylindrical
        side with a fully continuous normal field - no seam duplication
        needed anywhere on a capsule, unlike the cylinder/cone. Same
        reasoning applies to the tangent: it's the sphere's azimuthal
        formula throughout, unaffected by the per-hemisphere Y offset.
     *************************************************************************/
    inline MeshData MakeCapsule(float radius = 0.5f, float cylinderHeight = 1.0f,
        uint32_t radialSegments = 16, uint32_t capSegments = 8,
        glm::vec3 color = { 0.8f, 0.8f, 0.8f }) {
        radialSegments = std::max(radialSegments, 3u);
        capSegments = std::max(capSegments, 1u);
        const float halfCyl = cylinderHeight * 0.5f;
        constexpr float kHalfPi = 1.57079632679489661923f;

        MeshData mesh;

        auto addRing = [&](float yOffset, float theta) {
            const float y = yOffset + radius * std::cos(theta);
            const float ringRadius = radius * std::sin(theta);
            std::vector<uint32_t> ring;
            ring.reserve(radialSegments + 1);
            for (uint32_t i = 0; i <= radialSegments; ++i) {
                const float phi = (static_cast<float>(i) / static_cast<float>(radialSegments)) * Detail::kTwoPi;
                const glm::vec3 pos{ ringRadius * std::cos(phi), y, ringRadius * std::sin(phi) };
                const glm::vec3 n{ std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi) };
                const glm::vec4 tangent{ -std::sin(phi), 0.0f, std::cos(phi), 1.0f };
                const glm::vec2 uv{ static_cast<float>(i) / static_cast<float>(radialSegments), y / (radius + halfCyl) };
                ring.push_back(static_cast<uint32_t>(mesh.vertices.size()));
                mesh.vertices.push_back(Detail::MakeVertex(pos, n, uv, color, tangent));
            }
            return ring;
            };

        std::vector<std::vector<uint32_t>> rows;
        rows.reserve(2 * capSegments + 1);

        // Top hemisphere: theta 0 (pole) .. pi/2 (equator), centred at +halfCyl.
        for (uint32_t row = 0; row <= capSegments; ++row) {
            const float theta = (static_cast<float>(row) / static_cast<float>(capSegments)) * kHalfPi;
            rows.push_back(addRing(halfCyl, theta));
        }
        // Bottom hemisphere: theta pi/2 (equator) .. pi (pole), centred at -halfCyl. Row 0 here
        // (the equator) is skipped - it is identical to the top hemisphere's last row, and
        // connecting straight to it is exactly what produces the cylindrical side band.
        for (uint32_t row = 1; row <= capSegments; ++row) {
            const float theta = kHalfPi + (static_cast<float>(row) / static_cast<float>(capSegments)) * kHalfPi;
            rows.push_back(addRing(-halfCyl, theta));
        }

        for (size_t r = 0; r + 1 < rows.size(); ++r) {
            // Row r has the smaller theta of the pair -> larger Y -> "Upper"; row r+1 -> "Lower".
            for (uint32_t col = 0; col < radialSegments; ++col) {
                Detail::AddRingStrip(mesh.indices,
                    rows[r + 1][col], rows[r][col],
                    rows[r + 1][col + 1], rows[r][col + 1]);
            }
        }
        return mesh;
    }

}   // namespace Crucible::Primitives
