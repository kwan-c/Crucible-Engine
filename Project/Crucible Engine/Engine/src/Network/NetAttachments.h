/*************************************************************************
* \file:         NetAttachments.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                A network object hanging
*                under another one (a mech part on a mount of a player) -
*                the "Attachment" serializer and the client's side.
*
* \details
*                On the host, gameplay parents a network object's root
*                under an entity of ANOTHER network object (Transform.
*                SetParent(mount, false), as MechPart.AttachTo does). The
*                Attachment serializer sends, on the attached object's
*                root (NetAttachment, added by the network layer):
*                  u8 attached (0 or 1); when 1: varint parent NetworkId
*                  (the nearest network object above it) and u32 mount
*                  (ChildPathHash of the mount's name path below that
*                  object; 0 = the object's root): 1 to 10 bytes.
*                Its Transform serializer sends its LOCAL transform as
*                always - local to the mount while attached, the world one
*                when not - so a client must use the same parent before it
*                uses those values. Not attached to anything networked (a
*                plain scene entity, an unnamed or ambiguous mount): sent as
*                not attached, with one warning on the host.
*
*                The client (ApplyAttachment, after every record of such an
*                object and every frame while it waits):
*                  attach / change mount  find the parent object by its
*                         NetworkId and the mount by its path hash, then
*                         parent to it keeping the LOCAL transform; the
*                         object's interpolation restarts from the newest
*                         sample (RestartInterpolation): never blended
*                         across two parent spaces
*                  detach parent to nothing, same restart
*                  late join, parent not here yet  waits: retried every
*                         frame until the parent object arrives (the world
*                         stream sends objects in id order)
*                  a mount missing or named twice here  waits, reported
*                         once - never parented to a guess
*                Parent destruction: the host's World destroys an attached
*                object with its parent, and announces both despawns. A
*                client detaches the network objects below a proxy before
*                destroying it (DetachNetworkDescendants, keeping their
*                world pose), so one the host had detached first - whose
*                snapshot may arrive after the reliable despawn - survives;
*                one the host destroyed is despawned by the same batch.
*
* \history
*    -v1.0       attachments: the Attachment serializer and the client's side
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <functional>

#include "ECS/Core/ECSCore.h"
#include "EngineExport.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	class World;
	struct ReplicatedComponent;

	/// The Attachment serializer (ReplicationRegistry::RegisterBuiltIns registers it).
	ReplicatedComponent AttachmentSerializer();

	/// Finds a network object by its NetworkId on this machine (the session's id map).
	using NetObjectFinder = std::function<Entity(Net::NetworkId)>;

	/// Client: makes `root`'s parent what its NetAttachment says (see the file notes). True when
	/// done (attached as the host has it, or detached); false while it waits for the parent
	/// object or its mount - call again later. Restarts the object's interpolation whenever its
	/// parent changes, and refreshes its query bodies (a new parent can change its world scale).
	ENGINE_API bool ApplyAttachment(World& world, Entity root, const NetObjectFinder& find);

	/// Client: keeps only the newest interpolation sample of `root` (and of its synced children)
	/// and puts the Transform there: a change of parent space is a snap, never a slide.
	ENGINE_API void RestartInterpolation(World& world, Entity root);

	/// Client, before destroying a proxy: every network object attached below it is detached,
	/// keeping its world pose. Returns how many.
	ENGINE_API std::size_t DetachNetworkDescendants(World& world, Entity root);
}
