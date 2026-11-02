/*************************************************************************
* \file:         ECSPhysicsSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu (v1.0 skeleton),
*                Hugo Low Ren Hao (v2.0 physics binding)
* \par:          Email: a.patchamuthu@digipen.edu,
*                Email: low.h@digipen.edu
* \date          27 September 2026
* \version       2.6
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
*                  4. sends what started and stopped touching as
*                     PhysicsEvents on the engine bus
*
*                Gameplay mostly edits components, listens for
*                PhysicsEvents, and asks this class who is touching what
*                (GetTriggerOccupants, GetContacts). It reaches the world
*                itself through GetPhysics() for raycasts.
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
*    -v2.4       collision and trigger events, GetTriggerOccupants and
*                GetContacts; Update takes the SystemContext to send them
*    -v2.5       queries in entities: Raycast, RaycastAll, SphereCast,
*                CapsuleCast, OverlapSphere, OverlapBox, OverlapCapsule
*    -v2.6       AddExplosionForce (Unity's) and Explode
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <memory>
#include <vector>

// Crucible engine
#include "EngineExport.h"
#include "Physics/EntityQueries.h"
#include "Physics/PhysicsEvents.h"
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
        // The SystemContext version: it carries the event writer the collision and
        // trigger events go out through.
        void Update(World& world, SystemContext& ctx) override;
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

        // Everything inside a trigger collider right now - what Unity's
        // OnTriggerStay is called for. Call it from your own Update, where you
        // have a dt: a damage zone does Damage(victim, dps * dt) for each.
        // Fills `out` (cleared first; pass the same vector every frame and it
        // never reallocates). Empty if `trigger` has no trigger body.
        ENGINE_API void GetTriggerOccupants(const Entity& trigger, std::vector<Entity>& out) const;

        // Everything an entity is touching right now, triggers included, with the
        // latest contact points - what Unity hands OnCollisionStay. Works for a
        // mech too (what its character's hidden body touches). Fills `out`,
        // cleared first.
        ENGINE_API void GetContacts(const Entity& entity, std::vector<EntityContact>& out) const;

        // --- queries, in entities (Unity's Physics.Raycast and friends) -------- //
        // The same as Physics' own (see PhysicsSystem.h for what each does), but
        // every hit names its Entity, and EntityFilter::ignore takes an Entity -
        // a mech is turned into its hidden character body for you. Vectors are
        // cleared first; reuse them and a horde of AI asking every frame never
        // allocates. Empty / false outside Play (there is no physics world then).

        ENGINE_API bool Raycast(const Math::vec3& origin, const Math::vec3& direction, EntityHit& outHit, float maxDistance,
                                const EntityFilter& filter = EntityFilter{}) const;
        ENGINE_API void RaycastAll(const Math::vec3& origin, const Math::vec3& direction, float maxDistance, std::vector<EntityHit>& out,
                                   const EntityFilter& filter = EntityFilter{}) const;
        ENGINE_API bool SphereCast(const Math::vec3& origin, float radius, const Math::vec3& direction, EntityHit& outHit, float maxDistance,
                                   const EntityFilter& filter = EntityFilter{}) const;
        ENGINE_API bool CapsuleCast(const Math::vec3& point1, const Math::vec3& point2, float radius, const Math::vec3& direction,
                                    EntityHit& outHit, float maxDistance, const EntityFilter& filter = EntityFilter{}) const;
        ENGINE_API void OverlapSphere(const Math::vec3& center, float radius, std::vector<Entity>& out,
                                      const EntityFilter& filter = EntityFilter{}) const;
        ENGINE_API void OverlapBox(const Math::vec3& center, const Math::vec3& halfExtents, const Math::quat& orientation, std::vector<Entity>& out,
                                   const EntityFilter& filter = EntityFilter{}) const;
        ENGINE_API void OverlapCapsule(const Math::vec3& point0, const Math::vec3& point1, float radius, std::vector<Entity>& out,
                                       const EntityFilter& filter = EntityFilter{}) const;

        // --- explosions ------------------------------------------------------ //

        // Unity's Rigidbody.AddExplosionForce, same arguments and same default
        // mode. Pushes one entity's body away from the blast: full force at the
        // centre, fading to nothing at the radius (a radius of 0 means no fading),
        // landing on the side of the body facing the blast so it tumbles. A blast
        // inside the body pushes at its centre of mass instead (no spin).
        // upwardsModifier lowers the blast, and the point the push lands on, by
        // that many metres, so things are thrown up and flip as they go. Only
        // dynamic bodies move - static, kinematic and characters are left alone,
        // as in Unity. False if it pushed nothing.
        ENGINE_API bool AddExplosionForce(const Entity& entity, float explosionForce, const Math::vec3& explosionPosition,
                                          float explosionRadius, float upwardsModifier = 0.0f, ForceMode mode = ForceMode::Force);

        // A whole explosion in one call: every entity inside the sphere (passing the
        // filter, and not shielded by a wall if explosion.blockedByWalls) is pushed
        // with AddExplosionForce, and listed in `outHit` - characters and static
        // things too, so gameplay can damage everything the blast reached.
        ENGINE_API void Explode(const Explosion& explosion, std::vector<Entity>& outHit);

        // Update with a frame time you choose, instead of World::DeltaTime().
        // This is what a test harness drives, and how you simulate 30 or 144 fps.
        void Advance(World& world, float frameDeltaTime);

    private:

        struct State;                               // everything else, in the .cpp
        std::unique_ptr<State> m_state;

    };  // ECSPHYSICSSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
