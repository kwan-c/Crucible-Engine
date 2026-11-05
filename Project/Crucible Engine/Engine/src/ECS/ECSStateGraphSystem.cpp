/*************************************************************************
* \file:         ECSStateGraphSystem.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Making, running and dropping each enemy's state graph copy.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "ECSStateGraphSystem.h"

// Crucible engine
#include "Components/Components.h"      // StateGraphAgent, PlayerController, WorldTransform
#include "Diagnostics/Logger.h"
#include "ECSNavigationSystem.h"        // RunBeforeIfPresent needs the complete type
#include "Resources/ResourceManager.h"
#include "Systems/PlayMode.h"
#include "Systems/Query.h"
#include "World.h"

namespace Crucible {

    ECSStateGraphSystem::ECSStateGraphSystem(ResourceManager& resources, const PlayMode& playMode)
        : m_resources(resources), m_playMode(playMode) {

        s_active = this;

    }   // ECSStateGraphSystem::ECSStateGraphSystem()

    ECSStateGraphSystem::~ECSStateGraphSystem() {

        // Only if it's still us: scripts must never reach a destroyed system.
        if (s_active == this) s_active = nullptr;

    }   // ECSStateGraphSystem::~ECSStateGraphSystem()

    Blackboard* ECSStateGraphSystem::VariablesOf(World& world, Entity entity) {

        const StateGraphAgent* agent = world.Read<StateGraphAgent>(entity);
        if (!agent) return nullptr;

        // A script's OnStart can run before this Play's first Update, so the
        // old session's copies may still be here: drop them first.
        SyncSession();
        Running& running = Prepare(world, entity, agent->m_graph);
        return running.instance ? &running.instance->Variables() : nullptr;

    }   // ECSStateGraphSystem::VariablesOf()

    std::string_view ECSStateGraphSystem::CurrentStateOf(Entity entity) const {

        const auto it = m_running.find(KeyOf(entity));
        return (it != m_running.end() && it->second.instance) ? it->second.instance->CurrentStateName() : std::string_view{};

    }   // ECSStateGraphSystem::CurrentStateOf()

    void ECSStateGraphSystem::SyncSession() {

        // Play after Stop, or a scene loaded during Play: the old copies point
        // at entities that Stop has put back, so start over. Their nodes aren't
        // ended one by one - the scene they acted on has already been restored.
        if (m_playMode.SessionRevision() != m_session) {
            m_running.clear();
            m_session = m_playMode.SessionRevision();
        }

    }   // ECSStateGraphSystem::SyncSession()

    ECSStateGraphSystem::Running& ECSStateGraphSystem::Prepare(World& world, Entity entity, std::uint64_t asset) {

        Running& running = m_running[KeyOf(entity)];

        // New to us, or its graph was changed in the Inspector: make its copy
        // (cutting any old one short). A graph that fails to load leaves no
        // copy - ResourceManager has warned once, and remembers the failure,
        // so asking again each frame costs only a lookup.
        if (running.asset != asset || !running.instance) {
            if (running.instance) running.instance->Abort();
            running.asset = asset;
            running.instance.reset();
            if (asset != 0) {
                if (std::shared_ptr<const StateGraph> graph = m_resources.GetStateGraph(asset)) {
                    running.instance = StateGraphInstance::Create(std::move(graph), world, entity,
                        [this](std::uint64_t id) { return m_resources.GetBehaviorGraph(id); });   // Subgraph states' .behaviorgraph files
                }
            }
        }
        return running;

    }   // ECSStateGraphSystem::Prepare()

    void ECSStateGraphSystem::Describe(SystemDesc& desc) const {

        desc.SimulationOnly()
            .RunBeforeIfPresent<ECSNavigationSystem>();

    }   // ECSStateGraphSystem::Describe()

    Entity ECSStateGraphSystem::NearestPlayer(const Math::vec3& from) const {

        Entity nearest   = kNullEntity;
        float  nearestSq = 0.0f;
        for (std::size_t i = 0; i < m_players.size(); ++i) {
            const float dx = m_playerPositions[i].x - from.x;
            const float dz = m_playerPositions[i].z - from.z;
            const float sq = dx * dx + dz * dz;
            if (nearest == kNullEntity || sq < nearestSq) { nearest = m_players[i]; nearestSq = sq; }
        }
        return nearest;

    }   // ECSStateGraphSystem::NearestPlayer()

    /*!
        \brief
        Gives each new enemy its own copy of its graph, sets its Target, runs
        it one frame, and drops the copies of enemies that are gone.
    */
    void ECSStateGraphSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {

        SyncSession();

        // A zero or broken (NaN) frame time must change nothing.
        const float dt = world.DeltaTime();
        if (!(dt > 0.0f)) return;
        ++m_frame;

        // The players, for the temporary "Target = nearest player".
        m_players.clear();
        m_playerPositions.clear();
        world.CreateQuery<const PlayerController, const WorldTransform>()
            .ForEach([&](Entity player, const PlayerController&, const WorldTransform& transform) {
                if (!transform.valid) return;
                m_players.push_back(player);
                m_playerPositions.push_back(Math::vec3(transform.matrix[3]));
            });

        // Collect the agents first and run the graphs after, outside the query:
        // a running graph reaches into other components (its FlowFieldAgent),
        // and that is safest when no query is mid-iteration.
        m_found.clear();
        world.CreateQuery<const StateGraphAgent>()
            .ForEach([&](Entity entity, const StateGraphAgent& agent) {
                m_found.push_back({ entity, agent.m_graph, agent.m_logStateChanges });
            });

        for (const Found& found : m_found) {

            Running& running = Prepare(world, found.entity, found.asset);
            running.seenFrame = m_frame;
            if (!running.instance) continue;

            // TEMPORARY until perception: the target is simply the nearest
            // player (null if there is none), refreshed every frame.
            if (const WorldTransform* self = world.Read<WorldTransform>(found.entity); self && self->valid) {
                running.instance->Variables().Set("Target", NearestPlayer(Math::vec3(self->matrix[3])));
            }

            const int before = running.instance->CurrentState();
            running.instance->Tick(dt);
            if (found.log && running.instance->CurrentState() != before) {
                CRU_INFO("StateGraph: entity {} -> \"{}\"", found.entity.index, running.instance->CurrentStateName());
            }

        }

        // Enemies not seen this frame (removed, switched off, or their
        // StateGraphAgent taken away): end their running nodes so nothing
        // they started keeps going, then drop their copies.
        for (auto it = m_running.begin(); it != m_running.end();) {
            if (it->second.seenFrame == m_frame) { ++it; continue; }
            if (it->second.instance) it->second.instance->Abort();
            it = m_running.erase(it);
        }

    }   // ECSStateGraphSystem::Update()

}   // NAMESPACE CRUCIBLE
