#include "PhysicsSystem.h"

// Jolt - Jolt.h always comes before any other Jolt header
#pragma warning(push)
#pragma warning(disable : 26495)   // uninitialised member inside Jolt's own headers; not our code to fix
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#pragma warning(pop)

// STL
#include <algorithm>    // CreateCharacter()
#include <cmath>        // CreateCharacter()

// Crucible engine
#include "Diagnostics/Logger.h"
#include "Physics/JoltConversions.h"
#include "Physics/PhysicsInternal.h"

// ---------------------------------------------------------------------------
// The mech.
//
// A character is Jolt's CharacterVirtual, which is NOT a body. Nothing in the
// simulation pushes it: it moves only when MoveCharacter is called, and it
// sweeps its capsule through the world as it goes. That sweep is what lets it
// slide along a wall and step over a ledge instead of stopping dead or bouncing.
//
// Because it is outside the simulation, nothing else can see it - raycasts miss
// it, crates ignore it. So each one also carries an "inner body": a real body of
// the same shape that rides along, purely so the world can see and hit it. Jolt
// creates and owns that body, which is why it never appears in Impl::m_bodies
// and why Step() skips it.
// ---------------------------------------------------------------------------

namespace Crucible {

    using namespace JoltConvert;

    /*!
        \brief
        Builds the capsule and puts it in the world.
        \return
        A handle, or an invalid one with the reason in the log.
    */
    CharacterHandle Physics::CreateCharacter(const CharacterDesc& desc) {

        Impl& impl = *m_impl;
        if (!impl.m_initialised) return CharacterHandle{};

        if (!IsFinite(desc.m_position)) {
            CRU_ERROR("Physics::CreateCharacter: the position holds NaN or infinity");
            return CharacterHandle{};
        }

        if (static_cast<std::size_t>(desc.m_layer) >= kPhysicsLayerCount) {
            CRU_ERROR("Physics::CreateCharacter: layer {} is not one of the {} in PhysicsLayer",
                      static_cast<int>(desc.m_layer), kPhysicsLayerCount);
            return CharacterHandle{};
        }

        // The same shape builder the bodies use: a capsule, stated Unity-style as
        // a total height, handed to Jolt as half of the straight middle.
        ShapeDesc capsule;
        capsule.m_type       = ShapeType::Capsule;
        capsule.m_radius     = desc.m_radius;
        capsule.m_halfHeight = std::max(std::abs(desc.m_height) * 0.5f - std::abs(desc.m_radius), 0.0f);
        capsule.m_center     = desc.m_center;

        const JPH::RefConst<JPH::Shape> shape = Impl::BuildShape(capsule);
        if (shape == nullptr) return CharacterHandle{};      // BuildShape already logged why

        const auto layer = static_cast<JPH::ObjectLayer>(desc.m_layer);

        // THE JOLT CHARACTER ----------------------------------------------------- //
        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        settings->mShape          = shape;
        settings->mInnerBodyShape = shape;      // the hidden body raycasts and crates can see
        settings->mInnerBodyLayer = layer;
        // The low end clamps to 1 degree, not 0: Jolt turns slope limiting OFF
        // entirely once cos(angle) comes within 0.0001 of 1, so a designer
        // setting 0 to mean "climbs nothing" would get a mech that walks up walls.
        settings->mMaxSlopeAngle  = JPH::DegreesToRadians(std::clamp(desc.m_slopeLimit, 1.0f, 89.0f));
        settings->mMaxStrength    = std::max(desc.m_pushStrength, 0.0f);

        // Only contacts below the feet count as ground. Without this plane,
        // brushing a wall or clipping a ceiling would read as "standing on it",
        // and the mech could jump off a wall it happened to be touching.
        const float footHeight = desc.m_center.m_Y - std::max(std::abs(desc.m_height) * 0.5f - std::abs(desc.m_radius), 0.0f);
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -footHeight);

        JPH::Ref<JPH::CharacterVirtual> character =
            new JPH::CharacterVirtual(settings, ToJolt(desc.m_position), JPH::Quat::sIdentity(), desc.m_userData, impl.m_world.get());

        // Characters ignore each other unless they are all in this list.
        character->SetCharacterVsCharacterCollision(impl.m_characterVsCharacter.get());
        impl.m_characterVsCharacter->Add(character.GetPtr());

        // A SLOT FOR IT ---------------------------------------------------------- //
        std::uint32_t index = 0;
        if (!impl.m_freeCharacterSlots.empty()) {
            index = impl.m_freeCharacterSlots.back();
            impl.m_freeCharacterSlots.pop_back();
        }
        else {
            index = static_cast<std::uint32_t>(impl.m_characters.size());
            impl.m_characters.emplace_back();
        }

        Impl::Character& slot = impl.m_characters[index];
        slot.character  = character;
        slot.stepOffset = std::max(desc.m_stepOffset, 0.0f);
        slot.layer      = layer;
        slot.velocity   = JPH::Vec3::sZero();
        slot.inUse      = true;

        // Find out what it is standing on before its first move, or a mech placed
        // on the floor reports "in the air" for one frame and the first jump fails.
        slot.character->RefreshContacts(impl.m_world->GetDefaultBroadPhaseLayerFilter(layer),
                                        impl.m_world->GetDefaultLayerFilter(layer),
                                        JPH::BodyFilter{}, JPH::ShapeFilter{}, *impl.m_tempAllocator);

        return CharacterHandle{ index, slot.generation };

    }   // Physics::CreateCharacter()

    void Physics::DestroyCharacter(const CharacterHandle& character) {

        Impl& impl = *m_impl;
        Impl::Character* slot = impl.Slot(character);
        if (slot == nullptr) return;

        impl.m_characterVsCharacter->Remove(slot->character.GetPtr());
        slot->character = nullptr;      // releasing it takes the inner body with it
        slot->inUse     = false;
        ++slot->generation;             // old handles to this slot stop working
        impl.m_freeCharacterSlots.push_back(character.m_index);

    }   // Physics::DestroyCharacter()

    /*!
        \brief
        One step of walking: Jolt sweeps the capsule along velocity * fixedDt.
    */
    void Physics::MoveCharacter(const CharacterHandle& character, const Math::vec3& velocity, float fixedDt) {

        Impl& impl = *m_impl;
        Impl::Character* slot = impl.Slot(character);
        if (slot == nullptr || !IsFinite(velocity) || !(fixedDt > 0.0f)) return;

        JPH::CharacterVirtual& mech = *slot->character;

        mech.SetLinearVelocity(ToJolt(velocity));

        // ExtendedUpdate is the version that also walks up stairs: it retries a
        // blocked move lifted by this much, which is what a step offset is.
        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        update.mWalkStairsStepUp = JPH::Vec3(0.0f, slot->stepOffset, 0.0f);

        mech.ExtendedUpdate(fixedDt, impl.m_world->GetGravity(), update,
                            impl.m_world->GetDefaultBroadPhaseLayerFilter(slot->layer),
                            impl.m_world->GetDefaultLayerFilter(slot->layer),
                            JPH::BodyFilter{}, JPH::ShapeFilter{}, *impl.m_tempAllocator);

        // What it is moving at now that contacts have cancelled whatever it could
        // not achieve. Deliberately NOT (distance moved / dt): walking up a step
        // and snapping down to the floor also *teleport* the capsule, and those
        // would read as +18 m/s and -30 m/s spikes inside one 1/60 s step.
        slot->velocity = mech.GetLinearVelocity();

    }   // Physics::MoveCharacter()

    bool Physics::IsGrounded(const CharacterHandle& character) const {
        const Impl::Character* slot = m_impl->Slot(character);
        return slot != nullptr && slot->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

    Math::vec3 Physics::GetCharacterVelocity(const CharacterHandle& character) const {
        const Impl::Character* slot = m_impl->Slot(character);
        return slot != nullptr ? FromJolt(slot->velocity) : Math::vec3(0.0f, 0.0f, 0.0f);
    }

    // Jolt owns the inner body, so this is the only way to name it.
    BodyHandle Physics::GetCharacterBody(const CharacterHandle& character) const {
        const Impl::Character* slot = m_impl->Slot(character);
        return slot != nullptr ? BodyHandle{ slot->character->GetInnerBodyID().GetIndexAndSequenceNumber() } : BodyHandle{};
    }

    Math::vec3 Physics::GetCharacterPosition(const CharacterHandle& character) const {
        const Impl::Character* slot = m_impl->Slot(character);
        return slot != nullptr ? FromJolt(slot->character->GetPosition()) : Math::vec3(0.0f, 0.0f, 0.0f);
    }

    // A respawn. The stored velocity is cleared, and contacts are refreshed so
    // the mech knows what it landed on rather than what it left.
    void Physics::SetCharacterPosition(const CharacterHandle& character, const Math::vec3& position) {

        Impl& impl = *m_impl;
        Impl::Character* slot = impl.Slot(character);
        if (slot == nullptr || !IsFinite(position)) return;

        slot->character->SetPosition(ToJolt(position));
        slot->velocity = JPH::Vec3::sZero();

        slot->character->RefreshContacts(impl.m_world->GetDefaultBroadPhaseLayerFilter(slot->layer),
                                         impl.m_world->GetDefaultLayerFilter(slot->layer),
                                         JPH::BodyFilter{}, JPH::ShapeFilter{}, *impl.m_tempAllocator);

    }   // Physics::SetCharacterPosition()

}   // NAMESPACE CRUCIBLE
