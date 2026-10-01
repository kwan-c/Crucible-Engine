#pragma once
/*************************************************************************
* \file:         Bounds.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          1 October 2026
* \version       1.0
* \brief
*                Axis-aligned bounding boxes, view frustums and rays - the
*                shapes the spatial queries (DynamicAABBTree) work with.
*
* \details
*                Header-only and dependency-free apart from glm, so gameplay,
*                AI and physics-side code can use the same types as the
*                Renderer's culling and the editor's picking.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>

namespace Crucible {

    /*************************************************************************
     \brief  An axis-aligned box. A default-constructed box is "empty"
             (min > max) so that Merge()ing points into it works.
     *************************************************************************/
    struct AABB {
        glm::vec3 min{  std::numeric_limits<float>::max() };
        glm::vec3 max{ -std::numeric_limits<float>::max() };

        AABB() = default;
        AABB(const glm::vec3& mn, const glm::vec3& mx) : min(mn), max(mx) {}

        bool      IsValid()  const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
        glm::vec3 Center()   const { return (min + max) * 0.5f; }
        glm::vec3 Extents()  const { return (max - min) * 0.5f; }   //!< half sizes

        //! Surface area: the cost metric the tree minimises (bigger boxes = more wasted tests).
        float SurfaceArea() const {
            const glm::vec3 d = max - min;
            return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
        }

        bool Contains(const AABB& o) const {
            return min.x <= o.min.x && min.y <= o.min.y && min.z <= o.min.z
                && max.x >= o.max.x && max.y >= o.max.y && max.z >= o.max.z;
        }

        bool Overlaps(const AABB& o) const {
            return min.x <= o.max.x && max.x >= o.min.x
                && min.y <= o.max.y && max.y >= o.min.y
                && min.z <= o.max.z && max.z >= o.min.z;
        }

        AABB Expanded(float margin) const { return { min - glm::vec3(margin), max + glm::vec3(margin) }; }

        static AABB Union(const AABB& a, const AABB& b) { return { glm::min(a.min, b.min), glm::max(a.max, b.max) }; }

        //! The world-space box that encloses `local` after `m` (rotation/scale/translation). Uses the
        //! centre/extents form: exact for the transformed box's own AABB, 2 matrix ops, no corners.
        static AABB Transform(const AABB& local, const glm::mat4& m) {
            const glm::vec3 c = glm::vec3(m * glm::vec4(local.Center(), 1.0f));
            const glm::vec3 e = local.Extents();
            const glm::vec3 we(
                std::abs(m[0][0]) * e.x + std::abs(m[1][0]) * e.y + std::abs(m[2][0]) * e.z,
                std::abs(m[0][1]) * e.x + std::abs(m[1][1]) * e.y + std::abs(m[2][1]) * e.z,
                std::abs(m[0][2]) * e.x + std::abs(m[1][2]) * e.y + std::abs(m[2][2]) * e.z);
            return { c - we, c + we };
        }
    };

    /*************************************************************************
     \brief  A ray: origin + t * direction. Direction need not be unit
             length; t is measured in units of |direction|.
     *************************************************************************/
    struct Ray {
        glm::vec3 origin{ 0.0f };
        glm::vec3 direction{ 0.0f, 0.0f, -1.0f };

        /*********************************************************************
         \brief  Slab test against a box.
         \param[in]  box  The box.
         \param[in]  tMax Ignore hits beyond this t.
         \param[out] tHit Entry t (0 if the origin is inside the box).
         \return Whether the ray hits the box within [0, tMax].
         *********************************************************************/
        bool Intersects(const AABB& box, float tMax, float& tHit) const {
            float t0 = 0.0f, t1 = tMax;
            for (int i = 0; i < 3; ++i) {
                const float d = direction[i];
                if (std::abs(d) < 1e-12f) {
                    if (origin[i] < box.min[i] || origin[i] > box.max[i]) { return false; }
                    continue;
                }
                const float inv = 1.0f / d;
                float tNear = (box.min[i] - origin[i]) * inv;
                float tFar  = (box.max[i] - origin[i]) * inv;
                if (tNear > tFar) { std::swap(tNear, tFar); }
                t0 = std::max(t0, tNear);
                t1 = std::min(t1, tFar);
                if (t0 > t1) { return false; }
            }
            tHit = t0;
            return true;
        }
    };

    /*************************************************************************
     \brief  Six planes of a camera's view volume, pointing inwards.
     \details
        Built from a view-projection matrix in Vulkan / GLM_FORCE_DEPTH_ZERO_
        TO_ONE convention (clip z in [0, w]), so the near plane is row 2
        alone - not row 3 + row 2 as in OpenGL-convention tutorials.
     *************************************************************************/
    struct Frustum {
        enum class Result { Outside, Intersects, Inside };

        glm::vec4 planes[6]{};   //!< xyz = inward normal, w = distance; dot(n, p) + w >= 0 inside

        static Frustum FromViewProjection(const glm::mat4& m) {
            // GLM is column-major: row i of the matrix = (m[0][i], m[1][i], m[2][i], m[3][i]).
            auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
            const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
            Frustum f;
            f.planes[0] = r3 + r0;   // left
            f.planes[1] = r3 - r0;   // right
            f.planes[2] = r3 + r1;   // bottom
            f.planes[3] = r3 - r1;   // top
            f.planes[4] = r2;        // near (z >= 0)
            f.planes[5] = r3 - r2;   // far  (z <= w)
            for (glm::vec4& p : f.planes) {
                const float len = glm::length(glm::vec3(p));
                if (len > 0.0f) { p /= len; }
            }
            return f;
        }

        //! Box vs all six planes: Outside as soon as it is wholly behind one; Inside only if wholly in
        //! front of all six. (Conservative: a box near a corner can report Intersects while outside.)
        Result Classify(const AABB& box) const {
            const glm::vec3 c = box.Center(), e = box.Extents();
            Result result = Result::Inside;
            for (const glm::vec4& p : planes) {
                const glm::vec3 n(p);
                const float s = glm::dot(n, c) + p.w;
                const float r = glm::dot(e, glm::abs(n));
                if (s + r < 0.0f) { return Result::Outside; }
                if (s - r < 0.0f) { result = Result::Intersects; }
            }
            return result;
        }

        bool Intersects(const AABB& box) const { return Classify(box) != Result::Outside; }
    };

}   // NAMESPACE CRUCIBLE
