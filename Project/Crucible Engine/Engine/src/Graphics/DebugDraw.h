#pragma once
/*************************************************************************
* \file:         DebugDraw.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          1 October 2026
* \version       1.0
* \brief
*                Immediate-mode debug line drawing: call DebugDraw::Line /
*                Box / Sphere / Arrow ... from anywhere, any frame, and the
*                lines appear in every view that has debug drawing turned on.
*
* \details
*                Usage (gameplay, systems, tools - no Renderer reference needed):
*
*                    DebugDraw::Line(a, b);                                   // white, this frame only
*                    DebugDraw::Arrow(from, to, { .color = {1, 0, 0, 1} });   // red
*                    DebugDraw::Sphere(hit, 0.2f, { .duration = 2.0f });      // stays 2 seconds
*                    DebugDraw::Box(c, half, rot, { .depthTest = false });    // drawn on top of everything
*
*                Everything is a set of line segments collected into a CPU
*                list; the Renderer uploads that list once per frame and draws
*                it after the meshes and sky (Renderer::UpdateDebugBuffer /
*                DrawDebugLines). duration 0 = this frame only; > 0 = kept for
*                that many seconds (timed lines are aged by EndFrame(dt)).
*
*                Categories let a view hide whole kinds of debug drawing (the
*                Scene view's Gizmos menu toggles them). A call whose category
*                is switched off, or made while nothing is displaying debug
*                lines at all (IsActive() false - e.g. the Game build with
*                debug drawing off), returns immediately, so leaving debug
*                calls in gameplay code costs almost nothing.
*
*                Thread-safe: calls may come from worker-thread systems.
*
*                Colours are given in sRGB (what a colour picker shows); the
*                line shader converts them to linear before the sRGB target
*                re-encodes them, so they come out as written.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "EngineExport.h"

namespace Crucible {

    /*************************************************************************
     \brief  Kinds of debug drawing, as bit flags (toggled per category).
     *************************************************************************/
    enum class DebugCategory : std::uint32_t {
        Colliders  = 1u << 0,   //!< Collider / CharacterController shapes
        Velocities = 1u << 1,   //!< RigidBody / CharacterController velocity arrows
        Lights     = 1u << 2,   //!< light range spheres, spot cones, sun arrows
        Grid       = 1u << 3,   //!< ground grid + world axes
        Selection  = 1u << 4,   //!< selected entity's bounds + axes
        Gameplay   = 1u << 5,   //!< anything gameplay code draws (the default)
        Bounds     = 1u << 6,   //!< every render proxy's culling box (off by default - can be thousands)
        WorldUI    = 1u << 7,   //!< world-space canvases' pages, and the selected element on one
        Audio      = 1u << 8,   //!< audio sources' ranges, audio listeners' facing
        NavGrid    = 1u << 9,   //!< the FlowFieldSurface's outline, cells and blocked cells (ECSNavigationSystem)
        FlowField  = 1u << 10,  //!< the flow field's arrows towards the player, in Play (ECSNavigationSystem)
        Particles  = 1u << 11,  //!< the selected ParticleEmitter's shape: where particles start, which way they go
        All        = 0xFFFFFFFFu
    };

    /*************************************************************************
     \brief  How one debug shape is drawn.
     \details
        - color:     sRGB colour + alpha (alpha is currently ignored - lines
                     are opaque).
        - duration:  seconds to keep it; 0 = this frame only.
        - depthTest: true = hidden behind geometry like a normal object;
                     false = always drawn on top (good for selection).
        - category:  which toggle controls it.
     *************************************************************************/
    struct DebugStyle {
        glm::vec4     color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float         duration = 0.0f;
        bool          depthTest = true;
        DebugCategory category = DebugCategory::Gameplay;
    };

    /*************************************************************************
     \brief  One line-list vertex as the GPU reads it (16 bytes).
     \details position + colour packed RGBA8 (R in the low byte); matches
              DebugLinePass::Create's vertex input and
              DebugLine.slang.
     *************************************************************************/
    struct DebugVertex {
        glm::vec3     position{ 0.0f };
        std::uint32_t color = 0xFFFFFFFFu;
    };
    static_assert(sizeof(DebugVertex) == 16, "DebugVertex must stay 16 bytes (see the line pipeline's vertex input).");

    /*************************************************************************
     \brief  One sphere's outline as the GPU reads it (20 bytes, one per
             instance).
     \details The outline is the circle where the sphere meets what is
              behind it - which circle that is depends on where it is seen
              from, so unlike the lines it can't be stored as points. Each
              view's vertex shader works its own circle out from this
              (DebugLine.slang's sphereOutlineVert, drawn by DebugLinePass);
              matches that pipeline's per-instance vertex input.
     *************************************************************************/
    struct DebugSphere {
        glm::vec3     center{ 0.0f };
        float         radius = 0.0f;
        std::uint32_t color = 0xFFFFFFFFu;
    };
    static_assert(sizeof(DebugSphere) == 20, "DebugSphere must stay 20 bytes (see the sphere outline pipeline's vertex input).");

    /*************************************************************************
     \brief  Static immediate-mode debug drawing API. See the file header.
     *************************************************************************/
    class ENGINE_API DebugDraw {
    public:
        // ---- Primitives ----------------------------------------------------------------------

        /*********************************************************************
         \brief  One line segment.
         \param[in] a,b   World-space end points.
         \param[in] style Colour, duration, depth test, category.
         *********************************************************************/
        static void Line(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style = {});

        /*********************************************************************
         \brief  A line from `from` to `to` with an arrowhead at `to`.
         \param[in] headSize Arrowhead length; <= 0 picks 20% of the arrow
                             (clamped to 0.05..0.5).
         *********************************************************************/
        static void Arrow(const glm::vec3& from, const glm::vec3& to, const DebugStyle& style = {}, float headSize = 0.0f);

        /*********************************************************************
         \brief  An oriented box.
         \param[in] center      World-space centre.
         \param[in] halfExtents Half the size along each local axis.
         \param[in] rotation    Orientation.
         *********************************************************************/
        static void Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation, const DebugStyle& style = {});

        /*********************************************************************
         \brief  An axis-aligned box from its min/max corners.
         *********************************************************************/
        static void AABB(const glm::vec3& min, const glm::vec3& max, const DebugStyle& style = {});

        /*********************************************************************
         \brief  A box given by a local-space min/max and a transform (e.g. a
                 mesh's bounds under its world matrix).
         *********************************************************************/
        static void TransformedBox(const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& transform, const DebugStyle& style = {});

        /*********************************************************************
         \brief  A circle of `radius` around `center`, in the plane whose
                 normal is `normal`.
         \param[in] segments Line segments used (>= 4).
         *********************************************************************/
        static void Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const DebugStyle& style = {}, int segments = 32);

        /*********************************************************************
         \brief  A wire sphere: three great circles (XY, YZ, XZ), plus its
                 outline as each view sees it (Unity's "horizon disc") - the
                 circle where the sphere meets what is behind it, so its
                 size reads at a glance from any angle.
         \details The outline is worked out on the GPU for every view
                  separately (the Scene and Game views each get their own),
                  and is left out when the camera is inside the sphere.
                  `segments` is for the three circles; the outline always
                  has 64.
         *********************************************************************/
        static void Sphere(const glm::vec3& center, float radius, const DebugStyle& style = {}, int segments = 32);

        /*********************************************************************
         \brief  A wire capsule along the rotation's local Y axis.
         \param[in] center     Centre of the capsule.
         \param[in] radius     Cap/cylinder radius.
         \param[in] halfHeight Half the straight middle (caps are extra) -
                               Jolt's convention; a sphere when 0.
         *********************************************************************/
        static void Capsule(const glm::vec3& center, float radius, float halfHeight, const glm::quat& rotation, const DebugStyle& style = {}, int segments = 24);

        /*********************************************************************
         \brief  A wire cone from `apex` along `direction`, `length` long,
                 opening at `halfAngleDeg` (a spot light's shape).
         *********************************************************************/
        static void Cone(const glm::vec3& apex, const glm::vec3& direction, float length, float halfAngleDeg, const DebugStyle& style = {}, int segments = 24);

        /*********************************************************************
         \brief  Red/green/blue X/Y/Z axes of a transform, `size` long.
                 Only style.duration/depthTest/category are used.
         *********************************************************************/
        static void Axes(const glm::mat4& transform, float size = 1.0f, const DebugStyle& style = {});

        /*********************************************************************
         \brief  A square grid on the XZ plane at height `center.y`.
         \param[in] halfCells Cells from the centre to each edge.
         \param[in] spacing   Cell size.
         *********************************************************************/
        static void Grid(const glm::vec3& center, int halfCells, float spacing, const DebugStyle& style = {});

        // ---- Switches ------------------------------------------------------------------------

        /*********************************************************************
         \brief  Turns one category on or off for every view.
         *********************************************************************/
        static void SetCategoryEnabled(DebugCategory category, bool enabled);

        /*********************************************************************
         \brief  Whether a category is on.
         *********************************************************************/
        static bool IsCategoryEnabled(DebugCategory category);

        /*********************************************************************
         \brief  Whether anything will display debug lines this frame (set by
                 the Renderer from its views' settings). When false every
                 call returns immediately.
         *********************************************************************/
        static bool IsActive();

        /*********************************************************************
         \brief  Whether a call in `category` would be recorded right now
                 (active AND category on) - use it to skip expensive debug-only
                 work entirely.
         *********************************************************************/
        static bool ShouldDraw(DebugCategory category);

        // ---- Renderer hooks (engine-internal) ------------------------------------------------

        /*********************************************************************
         \brief  Copies this frame's lines (one-frame + timed) out, split into
                 depth-tested and always-on-top lists.
         *********************************************************************/
        static void Collect(std::vector<DebugVertex>& depthTested, std::vector<DebugVertex>& onTop);

        /*********************************************************************
         \brief  Copies this frame's sphere outlines (one-frame + timed) out,
                 split like Collect().
         *********************************************************************/
        static void CollectSpheres(std::vector<DebugSphere>& depthTested, std::vector<DebugSphere>& onTop);

        /*********************************************************************
         \brief  Drops one-frame lines, ages timed ones by dt, and records
                 whether any view will display debug lines next frame.
         *********************************************************************/
        static void EndFrame(float dt, bool activeNextFrame);

        /*********************************************************************
         \brief  Clears every recorded line (e.g. on scene change).
         *********************************************************************/
        static void Clear();

        /*********************************************************************
         \brief  Packs an sRGB colour into DebugVertex::color.
         *********************************************************************/
        static std::uint32_t PackColor(const glm::vec4& color);
    };

}   // NAMESPACE CRUCIBLE
