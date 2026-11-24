/*************************************************************************
* \file:         NetQueryBodies.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          25 November 2026
* \version       1.1
* \brief
*                Client physics queries (Networking C# Phase 8): query
*                bodies, the shapes a client's raycasts and overlaps hit on
*                network objects whose physics runs on the host only.
*
* \details
*                A network object's Collider, RigidBody and
*                CharacterController are normally Host only: a client
*                switches them off, so it has no body for the object and its
*                Physics.Raycast goes straight through it. A query body is a
*                hidden child the client adds instead:
*
*                  Transform           identity: it sits on the entity it
*                                      copies, which the replicated (and
*                                      interpolated) Transform moves
*                  Collider            for a switched-off CharacterController,
*                                      a capsule of its height, radius, centre
*                                      and layer (unscaled, like the host's
*                                      character capsule: the body's Transform
*                                      undoes the entity's scale); otherwise a
*                                      copy of the switched-off Collider -
*                                      shape, size, centre, layer, trigger. A
*                                      Collider on an entity with a
*                                      CharacterController is ignored, as the
*                                      host's physics ignores it
*                  RigidBody           kinematic, no gravity: the physics
*                                      system moves it with its parent and
*                                      never simulates it
*                  EntityName          kQueryBodyName
*
*                The client's physics never writes anything back to the
*                object and nothing of it is sent: the host's results (hits,
*                damage, movement) cannot change. A query sees the object
*                where it was drawn last frame (physics runs after scripts).
*
*                Not added for a Collider or CharacterController that is
*                switched on (it has a real body already), nor below another
*                query body. Callers decide which objects get them: not this
*                client's own player (its camera would hit its own body).
*                Spawned objects take theirs with them when destroyed; scene
*                objects' are removed with RemoveClientQueryBodies when the
*                session ends.
*
*                A hit lands on the query body, a child of the object: use
*                FindNetworkRoot (C# Network.ObjectOf) to get the object.
*
* \history
*    -v1.0       Networking C# Phase 8; FindClientQueryBody: the body whose on/off follows a
*                synced server-only Collider or CharacterController (EnabledMask)
*    -v1.1       CharacterController first (a Collider beside it is ignored) and unscaled
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "ECS/Core/ECSCore.h"
#include "EngineExport.h"

namespace Crucible
{
	class World;

	/// The EntityName of every query body.
	inline constexpr const char* kQueryBodyName = "Network Query Body";

	/// Client: adds a query body for every switched-off Collider and CharacterController in
	/// `root`'s subtree (see the file comment). Appends each new entity to `created`; returns how
	/// many were added. Changes the World's structure. Calling it again adds nothing twice.
	ENGINE_API std::size_t AddClientQueryBodies(World& world, Entity root, std::vector<Entity>& created);

	/// Destroys the query bodies in `bodies` (others, and dead entities, are skipped).
	ENGINE_API void RemoveClientQueryBodies(World& world, std::span<const Entity> bodies);

	/// Whether `entity` is a query body.
	ENGINE_API bool IsClientQueryBody(const World& world, Entity entity);

	/// The query body under `source` (the entity whose Collider or CharacterController it
	/// copies), or kNullEntity: none there (the host, or this client's own player).
	ENGINE_API Entity FindClientQueryBody(const World& world, Entity source);
}
