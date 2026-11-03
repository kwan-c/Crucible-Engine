/*************************************************************************
* \file:         ECSStateGraphSystem.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Runs every StateGraphAgent's state graph, once per frame,
*                while Playing.
*
* \details
*                Each enemy gets its own StateGraphInstance the first frame
*                it is seen (and a new one if its graph is changed); the
*                instances are thrown away when a new play session starts,
*                so every Play begins fresh.
*
*                Runs just BEFORE ECSNavigationSystem in the Update stage,
*                so the orders a graph gives (Move To, Arrive) are carried
*                out by the agents the same frame.
*
*                TEMPORARY until perception exists: before ticking, each
*                enemy's "Target" variable is set to the nearest player -
*                Jean David's "find the object tagged Player".
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// Crucible engine
#include "AI/StateGraph.h"
#include "Math/Math.h"
#include "Systems/IECSSystems.h"

namespace Crucible {

    class PlayMode;
    class ResourceManager;

    // Not exported (like the other ECS systems): nothing outside the engine calls it yet.
    class ECSStateGraphSystem final : public IECSSystem {

    public:

        // resources: where graphs are loaded from. playMode: its session number
        // says when Play started again. Core owns both; they outlive this system.
        ECSStateGraphSystem(ResourceManager& resources, const PlayMode& playMode);

        // --- IECSSystem ---------------------------------------------------- //

        void Update(World& world, CommandBuffer& commands) override;

        // SimulationOnly: a brain only thinks while Playing. And before
        // navigation, so its orders are followed the same frame.
        void Describe(SystemDesc& desc) const override;

        SystemStage Stage() const override { return SystemStage::Update; }

    private:

        // One enemy's running graph.
        struct Running {

            std::uint64_t                       asset     = 0;   // the graph it was made from (to spot a change)
            std::unique_ptr<StateGraphInstance> instance;        // null: no graph, or it failed to load
            std::uint64_t                       seenFrame = 0;   // last frame its StateGraphAgent was found

        };  // RUNNING STRUCT

        // An agent found this frame, collected before any graph runs (see Update).
        struct Found {

            Entity        entity;
            std::uint64_t asset = 0;
            bool          log   = false;

        };  // FOUND STRUCT

        // Entity -> one number, for the map: its generation in the top half, its slot below.
        static std::uint64_t KeyOf(const Entity& entity) {
            return (static_cast<std::uint64_t>(entity.generation) << 32) | entity.index;
        }

        Entity NearestPlayer(const Math::vec3& from) const;

        ResourceManager&                            m_resources;
        const PlayMode&                             m_playMode;
        std::uint64_t                               m_session = ~0ull;   // play session the instances belong to (none yet)
        std::uint64_t                               m_frame   = 0;
        std::unordered_map<std::uint64_t, Running>  m_running;
        std::vector<Found>                          m_found;             // reused every frame
        std::vector<Entity>                         m_players;           // this frame's players, for Target
        std::vector<Math::vec3>                     m_playerPositions;   // same order

    };  // ECSSTATEGRAPHSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
