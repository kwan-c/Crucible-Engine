/*************************************************************************
* \file:         ParticleSystem.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          05 October 2026
* \version       1.0
* \brief
*                See ParticleSystem.h for what this system does and why
*                particles are not entities.
*
* \history
*    -v1.0       initial implementation
**************************************************************************/

#include "ParticleSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ECS/World.h"
#include "ECS/HierarchyTransformSystem.h"
#include "Graphics/RenderSystem.h"
#include "Resources/ResourceManager.h"

namespace Crucible {

    namespace {

        constexpr float kGravity = 9.81f;
        constexpr float kTwoPi   = 6.28318530718f;

        // xorshift32: tiny, fast, and good enough to scatter particles. Never returns 0 from a
        // non-zero state.
        std::uint32_t Next(std::uint32_t& state) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
        float Rand01(std::uint32_t& state) { return static_cast<float>(Next(state) >> 8) * (1.0f / 16777216.0f); }
        float RandRange(std::uint32_t& state, float a, float b) { return a + (b - a) * Rand01(state); }

        // Inspector colours are sRGB; the renderer works in linear light.
        glm::vec3 ToLinear(const glm::vec3& srgb) {
            return glm::pow(glm::clamp(srgb, glm::vec3(0.0f), glm::vec3(1.0f)), glm::vec3(2.2f));
        }

        // The matrix's rotation with any scale removed (columns normalized).
        glm::mat3 RotationOf(const glm::mat4& m) {
            glm::mat3 r(m);
            for (int c = 0; c < 3; ++c) {
                const float len = glm::length(r[c]);
                r[c] = len > 1e-6f ? r[c] / len : glm::vec3(c == 0 ? 1.0f : 0.0f, c == 1 ? 1.0f : 0.0f, c == 2 ? 1.0f : 0.0f);
            }
            return r;
        }

    }   // anonymous namespace

    void ParticleSystem::Describe(SystemDesc& desc) const {

        desc.Uses<EmitterQuery>()                    // reads WorldTransform + ParticleEmitter
            .RunAfter<HierarchyTransformSystem>()    // WorldTransform must already be this frame's
            .Affinity(SystemAffinity::MainThread);   // touches the Renderer/ResourceManager

    }   // ParticleSystem::Describe()

    void ParticleSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {

        if (!m_query) { m_query.emplace(world.CreateQuery<const WorldTransform, const ParticleEmitter>()); }

        ++m_frame;
        m_liveParticles = 0;
        m_lights.clear();
        const float dt = std::max(world.DeltaTime(), 0.0f);

        m_query->ForEach([&](Entity entity, const WorldTransform& worldTransform, const ParticleEmitter& settings) {

            if (!worldTransform.valid) { return; }

            // Find (or start) this entity's pool. A reused slot with a new generation is a
            // different entity: start clean.
            if (entity.index >= m_emitters.size()) { m_emitters.resize(static_cast<size_t>(entity.index) + 1); }
            Emitter& state = m_emitters[entity.index];
            if (!state.inUse || state.generation != entity.generation) {
                if (!state.inUse) { m_active.push_back(entity.index); }
                state = Emitter{};
                state.inUse      = true;
                state.generation = entity.generation;
                state.rng        = (static_cast<std::uint32_t>(settings.seed) * 2654435761u) ^ (entity.index * 40503u + 1u);
                if (state.rng == 0) { state.rng = 1; }
            }
            state.seenFrame = m_frame;

            Simulate(state, settings, worldTransform.matrix, dt);
            Submit(state, settings, worldTransform.matrix);
            m_liveParticles += static_cast<std::uint32_t>(state.particles.size());
        });

        // Emitters whose entity is gone (destroyed, or lost its component): free their particles.
        for (size_t i = m_active.size(); i-- > 0;) {
            Emitter& state = m_emitters[m_active[i]];
            if (state.seenFrame == m_frame) { continue; }
            state = Emitter{};
            m_active[i] = m_active.back();
            m_active.pop_back();
        }

        // Lights: only the strongest few, so effects can't flood the Renderer's light list.
        if (m_lights.size() > kMaxParticleLights) {
            std::partial_sort(m_lights.begin(), m_lights.begin() + kMaxParticleLights, m_lights.end(),
                [](const LightCandidate& a, const LightCandidate& b) { return a.score > b.score; });
            m_lights.resize(kMaxParticleLights);
        }
        for (const LightCandidate& candidate : m_lights) { m_renderer.SubmitLight(candidate.light); }

    }   // ParticleSystem::Update()

    void ParticleSystem::Spawn(Emitter& state, const ParticleEmitter& settings, const glm::mat4& matrix, int count) {

        const int cap  = std::clamp(settings.maxParticles, 0, kMaxParticlesPerEmitter);
        const int room = cap - static_cast<int>(state.particles.size());
        count = std::min(count, room);
        if (count <= 0) { return; }

        const glm::mat3 rotation  = RotationOf(matrix);
        const float     cosSpread = std::cos(glm::radians(std::clamp(settings.spreadDeg, 0.0f, 180.0f)));
        const float     spin      = glm::radians(std::max(settings.spinDeg, 0.0f));

        for (int n = 0; n < count; ++n) {

            Particle p;

            // Where: a point in the emitter's local space, by shape.
            glm::vec3 local(0.0f);
            switch (settings.shape) {
                case ParticleShape::Point:
                    break;
                case ParticleShape::Sphere: {
                    // Rejection sampling: uniform inside the unit sphere.
                    glm::vec3 v;
                    do {
                        v = glm::vec3(RandRange(state.rng, -1.0f, 1.0f), RandRange(state.rng, -1.0f, 1.0f), RandRange(state.rng, -1.0f, 1.0f));
                    } while (glm::dot(v, v) > 1.0f);
                    local = v * std::max(settings.shapeRadius, 0.0f);
                    break;
                }
                case ParticleShape::Box:
                    local = glm::vec3(RandRange(state.rng, -1.0f, 1.0f), RandRange(state.rng, -1.0f, 1.0f), RandRange(state.rng, -1.0f, 1.0f))
                          * glm::max(settings.boxExtents, glm::vec3(0.0f));
                    break;
            }

            // Which way: uniformly inside a cone of half-angle spreadDeg around local +Y.
            const float cosTheta = RandRange(state.rng, cosSpread, 1.0f);
            const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
            const float phi      = Rand01(state.rng) * kTwoPi;
            const glm::vec3 dir(sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi));
            const float speed = RandRange(state.rng, settings.speedMin, settings.speedMax);

            if (settings.worldSpace) {
                p.position = glm::vec3(matrix * glm::vec4(local, 1.0f));
                p.velocity = rotation * dir * speed;
            } else {
                p.position = local;          // stays in the emitter's space; placed in the world on submit
                p.velocity = dir * speed;
            }

            p.lifetime = std::max(RandRange(state.rng, settings.lifetimeMin, settings.lifetimeMax), 0.01f);
            p.size     = std::max(RandRange(state.rng, settings.sizeMin, settings.sizeMax), 0.0f);
            if (spin > 0.0f) {
                p.rotation = Rand01(state.rng) * kTwoPi;
                p.spin     = RandRange(state.rng, -spin, spin);
            }
            state.particles.push_back(p);
        }

    }   // ParticleSystem::Spawn()

    void ParticleSystem::Simulate(Emitter& state, const ParticleEmitter& settings, const glm::mat4& matrix, float dt) {

        // (Re)started: begin a fresh run.
        if (settings.playing && !state.wasPlaying) {
            state.time      = 0.0f;
            state.spawnDebt = 0.0f;
            state.burstDone = false;
        }
        state.wasPlaying = settings.playing;

        // ---- Move and age what is alive. Dead particles are replaced by the last one (order in
        // the pool doesn't matter; the Renderer sorts when blending needs it).
        glm::vec3 gravity(0.0f, -kGravity * settings.gravityScale, 0.0f);
        if (!settings.worldSpace) { gravity = glm::transpose(RotationOf(matrix)) * gravity; }   // into the emitter's space
        const float damping = std::exp(-std::max(settings.drag, 0.0f) * dt);

        for (size_t i = 0; i < state.particles.size();) {
            Particle& p = state.particles[i];
            p.age += dt;
            if (p.age >= p.lifetime) {
                p = state.particles.back();
                state.particles.pop_back();
                continue;
            }
            p.velocity  = (p.velocity + gravity * dt) * damping;
            p.position += p.velocity * dt;
            p.rotation += p.spin * dt;
            ++i;
        }

        // maxParticles lowered in the Inspector: drop the excess.
        const size_t cap = static_cast<size_t>(std::clamp(settings.maxParticles, 0, kMaxParticlesPerEmitter));
        if (state.particles.size() > cap) { state.particles.resize(cap); }

        // ---- Spawn.
        if (!settings.playing || dt <= 0.0f) { return; }

        const float duration = std::max(settings.duration, 0.05f);
        if (!state.burstDone) {
            Spawn(state, settings, matrix, std::max(settings.burstCount, 0));
            state.burstDone = true;
        }
        if (settings.loop || state.time < duration) {
            state.spawnDebt += std::max(settings.rate, 0.0f) * dt;
            const int owed = static_cast<int>(state.spawnDebt);
            state.spawnDebt -= static_cast<float>(owed);
            Spawn(state, settings, matrix, owed);
        }
        state.time += dt;
        if (settings.loop && state.time >= duration) {
            state.time      = std::fmod(state.time, duration);
            state.burstDone = false;   // the next run bursts again
        }

    }   // ParticleSystem::Simulate()

    void ParticleSystem::Submit(Emitter& state, const ParticleEmitter& settings, const glm::mat4& matrix) {

        if (state.particles.empty()) { return; }

        const glm::vec3 colourStart = ToLinear(settings.colourStart);
        const glm::vec3 colourEnd   = ToLinear(settings.colourEnd);
        const float     brightness  = std::max(settings.brightness, 0.0f);
        const float     stretch     = std::max(settings.stretch, 0.0f);
        const int       cols        = std::max(settings.flipbookCols, 1);
        const int       rows        = std::max(settings.flipbookRows, 1);
        const float     frames      = static_cast<float>(cols * rows);
        const glm::mat3 basis(matrix);   // local -> world for directions (keeps the emitter's scale)

        m_gpuScratch.clear();
        m_gpuScratch.reserve(state.particles.size());

        glm::vec3 boundsMin(std::numeric_limits<float>::max());
        glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
        float     maxReach = 0.0f;       // largest half-size of any quad, for padding the bounds
        glm::vec3 lightSum(0.0f);
        float     lightWeight = 0.0f;

        for (const Particle& p : state.particles) {

            const float t = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);   // 0 = just born, 1 = about to die

            glm::vec3 position = p.position;
            glm::vec3 velocity = p.velocity;
            if (!settings.worldSpace) {
                position = glm::vec3(matrix * glm::vec4(position, 1.0f));
                velocity = basis * velocity;
            }

            GpuParticle gpu;
            const float size = p.size * glm::mix(1.0f, std::max(settings.endSizeScale, 0.0f), t);
            gpu.posSize = glm::vec4(position, size);
            gpu.color   = glm::vec4(glm::mix(colourStart, colourEnd, t) * brightness,
                                    std::clamp(glm::mix(settings.alphaStart, settings.alphaEnd, t), 0.0f, 1.0f));
            gpu.axisRot = glm::vec4(velocity * stretch, p.rotation);
            gpu.misc    = glm::vec4(std::min(std::floor(t * frames), frames - 1.0f), 0.0f, 0.0f, 0.0f);
            m_gpuScratch.push_back(gpu);

            boundsMin = glm::min(boundsMin, position);
            boundsMax = glm::max(boundsMax, position);
            maxReach  = std::max(maxReach, size * 0.75f + glm::length(velocity) * stretch * 0.5f);   // 0.75 ~ half the diagonal

            const float w = 1.0f - t;   // young particles are the hot ones
            lightSum    += position * w;
            lightWeight += w;
        }

        ParticleDrawDesc desc;
        desc.texture      = (m_resources && settings.textureAsset != 0) ? m_resources->GetTexture(settings.textureAsset, true) : TextureHandle{};
        desc.additive     = settings.blend == ParticleBlend::Additive;
        desc.flipbookCols = static_cast<std::uint32_t>(cols);
        desc.flipbookRows = static_cast<std::uint32_t>(rows);
        desc.boundsMin    = boundsMin - glm::vec3(maxReach);
        desc.boundsMax    = boundsMax + glm::vec3(maxReach);
        m_renderer.SubmitParticles(desc, m_gpuScratch.data(), static_cast<std::uint32_t>(m_gpuScratch.size()));

        // One light for the whole emitter, at the middle of its hot particles. Its strength follows
        // the population: full when the effect is at its usual size and fresh, fading as it dies.
        if (settings.emitLight && lightWeight > 1e-4f && settings.lightIntensity > 0.0f && settings.lightRange > 0.0f) {
            const float averageLife = 0.5f * (settings.lifetimeMin + settings.lifetimeMax);
            const float usual = std::max({ static_cast<float>(std::max(settings.burstCount, 0)),
                                           std::max(settings.rate, 0.0f) * averageLife * 0.5f, 1.0f });
            const float strength = std::min(lightWeight / usual, 1.0f);

            LightCandidate candidate;
            candidate.light.type        = LightType::Point;
            candidate.light.position    = lightSum / lightWeight;
            candidate.light.colour      = glm::clamp(settings.lightColour, glm::vec3(0.0f), glm::vec3(1.0f));
            candidate.light.intensity   = settings.lightIntensity * strength;
            candidate.light.range       = settings.lightRange;
            candidate.light.castShadows = false;
            candidate.score             = candidate.light.intensity * settings.lightRange;
            m_lights.push_back(candidate);
        }

    }   // ParticleSystem::Submit()

}   // NAMESPACE CRUCIBLE
