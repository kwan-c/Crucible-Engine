#pragma once
/*************************************************************************
* \file:         ParticleSystem.h
* \brief
*                ECS system that simulates every ParticleEmitter and hands
*                the live particles (and their light) to the Renderer.
*
* \details
*                One data-driven system for every effect: a ParticleEmitter
*                component describes WHAT the effect is (see Components.h);
*                this system does the same four things for all of them each
*                frame: spawn, move, age, and submit.
*
*                Particles are not entities. Each emitter entity gets a
*                private pool (a plain array) owned by this system, found by
*                Entity::index and dropped when the entity goes away. That
*                keeps tens of thousands of particles out of the ECS
*                archetypes and the Renderer's spatial index - an emitter is
*                culled as one box.
*
*                Lighting: an emitter with emitLight adds ONE point light at
*                the brightness-weighted middle of its particles, scaled by
*                how many are alive and how young they are. At most
*                kMaxParticleLights such lights are submitted per frame (the
*                strongest win), so effects can never flood the light list.
*
*                It runs in PreRender after HierarchyTransformSystem, in Edit
*                mode too, so effects preview in the editor. It keeps
*                simulating while Paused (it has no access to the play state).
*
* \history
*    -v1.0       initial implementation
**************************************************************************/

#include "ECS/Systems/IECSSystems.h"
#include "ECS/Core/ECSCore.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/Query.h"
#include "Graphics/RenderTypes.h"
#include "EngineExport.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Crucible {

    class Renderer;
    class ResourceManager;

    class ENGINE_API ParticleSystem final : public IECSSystem
    {
    public:
        /// Most point lights all emitters together may add in one frame.
        static constexpr std::uint32_t kMaxParticleLights = 32;
        /// Upper limit on ParticleEmitter::maxParticles.
        static constexpr int kMaxParticlesPerEmitter = 20000;

        /*********************************************************************
         \param[in] renderer  The Core-owned Renderer (receives the particles
                              and lights).
         \param[in] resources The Core-owned ResourceManager, resolves
                              ParticleEmitter::textureAsset (may be null).
         *********************************************************************/
        ParticleSystem(Renderer& renderer, ResourceManager* resources)
            : m_renderer(renderer), m_resources(resources) {}

        SystemStage Stage() const override { return SystemStage::PreRender; }
        int Priority() const override { return 0; }

        /*********************************************************************
         \brief  Reads WorldTransform + ParticleEmitter; after
                 HierarchyTransformSystem; main thread (calls the Renderer).
         *********************************************************************/
        void Describe(SystemDesc& desc) const override;

        /*********************************************************************
         \brief  Spawns, moves and ages every emitter's particles by the
                 World's delta time, then submits them and their lights.
         *********************************************************************/
        void Update(World& world, SystemContext& ctx) override;

        /// Live particles over all emitters after the last Update().
        std::uint32_t GetLiveParticleCount() const { return m_liveParticles; }

    private:
        struct Particle {
            glm::vec3 position{ 0.0f };   // world space, or emitter-local when !worldSpace
            glm::vec3 velocity{ 0.0f };
            float     age = 0.0f;
            float     lifetime = 1.0f;
            float     size = 0.1f;        // starting width
            float     rotation = 0.0f;    // radians
            float     spin = 0.0f;        // radians per second
        };

        // One emitter entity's private state.
        struct Emitter {
            std::uint32_t         generation = 0xFFFFFFFFu;   // of the entity this belongs to
            std::uint64_t         seenFrame = 0;
            bool                  inUse = false;
            std::vector<Particle> particles;
            float                 time = 0.0f;         // seconds into the current run
            float                 spawnDebt = 0.0f;    // fractional particles owed by `rate`
            bool                  burstDone = false;
            bool                  wasPlaying = false;
            std::uint32_t         rng = 1;
        };

        struct LightCandidate { LightData light; float score = 0.0f; };

        using EmitterQuery = Query<const WorldTransform, const ParticleEmitter>;

        void Simulate(Emitter& state, const ParticleEmitter& settings, const glm::mat4& matrix, float dt);
        void Spawn(Emitter& state, const ParticleEmitter& settings, const glm::mat4& matrix, int count);
        void Submit(Emitter& state, const ParticleEmitter& settings, const glm::mat4& matrix);

        Renderer&                   m_renderer;
        ResourceManager*            m_resources = nullptr;
        std::optional<EmitterQuery> m_query;
        std::vector<Emitter>        m_emitters;         // indexed by Entity::index
        std::vector<std::uint32_t>  m_active;           // indices into m_emitters that are in use
        std::vector<GpuParticle>    m_gpuScratch;
        std::vector<LightCandidate> m_lights;
        std::uint64_t               m_frame = 0;
        std::uint32_t               m_liveParticles = 0;
    };

}   // NAMESPACE CRUCIBLE
