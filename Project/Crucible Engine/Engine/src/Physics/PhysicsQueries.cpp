#include "PhysicsSystem.h"

// Crucible engine
#include "Physics/PhysicsInternal.h"

// ---------------------------------------------------------------------------
// Queries: the raycast.
//
// PLACEHOLDER - item 8 writes it for real. Here now for the same reason as
// PhysicsBodies.cpp: the DLL exports every function the class declares, so all
// of them need a definition before anything links.
// ---------------------------------------------------------------------------

namespace Crucible {

    bool Physics::Raycast(const Math::vec3&, const Math::vec3&, RaycastHit& outHit, float, const BodyHandle&) const {

        outHit = RaycastHit{};      // "hit nothing", and never uninitialised
        return false;

    }   // Physics::Raycast()

}   // NAMESPACE CRUCIBLE
