/*************************************************************************
* \file:         DebugDraw.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          1 October 2026
* \version       1.0
* \brief
*                See DebugDraw.h for the API and how the lines reach the GPU.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "DebugDraw.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace Crucible {

    namespace {

        struct TimedLine {
            DebugVertex a;
            DebugVertex b;
            float       remaining = 0.0f;
            bool        depthTest = true;
        };

        // All state lives in the Engine DLL (function-local static), so Editor, Game and Engine
        // code all draw into the same lists.
        struct State {
            std::mutex                 mutex;
            std::vector<DebugVertex>   frameTested;
            std::vector<DebugVertex>   frameOnTop;
            std::vector<TimedLine>     timed;
            std::atomic<std::uint32_t> categories{ static_cast<std::uint32_t>(DebugCategory::All) & ~static_cast<std::uint32_t>(DebugCategory::Bounds) };
            std::atomic<bool>          active{ true };   // true until the Renderer reports otherwise
        };

        State& GetState() {
            static State state;
            return state;
        }

        constexpr float kTwoPi = 6.28318530718f;

        // Two unit vectors perpendicular to `n` and to each other.
        void Basis(const glm::vec3& n, glm::vec3& t, glm::vec3& b) {
            const glm::vec3 seed = (std::abs(n.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            t = glm::normalize(glm::cross(n, seed));
            b = glm::cross(n, t);
        }

        // Records `count` points as count/2 independent segments, under one lock.
        void Emit(const glm::vec3* points, size_t count, const DebugStyle& style) {

            const std::uint32_t color = DebugDraw::PackColor(style.color);
            State& s = GetState();
            std::lock_guard<std::mutex> lock(s.mutex);

            if (style.duration > 0.0f) {
                for (size_t i = 0; i + 1 < count; i += 2) {
                    s.timed.push_back({ { points[i], color }, { points[i + 1], color }, style.duration, style.depthTest });
                }
                return;
            }

            std::vector<DebugVertex>& out = style.depthTest ? s.frameTested : s.frameOnTop;
            for (size_t i = 0; i + 1 < count; i += 2) {
                out.push_back({ points[i], color });
                out.push_back({ points[i + 1], color });
            }
        }

        bool Wanted(const DebugStyle& style) { return DebugDraw::ShouldDraw(style.category); }

    }   // anonymous namespace

    // ------------------------------------------------------------------------------------------------------------- //
    //  SWITCHES
    // ------------------------------------------------------------------------------------------------------------- //

    std::uint32_t DebugDraw::PackColor(const glm::vec4& c) {
        auto byte = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (byte(c.a) << 24);
    }

    void DebugDraw::SetCategoryEnabled(DebugCategory category, bool enabled) {
        State& s = GetState();
        const std::uint32_t bit = static_cast<std::uint32_t>(category);
        if (enabled) { s.categories.fetch_or(bit); } else { s.categories.fetch_and(~bit); }
    }

    bool DebugDraw::IsCategoryEnabled(DebugCategory category) {
        return (GetState().categories.load() & static_cast<std::uint32_t>(category)) != 0;
    }

    bool DebugDraw::IsActive() { return GetState().active.load(); }

    bool DebugDraw::ShouldDraw(DebugCategory category) { return IsActive() && IsCategoryEnabled(category); }

    // ------------------------------------------------------------------------------------------------------------- //
    //  SHAPES
    // ------------------------------------------------------------------------------------------------------------- //

    void DebugDraw::Line(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style) {
        if (!Wanted(style)) { return; }
        const glm::vec3 p[2] = { a, b };
        Emit(p, 2, style);
    }

    void DebugDraw::Arrow(const glm::vec3& from, const glm::vec3& to, const DebugStyle& style, float headSize) {

        if (!Wanted(style)) { return; }
        const glm::vec3 d = to - from;
        const float length = glm::length(d);
        if (length < 1e-5f) { return; }

        const glm::vec3 dir = d / length;
        const float head = headSize > 0.0f ? headSize : std::clamp(length * 0.2f, 0.05f, 0.5f);
        glm::vec3 t, b;
        Basis(dir, t, b);
        const glm::vec3 base = to - dir * head;
        const float w = head * 0.4f;

        const glm::vec3 p[10] = {
            from, to,
            to, base + t * w,
            to, base - t * w,
            to, base + b * w,
            to, base - b * w,
        };
        Emit(p, 10, style);
    }

    void DebugDraw::Box(const glm::vec3& center, const glm::vec3& h, const glm::quat& rotation, const DebugStyle& style) {

        if (!Wanted(style)) { return; }
        glm::vec3 c[8];
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 local((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
            c[i] = center + rotation * local;
        }
        // Edges between corners differing in exactly one bit.
        const glm::vec3 p[24] = {
            c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7],   // along X
            c[0], c[2], c[1], c[3], c[4], c[6], c[5], c[7],   // along Y
            c[0], c[4], c[1], c[5], c[2], c[6], c[3], c[7],   // along Z
        };
        Emit(p, 24, style);
    }

    void DebugDraw::AABB(const glm::vec3& min, const glm::vec3& max, const DebugStyle& style) {
        Box((min + max) * 0.5f, (max - min) * 0.5f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), style);
    }

    void DebugDraw::TransformedBox(const glm::vec3& mn, const glm::vec3& mx, const glm::mat4& m, const DebugStyle& style) {

        if (!Wanted(style)) { return; }
        glm::vec3 c[8];
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 local((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
            c[i] = glm::vec3(m * glm::vec4(local, 1.0f));
        }
        const glm::vec3 p[24] = {
            c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7],
            c[0], c[2], c[1], c[3], c[4], c[6], c[5], c[7],
            c[0], c[4], c[1], c[5], c[2], c[6], c[3], c[7],
        };
        Emit(p, 24, style);
    }

    void DebugDraw::Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const DebugStyle& style, int segments) {

        if (!Wanted(style) || radius <= 0.0f) { return; }
        const float len = glm::length(normal);
        if (len < 1e-6f) { return; }
        segments = std::max(segments, 4);

        glm::vec3 t, b;
        Basis(normal / len, t, b);
        std::vector<glm::vec3> p;
        p.reserve(static_cast<size_t>(segments) * 2);
        glm::vec3 prev = center + t * radius;
        for (int i = 1; i <= segments; ++i) {
            const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
            const glm::vec3 next = center + (t * std::cos(a) + b * std::sin(a)) * radius;
            p.push_back(prev);
            p.push_back(next);
            prev = next;
        }
        Emit(p.data(), p.size(), style);
    }

    void DebugDraw::Sphere(const glm::vec3& center, float radius, const DebugStyle& style, int segments) {
        Circle(center, glm::vec3(1.0f, 0.0f, 0.0f), radius, style, segments);
        Circle(center, glm::vec3(0.0f, 1.0f, 0.0f), radius, style, segments);
        Circle(center, glm::vec3(0.0f, 0.0f, 1.0f), radius, style, segments);
    }

    void DebugDraw::Capsule(const glm::vec3& center, float r, float h, const glm::quat& rotation, const DebugStyle& style, int segments) {

        if (!Wanted(style) || r <= 0.0f) { return; }
        segments = std::max(segments, 8);
        h = std::max(h, 0.0f);

        std::vector<glm::vec3> p;
        auto add = [&](const glm::vec3& a, const glm::vec3& b) {
            p.push_back(center + rotation * a);
            p.push_back(center + rotation * b);
        };

        // Rings where the cylinder meets each cap, and the four side lines.
        for (int i = 0; i < segments; ++i) {
            const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
            const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
            const glm::vec3 r0(std::cos(a0) * r, 0.0f, std::sin(a0) * r);
            const glm::vec3 r1(std::cos(a1) * r, 0.0f, std::sin(a1) * r);
            add(r0 + glm::vec3(0.0f, h, 0.0f), r1 + glm::vec3(0.0f, h, 0.0f));
            add(r0 - glm::vec3(0.0f, h, 0.0f), r1 - glm::vec3(0.0f, h, 0.0f));
        }
        add({ r, -h, 0.0f }, { r, h, 0.0f });
        add({ -r, -h, 0.0f }, { -r, h, 0.0f });
        add({ 0.0f, -h, r }, { 0.0f, h, r });
        add({ 0.0f, -h, -r }, { 0.0f, h, -r });

        // Half-circle arcs over each cap, in the XY and ZY planes.
        const int half = segments / 2;
        for (int i = 0; i < half; ++i) {
            const float a0 = 0.5f * kTwoPi * static_cast<float>(i) / static_cast<float>(half);
            const float a1 = 0.5f * kTwoPi * static_cast<float>(i + 1) / static_cast<float>(half);
            const float c0 = std::cos(a0) * r, s0 = std::sin(a0) * r;
            const float c1 = std::cos(a1) * r, s1 = std::sin(a1) * r;
            add({ c0, h + s0, 0.0f }, { c1, h + s1, 0.0f });
            add({ 0.0f, h + s0, c0 }, { 0.0f, h + s1, c1 });
            add({ c0, -h - s0, 0.0f }, { c1, -h - s1, 0.0f });
            add({ 0.0f, -h - s0, c0 }, { 0.0f, -h - s1, c1 });
        }
        Emit(p.data(), p.size(), style);
    }

    void DebugDraw::Cone(const glm::vec3& apex, const glm::vec3& direction, float length, float halfAngleDeg, const DebugStyle& style, int segments) {

        if (!Wanted(style) || length <= 0.0f) { return; }
        const float len = glm::length(direction);
        if (len < 1e-6f) { return; }
        const glm::vec3 dir = direction / len;
        const float radius = length * std::tan(glm::radians(std::clamp(halfAngleDeg, 0.0f, 89.0f)));
        const glm::vec3 baseCenter = apex + dir * length;

        Circle(baseCenter, dir, radius, style, segments);

        glm::vec3 t, b;
        Basis(dir, t, b);
        const glm::vec3 p[8] = {
            apex, baseCenter + t * radius, apex, baseCenter - t * radius,
            apex, baseCenter + b * radius, apex, baseCenter - b * radius,
        };
        Emit(p, 8, style);
    }

    void DebugDraw::Axes(const glm::mat4& m, float size, const DebugStyle& style) {

        const glm::vec3 o(m[3]);
        auto axis = [&](int i) {
            const glm::vec3 v(m[i]);
            const float l = glm::length(v);
            return l > 1e-6f ? v / l * size : glm::vec3(0.0f);
        };
        DebugStyle s = style;
        s.color = { 0.95f, 0.25f, 0.25f, 1.0f }; Arrow(o, o + axis(0), s);
        s.color = { 0.35f, 0.90f, 0.35f, 1.0f }; Arrow(o, o + axis(1), s);
        s.color = { 0.30f, 0.50f, 1.00f, 1.0f }; Arrow(o, o + axis(2), s);
    }

    void DebugDraw::Grid(const glm::vec3& center, int halfCells, float spacing, const DebugStyle& style) {

        if (!Wanted(style) || halfCells <= 0 || spacing <= 0.0f) { return; }
        const float extent = static_cast<float>(halfCells) * spacing;
        std::vector<glm::vec3> p;
        p.reserve(static_cast<size_t>(halfCells * 2 + 1) * 4);
        for (int i = -halfCells; i <= halfCells; ++i) {
            const float o = static_cast<float>(i) * spacing;
            p.push_back(center + glm::vec3(o, 0.0f, -extent));
            p.push_back(center + glm::vec3(o, 0.0f,  extent));
            p.push_back(center + glm::vec3(-extent, 0.0f, o));
            p.push_back(center + glm::vec3( extent, 0.0f, o));
        }
        Emit(p.data(), p.size(), style);
    }

    // ------------------------------------------------------------------------------------------------------------- //
    //  RENDERER HOOKS
    // ------------------------------------------------------------------------------------------------------------- //

    void DebugDraw::Collect(std::vector<DebugVertex>& depthTested, std::vector<DebugVertex>& onTop) {

        State& s = GetState();
        std::lock_guard<std::mutex> lock(s.mutex);

        depthTested.assign(s.frameTested.begin(), s.frameTested.end());
        onTop.assign(s.frameOnTop.begin(), s.frameOnTop.end());
        for (const TimedLine& line : s.timed) {
            std::vector<DebugVertex>& out = line.depthTest ? depthTested : onTop;
            out.push_back(line.a);
            out.push_back(line.b);
        }
    }

    void DebugDraw::EndFrame(float dt, bool activeNextFrame) {

        State& s = GetState();
        std::lock_guard<std::mutex> lock(s.mutex);

        s.frameTested.clear();
        s.frameOnTop.clear();
        for (TimedLine& line : s.timed) { line.remaining -= dt; }
        s.timed.erase(std::remove_if(s.timed.begin(), s.timed.end(),
            [](const TimedLine& line) { return line.remaining <= 0.0f; }), s.timed.end());

        s.active.store(activeNextFrame);
    }

    void DebugDraw::Clear() {

        State& s = GetState();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.frameTested.clear();
        s.frameOnTop.clear();
        s.timed.clear();
    }

}   // NAMESPACE CRUCIBLE
