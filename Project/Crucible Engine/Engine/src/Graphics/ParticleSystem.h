#pragma once
/*************************************************************************
* \file:         ParticleSystem.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          05 October 2026
* \version       1.2
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
*                simulating while Paused.
*
*                A non-looping emitter whose run is over (Duration reached)
*                is switched off: ParticleEmitter::playing goes back to false,
*                as Unity's isPlaying does, so Play() starts it again and the
*                Inspector shows it stopped. Only while the simulation runs
*                (Play, not Paused) - in Edit mode a preview never changes
*                the scene.
*
* \history
*    -v1.0       initial implementation
*    -v1.1       Emit / Restart requests and per-emitter GetParticleCount,
*                for C# scripting (ParticleEmitter.Emit / Restart / ParticleCount)
*    -v1.2       a finished non-looping run sets playing = false (Play only;
*                needs the ECSSystemManager to know). By Tan Chun Kwang Xavier
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
    class ECSSystemManager;

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
         \param[in] systems   The Core-owned ECSSystemManager, to tell Play
                              from Edit / Pause (IsSimulationActive). Null =
                              never switch a finished emitter off.
         *********************************************************************/
        ParticleSystem(Renderer& renderer, ResourceManager* resources, const ECSSystemManager* systems = nullptr)
            : m_renderer(renderer), m_resources(resources), m_systems(systems) {}

        SystemStage Stage() const override { return SystemStage::PreRender; }
        int Priority() const override { return 0; }

        /*********************************************************************
         \brief  Reads WorldTransform + ParticleEmitter (and writes
                 ParticleEmitter::playing when a run finishes); after
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

        // --- Requests from gameplay (C# ParticleEmitter) --------------------- //
        // Applied at this system's next Update. It runs in PreRender, after
        // gameplay, so a request made during Update lands in the same frame.
        // Main thread only. A request for an entity without a (valid) emitter
        // that frame is dropped.

        /*********************************************************************
         \brief  Unity's ParticleSystem.Emit: spawn `count` particles from the
                 entity's emitter now, whether or not it is playing. They use
                 the emitter's usual settings and still respect maxParticles.
         *********************************************************************/
        void Emit(Entity entity, int count);

        /*********************************************************************
         \brief  Start a fresh run (time 0, the burst fires again) at the next
                 update, even if the emitter is already playing. Does not
                 change ParticleEmitter::playing - a stopped emitter only
                 restarts once it is playing.
         *********************************************************************/
        void Restart(Entity entity);

        /*********************************************************************
         \brief  Live particles of one emitter after the last Update(); 0 for
                 an entity with no emitter (or a different generation).
         *********************************************************************/
        std::uint32_t GetParticleCount(Entity entity) const;

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

        // An Emit / Restart waiting for the entity's next update.
        struct Request {
            Entity entity{};
            int    emit = 0;          // extra particles to spawn
            bool   restart = false;   // begin a fresh run first
        };

        using EmitterQuery = Query<const WorldTransform, const ParticleEmitter>;

        void Simulate(Emitter& state, const ParticleEmitter& settings, const glm::mat4& matrix, float dt);
        void Spawn(Emitter& state, const ParticleEmitter& settings, const glm::mat4& matrix, int count);
        void Submit(Emitter& state, const ParticleEmitter& settings, const glm::mat4& matrix);

        Renderer&                   m_renderer;
        ResourceManager*            m_resources = nullptr;
        const ECSSystemManager*     m_systems = nullptr;
        std::optional<EmitterQuery> m_query;
        std::vector<Entity>         m_finished;         // runs that ended this Update: playing -> false after the loop
        std::vector<Emitter>        m_emitters;         // indexed by Entity::index
        std::vector<std::uint32_t>  m_active;           // indices into m_emitters that are in use
        std::vector<GpuParticle>    m_gpuScratch;
        std::vector<LightCandidate> m_lights;
        std::vector<Request>        m_requests;         // Emit / Restart for the next Update

        Request& RequestFor(Entity entity);
        std::uint64_t               m_frame = 0;
        std::uint32_t               m_liveParticles = 0;
    };

}   // NAMESPACE CRUCIBLE
