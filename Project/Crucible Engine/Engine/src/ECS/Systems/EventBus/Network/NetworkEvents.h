/*************************************************************************
* \file:         NetworkEvents.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.3
* \brief
*                Session-level events on the network bus
*                (Core::Events().network): a session started or ended, a
*                player joined, became ready or left, a join was refused,
*                the world arrived, a spawn failed.
*
* \details
*                NetworkSession posts them (EventBus::Enqueue, main thread);
*                they are delivered with the other buses' events, so a UI or
*                gameplay listener subscribes like any other event:
*                    events.network.Subscribe<NetworkEvents::PeerJoined>(...)
*
*                Rules for every type here:
*                  - Plain structs: copyable, nothrow-movable, no pointers to
*                    things that can die.
*                  - Never a raw Entity: its index and generation mean nothing
*                    on another machine. Players are named by PeerId.
*
* \history
*    -v1.0       placeholder only (no event types yet)
*    -v1.1       trimmed to a single placeholder; the rest of the network
*                design is left for later
*    -v1.2       SessionStarted, SessionEnded, PeerJoined, PeerLeft,
*                JoinRefused (Networking Guide Milestone 1.3)
*    -v1.3       PeerReady, WorldReady, SpawnFailed (Networking Guide 9.3,
*                18.3 issue 4, PR 8)
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <string>

#include "Network/NetTypes.h"

namespace Crucible::NetworkEvents
{
	/// Host: listening. Client: the host accepted the join (localPeer is our PeerId).
	struct SessionStarted
	{
		Net::NetRole role = Net::NetRole::Offline;
		Net::PeerId  localPeer = Net::kInvalidPeer;
	};

	/// The session is over (Stop, the host ended it, the connection was lost or refused).
	struct SessionEnded
	{
		std::string reason;
	};

	/// Host: a client's JoinRequest was accepted.
	struct PeerJoined
	{
		Net::PeerId peer = Net::kInvalidPeer;
		std::string name;
	};

	/// Host: a joined client left or was dropped. endReason is a CloseReason value or a GNS
	/// code (3000-5999: connection problems such as a timeout).
	struct PeerLeft
	{
		Net::PeerId peer = Net::kInvalidPeer;
		int         endReason = 0;
		std::string debug;
	};

	/// Client: the host refused or dropped us before the join completed
	/// (ServerFull, ProtocolMismatch, Timeout, ...). SessionEnded follows.
	struct JoinRefused
	{
		int         reason = 0;
		std::string debug;
	};

	/// Host: a joining client has applied the whole world (its ClientReady arrived) and is now
	/// in the game. The place to spawn its player.
	struct PeerReady
	{
		Net::PeerId peer = Net::kInvalidPeer;
	};

	/// Client: the host's world has been applied (WorldEnd processed, ClientReady sent).
	struct WorldReady
	{
		std::size_t entities = 0;   // networked entities once the world was applied
	};

	/// Host: a requested spawn was not performed, or its spawn record could not be written; no
	/// client ever receives it (or, if clients had it, they get its despawn). The id is never reused.
	struct SpawnFailed
	{
		Net::NetworkId   id = Net::kInvalidNetworkId;
		Net::NetPrefabId prefab = Net::kInvalidNetPrefab;
		std::string      reason;
	};
}
