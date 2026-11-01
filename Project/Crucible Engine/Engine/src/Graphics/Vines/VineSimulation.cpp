/*************************************************************************
* \file:         VineSimulation.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          16 October 2026
* \version       1.0
* \brief
*                See VineSimulation.h for the model (rest curve + rope) and
*                the phases.
*
* \history
*    -v1.0       initial implementation (proof of concept)
**************************************************************************/

#include "Graphics/Vines/VineSimulation.h"

#include <algorithm>
#include <cmath>

namespace Crucible {

    namespace {

        constexpr float kPi          = 3.14159265358979f;
        constexpr float kTwoPi       = 6.28318530718f;
        constexpr float kGoldenAngle = 2.39996323f;   // radians: spreads leaves evenly around the stem
        constexpr float kGravity     = 9.81f;

        constexpr int   kMaxStrands   = 8;
        constexpr float kPointSpacing = 0.03f;          // metres between rope points
        constexpr int   kMinPoints    = 12;
        constexpr int   kMaxPoints    = 160;
        constexpr float kStepTime     = 1.0f / 120.0f;  // longest simulation step
        constexpr int   kMaxSteps     = 8;              // per frame (dt is clamped to 1/15 s by Core)
        constexpr int   kIterations   = 3;              // distance-constraint passes per step

        constexpr int   kSides        = 8;              // tube cross-section
        constexpr int   kPieces       = 3;              // Catmull-Rom pieces per rope segment
        constexpr int   kLeafRows     = 6;              // leaf grid: kLeafRows + 1 rows x 3 columns

        glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
            const float lengthSq = glm::dot(v, v);
            return lengthSq > 1e-14f ? v / std::sqrt(lengthSq) : fallback;
        }

        glm::vec3 AnyPerpendicular(const glm::vec3& n) {
            const glm::vec3 reference = std::abs(n.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            return glm::normalize(glm::cross(reference, n));
        }

        // Integer hash (lowbias32): turns (seed, index) into well-mixed bits.
        std::uint32_t Hash(std::uint32_t x) {
            x ^= x >> 16; x *= 0x7feb352du;
            x ^= x >> 15; x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }
        float Rand01(std::uint32_t& state) {
            state = Hash(state + 0x9e3779b9u);
            return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
        }

        float Smooth01(float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        // Inspector colours are sRGB; vertex colours multiply linear albedo.
        glm::vec3 ToLinear(const glm::vec3& srgb) {
            return glm::pow(glm::clamp(srgb, glm::vec3(0.0f), glm::vec3(1.0f)), glm::vec3(2.2f));
        }

        // A matrix's position and unit axes (scale removed, re-orthogonalized; Y kept exact).
        struct Axes { glm::vec3 origin, x, y, z; };
        Axes AxesOf(const glm::mat4& m) {
            Axes a;
            a.origin = glm::vec3(m[3]);
            a.y = SafeNormalize(glm::vec3(m[1]), glm::vec3(0.0f, 1.0f, 0.0f));
            a.x = SafeNormalize(glm::vec3(m[0]) - a.y * glm::dot(glm::vec3(m[0]), a.y), AnyPerpendicular(a.y));
            a.z = glm::cross(a.x, a.y);
            return a;
        }

        glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
            const float t2 = t * t, t3 = t2 * t;
            return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        }

        // Linear resample of a point list to `count` points over the same parameter range.
        void Resample(std::vector<glm::vec3>& points, std::size_t count) {
            if (points.empty() || points.size() == count) { points.resize(count, points.empty() ? glm::vec3(0.0f) : points.back()); return; }
            std::vector<glm::vec3> old;
            old.swap(points);
            points.resize(count);
            const float scale = count > 1 ? static_cast<float>(old.size() - 1) / static_cast<float>(count - 1) : 0.0f;
            for (std::size_t j = 0; j < count; ++j) {
                const float u = static_cast<float>(j) * scale;
                const std::size_t i = std::min(static_cast<std::size_t>(u), old.size() - 1);
                const std::size_t i1 = std::min(i + 1, old.size() - 1);
                points[j] = glm::mix(old[i], old[i1], u - static_cast<float>(i));
            }
        }

        float TangentSign(const glm::vec3& normal, const glm::vec3& tangent, const glm::vec3& bitangentDirection) {
            return glm::dot(glm::cross(normal, tangent), bitangentDirection) < 0.0f ? -1.0f : 1.0f;
        }

    }   // anonymous namespace

    // ------------------------------------------------------------------------------------------------------------- //
    //  GRIP
    // ------------------------------------------------------------------------------------------------------------- //

    VineGrip MakeVineGrip(const glm::mat4& m, const glm::vec3& boundsMin, const glm::vec3& boundsMax, const Vine& settings) {

        const glm::vec3 lo = glm::min(boundsMin, boundsMax);
        const glm::vec3 hi = glm::max(boundsMin, boundsMax);
        const glm::vec3 centre = 0.5f * (lo + hi);
        const Axes      a = AxesOf(m);

        // The mesh's centre line (local Y through the middle of its box), in world space.
        const auto axisPoint = [&](float f) { return glm::vec3(m * glm::vec4(centre.x, lo.y + f * (hi.y - lo.y), centre.z, 1.0f)); };
        const float height = glm::length(axisPoint(1.0f) - axisPoint(0.0f));

        const float from = std::clamp(settings.wrapFrom, 0.0f, 1.0f);
        const float to   = std::clamp(settings.wrapTo, 0.0f, 1.0f);
        const float sign = to < from ? -1.0f : 1.0f;

        VineGrip g;
        g.base   = axisPoint(from);
        g.axis   = a.y * sign;
        g.side   = a.x;
        g.front  = glm::cross(g.side, g.axis);
        g.length = std::abs(to - from) * height;

        // Half-width from the box. A little over the largest half-extent: right for round
        // handles, close enough for square ones (the strands' own thickness covers the corners).
        const float halfX = 0.5f * (hi.x - lo.x) * glm::length(glm::vec3(m[0]));
        const float halfZ = 0.5f * (hi.z - lo.z) * glm::length(glm::vec3(m[2]));
        g.coreRadius = std::max(halfX, halfZ) * 1.12f;
        g.radius     = settings.wrapRadius > 0.0f ? settings.wrapRadius : g.coreRadius + std::max(settings.thickness, 0.0f);

        // The whole body along the axis, relative to base (for keeping strands out of it).
        const float low  = (0.0f - from) * height * sign;
        const float high = (1.0f - from) * height * sign;
        g.extentLow  = std::min(low, high);
        g.extentHigh = std::max(low, high);
        return g;

    }   // MakeVineGrip()

    // ------------------------------------------------------------------------------------------------------------- //
    //  UPDATE
    // ------------------------------------------------------------------------------------------------------------- //

    void VineSimulation::Update(const Vine& settings, const Inputs& in, float dt, float time) {

        dt = std::max(dt, 0.0f);
        SetupStrands(settings);

        // Loaded already wrapped around its target.
        if (!m_started) {
            m_started = true;
            if (settings.startGrown && in.desiredKey != 0) {
                m_bound = in.desiredKey; m_grip = in.desired; m_hasGrip = true;
                m_growth = 1.0f;
                m_resetRopes = true;
            }
        }

        // in.boundAlive / in.bound describe the target held when the caller asked (BoundKey());
        // the wanted target is alive by definition, whichever was asked about.
        const std::uint64_t asked = m_bound;
        const auto heldAlive = [&] {
            return m_bound != 0 && (m_bound == in.desiredKey || (m_bound == asked && in.boundAlive));
        };
        if (m_bound != 0 && m_bound == in.desiredKey)            { m_grip = in.desired; m_hasGrip = true; }
        else if (heldAlive() && in.boundGripValid)                 { m_grip = in.bound;   m_hasGrip = true; }

        const float growRate   = 1.0f / std::max(settings.growTime, 0.05f);
        const float unwindRate = 1.0f / std::max(settings.unwindTime, 0.05f);
        const float witherRate = 1.0f / std::max(settings.witherTime, 0.05f);
        bool skipCarry = false;

        switch (m_phase) {
            case Phase::Holding:
                if (m_bound == 0) {
                    // Nothing held (and nothing drawn): take the target, if there is one.
                    if (in.desiredKey != 0) {
                        m_bound = in.desiredKey; m_grip = in.desired; m_hasGrip = true;
                        m_growth = 0.0f;
                        m_resetRopes = true;
                    }
                } else if (!heldAlive()) {
                    StartSnap();   // the target vanished: nothing to unwind along
                    break;
                } else if (in.desiredKey != m_bound) {
                    if (settings.letGo == VineLetGo::Snap) { StartSnap(); break; }
                    if (settings.letGo == VineLetGo::Slide && in.desiredKey != 0) {
                        // Keep everything grown; the rest curve jumps to the new target and the
                        // strands are pulled over to it softly (m_clingRamp), not carried.
                        m_bound = in.desiredKey; m_grip = in.desired;
                        m_clingRamp = 0.06f;
                        skipCarry = true;
                    } else {
                        m_phase = Phase::Unwinding;
                        break;
                    }
                }
                if (m_bound != 0) { m_growth = std::min(1.0f, m_growth + dt * growRate); }
                break;

            case Phase::Unwinding:
                if (!heldAlive()) { StartSnap(); break; }
                if (in.desiredKey == m_bound) { m_phase = Phase::Holding; break; }   // changed its mind: grow back
                m_growth -= dt * unwindRate;
                if (m_growth <= 0.0f) {
                    m_growth = 0.0f;
                    m_bound = 0;
                    m_phase = Phase::Holding;   // takes the new target next frame
                    m_resetRopes = true;
                }
                break;

            case Phase::Snapped:
                m_cling   = std::max(0.0f, m_cling - dt * 6.0f);
                m_wither += dt * witherRate;
                if (m_wither >= 1.0f) {
                    m_wither = 0.0f;
                    m_growth = 0.0f;
                    m_cling  = 1.0f;
                    m_phase  = Phase::Holding;  // regrows onto whatever is wanted next frame
                    m_resetRopes = true;
                }
                break;
        }
        m_clingRamp = std::min(1.0f, m_clingRamp + dt * 1.6f);

        // Nothing drawn: nothing to simulate. Start on the rest curve when it grows again.
        if (!m_hasGrip || (m_growth <= 0.0f && m_phase != Phase::Snapped)) {
            m_resetRopes = true;
            return;
        }

        BuildRest(settings, in.root, time);
        if (m_resetRopes) { ResetRopes(); m_resetRopes = false; }
        if (skipCarry) { for (Strand& strand : m_strands) { strand.restPrev = strand.rest; } }   // no jump with the curve
        for (Strand& strand : m_strands) { Simulate(strand, settings, dt); }

    }   // VineSimulation::Update()

    void VineSimulation::SetupStrands(const Vine& settings) {

        const int count = std::clamp(settings.strands, 1, kMaxStrands);
        if (count == m_settingsStrands && settings.seed == m_settingsSeed) { return; }
        m_settingsStrands = count;
        m_settingsSeed    = settings.seed;

        m_strands.resize(static_cast<std::size_t>(count));
        for (int k = 0; k < count; ++k) {
            Strand& strand = m_strands[static_cast<std::size_t>(k)];
            std::uint32_t rng = Hash(static_cast<std::uint32_t>(settings.seed) * 2654435761u + static_cast<std::uint32_t>(k) * 40503u + 1u);
            strand.seed           = rng | 1u;
            // Evenly spaced roots; the coils interleave like a multi-start thread (stagger).
            const float even      = kTwoPi * static_cast<float>(k) / static_cast<float>(count);
            strand.rootAngle      = even + (Rand01(rng) - 0.5f) * 0.6f;
            strand.stagger        = static_cast<float>(k) / static_cast<float>(count);
            strand.coilPhase      = (Rand01(rng) - 0.5f) * 0.5f;
            strand.wobblePhase    = Rand01(rng) * kTwoPi;
            // Unit random numbers here; Vine::variation scales them where they are used.
            strand.radiusScale    = Rand01(rng) - 0.5f;
            strand.turnsScale     = Rand01(rng) - 0.5f;
            strand.thicknessScale = Rand01(rng) - 0.5f;
            strand.shade          = Rand01(rng) - 0.5f;
        }

    }   // VineSimulation::SetupStrands()

    void VineSimulation::StartSnap() {

        m_phase  = Phase::Snapped;
        m_bound  = 0;          // keeps m_grip (frozen where it was) for the rest lengths
        m_wither = 0.0f;
        // A small random kick past the arch, so it reads as breaking rather than just going limp.
        for (Strand& strand : m_strands) {
            if (!strand.valid) { continue; }
            std::uint32_t rng = strand.seed ^ 0x5bd1e995u;
            const float ds = strand.length / static_cast<float>(std::max<std::size_t>(strand.x.size() - 1, 1));
            for (std::size_t i = 1; i < strand.x.size(); ++i) {
                if (static_cast<float>(i) * ds < strand.bridgeEnd * 0.5f) { continue; }
                const glm::vec3 kick(Rand01(rng) - 0.5f, Rand01(rng) * 0.5f, Rand01(rng) - 0.5f);
                strand.xPrev[i] -= kick * 0.006f;
            }
        }

    }   // VineSimulation::StartSnap()

    void VineSimulation::ResetRopes() {

        for (Strand& strand : m_strands) { strand.valid = false; }

    }   // VineSimulation::ResetRopes()

    // ------------------------------------------------------------------------------------------------------------- //
    //  REST CURVE
    // ------------------------------------------------------------------------------------------------------------- //

    void VineSimulation::BuildRest(const Vine& settings, const glm::mat4& root, float time) {

        const Axes r = AxesOf(root);
        const glm::vec3 rootCentre = r.origin + r.x * settings.rootOffset.x + r.y * settings.rootOffset.y + r.z * settings.rootOffset.z;
        const glm::vec3 gripCentre = m_grip.base + m_grip.axis * (0.5f * m_grip.length);
        const glm::vec3 toward     = SafeNormalize(gripCentre - rootCentre, r.y);

        // The root ring faces the target; its orientation follows the entity's Y so it doesn't
        // spin when the target moves around.
        const glm::vec3 u = SafeNormalize(r.y - toward * glm::dot(r.y, toward), AnyPerpendicular(toward));
        const glm::vec3 v = glm::cross(toward, u);
        const float ringRadius = std::max(settings.rootRadius, 0.0f);

        for (Strand& strand : m_strands) {
            const glm::vec3 out     = std::cos(strand.rootAngle) * u + std::sin(strand.rootAngle) * v;
            const glm::vec3 rootPos = rootCentre + out * ringRadius;
            BuildPath(strand, settings, rootPos, out, toward, m_path);
            ResampleRest(strand, m_path, settings, time);
        }

    }   // VineSimulation::BuildRest()

    void VineSimulation::BuildPath(const Strand& strand, const Vine& settings, const glm::vec3& rootPos, const glm::vec3& rootOut,
                                   const glm::vec3& toward, Path& out) const {

        const VineGrip& g = m_grip;
        const float variation = std::clamp(settings.variation, 0.0f, 1.0f);
        // Never inside the keep-out cylinder Simulate() pushes the strands out of.
        const float thickness = std::max(settings.thickness, 0.0f);
        const float minRadius = std::max(0.0f, std::min(g.coreRadius, g.radius - thickness)) + thickness;
        const float radius = std::max(g.radius * (1.0f + strand.radiusScale * 0.3f * variation), minRadius);
        const float turns  = settings.turns * (1.0f + strand.turnsScale * 0.25f * variation);

        // The coil starts at the end of the wrapped part nearer the root, on the side facing it.
        const float along   = glm::dot(rootPos - g.base, g.axis);
        const bool  fromTop = along > 0.5f * g.length;
        const glm::vec3 rel = rootPos - (g.base + g.axis * std::clamp(along, 0.0f, g.length));
        const float face    = std::atan2(glm::dot(rel, g.front), glm::dot(rel, g.side));

        // Strands interleave like a multi-start thread: each starts a fraction of a turn further
        // along the axis (still facing the root, so no arch has to cross the target) - or, with
        // too few turns for that, a fraction of a turn further round.
        float offset = 0.0f, start = face + strand.coilPhase;
        if (std::abs(turns) >= 0.5f) { offset = std::min(strand.stagger / std::abs(turns), 0.6f) * g.length; }
        else                         { start += strand.stagger * kTwoPi; }
        const float span      = g.length - offset;
        const float coilTurns = g.length > 1e-5f ? turns * span / g.length : turns;
        const float wobble    = 0.12f * variation;   // irregular pitch and radius: less like a spring

        const auto angleAt = [&](float t) { return start + t * coilTurns * kTwoPi; };
        const auto coil = [&](float t) {
            const float pitch = g.length / std::max(std::abs(turns), 0.5f);
            float y = offset + span * t + wobble * pitch * std::sin(t * 5.0f + strand.wobblePhase) * t * (1.0f - t) * 4.0f;
            y = std::clamp(y, 0.0f, g.length);
            if (fromTop) { y = g.length - y; }
            const float angle = angleAt(t);
            const float r = std::max(radius * (1.0f + wobble * std::sin(t * 7.0f + 1.7f * strand.wobblePhase)), minRadius);
            return g.base + g.axis * y + r * (std::cos(angle) * g.side + std::sin(angle) * g.front);
        };
        const auto radialAt = [&](float t) { return std::cos(angleAt(t)) * g.side + std::sin(angleAt(t)) * g.front; };

        out.points.clear();
        out.s.clear();
        const auto push = [&](const glm::vec3& p) {
            out.s.push_back(out.points.empty() ? 0.0f : out.s.back() + glm::length(p - out.points.back()));
            out.points.push_back(p);
        };

        // Arch: a cubic Bezier leaving the root towards the target (bowed outwards by `arch`)
        // and arriving along the coil's own direction.
        const glm::vec3 c0  = coil(0.0f);
        const glm::vec3 t0  = SafeNormalize(coil(0.01f) - c0, fromTop ? -g.axis : g.axis);
        const float     gap = glm::length(c0 - rootPos);
        const glm::vec3 p0 = rootPos;
        const glm::vec3 p1 = rootPos + SafeNormalize(c0 - rootPos, toward) * (0.35f * gap) + rootOut * settings.arch;
        const glm::vec3 p2 = c0 - t0 * std::min(0.3f * gap, 3.0f * radius) + radialAt(0.0f) * (0.5f * settings.arch);
        // A coil starting on the far side would take the arch straight through the target: where
        // it would, it goes round the outside instead.
        constexpr int kArchSamples = 24;
        for (int i = 0; i < kArchSamples; ++i) {
            const float t = static_cast<float>(i) / kArchSamples, it = 1.0f - t;
            glm::vec3 p = it * it * it * p0 + 3.0f * it * it * t * p1 + 3.0f * it * t * t * p2 + t * t * t * c0;
            const glm::vec3 relative = p - g.base;
            const float axial = glm::dot(relative, g.axis);
            if (i > 0 && axial > g.extentLow - thickness && axial < g.extentHigh + thickness) {
                const glm::vec3 radial = relative - g.axis * axial;
                const float distance = glm::length(radial);
                if (distance < minRadius) { p += SafeNormalize(radial, g.side) * (minRadius - distance); }
            }
            push(p);
        }

        // Coil.
        const int coilSamples = std::max(12, static_cast<int>(std::abs(turns) * 24.0f) + 4);
        for (int i = 0; i <= coilSamples; ++i) { push(coil(static_cast<float>(i) / coilSamples)); }
        out.bridgeEnd = out.s[kArchSamples];
        out.coilEnd   = out.s.back();

        // Tendril: carries on along the coil, then curls away from the target, tighter and
        // tighter (a fiddlehead).
        const float tendril = std::max(settings.tendril, 0.0f);
        if (tendril > 1e-4f) {
            const glm::vec3 end = out.points.back();
            const glm::vec3 tE  = SafeNormalize(end - coil(0.99f), g.axis);
            const glm::vec3 nE  = SafeNormalize(radialAt(1.0f) - tE * glm::dot(radialAt(1.0f), tE), AnyPerpendicular(tE));
            constexpr int   kTendrilSamples = 16;
            constexpr float kCurl = 1.7f * kPi;
            const float ds = tendril / kTendrilSamples;
            const float weightSum = 0.5f * kTendrilSamples * (kTendrilSamples + 1);
            glm::vec3 p = end;
            float angle = 0.0f;
            for (int j = 0; j < kTendrilSamples; ++j) {
                angle += kCurl * static_cast<float>(j + 1) / weightSum;
                p += (std::cos(angle) * tE + std::sin(angle) * nE) * ds;
                push(p);
            }
        }
    }   // VineSimulation::BuildPath()

    void VineSimulation::ResampleRest(Strand& strand, const Path& path, const Vine& settings, float time) {

        const float length = std::max(path.s.back(), 1e-4f);
        const std::size_t count = static_cast<std::size_t>(std::clamp(static_cast<int>(std::ceil(length / kPointSpacing)) + 1, kMinPoints, kMaxPoints));

        // A different point count (the curve got longer or shorter): keep the motion by
        // resampling the state, rather than starting over.
        if (strand.valid && strand.x.size() != count) {
            Resample(strand.x, count);
            Resample(strand.xPrev, count);
            Resample(strand.restPrev, count);
        }
        strand.rest.resize(count);
        strand.clingRate.resize(count);
        strand.swayWeight.resize(count);
        strand.length    = length;
        strand.bridgeEnd = path.bridgeEnd;
        strand.coilEnd   = path.coilEnd;

        const float cling       = std::max(settings.cling, 0.0f);
        const float slack       = std::clamp(settings.slack, 0.0f, 1.0f);
        const float looseCling  = cling * (1.0f - slack) * (1.0f - slack) + 0.4f;   // the arch's middle
        const float tendrilLen  = std::max(length - path.coilEnd, 1e-4f);
        const float ds          = length / static_cast<float>(count - 1);

        // Idle sway moves the rest curve itself (in metres), so it is easy to dial in.
        std::uint32_t rng = strand.seed;
        const float ph1 = Rand01(rng) * kTwoPi, ph2 = Rand01(rng) * kTwoPi, ph3 = Rand01(rng) * kTwoPi;
        const float sway = std::max(settings.sway, 0.0f);

        std::size_t cursor = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const float s = std::min(static_cast<float>(i) * ds, path.s.back());
            while (cursor + 2 < path.s.size() && path.s[cursor + 1] < s) { ++cursor; }
            const float span = path.s[cursor + 1] - path.s[cursor];
            const float t    = span > 1e-7f ? std::clamp((s - path.s[cursor]) / span, 0.0f, 1.0f) : 0.0f;
            glm::vec3 p = glm::mix(path.points[cursor], path.points[cursor + 1], t);

            float rate, swayWeight;
            if (s < path.bridgeEnd) {
                const float hump = std::sin(kPi * s / std::max(path.bridgeEnd, 1e-4f));   // 0 at both ends
                rate = glm::mix(cling, looseCling, hump);
                swayWeight = hump;
            } else if (s <= path.coilEnd) {
                rate = cling;
                swayWeight = 0.08f;
            } else {
                const float f = (s - path.coilEnd) / tendrilLen;
                rate = cling * glm::mix(1.0f, 0.2f, f);
                swayWeight = 0.3f + 0.7f * f;
            }
            if (i == 0) { swayWeight = 0.0f; }

            const float phase = static_cast<float>(i) * 0.33f;
            p += sway * swayWeight * glm::vec3(std::sin(time * 1.3f + phase + ph1),
                                               0.6f * std::sin(time * 1.7f + phase * 0.8f + ph2),
                                               std::sin(time * 1.1f + phase * 1.2f + ph3));
            strand.rest[i]       = p;
            strand.clingRate[i]  = rate;
            strand.swayWeight[i] = swayWeight;
        }

    }   // VineSimulation::ResampleRest()

    // ------------------------------------------------------------------------------------------------------------- //
    //  ROPE
    // ------------------------------------------------------------------------------------------------------------- //

    void VineSimulation::Simulate(Strand& strand, const Vine& settings, float dt) {

        const std::size_t n = strand.rest.size();
        if (!strand.valid || strand.x.size() != n) {
            strand.x = strand.xPrev = strand.restPrev = strand.rest;
            strand.valid = true;
        }

        // Carried along with the rest curve's own movement (the weapon moving, the golem
        // turning), except for the share `inertia` leaves behind. Both positions move, so this
        // adds no velocity.
        const float carry = (1.0f - std::clamp(settings.inertia, 0.0f, 1.0f)) * m_cling;
        for (std::size_t i = 0; i < n; ++i) {
            const glm::vec3 d = (strand.rest[i] - strand.restPrev[i]) * carry;
            strand.x[i] += d;
            strand.xPrev[i] += d;
        }
        strand.restPrev = strand.rest;

        const int steps = dt > 0.0f ? std::clamp(static_cast<int>(std::ceil(dt / kStepTime)), 1, kMaxSteps) : 0;
        const float h   = steps > 0 ? dt / static_cast<float>(steps) : 0.0f;
        const float retain = std::exp(-(m_phase == Phase::Snapped ? 1.0f : 3.0f) * h);   // velocity kept per step
        const glm::vec3 gravityStep(0.0f, -kGravity * std::max(settings.gravity, 0.0f) * h * h, 0.0f);
        const float clingScale = m_cling * m_clingRamp;
        const float thickness = std::max(settings.thickness, 0.0f);

        // Constraint lengths come from the rest curve, so the rope never fights its own shape.
        m_restLength.resize(n > 0 ? n - 1 : 0);
        for (std::size_t i = 0; i + 1 < n; ++i) { m_restLength[i] = glm::length(strand.rest[i + 1] - strand.rest[i]); }

        // Keep-out cylinder: the target's body (only while it is held).
        const bool  collide = m_phase != Phase::Snapped && m_hasGrip;
        const float core    = std::max(0.0f, std::min(m_grip.coreRadius, m_grip.radius - thickness));
        const float keepOut = core + thickness * 0.9f;

        for (int step = 0; step < steps; ++step) {
            for (std::size_t i = 1; i < n; ++i) {
                const glm::vec3 velocity = (strand.x[i] - strand.xPrev[i]) * retain;
                strand.xPrev[i] = strand.x[i];
                strand.x[i] += velocity + gravityStep;
            }
            if (clingScale > 0.0f) {
                for (std::size_t i = 1; i < n; ++i) {
                    const float pull = 1.0f - std::exp(-strand.clingRate[i] * clingScale * h);
                    strand.x[i] += (strand.rest[i] - strand.x[i]) * pull;
                }
            }
            strand.x[0] = strand.rest[0];
            for (int iteration = 0; iteration < kIterations; ++iteration) {
                for (std::size_t i = 0; i + 1 < n; ++i) {
                    const glm::vec3 d = strand.x[i + 1] - strand.x[i];
                    const float length = glm::length(d);
                    if (length < 1e-7f) { continue; }
                    const glm::vec3 correction = d * ((length - m_restLength[i]) / length);
                    if (i == 0) { strand.x[1] -= correction; }   // the root is pinned
                    else        { strand.x[i] += 0.5f * correction; strand.x[i + 1] -= 0.5f * correction; }
                }
            }
            if (collide && keepOut > 0.0f) {
                for (std::size_t i = 1; i < n; ++i) {
                    const glm::vec3 rel = strand.x[i] - m_grip.base;
                    const float along = glm::dot(rel, m_grip.axis);
                    if (along < m_grip.extentLow - thickness || along > m_grip.extentHigh + thickness) { continue; }
                    const glm::vec3 radial = rel - m_grip.axis * along;
                    const float distance = glm::length(radial);
                    if (distance >= keepOut) { continue; }
                    const glm::vec3 outward = distance > 1e-6f ? radial / distance : m_grip.side;
                    strand.x[i] += outward * (keepOut - distance);
                }
            }
            strand.x[0] = strand.xPrev[0] = strand.rest[0];
        }
        if (steps == 0) { strand.x[0] = strand.xPrev[0] = strand.rest[0]; }

    }   // VineSimulation::Simulate()

    // ------------------------------------------------------------------------------------------------------------- //
    //  MESHES
    // ------------------------------------------------------------------------------------------------------------- //

    void VineSimulation::BuildMeshes(const Vine& settings, float time,
                                     std::vector<Vertex>& stemVertices, std::vector<uint32_t>& stemIndices,
                                     std::vector<Vertex>& leafVertices, std::vector<uint32_t>& leafIndices) {

        if (m_growth <= 0.0f || !m_hasGrip) { return; }
        for (std::size_t k = 0; k < m_strands.size(); ++k) {
            if (!m_strands[k].valid) { continue; }
            AppendStrandMesh(m_strands[k], k, settings, time, stemVertices, stemIndices, leafVertices, leafIndices);
        }

    }   // VineSimulation::BuildMeshes()

    void VineSimulation::AppendStrandMesh(const Strand& strand, std::size_t index, const Vine& settings, float time,
                                          std::vector<Vertex>& stemVertices, std::vector<uint32_t>& stemIndices,
                                          std::vector<Vertex>& leafVertices, std::vector<uint32_t>& leafIndices) {

        const std::size_t n = strand.x.size();
        const float visible = m_growth * strand.length;
        if (n < 2 || visible < 0.005f) { return; }
        const float ds = strand.length / static_cast<float>(n - 1);

        // ---- Centre line: the grown part of the rope, ending at the exact tip ----
        m_control.clear();
        m_controlS.clear();
        const std::size_t last = std::min(static_cast<std::size_t>(visible / ds), n - 1);
        for (std::size_t i = 0; i <= last; ++i) { m_control.push_back(strand.x[i]); m_controlS.push_back(static_cast<float>(i) * ds); }
        if (last + 1 < n) {
            const float t = (visible - static_cast<float>(last) * ds) / ds;
            if (t > 0.02f) { m_control.push_back(glm::mix(strand.x[last], strand.x[last + 1], t)); m_controlS.push_back(visible); }
        }
        const std::size_t m = m_control.size();
        if (m < 2) { return; }

        // Catmull-Rom through the points (mirrored end points), a few pieces per segment.
        m_rings.clear();
        for (std::size_t j = 0; j + 1 < m; ++j) {
            const glm::vec3& p1 = m_control[j];
            const glm::vec3& p2 = m_control[j + 1];
            const glm::vec3  p0 = j > 0 ? m_control[j - 1] : 2.0f * p1 - p2;
            const glm::vec3  p3 = j + 2 < m ? m_control[j + 2] : 2.0f * p2 - p1;
            const float s1 = m_controlS[j], s2 = m_controlS[j + 1];
            const int pieces = std::max(1, static_cast<int>(std::round(kPieces * (s2 - s1) / ds)));
            for (int q = 0; q < pieces; ++q) {
                const float t = static_cast<float>(q) / static_cast<float>(pieces);
                m_rings.push_back(Ring{ CatmullRom(p0, p1, p2, p3, t), {}, {}, {}, s1 + (s2 - s1) * t, 0.0f });
            }
        }
        m_rings.push_back(Ring{ m_control.back(), {}, {}, {}, m_controlS.back(), 0.0f });
        const std::size_t rings = m_rings.size();

        // Tangents, then parallel-transported frames (no twisting along the strand).
        for (std::size_t r = 0; r < rings; ++r) {
            const glm::vec3& a = m_rings[r > 0 ? r - 1 : 0].centre;
            const glm::vec3& b = m_rings[std::min(r + 1, rings - 1)].centre;
            m_rings[r].tangent = SafeNormalize(b - a, r > 0 ? m_rings[r - 1].tangent : glm::vec3(0.0f, 1.0f, 0.0f));
        }
        m_rings[0].normal = SafeNormalize(glm::cross(m_rings[0].tangent, glm::vec3(0.0f, 1.0f, 0.0f)), AnyPerpendicular(m_rings[0].tangent));
        for (std::size_t r = 0; r < rings; ++r) {
            Ring& ring = m_rings[r];
            if (r > 0) {
                const glm::vec3& prev = m_rings[r - 1].normal;
                ring.normal = SafeNormalize(prev - ring.tangent * glm::dot(prev, ring.tangent), AnyPerpendicular(ring.tangent));
            }
            ring.binormal = glm::cross(ring.tangent, ring.normal);
        }

        // Radius: thinning towards the end, a short taper to a point at the tip, a slight flare
        // where it leaves the root; thinner as it withers.
        const float variation  = std::clamp(settings.variation, 0.0f, 1.0f);
        const float baseRadius = std::max(settings.thickness, 0.0005f) * (1.0f + strand.thicknessScale * 0.5f * variation);
        const float radius0    = baseRadius * (1.0f - 0.45f * m_wither);
        const float tipFrac    = std::clamp(settings.tipThickness, 0.0f, 1.0f);
        const float taper      = std::min(0.05f, 0.5f * visible);
        for (Ring& ring : m_rings) {
            const float f = ring.s / strand.length;
            ring.radius = radius0 * glm::mix(1.0f, tipFrac, f)
                        * Smooth01((visible - ring.s) / taper)
                        * (1.0f + 0.35f * (1.0f - Smooth01(ring.s / 0.03f)));
        }

        // Colour along the strand (linear), each strand a little lighter or darker.
        const float shade    = 1.0f + strand.shade * 0.25f * variation;
        const glm::vec3 rootC = ToLinear(settings.rootColour) * shade;
        const glm::vec3 tipC  = ToLinear(settings.tipColour) * shade;
        const glm::vec3 deadC = ToLinear(settings.witherColour);
        const float dead      = Smooth01(m_wither * 1.4f);

        // ---- Tube ----
        const uint32_t first = static_cast<uint32_t>(stemVertices.size());
        const float vScale = 1.0f / (kTwoPi * baseRadius);   // square texels: one U repeat = one circumference of V
        for (std::size_t r = 0; r < rings; ++r) {
            const Ring& ring = m_rings[r];
            const glm::vec3 colour = glm::mix(glm::mix(rootC, tipC, Smooth01(ring.s / strand.length)), deadC, dead);
            for (int a = 0; a <= kSides; ++a) {
                const float angle = kTwoPi * static_cast<float>(a) / kSides;
                const float c = std::cos(angle), s = std::sin(angle);
                const glm::vec3 normal  = c * ring.normal + s * ring.binormal;
                const glm::vec3 tangent = -s * ring.normal + c * ring.binormal;   // around (+U)
                stemVertices.push_back(Vertex{ ring.centre + normal * ring.radius, normal,
                                               glm::vec2(static_cast<float>(a) / kSides, ring.s * vScale), colour,
                                               glm::vec4(tangent, TangentSign(normal, tangent, ring.tangent)) });
            }
        }
        // Counter-clockwise seen from outside: (around, along) is a right-handed pair with the
        // outward normal.
        const uint32_t stride = kSides + 1;
        for (uint32_t r = 0; r + 1 < rings; ++r) {
            for (uint32_t a = 0; a < kSides; ++a) {
                const uint32_t v00 = first + r * stride + a, v01 = v00 + 1, v10 = v00 + stride, v11 = v10 + 1;
                stemIndices.insert(stemIndices.end(), { v00, v01, v11, v00, v11, v10 });
            }
        }
        // Root cap (usually buried in the golem, but never shows a hole).
        {
            const Ring& ring = m_rings.front();
            const uint32_t centre = static_cast<uint32_t>(stemVertices.size());
            const glm::vec3 colour = glm::mix(rootC, deadC, dead);
            stemVertices.push_back(Vertex{ ring.centre, -ring.tangent, glm::vec2(0.5f, 0.0f), colour, glm::vec4(ring.normal, 1.0f) });
            for (uint32_t a = 0; a < kSides; ++a) {
                const uint32_t capFirst = static_cast<uint32_t>(stemVertices.size());
                for (uint32_t e = 0; e < 2; ++e) {
                    Vertex v = stemVertices[first + a + e];
                    v.normal = -ring.tangent;
                    v.tangent = glm::vec4(ring.normal, 1.0f);
                    stemVertices.push_back(v);
                }
                stemIndices.insert(stemIndices.end(), { centre, capFirst + 1, capFirst });
            }
        }

        // ---- Leaves ----
        const float spacing = settings.leafSpacing;
        if (spacing < 0.005f || settings.leafSize <= 0.0f) { return; }
        const glm::vec3 leafBase = ToLinear(settings.leafColour);
        const int count = static_cast<int>(strand.length / std::max(spacing, 0.02f));

        for (int l = 0; l < count; ++l) {
            std::uint32_t rng = Hash(strand.seed * 7919u + static_cast<std::uint32_t>(l) * 104729u + static_cast<std::uint32_t>(index) * 31u + 17u);
            const float sl = (static_cast<float>(l) + 0.5f + (Rand01(rng) - 0.5f) * 0.7f) * std::max(spacing, 0.02f);
            const float sizeJitter = 0.75f + 0.5f * Rand01(rng);
            const float spin  = static_cast<float>(l) * kGoldenAngle + Rand01(rng) * 0.8f;
            const float tint  = 0.85f + 0.3f * Rand01(rng);
            const float phase = Rand01(rng) * kTwoPi;
            if (sl < 0.05f || sl > strand.length - 0.02f) { continue; }

            // Unfurls just after the tip has passed (and folds away as it retracts).
            const float grow = Smooth01((visible - sl) / 0.1f);
            if (grow <= 0.001f) { continue; }
            const float size = settings.leafSize * sizeJitter * grow * (1.0f - 0.35f * m_wither);

            // Where on the stem.
            std::size_t r = 0;
            while (r + 2 < rings && m_rings[r + 1].s < sl) { ++r; }
            const Ring& a = m_rings[r];
            const Ring& b = m_rings[std::min(r + 1, rings - 1)];
            const float span = b.s - a.s;
            const float t = span > 1e-6f ? std::clamp((sl - a.s) / span, 0.0f, 1.0f) : 0.0f;
            const glm::vec3 centre = glm::mix(a.centre, b.centre, t);
            const glm::vec3 along  = SafeNormalize(glm::mix(a.tangent, b.tangent, t), a.tangent);
            const glm::vec3 nrm    = SafeNormalize(glm::mix(a.normal, b.normal, t), a.normal);
            const glm::vec3 bin    = glm::cross(along, nrm);
            const float stemRadius = glm::mix(a.radius, b.radius, t);

            // Pointing out from the stem, turned away from the weapon where the stem lies on it.
            glm::vec3 out = std::cos(spin) * nrm + std::sin(spin) * bin;
            if (m_phase != Phase::Snapped) {
                const glm::vec3 rel = centre - m_grip.base;
                const float axial = glm::dot(rel, m_grip.axis);
                const glm::vec3 radial = rel - m_grip.axis * axial;
                const float distance = glm::length(radial);
                if (axial > m_grip.extentLow && axial < m_grip.extentHigh && distance > 1e-5f) {
                    const float closeness = 1.0f - std::clamp((distance - m_grip.coreRadius) / (4.0f * settings.thickness + 0.03f), 0.0f, 1.0f);
                    out = SafeNormalize(out + (radial / distance) * (2.5f * closeness), out);
                    out = SafeNormalize(out - along * glm::dot(out, along), nrm);
                }
            }

            const glm::vec3 blade = SafeNormalize(out * std::cos(0.65f) + along * std::sin(0.65f), out);
            const glm::vec3 side  = SafeNormalize(glm::cross(along, out), AnyPerpendicular(blade));
            const glm::vec3 up    = glm::cross(side, blade);   // the leaf's front
            const glm::vec3 start = centre + out * (stemRadius * 0.85f);
            const float width = size * 0.42f;
            const float cup   = width * 0.18f;
            const float droop = (std::clamp(settings.leafDroop, 0.0f, 1.0f) * 0.7f + 0.5f * m_wither
                                 + 0.06f * std::sin(time * 2.3f + phase)) * size;

            glm::vec3 grid[kLeafRows + 1][3];
            for (int row = 0; row <= kLeafRows; ++row) {
                const float f = static_cast<float>(row) / kLeafRows;
                const float shape = std::pow(std::max(std::sin(kPi * f), 0.0f), 0.8f) * (1.0f - 0.3f * f);
                for (int col = 0; col < 3; ++col) {
                    const float c = static_cast<float>(col - 1);
                    grid[row][col] = start + blade * (size * f) + side * (c * width * shape) + up * (cup * c * c * shape)
                                   + glm::vec3(0.0f, -1.0f, 0.0f) * (droop * f * f);
                }
            }

            const glm::vec3 colourBase = glm::mix(leafBase * tint, deadC, dead);
            for (int face = 0; face < 2; ++face) {
                const float facing = face == 0 ? 1.0f : -1.0f;
                const uint32_t base = static_cast<uint32_t>(leafVertices.size());
                for (int row = 0; row <= kLeafRows; ++row) {
                    for (int col = 0; col < 3; ++col) {
                        const glm::vec3 dRow = grid[std::min(row + 1, kLeafRows)][col] - grid[std::max(row - 1, 0)][col];
                        const glm::vec3 dCol = grid[row][std::min(col + 1, 2)] - grid[row][std::max(col - 1, 0)];
                        glm::vec3 normal = SafeNormalize(glm::cross(dCol, dRow), up);
                        if (glm::dot(normal, up) < 0.0f) { normal = -normal; }
                        normal *= facing;
                        const glm::vec3 tangent = SafeNormalize(dCol - normal * glm::dot(dCol, normal), side);
                        const glm::vec3 colour  = colourBase * (col == 1 ? 1.12f : 1.0f);
                        leafVertices.push_back(Vertex{ grid[row][col], normal,
                                                       glm::vec2(0.5f * static_cast<float>(col), static_cast<float>(row) / kLeafRows), colour,
                                                       glm::vec4(tangent, TangentSign(normal, tangent, blade)) });
                    }
                }
                for (uint32_t row = 0; row < kLeafRows; ++row) {
                    for (uint32_t col = 0; col < 2; ++col) {
                        const uint32_t v00 = base + row * 3 + col, v01 = v00 + 1, v10 = v00 + 3, v11 = v10 + 1;
                        // Front: (side, blade) is right-handed with `up`. Back: reversed.
                        if (face == 0) { leafIndices.insert(leafIndices.end(), { v00, v01, v11, v00, v11, v10 }); }
                        else           { leafIndices.insert(leafIndices.end(), { v00, v11, v01, v00, v10, v11 }); }
                    }
                }
            }
        }

    }   // VineSimulation::AppendStrandMesh()

}   // NAMESPACE CRUCIBLE
