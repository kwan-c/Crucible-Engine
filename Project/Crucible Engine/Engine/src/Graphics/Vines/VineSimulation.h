#pragma once
/*************************************************************************
* \file:         VineSimulation.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          16 October 2026
* \version       1.0
* \brief
*                One Vine component's strands: where they should be, how they
*                move, and the triangles that draw them. No ECS and no GPU
*                here - VineSystem feeds it transforms and uploads what it
*                builds - so it can be tested on its own.
*
* \details
*                Each strand is a chain of points (a rope) simulated with
*                Verlet integration, pulled every step towards a REST CURVE
*                rebuilt every frame from the root and the target:
*
*                    root ring -> arch (cubic Bezier) -> coil (helix around
*                    the target's local Y) -> tendril (a tightening curl)
*
*                How hard each point is pulled depends on the part it is in
*                (the coil clings, the arch can hang, the tendril floats), and
*                the whole rope is first carried along with the curve's own
*                movement by (1 - inertia), so a moving weapon drags its
*                vines instead of leaving them behind. Distance constraints
*                keep the rope's length; a cylinder push-out keeps the strands
*                outside the weapon.
*
*                Growing is a reveal: the rope always spans the whole curve,
*                and only the first `growth` of it is drawn (with a tapered
*                tip), so growing, unwinding and regrowing never have to
*                create or move points.
*
*                Phases (per Vine, all strands together):
*                  - Holding:   grows towards fully wrapped while it has a
*                               target. A new target, or `attached` off,
*                               starts letting go (Vine::letGo).
*                  - Unwinding: retracts back to the root, then takes the
*                               new target (if any) and grows again.
*                  - Snapped:   lets go of everything (the rope hangs from
*                               its root), withers, then starts over.
*                A target that disappears always snaps.
*
* \history
*    -v1.0       initial implementation (proof of concept)
**************************************************************************/

#include "ECS/Components/Components.h"      // Vine
#include "Graphics/Vulkan/RenderLayouts.h"  // Vertex
#include "Math/Math.h"

#include <cstdint>
#include <vector>

namespace Crucible {

    /*************************************************************************
     \brief  Where a vine coils on its target, in world space (metres, unit
             axes), worked out from the target's transform and mesh bounds
             by MakeVineGrip().
     *************************************************************************/
    struct VineGrip {
        glm::vec3 base{ 0.0f };                 //!< point on the target's axis where the wrapped part starts (wrapFrom)
        glm::vec3 axis{ 0.0f, 1.0f, 0.0f };     //!< towards wrapTo
        glm::vec3 side{ 1.0f, 0.0f, 0.0f };     //!< perpendicular to axis (the target's local X)
        glm::vec3 front{ 0.0f, 0.0f, 1.0f };    //!< cross(side, axis): completes the frame
        float     length = 0.4f;                //!< metres from base along axis to wrapTo
        float     radius = 0.05f;               //!< coil centre-line radius
        float     coreRadius = 0.035f;          //!< the target's own half-width (strands stay outside it)
        float     extentLow = -0.2f;            //!< the target's whole body along the axis, relative to base
        float     extentHigh = 0.6f;
    };

    /*************************************************************************
     \brief  Builds the grip for a target.
     \param[in] targetMatrix  The target's world matrix (scale included).
     \param[in] boundsMin     Its mesh's object-space box (pass a default rod,
     \param[in] boundsMax       e.g. (-0.03,-0.5,-0.03)..(0.03,0.5,0.03), if
                                it has no mesh).
     \param[in] settings      wrapFrom / wrapTo / wrapRadius / thickness.
     *************************************************************************/
    VineGrip MakeVineGrip(const glm::mat4& targetMatrix, const glm::vec3& boundsMin, const glm::vec3& boundsMax, const Vine& settings);

    /*************************************************************************
     \brief  See the file header.
     *************************************************************************/
    class VineSimulation {
    public:
        enum class Phase : std::uint8_t { Holding, Unwinding, Snapped };

        /// What the simulation needs to know about the world this frame.
        struct Inputs {
            glm::mat4     root{ 1.0f };           //!< the Vine entity's world matrix
            std::uint64_t desiredKey = 0;         //!< the target it should hold (0 = none); any stable non-zero id
            VineGrip      desired;                //!< its grip (when desiredKey != 0)
            bool          boundAlive = false;     //!< the target it holds now (BoundKey()) still exists
            bool          boundGripValid = false; //!< ...and `bound` is its grip this frame (else the last one is kept)
            VineGrip      bound;
        };

        /*********************************************************************
         \brief  Advances the phases and the strands by dt seconds.
         \param[in] time  Seconds since some fixed start (drives the sway).
         *********************************************************************/
        void Update(const Vine& settings, const Inputs& inputs, float dt, float time);

        /*********************************************************************
         \brief  Appends the visible strands (stemVertices/stemIndices) and
                 their leaves (leafVertices/leafIndices), in world space.
                 Nothing is appended for a vine that has not grown at all.
         *********************************************************************/
        void BuildMeshes(const Vine& settings, float time,
                         std::vector<Vertex>& stemVertices, std::vector<uint32_t>& stemIndices,
                         std::vector<Vertex>& leafVertices, std::vector<uint32_t>& leafIndices);

        std::uint64_t BoundKey() const { return m_bound; }   //!< target currently held (0 = none)
        Phase         GetPhase() const { return m_phase; }
        float         Growth() const   { return m_growth; }  //!< 0 = nothing drawn, 1 = fully wrapped
        float         Wither() const   { return m_wither; }  //!< 0 = fresh, 1 = dead (Snapped only)

        // For tests and debug drawing.
        std::size_t                   StrandCount() const { return m_strands.size(); }
        const std::vector<glm::vec3>& StrandPoints(std::size_t strand) const { return m_strands[strand].x; }
        const std::vector<glm::vec3>& StrandRest(std::size_t strand) const { return m_strands[strand].rest; }
        float                         StrandLength(std::size_t strand) const { return m_strands[strand].length; }
        const VineGrip&               CurrentGrip() const { return m_grip; }

    private:
        // The rest curve as a dense polyline with arc lengths.
        struct Path {
            std::vector<glm::vec3> points;
            std::vector<float>     s;
            float bridgeEnd = 0.0f;   // arc length where the coil starts
            float coilEnd = 0.0f;     // ...and ends (the tendril follows)
        };

        struct Strand {
            std::vector<glm::vec3> x, xPrev;     // simulated points (world)
            std::vector<glm::vec3> rest;         // this frame's rest points
            std::vector<glm::vec3> restPrev;     // last frame's (what carrying follows)
            std::vector<float>     clingRate;    // per point, 1/s
            std::vector<float>     swayWeight;   // per point, 0..1
            float length = 0.0f;                 // rest arc length
            float bridgeEnd = 0.0f, coilEnd = 0.0f;
            bool  valid = false;                 // x/xPrev hold a state
            // Per-strand variation (from the seed).
            float rootAngle = 0.0f, coilPhase = 0.0f, radiusScale = 1.0f, turnsScale = 1.0f;
            float thicknessScale = 1.0f, shade = 1.0f;
            float stagger = 0.0f;                // k / strand count: where its coil starts
            float wobblePhase = 0.0f;
            std::uint32_t seed = 1;
        };

        void SetupStrands(const Vine& settings);
        void BuildRest(const Vine& settings, const glm::mat4& root, float time);
        void BuildPath(const Strand& strand, const Vine& settings, const glm::vec3& rootPos, const glm::vec3& rootOut,
                       const glm::vec3& towardCoil, Path& out) const;
        void ResampleRest(Strand& strand, const Path& path, const Vine& settings, float time);
        void Simulate(Strand& strand, const Vine& settings, float dt);
        void StartSnap();
        void ResetRopes();

        void AppendStrandMesh(const Strand& strand, std::size_t index, const Vine& settings, float time,
                              std::vector<Vertex>& stemVertices, std::vector<uint32_t>& stemIndices,
                              std::vector<Vertex>& leafVertices, std::vector<uint32_t>& leafIndices);

        std::vector<Strand> m_strands;
        Path                m_path;               // scratch
        Phase               m_phase = Phase::Holding;
        std::uint64_t       m_bound = 0;          // target held (0 = none)
        VineGrip            m_grip;               // its grip (the last one seen, if it is gone)
        bool                m_hasGrip = false;
        bool                m_started = false;    // first Update() done
        bool                m_resetRopes = true;  // put the ropes back on their rest curve next step
        float               m_growth = 0.0f;
        float               m_wither = 0.0f;
        float               m_cling = 1.0f;       // 0..1, drops to 0 when snapped
        float               m_clingRamp = 1.0f;   // < 1 just after a Slide switch
        int                 m_settingsStrands = -1;
        int                 m_settingsSeed = 0;

        // Dense tube centre-line scratch (BuildMeshes).
        struct Ring { glm::vec3 centre, tangent, normal, binormal; float s; float radius; };
        std::vector<Ring>      m_rings;
        std::vector<glm::vec3> m_control;
        std::vector<float>     m_controlS;
        std::vector<float>     m_restLength;   // Simulate() scratch
    };

}   // NAMESPACE CRUCIBLE
