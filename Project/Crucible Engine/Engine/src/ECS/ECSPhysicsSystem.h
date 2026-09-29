/*************************************************************************
* \file:         ECSPhysicsSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu (v1.0 skeleton),
*                Hugo (v2.0 physics binding)
* \date          27 September 2026
* \version       2.0
* \brief
*                The ECS side of physics: keeps Crucible::Physics in step
*                with the Transform, Collider and RigidBody components.
*
* \details
*                Each Update():
*                  1. works out how many fixed 1/60 s steps are due
*                  2. syncs components into the physics world - creates,
*                     rebuilds and removes bodies, and passes on anything
*                     gameplay changed
*                  3. runs those steps and writes the results back into
*                     the Transforms
*
*                Gameplay never talks to this class. It edits components,
*                and reaches the world itself through GetPhysics() for
*                raycasts.
*
* \history
*    -v1.0       skeleton / placeholder system, no physics logic yet
*    -v2.0       the real binding
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <memory>

// Crucible engine
#include "Physics/PhysicsSystem.h"
#include "Systems/IECSSystems.h"

namespace Crucible {

    class ECSPhysicsSystem final : public IECSSystem {

    public:

        explicit ECSPhysicsSystem(const PhysicsSettings& settings = PhysicsSettings{});
        ~ECSPhysicsSystem() override;

        ECSPhysicsSystem(const ECSPhysicsSystem&)            = delete;
        ECSPhysicsSystem& operator=(const ECSPhysicsSystem&) = delete;

        // --- IECSSystem ---------------------------------------------------- //

        void Init() override;
        void Update(World& world, CommandBuffer& commands) override;
        void Shutdown() noexcept override;

        // Physics belongs after gameplay has had its say and before rendering
        // reads the results - which is exactly what PostUpdate is for.
        SystemStage Stage() const override { return SystemStage::PostUpdate; }
        int         Priority() const override { return 1; }

        // --- for gameplay and tests ------------------------------------------ //

        // The world itself, for raycasts. Gameplay gets here through Core.
        Physics&      GetPhysics();
        std::uint32_t GetStepsLastFrame() const;    // 0, 1 or more after a hitch

        // Update with a frame time you choose, instead of World::DeltaTime().
        // This is what a test harness drives, and how you simulate 30 or 144 fps.
        void Advance(World& world, float frameDeltaTime);

    private:

        struct State;                               // everything else, in the .cpp
        std::unique_ptr<State> m_state;

    };  // ECSPHYSICSSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
