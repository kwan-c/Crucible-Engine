/*************************************************************************
* \file:         NetReplicationSendSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The host's sending system. This version sends the
*                lifecycle stream only: the world for a joining client,
*                then every spawn and despawn. Snapshots come later.
*
* \details
*                PostUpdate, after physics (priority 1), so a spawn record
*                carries this frame's state. Exclusive: serializers may read
*                any component, and an entity whose spawn record cannot be
*                written is destroyed here. Not SimulationOnly: joins and
*                despawns are still sent while Paused.
*
*                Each frame, on the host (Networking Guide 9.3, 9.4):
*                  1. Joining clients (JoinAccepted sent this frame): their
*                     whole world, from the World as it is now - WorldBegin,
*                     SpawnBatches for every mapped networked entity in id
*                     order, WorldEnd. The peer then waits for ClientReady.
*                  2. Everyone else already Synchronizing or InGame: the
*                     spawns performed this frame, then the despawns queued
*                     since last frame.
*                A joining client gets no incremental messages in its join
*                frame, because its world already holds every change made up
*                to now; from the next frame on it gets all of them. Every
*                change is therefore in exactly one of "the world" or "a
*                later incremental message", which lane 0's ordering then
*                delivers in order.
*
*                Messages are split at kMaxReliableBytes (16 KB) and
*                kMaxEntitiesPerMessage (512). Each is stamped with the
*                peer's next lifecycle sequence (NetworkSession::
*                SendLifecycle); a failed send closes that peer.
*
*                A spawn record that cannot be written (a serializer fails)
*                is a server bug: the entity is left out, destroyed, and
*                reported (NetworkEvents::SpawnFailed). Clients that had it
*                get its despawn next frame; the others ignore that despawn.
*                Entity sets stay equal on every peer.
*
* \history
*    -v1.0       lifecycle only: world stream, spawns, despawns (Networking
*                Guide 9.3, 9.4, PR 8)
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"
#include "Network/NetTypes.h"
#include "Network/Protocol.h"

namespace Crucible
{
	class NetworkSession;
	class NetContent;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members of an exported class: used only inside Engine.dll
#endif
	class ENGINE_API NetReplicationSendSystem final : public IECSSystem
	{
	public:
		/// `session` and `content` are owned by Core and outlive every system.
		NetReplicationSendSystem(NetworkSession& session, const NetContent& content);
		NetReplicationSendSystem(const NetReplicationSendSystem&) = delete;
		NetReplicationSendSystem& operator=(const NetReplicationSendSystem&) = delete;

		SystemStage Stage() const override { return SystemStage::PostUpdate; }
		int         Priority() const override { return 100; }   // after ECSPhysicsSystem (1)
		void        Describe(SystemDesc& desc) const override;
		void        Update(World& world, SystemContext& ctx) override;

	private:
		/// One spawn record, its state bytes at [offset, offset + size) of m_state.
		struct Record
		{
			Net::NetworkId   id = Net::kInvalidNetworkId;
			Net::NetPrefabId prefab = Net::kInvalidNetPrefab;
			Net::PeerId      owner = Net::kServerPeer;
			std::uint8_t     mask = 0;
			std::uint32_t    offset = 0;
			std::uint8_t     size = 0;
		};

		/// Appends the records of `ids` (mapped, alive) to `out`. An entity whose record cannot be
		/// written goes to m_failed instead.
		void BuildRecords(const World& world, std::span<const Net::NetworkId> ids, std::vector<Record>& out);
		bool SendWorld(Net::PeerId peer, std::span<const Record> records);
		bool SendSpawnBatches(Net::PeerId peer, std::span<const Record> records);
		bool SendDespawnBatches(Net::PeerId peer, std::span<const Net::NetworkId> ids);
		bool Send(Net::PeerId peer, std::span<const std::byte> message);
		void DestroyFailed(World& world);

		NetworkSession&             m_session;
		const NetContent&           m_content;

		std::vector<Net::NetworkId> m_spawned;      // this frame's performed spawns
		std::vector<Net::NetworkId> m_despawned;    // queued by the identity hooks
		std::vector<Net::NetworkId> m_destroyed;    // per-client cleanup: unused until snapshots
		std::vector<Net::NetworkId> m_worldIds;     // every mapped entity, in id order
		std::vector<Record>         m_worldRecords;
		std::vector<Record>         m_spawnRecords;
		std::vector<std::byte>      m_state;        // record bodies, back to back
		std::vector<Net::PeerId>    m_joining;      // peers that get the world this frame
		std::vector<Net::PeerId>    m_incremental;  // peers that get this frame's changes
		SpawnBatchView              m_spawnBatch;   // one message being encoded (reused)
		DespawnBatchView            m_despawnBatch;
		std::vector<std::byte>      m_message;      // one encoded message (kMaxReliableBytes)
		struct Failed { Net::NetworkId id; Net::NetPrefabId prefab; std::string reason; };
		std::vector<Failed>         m_failed;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
