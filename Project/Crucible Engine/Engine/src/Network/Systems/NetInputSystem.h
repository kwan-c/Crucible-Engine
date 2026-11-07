/*************************************************************************
* \file:         NetInputSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          6 November 2026
* \version       1.3
* \brief
*                Player input across the network: a client turns its own
*                player's PlayerInput into commands; the host turns each
*                client's commands back into that client's players'
*                PlayerInput. The host also spawns a player for itself and
*                for every client that joins, and removes it when the
*                client leaves.
*
* \details
*                PreUpdate, after LocalPlayerInputSystem (this frame's
*                sampled input) and NetReceiveSystem (this frame's
*                messages). Not SimulationOnly: a client must keep sending
*                while Paused, because the same message is its heartbeat
*                and carries every acknowledgement (Networking Guide 6.2).
*
*                Client, every input interval (30 Hz, NetClock::InputDue):
*                  - a command for the player this machine owns, if it has
*                    one: a new sequence number, the walk direction, yaw
*                    and jump. NEUTRAL (no walk, no jump; yaw kept) when the
*                    sampler did not write since the last command
*                    (PlayerInput::sampleSerial, issue 5). The jump is
*                    consumed by the command that carries it;
*                  - the last 3 commands go in every message (redundancy:
*                    a command is lost only if 3 packets in a row are), with
*                    the snapshot acks and appliedLifecycle. With no player
*                    there is no command, but the message still goes out.
*
*                Host, every frame:
*                  - auto-spawn (only when there is a player prefab: the
*                    authored one with spawnPerPlayer, else the one given
*                    to the constructor): one player for the
*                    host (owner 0) when the session starts, one for each
*                    client when it reaches InGame (owner = its peer id),
*                    at SpawnPoint(peer); the player of a client that left
*                    is destroyed (which despawns it everywhere);
*                  - each InGame client's intent (NetworkSession::
*                    TakePeerIntent: newest command, clamped, neutral after
*                    the input timeout) goes into the PlayerInput of every
*                    entity that client owns. Nothing else is reachable: the
*                    peer comes from the connection, never from the packet.
*                  The host's own player is never touched here: its input
*                  is local (LocalPlayerInputSystem), with no network delay.
*
*                Offline, and in Edit: nothing.
*
* \history
*    -v1.0       client commands with redundancy, server intents with the
*                input timeout, auto-spawned players (Networking Guide 6.2,
*                6.3, 11.3, PR 11)
*    -v1.1       client: the own player starting to move starts the
*                session's key-to-motion measurement (PR 12)
*    -v1.2       the player prefab: the discovered spawnPerPlayer one, else
*                the constructor's (Networking Phase 14.2)
*    -v1.3       players spawn at the scene's placed players (Phase 14.6)
*
**************************************************************************/
#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"
#include "Math/Math.h"
#include "Network/NetworkSession.h"

namespace Crucible
{
	class NetContent;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members of an exported class: used only inside Engine.dll
#endif
	class ENGINE_API NetInputSystem final : public IECSSystem
	{
	public:
		/// Where peer `peer`'s player appears (0 = the host). Empty: the scene's placed players
		/// (NetworkSession::PlayerSpawnPoints, peer i at point i mod count, 3 m further along +X
		/// per lap), else DefaultSpawnPoint.
		using SpawnPointFn = std::function<Math::vec3(Net::PeerId peer)>;

		/// 3 m apart along +X, 1 m up: peer 0 at (0, 1, 0), peer 1 at (3, 1, 0), ...
		static Math::vec3 DefaultSpawnPoint(Net::PeerId peer) noexcept
		{
			return Math::vec3(3.f * static_cast<float>(peer), 1.f, 0.f);
		}

		/// `session` and `content` are Core's and outlive this system. `playerPrefab`: the network
		/// prefab spawned for each player; nothing is spawned while it is not registered.
		NetInputSystem(NetworkSession& session, const NetContent& content, Net::NetPrefabId playerPrefab,
			SpawnPointFn spawnPoint = {});
		NetInputSystem(const NetInputSystem&) = delete;
		NetInputSystem& operator=(const NetInputSystem&) = delete;

		SystemStage Stage() const override { return SystemStage::PreUpdate; }
		void        Describe(SystemDesc& desc) const override;
		void        Update(World& world, CommandBuffer& commands) override;

		/// The players NetInputSystem spawned this session: index 0 the host's, i the client in
		/// slot i - 1 (kInvalidNetworkId: none). For tests and diagnostics.
		Net::NetworkId HostPlayer() const noexcept { return m_hostPlayer; }
		Net::NetworkId ClientPlayer(std::size_t slot) const noexcept { return slot < m_slots.size() ? m_slots[slot].player : Net::kInvalidNetworkId; }

	private:
		struct PlayerSlot
		{
			NetConnection  connection = kInvalidConnection;   // the client the player was spawned for
			Net::NetworkId player = Net::kInvalidNetworkId;
		};

		void BeginSession();
		void UpdateClient(World& world);
		void UpdateServer(World& world, CommandBuffer& commands);
		void SpawnAndRemovePlayers(World& world, CommandBuffer& commands);
		void RemovePlayer(World& world, CommandBuffer& commands, Net::NetworkId player);
		bool AutoSpawn() const;
		Net::NetPrefabId PlayerPrefab() const;
		Net::NetworkId   RequestPlayer(Net::PeerId peer);

		NetworkSession&   m_session;
		const NetContent& m_content;
		Net::NetPrefabId  m_playerPrefab;
		SpawnPointFn      m_spawnPoint;
		bool              m_customSpawnPoint = false;   // given by the game: wins over placed players
		std::uint32_t     m_generation = 0;   // the session's, when the state below was reset

		// Client
		std::uint32_t                              m_sequence = 0;         // last command's; 1 is the first
		std::uint32_t                              m_lastSentSerial = 0;   // PlayerInput::sampleSerial then
		std::array<InputCommand, kMaxInputCommands> m_history{};           // oldest first
		std::size_t                                m_historyCount = 0;
		bool                                       m_wasMoving = false;    // the own player's input last frame

		// Host
		bool                                     m_hostSpawned = false;
		Net::NetworkId                           m_hostPlayer = Net::kInvalidNetworkId;
		std::array<PlayerSlot, Net::kMaxClients> m_slots{};     // slot i = PeerState slot i
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
