#include "PhysicsSystem.h"

// Crucible engine
#include "Physics/PhysicsInternal.h"

// ---------------------------------------------------------------------------
// Bodies: create, destroy, move, push.
//
// PLACEHOLDERS - item 7 writes these for real.
//
// They exist already because Engine is a DLL: exporting a class exports every
// function it declares, and the linker wants a definition for each one. Without
// these the whole DLL fails to link, so nothing at all would build until every
// function was written.
//
// Each one does the "nothing happened" thing, and the ones with out-parameters
// still fill them in, so a caller never reads uninitialised memory.
// ---------------------------------------------------------------------------

namespace Crucible {

    BodyHandle Physics::CreateBody(const BodyDesc&) { return BodyHandle{}; }

    void Physics::DestroyBody(const BodyHandle&) {}

    void Physics::SetTransform(const BodyHandle&, const Math::vec3&, const Math::quat&) {}

    void Physics::GetTransform(const BodyHandle&, Math::vec3& outPosition, Math::quat& outRotation) const {
        outPosition = Math::vec3(0.0f, 0.0f, 0.0f);
        outRotation = Math::quat::fromIdentity();
    }

    void Physics::MoveKinematic(const BodyHandle&, const Math::vec3&, const Math::quat&, float) {}

    void Physics::SetLinearVelocity(const BodyHandle&, const Math::vec3&) {}

    Math::vec3 Physics::GetLinearVelocity(const BodyHandle&) const { return Math::vec3(0.0f, 0.0f, 0.0f); }

    void Physics::ApplyImpulse(const BodyHandle&, const Math::vec3&) {}

}   // NAMESPACE CRUCIBLE
