/*************************************************************************
* \file:         ECSPhysicsSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu (v1.0 skeleton),
*                Hugo Low Ren Hao (v2.0 physics binding)
* \par:          Email: a.patchamuthu@digipen.edu,
*                Email: low.h@digipen.edu
* \date          27 September 2026
* \version       2.3
* \brief
*                The ECS side of physics: keeps Crucible::Physics in step
*                with the Transform, Collider, RigidBody, ConstantForce and
*                PhysicsJoint components.
*
* \details
*                Each Update():
*                  1. works out how many fixed 1/60 s steps are due
*                  2. syncs components into the physics world - creates,
*                     rebuilds and removes bodies, then joints, and passes
*                     on anything gameplay changed
*                  3. runs those steps and writes the results back into
*                     the Transforms (and PhysicsJoint::m_isBroken)
*
*                Gameplay never talks to this class. It edits components,
*                and reaches the world itself through GetPhysics() for
*                raycasts.
*
* \history
*    -v1.0       skeleton / placeholder system, no physics logic yet
*    -v2.0       the real binding
*    -v2.1       play-mode sessions: BeginSession/EndSession (Jolt per
*                session, the system object kept), SimulationOnly
*    -v2.2       forces: RigidBody's AddForce/AddTorque/ApplyImpulse and
*                the ConstantForce component
*    -v2.3       PhysicsJoint; takes the SceneManager to resolve a joint's
*                saved connected body
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <memory>

// Crucible engine
#include "EngineExport.h"
#include "Physics/PhysicsSystem.h"
#include "Systems/IECSSystems.h"

namespace Crucible {

    class SceneManager;

    class ECSPhysicsSystem final : public IECSSystem {

    public:

        // scenes: how a PhysicsJoint's saved connected body (a scene id) is
        // turned back into an entity. Null is allowed - joints then only
        // connect through PhysicsJoint::m_connectedEntity, or to the world.
        explicit ECSPhysicsSystem(SceneManager* scenes = nullptr, const PhysicsSettings& settings = PhysicsSettings{});
        ~ECSPhysicsSystem() override;

        ECSPhysicsSystem(const ECSPhysicsSystem&)            = delete;
        ECSPhysicsSystem& operator=(const ECSPhysicsSystem&) = delete;

        // --- IECSSystem ---------------------------------------------------- //

        void Init() override;       // starts nothing: the physics world starts with BeginSession
        void Update(World& world, CommandBuffer& commands) override;
        void Shutdown() noexcept override;
        void Describe(SystemDesc& desc) const override;   // SimulationOnly: steps only while Playing

        // Registered .ReactiveTo<CharacterController>() (Core): a CharacterController that is
        // switched back on starts without the fall it had when it was switched off.
        void OnComponentActivated(World& world, Entity entity, ComponentId component) override;

        // Physics belongs after gameplay has had its say and before rendering
        // reads the results - which is exactly what PostUpdate is for.
        SystemStage Stage() const override { return SystemStage::PostUpdate; }
        int         Priority() const override { return 1; }

        // --- play-mode session (Core's PlayMode drives these) ---------------- //

        // The object lives as long as the engine; a SESSION is one Play..Stop.
        // BeginSession (Play) starts a fresh Jolt world, so bodies are built
        // from the components as they are now. EndSession (Stop) removes every
        // body and character, stops Jolt and resets the fixed-step clock.
        // Pause needs nothing here: the manager just stops updating physics,
        // and Resume carries on with the same world, bodies and clock.
        // Both are no-ops when already in that state, like Physics::Init and
        // Physics::Shutdown.
        ENGINE_API void BeginSession();
        ENGINE_API void EndSession() noexcept;
        ENGINE_API bool IsInSession() const;

        // --- for gameplay and tests ------------------------------------------ //

        // The world itself, for raycasts. Gameplay gets here through Core.
        ENGINE_API Physics&      GetPhysics();
        ENGINE_API std::uint32_t GetStepsLastFrame() const;    // 0, 1 or more after a hitch

        // Update with a frame time you choose, instead of World::DeltaTime().
        // This is what a test harness drives, and how you simulate 30 or 144 fps.
        void Advance(World& world, float frameDeltaTime);

    private:

        struct State;                               // everything else, in the .cpp
        std::unique_ptr<State> m_state;

    };  // ECSPHYSICSSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
