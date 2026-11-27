/*************************************************************************
* \file:         NetworkSession.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.26
* \brief
*                NetworkSession: start/stop, connection events, the join
*                handshake, bounded receive, quotas and timeouts, spawn
*                requests and the lifecycle stream.
*
* \history
*    -v1.0       connection handling (see the header)
*    -v1.1       review fixes: refused Start changes nothing, FreeSlot
*                guarded, per-message receive budget, client join deadline
*    -v1.2       LastStartError, LastEndReason
*    -v1.3       identity hooks, rejection, despawn ownership;
*                review fix: oversized messages count towards the frame's
*                message limit and their sender's quota, released at once
*    -v1.4       invalid network content refuses Start; client logs its
*                content hash
*    -v1.5       RequestSpawn, SendLifecycle, ClientReady, Synchronizing
*                timeout; the client hands lifecycle messages to
*                NetReceiveSystem
*    -v1.6       SendSnapshot, SendInput, HandleInput; ClientReady counts as
*                confirming the world
*    -v1.7       HandleInput takes commands (by sequence, newest wins, jumps
*                latched); TakePeerIntent with the input timeout; the
*                client's SnapshotAcks() reset by Start and Stop
*    -v1.8       gameplay events, ServerTime(), the input latency probe;
*                review fix: a pending measurement is cancelled
*                when its entity is removed and expires in Update
*    -v1.9       bytes by message type, both directions
*    -v1.10      SceneObjectBatch is a lifecycle message (protocol 4,
*                Networking Phase 14.4)
*    -v1.11      scene objects: AdoptSceneObject, SceneIdOf, the scene
*                identity (Networking Phase 14.5)
*    -v1.12      PlayerSpawnPoints cleared with the entity state (Phase 14.6)
*    -v1.13      network simulation applied by Start, cleared by Stop;
*                PeerLinkStatus, HostLinkStatus (Networking Phase 0)
*    -v1.14      the roster (RefreshRoster, BroadcastRoster,
*                ApplyPeerRoster) and commands (SendCommand, HandleCommand)
*    -v1.15      gameplay events with a target; BeginSpawn
*    -v1.16      events to everyone except one peer; SendUnreliableEvent
*    -v1.17      scene changes: BeginSceneTransition, HandleSceneLoaded, BeginClientSceneLoad,
*                AcceptSceneChange, CheckSceneLoad, ConfirmSceneLoaded, AllPeersReady (Phase 9);
*                RequestStart, ApplyStartRequest (Phase 10)
*    -v1.18      the LAN lobby list (PumpDiscovery, SetLobby, StartBrowsing); closed lobbies
*                refuse joins; LastEndCode (Networking C# lobby browser)
*    -v1.19      a session a script starts no longer becomes the settings of the next Play
*    -v1.20      the LAN connection server: registration, list from the server with LAN fallback, secure lobby
*                ids and owner tokens (BCryptGenRandom), Unregister on Stop; LobbyListingProblem;
*                Auto lists from the server only until UseLanDiscovery; a closed lobby (SetLobby
*                false: the match started) is unregistered from the server, registered again on reopen
*    -v1.21      the scene seed (protocol 9): picked at host start and per scene change (random, or
*                RequestSceneChange(asset, seed)), sent in SceneChange, set on clients before the load
*    -v1.22      ownership transfer: requests, applied changes, OwnerChange is a
*                lifecycle message
*    -v1.23      player input for scripts (protocol 10): SetLocalInput, TakeLocalCommandInput,
*                GetPlayerInput, PublishPlayerInput at the end of every Update; commands' pressed actions
*    -v1.24      pings and kicking (protocol 11): PeerPings once a second, PingOf, Kick; a kicked
*                client stops with 'kicked by the host: <reason>'
*    -v1.25      kept network objects (protocol 12): the host keeps their mapping and lists them in
*                SceneChange; a client keeps its copies; ClientReady after kReadyAfterFrames frames
*    -v1.26      ViewTicks; the lag history is cleared when the session ends
*
**************************************************************************/
#include "Network/NetworkSession.h"

#include "Diagnostics/Logger.h"
#include "ECS/Systems/EventBus/EventBuses.h"
#include "ECS/Systems/EventBus/Network/NetworkEvents.h"
#include "Network/ByteStream.h"
#include "Network/CanonicalHash.h"
#include "Network/Quantize.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <utility>

namespace Crucible
{
	namespace
	{
		using SteadyClock = std::chrono::steady_clock;

		static_assert(NetStats::kMessageTypes == static_cast<std::size_t>(MessageType::Count), "NetStats::kMessageTypes must follow MessageType");

		// Our bytes by message type (the first byte), for NetStats.
		void CountBytes(std::array<std::uint64_t, NetStats::kMessageTypes>& byType, std::span<const std::byte> message) noexcept
		{
			if (message.empty()) return;
			const auto type = static_cast<std::size_t>(message[0]);
			if (type < byType.size()) byType[type] += message.size();
		}

		const char* RoleName(Net::NetRole role) noexcept
		{
			switch (role)
			{
			case Net::NetRole::Server: return "host";
			case Net::NetRole::Client: return "client";
			default:                   return "offline";
			}
		}
	}

	NetworkSession::NetworkSession(EventBuses& events)
		: m_events(events), m_contentHash(DefaultContentHash())
	{
	}

	NetworkSession::~NetworkSession()
	{
		Stop("session destroyed");
	}

	std::uint64_t NetworkSession::DefaultContentHash() noexcept
	{
		CanonicalHash hash;
		hash.Str("Crucible.Net");
		hash.U32(kProtocolVersion);
		hash.U32(NetClock::kTickRate);
		hash.U32(NetClock::kSnapshotInterval);
		return hash.Value();
	}

	void NetworkSession::Post(auto event)
	{
		m_events.network.Enqueue(std::move(event));
	}

	template <typename Msg>
	SendResult NetworkSession::SendReliable(NetConnection connection, const Msg& message)
	{
		std::array<std::byte, 64> buffer;   // the handshake messages are at most 46 bytes
		ByteWriter writer(buffer);
		if (!Encode(writer, message)) return SendResult::Failed;
		const SendResult result = m_transport->Send(connection, writer.Written(), Delivery::Reliable, Lane::Gameplay);
		if (result != SendResult::Ok) ++m_stats.sendFailed;
		else CountBytes(m_stats.bytesSentByType, writer.Written());
		return result;
	}

	// ---- lifecycle -------------------------------------------------------------------------

	bool NetworkSession::Start(const NetworkSettings& settings, std::string& error)
	{
		// A refusal because a session is already running must not touch that session at all.
		const bool running = m_state != SessionState::Offline || m_transport != nullptr;
		const bool started = TryStart(settings, error);
		if (!running) m_lastStartError = started ? std::string() : error;
		return started;
	}

	bool NetworkSession::TryStart(const NetworkSettings& settings, std::string& error)
	{
		// Every check comes before any member changes, so a refused Start leaves the session,
		// its settings and its connections exactly as they were. `settings` may BE m_settings
		// (Start(error)), so work on a copy.
		if (m_state != SessionState::Offline || m_transport != nullptr) { error = "a session is already running; stop it first"; return false; }
		const NetworkSettings proposed = settings;
		if (!Validate(proposed, error)) return false;
		if (proposed.mode == NetMode::Offline) { m_settings = proposed; return true; }
		if (!m_contentError.empty()) { error = "network content is invalid: " + m_contentError; return false; }
		if (!GnsTransport::InitLibrary(error)) return false;   // once per process; kept until Core shuts down

		auto transport = std::make_unique<GnsTransport>();
		NetConnection server = kInvalidConnection;
		if (proposed.mode == NetMode::Host)
		{
			if (!transport->Listen(ToListenSpec(proposed), error)) return false;   // the local transport is destroyed
		}
		else if (!transport->Connect(ToConnectSpec(proposed), server, error))
		{
			return false;
		}

		// Committed: nothing below can fail.
		m_settings = proposed;
		m_active = proposed;
		m_transport = std::move(transport);
		m_carry.clear();
		m_messages.reserve(kMaxMessagesPerFrame);   // so moving messages in never reallocates mid-frame
		m_worldMessages.reserve(kMaxMessagesPerFrame);
		m_peers = {};
		m_stats = {};
		ClearEntityState();
		m_clock.Reset();
		m_snapshotAcks.Reset();
		m_serverTime.Reset();
		m_latency = {};
		m_roster = {};
		m_peerPings.fill(-1);
		m_rosterChanged = false;
		m_nextCommandSequence = 1;
		ResetSceneChange();
		m_lobbyOpen = true;
		m_lobbyStatus.clear();
		NewLobbyIdentity();
		m_advertiseFailed = false;
		m_noServerWarned = false;
		m_endCode = 0;
		m_stopRequested = false;
		m_stopReason.clear();
		m_state = SessionState::Running;
		++m_generation;

		// Testing conditions, for the whole process while this session runs. A refusal only
		// warns: the session itself is fine, it just runs on the real network.
		if (m_active.simulation.IsActive())
		{
			std::string simulationError;
			m_simulationApplied = true;   // even if GNS took only part of it: Stop clears every value
			if (GnsTransport::SetSimulation(m_active.simulation, simulationError))
				CRU_WARN("[Net] simulating a bad network: +{} ms round trip, {}% packet loss each way",
					m_active.simulation.extraRoundTripMs, m_active.simulation.packetLossPercent);
			else
				CRU_ERROR("[Net] network simulation not applied: {}", simulationError);
		}

		if (m_active.mode == NetMode::Host)
		{
			m_role = Net::NetRole::Server;
			m_localPeer = Net::kServerPeer;
			m_sceneSeed = NewSceneSeed();   // the scene the host starts in (protocol 9)
			m_sceneSeedKnown = true;
			RefreshRoster();   // the host is its first player
			CRU_INFO("[Net] hosting on {}:{} (up to {} clients, content hash {:016x})",
				m_active.listenAddress.empty() ? "all interfaces" : m_active.listenAddress,
				m_active.hostPort, m_active.maxClients, m_contentHash);
			Post(NetworkEvents::SessionStarted{ .role = m_role, .localPeer = m_localPeer });
		}
		else
		{
			m_role = Net::NetRole::Client;
			m_serverConnection = server;
			m_clientPhase = ClientPhase::Connecting;
			CRU_INFO("[Net] joining {}:{} as '{}' (content hash {:016x})", m_active.connectAddress, m_active.connectPort, m_active.playerName, m_contentHash);
		}
		return true;
	}

	void NetworkSession::Stop(std::string_view reason) noexcept
	{
		m_startRequested = false;   // a start asked for this frame belongs to what is ending
		m_discovery.StopAdvertising();
		m_lobbyServer.StopHosting();   // Unregister: browsers stop listing it at their next refresh
		m_stopRequested = false;
		if (m_state == SessionState::Offline && m_transport == nullptr) return;
		m_state = SessionState::Stopping;
		try
		{
			const std::string text = reason.empty() ? std::string("session ended") : std::string(reason);
			if (m_transport != nullptr)
			{
				// Each client hears why: the reason travels in the close packet itself. No linger:
				// the listen socket closes in the same call, and a lingering connection whose listen
				// socket is gone never finishes closing - measured, its client got neither the close
				// nor a timeout within 15 s. Without linger the reason arrives within milliseconds.
				for (const PeerState& peer : m_peers)
					if (peer.phase != PeerState::Phase::Free) m_transport->Close(peer.connection, CloseReason::SessionEnded, text.c_str(), false);
				if (m_serverConnection != kInvalidConnection) m_transport->Close(m_serverConnection, CloseReason::Generic, text.c_str(), false);
				m_transport->CloseAll(CloseReason::SessionEnded, false);
			}
			m_messages.clear();      // release received messages while GNS is certainly alive
			m_worldMessages.clear();
			m_batch.clear();
			m_carry.clear();
			m_transport.reset();
			CRU_INFO("[Net] {} session ended: {}", RoleName(m_role), text);
			m_lastEndReason = text;
			m_lastEndCode = std::exchange(m_endCode, 0);
			Post(NetworkEvents::SessionEnded{ .reason = text });
		}
		catch (...)
		{
			m_messages.clear();
			m_worldMessages.clear();
			m_batch.clear();
			m_carry.clear();
			m_transport.reset();
		}
		ClearSimulation();
		m_frameEvents.clear();
		m_peers = {};
		ClearEntityState();   // entities keep their NetworkIdentity; their later removal finds no mapping
		m_role = Net::NetRole::Offline;
		m_clientPhase = ClientPhase::None;
		m_serverConnection = kInvalidConnection;
		m_localPeer = Net::kInvalidPeer;
		m_appliedLifecycle = 0;
		m_snapshotAcks.Reset();
		m_serverTime.Reset();
		m_latency = {};
		m_serverTickAtJoin = 0;
		m_joinDeadline = {};
		m_roster = {};
		m_peerPings.fill(-1);
		m_nextPingBroadcast = {};
		m_keptPending.clear();
		m_readySeq = 0;
		m_readyFramesLeft = 0;
		m_lagHistory.Clear();
		m_rosterChanged = false;
		m_nextCommandSequence = 1;
		ResetSceneChange();
		++m_generation;
		m_state = SessionState::Offline;
	}

	void NetworkSession::ClearSimulation() noexcept
	{
		if (!m_simulationApplied) return;
		m_simulationApplied = false;
		try
		{
			std::string error;
			if (!GnsTransport::SetSimulation(NetworkSimulation{}, error)) CRU_ERROR("[Net] network simulation not cleared: {}", error);
		}
		catch (...)
		{
			// Only the error text can throw (allocation); the values were set or refused before it.
		}
	}

	// ---- link status (diagnostics) ---------------------------------------------------------

	bool NetworkSession::PeerLinkStatus(Net::PeerId peer, ConnectionStatus& out) const
	{
		if (!IsServer() || m_transport == nullptr || peer < 1 || peer > Net::kMaxClients) return false;
		const PeerState& slot = m_peers[peer - 1];
		return slot.phase != PeerState::Phase::Free && m_transport->Status(slot.connection, out);
	}

	bool NetworkSession::HostLinkStatus(ConnectionStatus& out) const
	{
		return IsClient() && m_transport != nullptr && m_serverConnection != kInvalidConnection
			&& m_transport->Status(m_serverConnection, out);
	}

	// ---- networked entities ----------------------------------------------------------------

	void NetworkSession::OnIdentityAdded(Entity entity, const NetworkIdentity& identity, const std::function<bool(Entity)>& isAlive)
	{
		if (m_state != SessionState::Running) return;   // no session to name entities in
		if (m_entities.TryAdd(identity.id, entity, isAlive)) return;

		m_rejected.push_back(entity);
		++m_stats.identityRejected;
		if (identity.id == Net::kInvalidNetworkId)
		{
			CRU_ERROR("[Net] entity {}:{} was given a NetworkIdentity with no id - rejected, destroyed next frame",
				entity.index, entity.generation);
		}
		else
		{
			const Entity holder = m_entities.Find(identity.id);
			CRU_ERROR("[Net] NetworkIdentity {} on entity {}:{} rejected: the id already names entity {}:{} - destroyed next frame",
				identity.id, entity.index, entity.generation, holder.index, holder.generation);
		}
	}

	void NetworkSession::OnIdentityRemoved(Entity entity, const NetworkIdentity& identity)
	{
		// Not the mapped entity (a rejected duplicate, or an entity from before this session):
		// nothing to announce, and the original keeps its mapping.
		if (!m_entities.RemoveIfMapped(identity.id, entity)) return;
		m_destroyedThisFrame.push_back(identity.id);
		m_sceneObjects.erase(identity.id);   // a destroyed scene object is not in later world streams
		if (m_latency.pending && m_latency.player == identity.id) AbandonLatencyProbe();   // it can never move now
		if (m_role == Net::NetRole::Server && m_state == SessionState::Running)
		{
			m_pendingDespawns.push_back(identity.id);
			++m_stats.despawnsQueued;
		}
	}

	void NetworkSession::TakeRejected(std::vector<Entity>& out)
	{
		out.clear();
		out.swap(m_rejected);
	}

	void NetworkSession::TakePendingDespawns(std::vector<Net::NetworkId>& out)
	{
		out.clear();
		out.swap(m_pendingDespawns);
	}

	void NetworkSession::TakeDestroyedThisFrame(std::vector<Net::NetworkId>& out)
	{
		out.clear();
		out.swap(m_destroyedThisFrame);
	}

	void NetworkSession::ClearEntityState() noexcept
	{
		m_entities.Clear();
		m_rejected.clear();
		m_pendingDespawns.clear();
		m_destroyedThisFrame.clear();
		m_pendingSpawns.clear();
		m_spawned.clear();
		m_gameplayEvents.clear();
		m_ownerRequests.clear();
		m_ownerChanges.clear();
		m_sceneObjects.clear();
		m_sceneAsset = 0;
		m_sceneHash = 0;
		m_playerSpawnPoints.clear();
		m_scenePlayerPrefab = Net::kInvalidNetPrefab;
		m_worldMessages.clear();
		m_nextId = 1;
		m_clientWorld = ClientWorld::AwaitingWorld;
	}

	// ---- spawning (server) -------------------------------------------------------------------

	Net::NetworkId NetworkSession::RequestSpawn(Net::NetPrefabId prefab, Net::PeerId owner, const Math::vec3& position,
		const Math::quat& rotation)
	{
		if (m_role != Net::NetRole::Server || m_state != SessionState::Running) return Net::kInvalidNetworkId;
		if (!m_isNetworkPrefab || !m_isNetworkPrefab(prefab))
		{
			CRU_ERROR("[Net] RequestSpawn: prefab {} is not a registered network prefab", prefab);
			return Net::kInvalidNetworkId;
		}
		if (owner > Net::kMaxClients)
		{
			CRU_ERROR("[Net] RequestSpawn: owner {} is not a peer (0 = host, 1..{} = clients)", owner, Net::kMaxClients);
			return Net::kInvalidNetworkId;
		}
		if (m_nextId == Net::kInvalidNetworkId) return Net::kInvalidNetworkId;   // 4 billion spawns: wrapped
		PendingSpawn spawn;
		spawn.id = m_nextId;
		spawn.prefab = prefab;
		spawn.owner = owner;
		spawn.position = position;
		spawn.rotation = rotation;
		m_pendingSpawns.push_back(spawn);   // before the id is used up: a throw here allocates nothing
		++m_nextId;
		return spawn.id;
	}

	Net::NetworkId NetworkSession::BeginSpawn(Net::NetPrefabId prefab, Net::PeerId owner)
	{
		if (m_role != Net::NetRole::Server || m_state != SessionState::Running || m_stopRequested) return Net::kInvalidNetworkId;
		if (!m_isNetworkPrefab || !m_isNetworkPrefab(prefab))
		{
			CRU_ERROR("[Net] BeginSpawn: prefab {} is not a registered network prefab", prefab);
			return Net::kInvalidNetworkId;
		}
		if (owner > Net::kMaxClients)
		{
			CRU_ERROR("[Net] BeginSpawn: owner {} is not a peer (0 = host, 1..{} = clients)", owner, Net::kMaxClients);
			return Net::kInvalidNetworkId;
		}
		if (m_nextId == Net::kInvalidNetworkId) return Net::kInvalidNetworkId;
		++m_stats.spawnsImmediate;
		return m_nextId++;
	}

	void NetworkSession::TakePendingSpawns(std::vector<PendingSpawn>& out)
	{
		out.clear();
		out.swap(m_pendingSpawns);
	}

	void NetworkSession::NotifySpawned(Net::NetworkId id)
	{
		m_spawned.push_back(id);
		++m_stats.spawnsPerformed;
	}

	void NetworkSession::TakeSpawned(std::vector<Net::NetworkId>& out)
	{
		out.clear();
		out.swap(m_spawned);
	}

	void NetworkSession::NotifySpawnFailed(Net::NetworkId id, Net::NetPrefabId prefab, std::string reason)
	{
		++m_stats.spawnsFailed;
		CRU_ERROR("[Net] spawn {} (prefab {}) failed: {}", id, prefab, reason);
		Post(NetworkEvents::SpawnFailed{ .id = id, .prefab = prefab, .reason = std::move(reason) });
	}

	// ---- the lifecycle stream ----------------------------------------------------------------

	PeerState* NetworkSession::PeerById(Net::PeerId peer) noexcept
	{
		if (peer < 1 || peer > Net::kMaxClients) return nullptr;
		PeerState& slot = m_peers[peer - 1];
		return slot.phase == PeerState::Phase::Free ? nullptr : &slot;
	}

	std::uint32_t NetworkSession::NextLifecycleSeq(Net::PeerId peer) const noexcept
	{
		if (peer < 1 || peer > Net::kMaxClients) return 0;
		const PeerState& slot = m_peers[peer - 1];
		return slot.IsJoined() ? slot.nextLifecycleSeq : 0;
	}

	bool NetworkSession::SendLifecycle(Net::PeerId peerId, std::span<const std::byte> message)
	{
		PeerState* peer = PeerById(peerId);
		if (peer == nullptr || !peer->IsJoined() || m_transport == nullptr) return false;

		// Every lifecycle message starts u8 type, u32 lifecycleSeq.
		ByteReader reader(message);
		MessageType type{};
		const bool known = ReadMessageType(reader, type);
		const std::uint32_t seq = reader.U32();
		const bool lifecycle = type == MessageType::WorldBegin || type == MessageType::SpawnBatch || type == MessageType::DespawnBatch
			|| type == MessageType::WorldEnd || type == MessageType::GameplayEvent || type == MessageType::SceneObjectBatch
			|| type == MessageType::PeerRoster || type == MessageType::SceneChange || type == MessageType::OwnerChange;
		if (!known || !reader.Ok() || !lifecycle)
		{
			CRU_ERROR("[Net] SendLifecycle for peer {}: not a lifecycle message - not sent", peerId);
			return false;
		}
		if (seq != peer->nextLifecycleSeq)
		{
			CRU_ERROR("[Net] lifecycle message {} for peer {} is not the next one ({}) - not sent", seq, peerId, peer->nextLifecycleSeq);
			return false;
		}
		if (m_transport->Send(peer->connection, message, Delivery::Reliable, Lane::Gameplay) != SendResult::Ok)
		{
			++m_stats.sendFailed;
			ClosePeer(*peer, CloseReason::SendFailed, std::format("could not send lifecycle message {}", seq));
			return false;
		}
		++peer->nextLifecycleSeq;
		++m_stats.lifecycleMessagesSent;
		CountBytes(m_stats.bytesSentByType, message);
		return true;
	}

	void NetworkSession::MarkWorldSent(Net::PeerId peerId, std::uint32_t worldEndSeq)
	{
		PeerState* peer = PeerById(peerId);
		if (peer == nullptr || peer->phase != PeerState::Phase::Synchronizing) return;
		peer->needsWorld = false;
		peer->worldEndSeq = worldEndSeq;
		++m_stats.worldsSent;
	}

	void NetworkSession::DropPeer(Net::PeerId peerId, CloseReason reason, const std::string& debug)
	{
		if (PeerState* peer = PeerById(peerId); peer != nullptr && m_transport != nullptr) ClosePeer(*peer, reason, debug);
	}

	void NetworkSession::MarkLifecycleApplied(std::uint32_t seq) noexcept
	{
		m_appliedLifecycle = seq;
		++m_stats.lifecycleApplied;
	}

	void NetworkSession::TakeWorldMessages(std::vector<ReceivedMessage>& out)
	{
		out.clear();
		out.swap(m_worldMessages);
		// The capacities swap too; keep this side able to take a whole frame without reallocating.
		if (m_worldMessages.capacity() < kMaxMessagesPerFrame) m_worldMessages.reserve(kMaxMessagesPerFrame);
	}

	bool NetworkSession::CompleteWorld(std::uint32_t worldEndSeq, std::size_t entities)
	{
		if (!IsClient() || m_transport == nullptr) return false;
		// ClientReady waits until this machine has drawn the new world a few times: the first
		// draws compile pipelines and upload textures, which would stall the start of a round.
		// The scripts hear WorldReady now.
		m_readySeq = worldEndSeq;
		m_readyFramesLeft = kReadyAfterFrames;
		CRU_INFO("[Net] world received: {} networked entities", entities);
		Post(NetworkEvents::WorldReady{ .entities = entities });
		return true;
	}

	// Once per frame on a client (Update): counts down the frames before ClientReady.
	void NetworkSession::SendDeferredReady()
	{
		if (m_readySeq == 0 || !IsClient() || m_transport == nullptr) return;
		if (m_readyFramesLeft > 0 && --m_readyFramesLeft > 0) return;
		ClientReady ready;
		ready.worldEndSeq = std::exchange(m_readySeq, 0u);
		if (SendReliable(m_serverConnection, ready) != SendResult::Ok) CloseAndStop(CloseReason::SendFailed, "could not send ClientReady");
	}

	void NetworkSession::RequestStop(std::string_view reason)
	{
		if (m_stopRequested) return;   // the first reason wins
		m_stopRequested = true;
		m_stopReason = std::string(reason);
	}

	// ---- one frame -------------------------------------------------------------------------

	void NetworkSession::Update()
	{
		PumpDiscovery();   // the lobby list: hosting or browsing, also while Offline
		if (m_stopRequested)
		{
			const std::string reason = std::move(m_stopReason);
			Stop(reason);
			PublishPlayerInput();
			return;
		}
		if (m_transport == nullptr) { PublishPlayerInput(); return; }   // offline: this machine's own input
		const SteadyClock::time_point start = SteadyClock::now();
		if (m_latency.pending && start - m_latency.started > kLatencyProbeTimeout) AbandonLatencyProbe();   // whether or not it is drawn

		m_frameEvents.clear();
		m_transport->PollEvents(m_frameEvents);   // GNS callbacks run inside, on this thread
		for (const ConnectionEvent& event : m_frameEvents)
		{
			if (IsServer()) HandleServerEvent(event);
			else HandleClientEvent(event);
		}

		CollectFrameMessages();
		for (ReceivedMessage& message : m_messages)   // the client moves lifecycle messages out
		{
			if (IsServer()) HandleServerMessage(message);
			else HandleClientMessage(message);
		}
		m_messages.clear();   // each destructor returns its buffer to GNS

		if (IsServer())
		{
			EnforceQuotas();
			EnforceJoinTimeouts(start);
			if (start >= m_nextPingBroadcast)
			{
				m_nextPingBroadcast = start + kPingInterval;
				BroadcastPings();
			}
			// After everything that can change it this frame, and before the send system streams
			// the world to a peer that just joined (PostUpdate): its roster comes first.
			if (m_rosterChanged)
			{
				m_rosterChanged = false;
				BroadcastRoster();   // a failed send closes that peer and marks it again for next frame
			}
			// A scene change is complete once no client is still loading it or receiving its world.
			if (m_waitingForPeers && !m_sceneChangeExpected
				&& std::ranges::none_of(m_peers, [](const PeerState& p) { return p.phase == PeerState::Phase::Loading || p.phase == PeerState::Phase::Synchronizing; }))
			{
				m_waitingForPeers = false;
				CRU_INFO("[Net] every player has loaded scene change {}", m_transition);
				Post(NetworkEvents::AllPeersReady{ .transition = m_transition });
			}
		}
		else if (m_clientPhase == ClientPhase::Joined)
		{
			SendDeferredReady();
		}
		else if (m_clientPhase == ClientPhase::Joining && start >= m_joinDeadline)
		{
			// The host accepted the connection but never answered JoinRequest.
			const std::string why = std::format("no JoinAccepted within {} ms", m_active.joinTimeoutMs);
			Post(NetworkEvents::JoinRefused{ .reason = static_cast<int>(CloseReason::Timeout), .debug = why });
			CloseAndStop(CloseReason::Timeout, why);
		}
		PublishPlayerInput();   // after this frame's commands, before the scripts run
		m_stats.receiveMicros = std::chrono::duration_cast<std::chrono::microseconds>(SteadyClock::now() - start).count();
	}

	// Fills m_messages with this frame's work, oldest first: messages carried over from the
	// last frame, then new ones from GNS, until the message or byte budget is reached. The
	// budget is counted per message; the first message of a frame is always taken, so no
	// message can stall the queue. A message over the budget, and everything received after it,
	// waits in m_carry. Nothing new is taken from GNS while m_carry is not empty, so it never
	// holds more than one receive batch (64 messages, each at most kMaxReliableBytes).
	void NetworkSession::CollectFrameMessages()
	{
		for (PeerState& peer : m_peers) peer.messagesThisFrame = 0;
		m_messages.clear();
		std::size_t bytes = 0;
		const auto fits = [&](const ReceivedMessage& message) {
			return m_messages.size() < kMaxMessagesPerFrame
				&& (m_messages.empty() || bytes + message.Data().size() <= kMaxBytesPerFrame);
		};
		const auto take = [&](ReceivedMessage&& message) {
			bytes += message.Data().size();
			m_messages.push_back(std::move(message));   // capacity reserved at Start: cannot throw
		};

		while (!m_carry.empty() && fits(m_carry.front()))
		{
			take(std::move(m_carry.front()));
			m_carry.pop_front();
		}

		// Every message taken from GNS is a message of work, valid or not: oversized ones count
		// towards the frame's message limit too, so a flood of them cannot keep this loop going.
		std::size_t rejected = 0;
		while (m_carry.empty() && m_messages.size() + rejected < kMaxMessagesPerFrame && bytes < kMaxBytesPerFrame)
		{
			m_batch.clear();
			const std::size_t wanted = std::min<std::size_t>(64, kMaxMessagesPerFrame - m_messages.size() - rejected);
			if (m_transport->Receive(m_batch, wanted) == 0) break;
			for (ReceivedMessage& message : m_batch)
			{
				if (message.Data().size() > kMaxReliableBytes)
				{
					RejectOversized(message);
					message = ReceivedMessage(nullptr);   // its buffer goes back to GNS now
					++rejected;
					continue;
				}
				if (m_carry.empty() && fits(message)) take(std::move(message));
				else m_carry.push_back(std::move(message));   // order kept: everything after an overflow waits too
			}
		}
		m_batch.clear();

		m_stats.lastFrameOversized = static_cast<std::uint32_t>(rejected);
		m_stats.lastFrameMessages = static_cast<std::uint32_t>(m_messages.size());
		m_stats.lastFrameBytes = static_cast<std::uint32_t>(bytes);
		m_stats.carriedOver = static_cast<std::uint32_t>(m_carry.size());
		m_stats.messagesReceived += m_messages.size();
		m_stats.bytesReceived += bytes;
		for (const ReceivedMessage& message : m_messages) CountBytes(m_stats.bytesReceivedByType, message.Data());
	}

	// Bigger than any message of our protocol can be: never valid, never read and never carried
	// over. It costs one of the frame's 256 messages (CollectFrameMessages) and one of its sender's
	// 64 a frame, and counts as malformed. Its bytes are not billed to the 256 KB decode budget:
	// they are never decoded, and billing them would let one huge message a frame starve every
	// other peer's valid traffic.
	void NetworkSession::RejectOversized(const ReceivedMessage& message)
	{
		++m_stats.oversized;
		if (IsServer())
		{
			if (PeerState* peer = PeerByConnection(message.Connection()))
			{
				++peer->messagesThisFrame;   // first: Malformed may close (and reset) the peer
				Malformed(*peer, "a message bigger than any protocol message");
			}
			else
			{
				++m_stats.unknownSender;
			}
		}
		else
		{
			++m_stats.malformed;
		}
	}

	// ---- host --------------------------------------------------------------------------------

	std::size_t NetworkSession::OccupiedSlots() const noexcept
	{
		return static_cast<std::size_t>(std::ranges::count_if(m_peers, [](const PeerState& p) { return p.phase != PeerState::Phase::Free; }));
	}

	std::size_t NetworkSession::JoinedPeers() const noexcept
	{
		return static_cast<std::size_t>(std::ranges::count_if(m_peers, [](const PeerState& p) { return p.IsJoined(); }));
	}

	PeerState* NetworkSession::FreeSlot() noexcept
	{
		for (PeerState& peer : m_peers)
			if (peer.phase == PeerState::Phase::Free) return &peer;
		return nullptr;
	}

	PeerState* NetworkSession::PeerByConnection(NetConnection connection) noexcept
	{
		for (PeerState& peer : m_peers)
			if (peer.phase != PeerState::Phase::Free && peer.connection == connection) return &peer;
		return nullptr;
	}

	void NetworkSession::HandleServerEvent(const ConnectionEvent& event)
	{
		switch (event.kind)
		{
		case ConnectionEvent::Kind::Incoming:
		{
			if (!m_lobbyOpen)
			{
				m_transport->Close(event.connection, CloseReason::LobbyClosed, "the game has already started", false);
				++m_stats.refusedFull;
				CRU_INFO("[Net] refused {}: the lobby is closed", event.remoteAddress);
				return;
			}
			if (OccupiedSlots() >= m_active.maxClients)
			{
				m_transport->Close(event.connection, CloseReason::ServerFull, "server full", false);
				++m_stats.refusedFull;
				CRU_INFO("[Net] refused {}: server full ({} of {} slots used)", event.remoteAddress, OccupiedSlots(), m_active.maxClients);
				return;
			}
			// Fewer than maxClients (<= kMaxClients) slots are used, so a free one exists. Checked
			// anyway, BEFORE accepting: a broken invariant refuses the client instead of crashing.
			PeerState* const free = FreeSlot();
			if (free == nullptr)
			{
				m_transport->Close(event.connection, CloseReason::ServerFull, "server full", false);
				++m_stats.refusedFull;
				CRU_ERROR("[Net] no free peer slot although {} of {} are used; refused {}", OccupiedSlots(), m_active.maxClients, event.remoteAddress);
				return;
			}
			if (!m_transport->Accept(event.connection)) return;   // it gave up already
			PeerState& slot = *free;
			slot = PeerState{};
			slot.phase = PeerState::Phase::Connecting;
			slot.connection = event.connection;
			slot.peer = static_cast<Net::PeerId>((&slot - m_peers.data()) + 1);
			slot.remoteAddress = event.remoteAddress;
			m_transport->SetUserData(event.connection, slot.peer);   // every message from it carries its PeerId
			++m_stats.accepted;
			return;
		}
		case ConnectionEvent::Kind::Connected:
			if (PeerState* peer = PeerByConnection(event.connection); peer != nullptr && peer->phase == PeerState::Phase::Connecting)
			{
				peer->phase = PeerState::Phase::AwaitingJoin;
				peer->joinDeadline = SteadyClock::now() + std::chrono::milliseconds(m_active.joinTimeoutMs);
			}
			return;
		case ConnectionEvent::Kind::Closed:
			if (PeerState* peer = PeerByConnection(event.connection))
			{
				CRU_INFO("[Net] peer {} ({}) left: {} ({})", peer->peer, peer->remoteAddress, event.debug, event.endReason);
				const bool wasJoined = peer->IsJoined();
				if (wasJoined)
				{
					++m_stats.peersLeft;
					Post(NetworkEvents::PeerLeft{ .peer = peer->peer, .endReason = event.endReason, .debug = event.debug });
				}
				*peer = PeerState{};   // the transport frees the handle at its next PollEvents
				if (wasJoined) RefreshRoster();
			}
			return;
		}
	}

	void NetworkSession::HandleServerMessage(const ReceivedMessage& message)
	{
		PeerState* peer = PeerByConnection(message.Connection());
		if (peer == nullptr) { ++m_stats.unknownSender; return; }   // closed earlier this frame
		if (++peer->messagesThisFrame > kMaxMessagesPerPeerPerFrame) { ++m_stats.quotaDropped; return; }

		const std::span<const std::byte> data = message.Data();
		ByteReader reader(data);
		MessageType type{};
		if (message.LaneIndex() != Lane::Gameplay) { Malformed(*peer, "a message on a lane clients never use"); return; }
		if (!ReadMessageType(reader, type))        { Malformed(*peer, "an unknown message type"); return; }
		if (data.size() > MaxMessageBytes(type))   { Malformed(*peer, "a message over its size limit"); return; }

		switch (type)
		{
		case MessageType::JoinRequest:
			if (peer->phase != PeerState::Phase::AwaitingJoin) { Malformed(*peer, "a JoinRequest outside the handshake"); return; }
			HandleJoinRequest(*peer, reader);
			return;
		case MessageType::ClientReady:
			HandleClientReady(*peer, reader);
			return;
		case MessageType::Input:
			HandleInput(*peer, reader);
			return;
		case MessageType::Command:
			HandleCommand(*peer, reader);
			return;
		case MessageType::SceneLoaded:
			HandleSceneLoaded(*peer, reader);
			return;
		default:
			Malformed(*peer, "a host-to-client message type");
			return;
		}
	}

	void NetworkSession::HandleJoinRequest(PeerState& peer, ByteReader& reader)
	{
		JoinRequest request;
		if (!Decode(reader, request))
		{
			++m_stats.refusedProtocol;
			ClosePeer(peer, CloseReason::ProtocolMismatch, "unreadable JoinRequest (a different protocol?)");
			return;
		}
		if (request.protocolVersion != kProtocolVersion)
		{
			++m_stats.refusedProtocol;
			ClosePeer(peer, CloseReason::ProtocolMismatch,
				std::format("protocol version {}, host has {}", request.protocolVersion, kProtocolVersion));
			return;
		}
		if (request.contentHash != m_contentHash)
		{
			++m_stats.refusedProtocol;
			ClosePeer(peer, CloseReason::ProtocolMismatch,
				std::format("content hash {:016x}, host has {:016x}", request.contentHash, m_contentHash));
			return;
		}

		JoinAccepted accepted;
		accepted.lifecycleSeq = kFirstLifecycleSeq;
		accepted.peer = peer.peer;
		accepted.serverTick = m_clock.Tick();
		accepted.tickRate = static_cast<std::uint16_t>(NetClock::kTickRate);
		if (SendReliable(peer.connection, accepted) != SendResult::Ok)
		{
			ClosePeer(peer, CloseReason::SendFailed, "could not send JoinAccepted");
			return;
		}

		peer.name = std::string(request.Name());
		peer.nextLifecycleSeq = kFirstLifecycleSeq + 1;
		peer.phase = PeerState::Phase::Synchronizing;
		peer.needsWorld = true;   // the send system streams the world to it this frame
		peer.worldEndSeq = 0;
		peer.syncDeadline = SteadyClock::now() + kSynchronizeTimeout;
		++m_stats.lifecycleMessagesSent;
		++m_stats.joined;
		CRU_INFO("[Net] peer {} '{}' joined from {}", peer.peer, peer.name, peer.remoteAddress);
		Post(NetworkEvents::PeerJoined{ .peer = peer.peer, .name = peer.name });
		RefreshRoster();
	}

	// The client's receipt for the world: accepted only from a Synchronizing peer, and
	// only if it echoes the sequence of the WorldEnd it was sent. Sending a GNS message reliably
	// says nothing about whether the client's World holds the entities; this does.
	void NetworkSession::HandleClientReady(PeerState& peer, ByteReader& reader)
	{
		ClientReady ready;
		if (!Decode(reader, ready)) { Malformed(peer, "an unreadable ClientReady"); return; }
		if (peer.phase == PeerState::Phase::InGame) { ++m_stats.unexpected; return; }   // a duplicate
		if (peer.phase == PeerState::Phase::Loading) { ++m_stats.staleSceneMessages; return; }   // the old scene's world
		if (peer.phase != PeerState::Phase::Synchronizing) { Malformed(peer, "a ClientReady before joining"); return; }
		if (peer.worldEndSeq == 0 || ready.worldEndSeq != peer.worldEndSeq)
		{
			Malformed(peer, "a ClientReady for a WorldEnd that was not sent");
			return;
		}
		peer.phase = PeerState::Phase::InGame;
		peer.appliedLifecycle = std::max(peer.appliedLifecycle, peer.worldEndSeq);   // it applied everything up to WorldEnd
		++m_stats.peersReady;
		CRU_INFO("[Net] peer {} '{}' is in the game", peer.peer, peer.name);
		Post(NetworkEvents::PeerReady{ .peer = peer.peer });
		RefreshRoster();
	}

	// A client's gameplay request. Lane 0 is reliable and ordered, so a sequence that is not
	// newer than the last one can only be a bug or a forgery; a gap means the quota dropped some
	// (counted, the newer one still stands). The sender is the connection's peer, never a field of
	// the packet. A target that no longer exists is an ordinary race with a despawn, not an error.
	void NetworkSession::HandleCommand(PeerState& peer, ByteReader& reader)
	{
		CommandView command;
		if (!Decode(reader, command)) { Malformed(peer, "an unreadable Command"); return; }
		if (peer.phase == PeerState::Phase::Loading || (peer.phase == PeerState::Phase::Synchronizing && m_transition != 0))
		{
			// Sent in the old scene, before this client heard of the change: it is about a world
			// that is gone. The number still counts, so the next one is in order.
			peer.lastCommandSequence = std::max(peer.lastCommandSequence, command.sequence);
			++m_stats.staleSceneMessages;
			return;
		}
		if (peer.phase != PeerState::Phase::InGame) { Malformed(peer, "a Command before it is in the game"); return; }
		if (command.sequence <= peer.lastCommandSequence) { Malformed(peer, "a Command that is not newer than the last one"); return; }
		m_stats.commandsMissed += command.sequence - peer.lastCommandSequence - 1;
		peer.lastCommandSequence = command.sequence;
		if (command.target != Net::kInvalidNetworkId && !m_entities.Contains(command.target)) { ++m_stats.commandsNoTarget; return; }
		++m_stats.commandMessagesReceived;
		Post(NetworkEvents::CommandReceived{ .sender = peer.peer, .kind = command.kind, .target = command.target,
			.payload = { command.payload.begin(), command.payload.end() }, .sequence = command.sequence, .local = false });
	}

	// Input is the client's heartbeat and carries both acknowledgements
	// and its commands. appliedLifecycle is accepted while Synchronizing too, only
	// forwards, and never for a message that was not sent. Commands and snapshot acks only matter
	// once InGame. Commands are taken by sequence: each of them arrives up to 3 times (redundancy),
	// so a copy or an older one is skipped; the newest wins, and a jump in any new one is latched
	// (never lost, never doubled). An ack for a snapshot never sent is dropped. A newer newest
	// replaces the window; the same newest with more bits (an older snapshot arrived late) is merged.
	void NetworkSession::HandleInput(PeerState& peer, ByteReader& reader)
	{
		InputMessage input;
		if (!Decode(reader, input)) { Malformed(peer, "an unreadable Input"); return; }
		if (!peer.IsJoined()) { ++m_stats.unexpected; return; }
		++m_stats.inputsReceived;
		if (input.appliedLifecycle >= peer.nextLifecycleSeq)
		{
			Malformed(peer, "an Input confirming a lifecycle message that was not sent");
			return;
		}
		if (input.appliedLifecycle > peer.appliedLifecycle) peer.appliedLifecycle = input.appliedLifecycle;
		if (peer.phase != PeerState::Phase::InGame) return;   // no snapshots or control yet

		for (const InputCommand& command : input.Commands())   // oldest first; Decode checked the buttons
		{
			if (command.sequence <= peer.lastInputSequence) { ++m_stats.commandsDuplicate; continue; }   // 32 bits: never wraps
			peer.lastInputSequence = command.sequence;
			peer.lastInputTick = m_clock.Tick();
			// Pressed: what the client saw go down, plus anything held now that was not before (a
			// press whose every copy was lost still shows once).
			const std::uint32_t heldBefore = peer.hasCommand ? peer.latest.actions : 0u;
			peer.actionsPressedPending |= command.actionsPressed | (command.actions & ~heldBefore);
			peer.latest = command;
			peer.hasCommand = true;
			peer.inputTimedOut = false;
			peer.jumpLatched = peer.jumpLatched || (command.buttons & InputCommand::kJump) != 0;
			++m_stats.commandsAccepted;
		}

		if (input.ackedSnapshot == 0 || !peer.snapshotSent) return;   // nothing received yet
		if (SequenceGreater(input.ackedSnapshot, peer.lastSentSnapshot)) { ++m_stats.acksDropped; return; }
		if (!peer.hasAck || SequenceGreater(input.ackedSnapshot, peer.newestAckedSnapshot))
		{
			peer.hasAck = true;
			peer.newestAckedSnapshot = input.ackedSnapshot;
			peer.ackBits = input.ackBits;
		}
		else if (input.ackedSnapshot == peer.newestAckedSnapshot)
		{
			peer.ackBits |= input.ackBits;
		}
	}

	SendResult NetworkSession::SendSnapshot(Net::PeerId peerId, std::span<const std::byte> message, std::uint16_t sequence)
	{
		PeerState* peer = PeerById(peerId);
		if (peer == nullptr || peer->phase != PeerState::Phase::InGame || m_transport == nullptr || sequence == 0) return SendResult::Failed;
		const SendResult result = m_transport->Send(peer->connection, message, Delivery::Unreliable, Lane::Snapshot);
		if (result != SendResult::Ok) { ++m_stats.snapshotSendFailed; return result; }
		peer->snapshotSent = true;
		peer->lastSentSnapshot = sequence;
		++m_stats.snapshotsSent;
		m_stats.snapshotBytesSent += message.size();
		CountBytes(m_stats.bytesSentByType, message);
		return result;
	}

	// What a client's players do this frame. The intent stays
	// in force over every step until a newer command replaces it - or until kInputTimeoutTicks steps
	// pass without one (loss burst, frozen client, cable pulled): then it is neutral, so the mech
	// stops within about 270 ms instead of walking for the ~10 s GNS takes to time the connection
	// out, and a jump pressed before the gap is dropped, never replayed. The tick only counts
	// simulation steps, so a paused host times nobody out.
	bool NetworkSession::TakePeerIntent(Net::PeerId peerId, PeerIntent& out)
	{
		out = PeerIntent{};
		PeerState* peer = PeerById(peerId);
		if (peer == nullptr || peer->phase != PeerState::Phase::InGame) return false;
		if (!peer->hasCommand) return true;   // nothing yet: neutral

		out.yawDegrees = Quantize::U16ToAngle(peer->latest.yaw);   // facing is kept, even when timed out
		out.hasYaw = true;
		if (m_clock.Tick() - peer->lastInputTick > kInputTimeoutTicks)
		{
			out.timedOut = true;
			peer->jumpLatched = false;
			if (!peer->inputTimedOut) { peer->inputTimedOut = true; ++m_stats.inputTimeouts; }
			return true;
		}
		Math::vec3 move(Quantize::I8ToSignedUnit(peer->latest.moveX), 0.f, Quantize::I8ToSignedUnit(peer->latest.moveZ));
		if (glm::dot(move, move) > 1.f) move = glm::normalize(move);   // never faster than full speed
		out.moveDirection = move;
		out.jump = peer->jumpLatched;
		peer->jumpLatched = false;
		return true;
	}

	// ---- the shared clock -----------------------------------------------------------------------

	bool NetworkSession::SharedTicks(double& ticks) const noexcept
	{
		ticks = 0.0;
		if (m_state != SessionState::Running) return false;
		if (IsServer()) { ticks = static_cast<double>(m_clock.Tick()); return true; }
		if (!IsClient() || m_clientPhase != ClientPhase::Joined) return false;
		ticks = m_serverTime.Valid() ? std::max(0.0, m_serverTime.Estimate()) : static_cast<double>(m_serverTickAtJoin);
		return true;
	}

	bool NetworkSession::ViewTicks(double& ticks) const noexcept
	{
		if (!SharedTicks(ticks)) return false;
		if (IsClient()) ticks = std::max(0.0, ticks - kViewDelayTicks);
		return true;
	}

	// ---- pings and kicking ----------------------------------------------------------------------

	int NetworkSession::PingOf(Net::PeerId peer) const noexcept
	{
		if (peer >= Net::kMaxPlayers) return -1;
		if (m_role == Net::NetRole::Offline) return peer == Net::kServerPeer ? 0 : -1;
		ConnectionStatus status;
		if (IsServer())
		{
			if (peer == Net::kServerPeer) return 0;
			return PeerLinkStatus(peer, status) ? std::max(0, status.pingMs) : -1;
		}
		if (!m_roster[peer].Present()) return -1;   // not (or no longer) in the session
		if (peer == m_localPeer && HostLinkStatus(status)) return std::max(0, status.pingMs);
		return m_peerPings[peer];
	}

	// Every player's ping, to every InGame client, unreliably: a lost one is replaced a second
	// later. A client still loading gets the next one once it is in the game.
	void NetworkSession::BroadcastPings()
	{
		if (m_transport == nullptr) return;
		PeerPings pings;
		pings.entries[pings.count++] = PeerPings::Entry{ .peer = Net::kServerPeer, .pingMs = 0 };
		for (const PeerState& peer : m_peers)
		{
			if (!peer.IsJoined() || peer.peer == Net::kInvalidPeer || peer.peer > Net::kMaxClients) continue;
			ConnectionStatus status;
			const bool known = m_transport->Status(peer.connection, status) && status.pingMs >= 0;
			pings.entries[pings.count++] = PeerPings::Entry{ .peer = peer.peer,
				.pingMs = known ? static_cast<std::uint16_t>(std::min(status.pingMs, 0xFFFE)) : PeerPings::kUnknown };
		}
		std::array<std::byte, 32> buffer;
		ByteWriter writer(buffer);
		if (!Encode(writer, pings)) return;
		for (const PeerState& peer : m_peers)
		{
			if (peer.phase != PeerState::Phase::InGame) continue;
			if (m_transport->Send(peer.connection, writer.Written(), Delivery::Unreliable, Lane::Snapshot) != SendResult::Ok) continue;
			++m_stats.pingsSent;
			CountBytes(m_stats.bytesSentByType, writer.Written());
		}
	}

	bool NetworkSession::Kick(Net::PeerId peerId, std::string_view reason)
	{
		if (!IsServer() || m_state != SessionState::Running || m_transport == nullptr || peerId == Net::kServerPeer) return false;
		PeerState* peer = PeerById(peerId);
		if (peer == nullptr) return false;
		const std::string why = reason.empty() ? std::string("removed by the host") : std::string(reason.substr(0, 128));
		++m_stats.peersKicked;
		ClosePeer(*peer, CloseReason::Kicked, why);
		return true;
	}

	// ---- player input for scripts --------------------------------------------------------------

	namespace
	{
		float SignedDegrees(float degrees) noexcept   // [0, 360) to (-180, 180]
		{
			return degrees > 180.f ? degrees - 360.f : degrees;
		}

		Math::vec3 FlatMove(const Math::vec3& move) noexcept   // world XZ, never longer than 1
		{
			Math::vec3 flat(std::isfinite(move.x) ? move.x : 0.f, 0.f, std::isfinite(move.z) ? move.z : 0.f);
			if (glm::dot(flat, flat) > 1.f) flat = glm::normalize(flat);
			return flat;
		}
	}

	void NetworkSession::SetLocalInput(const Math::vec3& move, float yawDegrees, float pitchDegrees, std::uint32_t held,
		bool hasMotion) noexcept
	{
		LocalInputState& in = m_localInput;
		const std::uint32_t wentDown = held & ~in.held;
		in.pressedFrame |= wentDown;
		in.pressedCommand |= wentDown;
		in.held = held;
		in.hasMotion = hasMotion;
		in.move = hasMotion ? FlatMove(move) : Math::vec3(0.f);
		in.yawDegrees = hasMotion && std::isfinite(yawDegrees) ? yawDegrees : 0.f;
		in.pitchDegrees = std::isfinite(pitchDegrees) ? pitchDegrees : 0.f;
		in.everSet = true;
		in.setFrame = m_inputFrame;
	}

	bool NetworkSession::LocalInputLive() const noexcept
	{
		return m_localInput.everSet && m_inputFrame - m_localInput.setFrame <= kLocalInputTimeoutFrames;
	}

	bool NetworkSession::TakeLocalCommandInput(InputCommand& command, bool& motion) noexcept
	{
		motion = false;
		if (!LocalInputLive()) { m_localInput.pressedCommand = 0; return false; }
		const LocalInputState& in = m_localInput;
		command.actions = in.held;
		command.actionsPressed = std::exchange(m_localInput.pressedCommand, 0u);
		command.pitch = Quantize::AngleToU16(in.pitchDegrees);
		if (in.hasMotion)
		{
			motion = true;
			command.moveX = Quantize::SignedUnitToI8(in.move.x);
			command.moveZ = Quantize::SignedUnitToI8(in.move.z);
			command.yaw = Quantize::AngleToU16(in.yawDegrees);
		}
		return true;
	}

	// Once per frame, at the end of Update: what scripts read until the next one. A press is
	// handed out in exactly one publish; input that stopped arriving is neutral (no move, nothing
	// held), facing and pitch kept.
	void NetworkSession::PublishPlayerInput() noexcept
	{
		NetPlayerInput& local = m_localPublished;
		local = NetPlayerInput{};
		if (m_localInput.everSet)
		{
			local.yawDegrees = Quantize::U16ToAngle(Quantize::AngleToU16(m_localInput.yawDegrees));   // [0, 360), as remote
			local.pitchDegrees = m_localInput.pitchDegrees;
			if (LocalInputLive())
			{
				local.move = m_localInput.move;
				local.held = m_localInput.held;
				local.pressed = m_localInput.pressedFrame;
				local.live = true;
			}
		}
		m_localInput.pressedFrame = 0;
		++m_inputFrame;

		for (PeerState& peer : m_peers)
		{
			NetPlayerInput& out = peer.published;
			out = NetPlayerInput{};
			if (IsServer() && peer.phase == PeerState::Phase::InGame && peer.hasCommand)
			{
				const InputCommand& c = peer.latest;
				out.yawDegrees = Quantize::U16ToAngle(c.yaw);
				out.pitchDegrees = SignedDegrees(Quantize::U16ToAngle(c.pitch));
				if (m_clock.Tick() - peer.lastInputTick <= kInputTimeoutTicks)
				{
					out.move = FlatMove({ Quantize::I8ToSignedUnit(c.moveX), 0.f, Quantize::I8ToSignedUnit(c.moveZ) });
					out.held = c.actions;
					out.pressed = peer.actionsPressedPending;
					out.live = true;
				}
			}
			peer.actionsPressedPending = 0;
		}
	}

	bool NetworkSession::GetPlayerInput(Net::PeerId peer, NetPlayerInput& out) const noexcept
	{
		out = NetPlayerInput{};
		const bool local = (m_role == Net::NetRole::Offline && peer == Net::kServerPeer)
			|| (m_localPeer != Net::kInvalidPeer && peer == m_localPeer);
		if (local) { out = m_localPublished; return true; }
		if (!IsServer() || peer == Net::kInvalidPeer) return false;
		for (const PeerState& p : m_peers)
		{
			if (p.phase == PeerState::Phase::Free || p.peer != peer) continue;
			out = p.published;
			return p.phase == PeerState::Phase::InGame;
		}
		return false;
	}

	// ---- scene objects ----------------------------------------------------------------------------

	Net::NetworkId NetworkSession::AdoptSceneObject(std::uint64_t sceneId)
	{
		if (m_role != Net::NetRole::Server || m_state != SessionState::Running || sceneId == 0) return Net::kInvalidNetworkId;
		const Net::NetworkId id = m_nextId++;   // the same counter as spawns: never reused
		m_sceneObjects[id] = sceneId;
		++m_stats.sceneObjectsAdopted;
		return id;
	}

	std::uint64_t NetworkSession::SceneIdOf(Net::NetworkId id) const noexcept
	{
		const auto it = m_sceneObjects.find(id);
		return it == m_sceneObjects.end() ? 0 : it->second;
	}

	// ---- gameplay events ------------------------------------------------------------------------

	bool NetworkSession::SendGameplayEvent(std::uint16_t kind, std::span<const std::byte> payload, Net::PeerId to, Net::NetworkId about,
		Net::PeerId except)
	{
		const char* refusal = nullptr;
		if (kind == 0) refusal = "kind 0 is reserved";
		else if (payload.size() > kMaxGameplayEventPayload) refusal = "the payload is too large";
		else if (IsClient()) refusal = "only the host sends gameplay events";
		else if (IsServer() && m_state != SessionState::Running) refusal = "the session is stopping";
		else if (to != kEveryone && to > Net::kMaxClients) refusal = "the target is not a peer";
		else if (except != Net::kInvalidPeer && except > Net::kMaxClients) refusal = "the excepted peer is not a peer";
		else if (about != Net::kInvalidNetworkId && !m_entities.Contains(about)) refusal = "it is about a network object that does not exist";
		else if (IsServer() && m_gameplayEvents.size() >= kMaxQueuedGameplayEvents) refusal = "too many events this frame";
		if (refusal != nullptr)
		{
			++m_stats.gameplayEventsRefused;
			CRU_ERROR("[Net] SendGameplayEvent(kind {}, {} bytes, to {}): {}", kind, payload.size(), to, refusal);
			return false;
		}

		// The host, and an offline game, hear their own events: one listener serves every role.
		if (except != Net::kServerPeer && (to == kEveryone || to == Net::kServerPeer || !IsServer()))
			Post(NetworkEvents::GameplayEventReceived{ .kind = kind, .payload = { payload.begin(), payload.end() }, .local = true, .target = about });
		if (IsServer() && to != Net::kServerPeer && (to == kEveryone || to != except))   // kEveryone and "no exception" are the same value
		{
			m_gameplayEvents.push_back(QueuedGameplayEvent{ .to = to, .kind = kind, .payload = { payload.begin(), payload.end() }, .target = about, .except = except });
			++m_stats.gameplayEventsQueued;
		}
		return true;
	}

	// ---- ownership ---------------------------------------------------------

	bool NetworkSession::RequestOwnerChange(Net::NetworkId id, Net::PeerId owner)
	{
		if (!IsServer() || m_state != SessionState::Running || m_stopRequested)
		{
			CRU_ERROR("[Net] RequestOwnerChange: only a running host changes ownership");
			return false;
		}
		if (id == Net::kInvalidNetworkId || !Entities().Find(id).IsValid())
		{
			CRU_ERROR("[Net] RequestOwnerChange: {} is not a network object", id);
			return false;
		}
		const PeerState* peer = owner == Net::kServerPeer ? nullptr : PeerById(owner);
		if (owner != Net::kServerPeer && (peer == nullptr || !peer->IsJoined()))
		{
			CRU_ERROR("[Net] RequestOwnerChange: peer {} is not in the session", owner);
			return false;
		}
		std::erase_if(m_ownerRequests, [id](const OwnerChangeRequest& r) { return r.id == id; });   // the latest wins
		m_ownerRequests.push_back(OwnerChangeRequest{ .id = id, .owner = owner });
		return true;
	}

	void NetworkSession::TakeOwnerRequests(std::vector<OwnerChangeRequest>& out)
	{
		out.clear();
		out.swap(m_ownerRequests);
	}

	void NetworkSession::NoteOwnerChanged(Net::NetworkId id, Net::PeerId previous, Net::PeerId owner)
	{
		++m_stats.ownerChanges;
		if (IsServer()) m_ownerChanges.push_back(OwnerChangeRequest{ .id = id, .owner = owner });
		Post(NetworkEvents::OwnershipChanged{ .id = id, .previous = previous, .owner = owner });
	}

	void NetworkSession::TakeOwnerChanges(std::vector<OwnerChangeRequest>& out)
	{
		out.clear();
		out.swap(m_ownerChanges);
	}

	void NetworkSession::TakeGameplayEvents(std::vector<QueuedGameplayEvent>& out)
	{
		out.clear();
		out.swap(m_gameplayEvents);
	}

	void NetworkSession::DeliverGameplayEvent(std::uint16_t kind, std::span<const std::byte> payload, Net::NetworkId about)
	{
		// About an object this client no longer has (its despawn was applied first, or a later
		// world never had it): nothing here can use the event.
		if (about != Net::kInvalidNetworkId && !m_entities.Contains(about)) { ++m_stats.gameplayEventsNoTarget; return; }
		++m_stats.gameplayEventsReceived;
		Post(NetworkEvents::GameplayEventReceived{ .kind = kind, .payload = { payload.begin(), payload.end() }, .local = false, .target = about });
	}

	// ---- players ---------------------------------------------------------------------------------

	const NetPeerInfo* NetworkSession::FindPlayer(Net::PeerId peer) const noexcept
	{
		return peer < m_roster.size() && m_roster[peer].Present() ? &m_roster[peer] : nullptr;
	}

	std::size_t NetworkSession::PlayerCount() const noexcept
	{
		return static_cast<std::size_t>(std::ranges::count_if(m_roster, [](const NetPeerInfo& p) { return p.Present(); }));
	}

	// The host's roster is its own slots, read again whenever one of them joins, becomes ready or
	// leaves; Update then sends it to every joined client (BroadcastRoster).
	void NetworkSession::RefreshRoster()
	{
		if (!IsServer()) return;
		m_roster = {};
		m_roster[Net::kServerPeer] = NetPeerInfo{ .peer = Net::kServerPeer, .name = m_active.playerName, .inGame = true };
		for (const PeerState& peer : m_peers)
			if (peer.IsJoined())
				m_roster[peer.peer] = NetPeerInfo{ .peer = peer.peer, .name = peer.name, .inGame = peer.phase == PeerState::Phase::InGame };
		m_rosterChanged = true;
	}

	// The whole list, to every joined client, each with its own next lifecycle number. A client
	// whose send fails is closed by SendLifecycle; that changes the roster again, so the others
	// hear about it next frame.
	void NetworkSession::BroadcastRoster()
	{
		PeerRoster message;
		for (const NetPeerInfo& info : m_roster)
		{
			if (!info.Present()) continue;
			RosterEntry& entry = message.entries[message.count++];
			entry.peer = info.peer;
			entry.flags = info.inGame ? RosterEntry::kInGame : std::uint8_t{ 0 };
			entry.SetName(info.name);   // valid: Start checked the host's, JoinRequest's decoder each client's
		}
		for (std::size_t i = 0; i < m_peers.size(); ++i)   // by index: a failed send resets the slot
		{
			if (!m_peers[i].IsJoined()) continue;
			const Net::PeerId peer = m_peers[i].peer;
			message.lifecycleSeq = m_peers[i].nextLifecycleSeq;
			std::array<std::byte, 160> buffer;   // 146 bytes at most
			ByteWriter writer(buffer);
			if (!Encode(writer, message))
			{
				CRU_ERROR("[Net] the roster for peer {} could not be encoded", peer);
				continue;
			}
			if (SendLifecycle(peer, writer.Written())) ++m_stats.rostersSent;
		}
	}

	// The client's roster is whatever the host said last. What changed since the previous one
	// becomes the same events the host posts for itself, so gameplay needs one listener.
	bool NetworkSession::ApplyPeerRoster(const PeerRoster& roster)
	{
		if (!IsClient() || m_clientPhase != ClientPhase::Joined || m_localPeer >= m_roster.size()) return false;
		std::array<NetPeerInfo, Net::kMaxPlayers> next{};
		for (const RosterEntry& entry : roster.Entries())
		{
			if (entry.peer >= next.size()) return false;   // the decoder checked; never index on trust
			next[entry.peer] = NetPeerInfo{ .peer = entry.peer, .name = std::string(entry.Name()), .inGame = entry.InGame() };
		}
		if (!next[m_localPeer].Present()) return false;

		for (std::size_t i = 0; i < next.size(); ++i)
		{
			if (i == m_localPeer) continue;
			const NetPeerInfo& was = m_roster[i];
			const NetPeerInfo& now = next[i];
			if (!was.Present() && now.Present())
			{
				Post(NetworkEvents::PeerJoined{ .peer = now.peer, .name = now.name });
				if (now.inGame) Post(NetworkEvents::PeerReady{ .peer = now.peer });
			}
			else if (was.Present() && !now.Present())
			{
				Post(NetworkEvents::PeerLeft{ .peer = was.peer, .endReason = 0, .debug = {} });
			}
			else if (was.Present() && now.Present() && !was.inGame && now.inGame)
			{
				Post(NetworkEvents::PeerReady{ .peer = now.peer });
			}
		}
		m_roster = std::move(next);
		++m_stats.rostersApplied;
		return true;
	}

	// ---- commands ------------------------------------------------------------------------------

	bool NetworkSession::SendCommand(std::uint16_t kind, std::span<const std::byte> payload, Net::NetworkId target)
	{
		const char* refusal = nullptr;
		if (kind == 0) refusal = "kind 0 is reserved";
		else if (payload.size() > kMaxCommandPayload) refusal = "the payload is too large";
		else if (IsActive() && (m_state != SessionState::Running || m_stopRequested)) refusal = "the session is stopping";
		else if (IsClient() && (m_clientPhase != ClientPhase::Joined || m_clientWorld != ClientWorld::Ready || m_transport == nullptr))
			refusal = "this client is not in the game yet";
		if (refusal != nullptr)
		{
			++m_stats.commandsRefused;
			CRU_ERROR("[Net] SendCommand(kind {}, {} bytes, target {}): {}", kind, payload.size(), target, refusal);
			return false;
		}

		// The host's own request, and an offline game's, never touch the network: the same event,
		// so the gameplay that answers commands is written once.
		if (!IsClient())
		{
			++m_stats.commandsLocal;
			Post(NetworkEvents::CommandReceived{ .sender = Net::kServerPeer, .kind = kind, .target = target,
				.payload = { payload.begin(), payload.end() }, .sequence = 0, .local = true });
			return true;
		}

		std::array<std::byte, 1 + 4 + 2 + 5 + kMaxCommandPayload> buffer;
		ByteWriter writer(buffer);
		const CommandView view{ .sequence = m_nextCommandSequence, .kind = kind, .target = target, .payload = payload };
		if (!Encode(writer, view) || m_transport->Send(m_serverConnection, writer.Written(), Delivery::Reliable, Lane::Gameplay) != SendResult::Ok)
		{
			++m_stats.commandSendFailed;   // nothing was sent, so the number is not used up
			return false;
		}
		++m_nextCommandSequence;
		++m_stats.commandMessagesSent;
		CountBytes(m_stats.bytesSentByType, writer.Written());
		return true;
	}

	bool NetworkSession::SendUnreliableEvent(std::uint16_t kind, std::span<const std::byte> payload, Net::NetworkId about, Net::PeerId except)
	{
		const char* refusal = nullptr;
		if (kind == 0) refusal = "kind 0 is reserved";
		else if (payload.size() > kMaxUnreliableEventPayload) refusal = "the payload is too large for an unreliable event";
		else if (IsClient()) refusal = "only the host sends gameplay events";
		else if (IsServer() && m_state != SessionState::Running) refusal = "the session is stopping";
		else if (except != Net::kInvalidPeer && except > Net::kMaxClients) refusal = "the excepted peer is not a peer";
		else if (about != Net::kInvalidNetworkId && !m_entities.Contains(about)) refusal = "it is about a network object that does not exist";
		if (refusal != nullptr)
		{
			++m_stats.gameplayEventsRefused;
			CRU_ERROR("[Net] SendUnreliableEvent(kind {}, {} bytes): {}", kind, payload.size(), refusal);
			return false;
		}

		if (except != Net::kServerPeer)
			Post(NetworkEvents::GameplayEventReceived{ .kind = kind, .payload = { payload.begin(), payload.end() }, .local = true, .target = about });
		if (!IsServer() || m_transport == nullptr) return true;

		// Straight to GNS, now: nothing is queued and nothing is retried.
		std::array<std::byte, kMaxSnapshotBytes> buffer;
		ByteWriter writer(buffer);
		if (!Encode(writer, UnreliableEventView{ .kind = kind, .target = about, .payload = payload })) return false;
		for (const PeerState& peer : m_peers)
		{
			if (peer.phase != PeerState::Phase::InGame || peer.peer == except) continue;
			if (m_transport->Send(peer.connection, writer.Written(), Delivery::Unreliable, Lane::Snapshot) == SendResult::Ok)
			{
				++m_stats.unreliableEventsSent;
				CountBytes(m_stats.bytesSentByType, writer.Written());
			}
			else ++m_stats.unreliableEventsNotSent;
		}
		return true;
	}

	// ---- the LAN lobby list ------------------------------------------------------------------------

	void NetworkSession::SetLobby(bool open, std::string status)
	{
		m_lobbyOpen = open;
		m_lobbyStatus = std::move(status);
	}

	bool NetworkSession::StartBrowsing(std::string& error)
	{
		StopBrowsing();
		const LobbyDiscovery mode = m_settings.lobbyDiscovery;
		const bool hasServer = !m_settings.lobbyServerAddress.empty() && m_settings.lobbyServerPort != 0;
		if (mode == LobbyDiscovery::Server && !hasServer)
		{
			error = "Lobby discovery is set to the connection server, but no server address is set.";
			return false;
		}
		if (mode != LobbyDiscovery::Server)
		{
			std::string lanError;
			if (!m_discovery.StartBrowsing(m_settings.discoveryPort, lanError))
			{
				if (mode == LobbyDiscovery::Lan || !hasServer)
				{
					error = lanError;
					return false;
				}
				CRU_WARN("[Net] LAN lobby discovery unavailable ({}); using the connection server only", lanError);
			}
		}
		m_browseMode = mode;
		m_lanChosen = false;   // every browse tries the server first again
		m_browseServerAddress = mode != LobbyDiscovery::Lan && hasServer ? m_settings.lobbyServerAddress : std::string();
		m_browseServerPort = m_settings.lobbyServerPort;
		if (!m_browseServerAddress.empty()) m_lobbyServer.StartBrowsing(kProtocolVersion);
		m_browsing = true;
		return true;
	}

	void NetworkSession::StopBrowsing() noexcept
	{
		m_discovery.StopBrowsing();
		m_lobbyServer.StopBrowsing();
		m_browsing = false;
		m_lanChosen = false;
		m_lobbyList.clear();
		m_listFromServer = false;
	}

	bool NetworkSession::UseLanDiscovery(std::string& error)
	{
		if (!m_browsing)
		{
			error = "not browsing (StartBrowsing first)";
			return false;
		}
		if (m_browseMode == LobbyDiscovery::Server)
		{
			error = "Lobby discovery is Server: the connection server only";
			return false;
		}
		if (m_browseMode == LobbyDiscovery::Lan || m_browseServerAddress.empty()) return true;   // LAN already
		if (!m_discovery.IsBrowsing())
		{
			error = "LAN lobby discovery could not start on this PC";
			return false;
		}
		if (!m_lanChosen) CRU_INFO("[Net] lobby list: LAN discovery, chosen instead of the connection server {}", LobbyServerEndpoint());
		m_lanChosen = true;
		RebuildLobbyList(SteadyClock::now());
		return true;
	}

	bool NetworkSession::CanUseLanDiscovery() const noexcept
	{
		return m_browsing && m_browseMode == LobbyDiscovery::Auto && !m_browseServerAddress.empty() && !m_lanChosen &&
		       m_discovery.IsBrowsing();
	}

	LobbyServerStatus NetworkSession::LobbyServerState() const noexcept
	{
		return m_lobbyServer.Status(SteadyClock::now());
	}

	std::string NetworkSession::LobbyServerEndpoint() const
	{
		if (!m_lobbyServer.HasServer()) return {};
		return std::format("{}:{}", LobbyServer::AddressText(m_lobbyServer.ServerIp()), m_lobbyServer.ServerPort());
	}

	std::string NetworkSession::LobbyListingProblem() const
	{
		if (!IsServer() || m_state != SessionState::Running) return {};
		const LobbyDiscovery mode = m_active.lobbyDiscovery;
		const bool hasServer = !m_active.lobbyServerAddress.empty();
		if (mode == LobbyDiscovery::Server && !hasServer)
			return "Lobby discovery is Server, but no connection server address is set.";
		if (m_advertiseFailed && (mode == LobbyDiscovery::Lan || !hasServer))
			return "LAN lobby lists cannot see it: the discovery port could not be opened.";
		return {};
	}

	// The lobby id and the owner token: from the operating system's secure generator, so another
	// host cannot guess the token and change or remove this lobby at the server.
	void NetworkSession::NewLobbyIdentity()
	{
		const auto fallback = [] {
			std::random_device rd;
			return ((static_cast<std::uint64_t>(rd()) << 32) ^ rd()) | 1u;
		};
		if (!LobbyServerClient::SecureRandom64(m_lobbyInstance)) m_lobbyInstance = fallback();
		if (!LobbyServerClient::SecureRandom64(m_lobbyToken)) m_lobbyToken = fallback();
	}

	namespace
	{
		// At most `limit` bytes, not cutting a UTF-8 character in half.
		std::string Clip(const std::string& text, std::size_t limit)
		{
			if (text.size() <= limit) return text;
			std::size_t n = limit;
			while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
			return text.substr(0, n);
		}
	}

	// A running host keeps its answer up to date every frame (players come and go); anything else
	// answers nobody. Browsing is independent of the session.
	void NetworkSession::PumpDiscovery()
	{
		const auto now = SteadyClock::now();
		const bool hosting = IsServer() && m_state == SessionState::Running;
		const LobbyDiscovery hostMode = m_active.lobbyDiscovery;

		LobbyAdvert advert;
		if (hosting)
		{
			advert.instance = m_lobbyInstance;
			advert.protocolVersion = kProtocolVersion;
			advert.gamePort = m_active.hostPort;
			advert.players = static_cast<std::uint8_t>(PlayerCount());
			advert.maxPlayers = static_cast<std::uint8_t>(m_active.maxClients + 1);
			advert.open = m_lobbyOpen;
			advert.name = m_active.playerName;
			advert.status = m_lobbyStatus;
		}

		// LAN broadcast answers (not in Server mode).
		if (hosting && hostMode != LobbyDiscovery::Server && m_active.discoveryPort != 0 && !m_advertiseFailed)
		{
			std::string error;
			if (!m_discovery.Advertise(m_active.discoveryPort, advert, error))
			{
				m_advertiseFailed = true;   // the session itself is fine: it just is not listed
				CRU_WARN("[Net] this session will not appear in LAN lobby lists: {}", error);
			}
		}
		else if (m_discovery.IsAdvertising())
		{
			m_discovery.StopAdvertising();
		}

		// The connection server: one server per session object. A listed host uses the one it
		// started with; otherwise the browser's. The server is a directory of lobbies, so only an
		// open lobby is registered: closing it (SetLobby false: the match started) unregisters it
		// below (StopHosting), which frees its slot at once - or, if the Unregister is lost, when
		// the server's lifetime passes without a heartbeat. The session itself never depends on
		// it. Opening it again (back to the lobby) registers it again, same id and token.
		const bool hostOnServer = hosting && m_lobbyOpen && hostMode != LobbyDiscovery::Lan && !m_active.lobbyServerAddress.empty();
		if (hostOnServer)
			m_lobbyServer.SetServer(m_active.lobbyServerAddress, m_active.lobbyServerPort);
		else if (m_browsing)
			m_lobbyServer.SetServer(m_browseServerAddress, m_browseServerPort);
		if (hosting && hostMode == LobbyDiscovery::Server && m_active.lobbyServerAddress.empty() && !m_noServerWarned)
		{
			m_noServerWarned = true;
			CRU_WARN("[Net] lobby discovery is 'server' but no connection server address is set: this session is not listed");
		}

		if (hostOnServer)
		{
			if (m_hostNetworks.empty() || now - m_hostNetworksAt > std::chrono::seconds(10))
			{
				m_hostNetworks = LobbyServerClient::LocalNetworks();
				if (m_hostNetworks.size() > LobbyServer::kMaxAddresses) m_hostNetworks.resize(LobbyServer::kMaxAddresses);
				m_hostNetworksAt = now;
			}
			LobbyServer::RegisterMsg registration;
			registration.lobby = m_lobbyInstance;
			registration.ownerToken = m_lobbyToken;
			registration.protocolVersion = kProtocolVersion;
			registration.gamePort = advert.gamePort;
			registration.players = advert.players;
			registration.maxPlayers = advert.maxPlayers;
			registration.open = advert.open;
			registration.name = Clip(advert.name, LobbyServer::kMaxName);
			registration.status = Clip(advert.status, LobbyServer::kMaxStatus);
			registration.addresses = m_hostNetworks;
			m_lobbyServer.Host(registration);
		}
		else if (m_lobbyServer.IsHosting())
		{
			m_lobbyServer.StopHosting();
		}

		m_discovery.Pump(now);
		m_lobbyServer.Pump(now);

		if (m_lobbyServer.TakeIdConflict())
		{
			// Another host already has this id at the server (it would need the same 64 random
			// bits): draw a new one. LAN browsers see the new id at their next query.
			NewLobbyIdentity();
			CRU_WARN("[Net] the connection server already lists a lobby with this id; registering under a new one");
		}

		if (m_browsing) RebuildLobbyList(now);
	}

	// Auto with a server: the server's list (LAN copies of the same lobby are merged in) - empty
	// while it is tried and after it failed - until UseLanDiscovery, then the LAN list. Auto with
	// no server: LAN. Server / Lan: that source only.
	void NetworkSession::RebuildLobbyList(std::chrono::steady_clock::time_point)
	{
		const bool useServer = m_browseMode == LobbyDiscovery::Server ||
		                       (m_browseMode == LobbyDiscovery::Auto && !m_browseServerAddress.empty() && !m_lanChosen);
		m_listFromServer = useServer;
		m_lobbyList.clear();
		if (!useServer)
		{
			m_lobbyList.assign(m_discovery.Lobbies().begin(), m_discovery.Lobbies().end());
			// The same lobby seen at the server too (Auto, server still connecting): say so.
			for (LobbyInfo& lan : m_lobbyList)
				for (const LobbyInfo& s : m_lobbyServer.Lobbies())
					if (s.advert.instance == lan.advert.instance) lan.sources |= kLobbyFromServer;
			return;
		}
		m_lobbyList.assign(m_lobbyServer.Lobbies().begin(), m_lobbyServer.Lobbies().end());
		for (const LobbyInfo& lan : m_discovery.Lobbies())
		{
			const auto same = std::ranges::find_if(m_lobbyList, [&](const LobbyInfo& s) { return s.advert.instance == lan.advert.instance; });
			if (same == m_lobbyList.end()) continue;
			// One lobby, two sources: listed once. The LAN reply came straight from the host to
			// this PC, so its address is the one known to work (127.0.0.1 on the same PC).
			same->sources |= kLobbyFromLan;
			same->address = lan.address;
			same->lastHeard = std::max(same->lastHeard, lan.lastHeard);
		}
	}

	// ---- scene changes (Networking C# Phase 9) ----------------------------------------------------

	void NetworkSession::ResetSceneChange() noexcept
	{
		m_sceneEpoch = 0;
		m_transition = 0;
		m_requestedScene = 0;
		m_requestedSeed = 0;
		m_requestedSeedSet = false;
		m_sceneSeed = 0;
		m_sceneSeedKnown = false;
		m_sceneChangeExpected = false;
		m_sceneLoadIssued = false;
		m_waitingForPeers = false;
		m_confirmPending = false;
	}

	bool NetworkSession::RequestSceneChange(std::uint64_t sceneAsset)
	{
		if (!IsServer() || m_state != SessionState::Running || m_stopRequested || sceneAsset == 0)
		{
			CRU_ERROR("[Net] RequestSceneChange({:016X}): only a running host changes the session's scene", sceneAsset);
			return false;
		}
		m_requestedScene = sceneAsset;
		m_requestedSeedSet = false;   // a plain request: a random seed
		return true;
	}

	bool NetworkSession::RequestSceneChange(std::uint64_t sceneAsset, std::uint32_t seed)
	{
		if (!RequestSceneChange(sceneAsset)) return false;
		m_requestedSeed = seed;
		m_requestedSeedSet = true;
		return true;
	}

	std::uint32_t NetworkSession::NewSceneSeed()
	{
		try
		{
			std::random_device device;
			return device();
		}
		catch (...)   // no entropy source: still a different number each time
		{
			return static_cast<std::uint32_t>(SteadyClock::now().time_since_epoch().count() * 0x9E3779B97F4A7C15ull >> 32);
		}
	}

	bool NetworkSession::TakeSceneChangeRequest(std::uint64_t& sceneAsset) noexcept
	{
		sceneAsset = std::exchange(m_requestedScene, std::uint64_t{ 0 });
		return sceneAsset != 0 && IsServer() && m_state == SessionState::Running;
	}

	// The old scene's entities are about to be destroyed by the load. With the map emptied first,
	// none of those destructions is mapped, so none is announced as a despawn: clients drop their
	// copies with their own scene. Ids keep counting from where they were (never reused).
	void NetworkSession::BeginSceneTransition(std::uint64_t sceneAsset)
	{
		if (!IsServer() || m_state != SessionState::Running) return;
		// The objects kept through the load (DontDestroyOnLoad, KeepAcrossScenes): their mapping
		// stays, so destroying the rest still announces nothing and the kept ones keep their ids.
		std::vector<std::pair<Net::NetworkId, Entity>> kept;
		if (m_keptQuery) m_keptQuery(kept);
		std::ranges::sort(kept, {}, [](const auto& k) { return k.first; });   // by id
		if (kept.size() > kMaxKeptObjects)
		{
			CRU_ERROR("[Net] scene change: {} network objects are kept, at most {} can be - the rest stop being network objects",
				kept.size(), kMaxKeptObjects);
			kept.resize(kMaxKeptObjects);
		}
		const Net::NetworkId nextId = m_nextId;
		ClearEntityState();
		m_nextId = nextId;
		for (const auto& [id, entity] : kept) m_entities.TryAdd(id, entity, [](Entity) { return true; });
		m_lastKeptCount = kept.size();
		++m_transition;
		m_sceneChangeExpected = true;
		m_waitingForPeers = true;
		++m_stats.sceneChanges;
		m_sceneSeed = m_requestedSeedSet ? m_requestedSeed : NewSceneSeed();
		m_sceneSeedKnown = true;
		m_requestedSeedSet = false;
		CRU_INFO("[Net] scene change {}: loading scene {:016X} (seed {}), keeping {} network objects", m_transition, sceneAsset, m_sceneSeed, kept.size());

		const SteadyClock::time_point deadline = SteadyClock::now() + kSceneLoadTimeout;
		for (std::size_t i = 0; i < m_peers.size(); ++i)   // by index: a failed send resets the slot
		{
			PeerState& peer = m_peers[i];
			if (!peer.IsJoined()) continue;
			std::array<std::byte, 384> buffer;
			ByteWriter writer(buffer);
			SceneChange change{ .lifecycleSeq = peer.nextLifecycleSeq, .transition = m_transition, .sceneAsset = sceneAsset, .sceneSeed = m_sceneSeed };
			for (const auto& [id, entity] : kept) change.kept[change.keptCount++] = id;
			const Net::PeerId id = peer.peer;
			if (!Encode(writer, change) || !SendLifecycle(id, writer.Written())) continue;   // closed on failure
			peer.phase = PeerState::Phase::Loading;
			peer.loadDeadline = deadline;
			peer.needsWorld = false;
			peer.worldEndSeq = 0;
			// No snapshot of the old scene is worth acknowledging any more.
			peer.snapshotSent = false;
			peer.hasAck = false;
			peer.ackBits = 0;
			peer.hasCommand = false;
			peer.jumpLatched = false;
		}
		RefreshRoster();   // everyone is loading: not in the game
		Post(NetworkEvents::SceneChangeStarted{ .sceneAsset = sceneAsset, .transition = m_transition });
	}

	void NetworkSession::HandleSceneLoaded(PeerState& peer, ByteReader& reader)
	{
		SceneLoaded loaded;
		if (!Decode(reader, loaded)) { Malformed(peer, "an unreadable SceneLoaded"); return; }
		if (peer.phase != PeerState::Phase::Loading || loaded.transition != m_transition)
		{
			// For a change that was overtaken by a newer one: the newer SceneLoaded follows.
			if (loaded.transition < m_transition) { ++m_stats.staleSceneMessages; return; }
			Malformed(peer, "a SceneLoaded for a scene change that did not happen");
			return;
		}
		peer.phase = PeerState::Phase::Synchronizing;
		peer.needsWorld = true;   // the send system streams the new world this frame
		peer.syncDeadline = SteadyClock::now() + kSynchronizeTimeout;
		CRU_INFO("[Net] peer {} '{}' loaded scene change {}", peer.peer, peer.name, m_transition);
	}

	void NetworkSession::BeginClientSceneLoad(const SceneChange& change, std::span<const std::pair<Net::NetworkId, Entity>> kept)
	{
		if (!IsClient()) return;
		ClearEntityState();   // AwaitingWorld; the proxies go with the scene, but the kept ones
		m_readySeq = 0;       // a ClientReady still waiting belongs to the old world
		m_readyFramesLeft = 0;
		m_keptPending.clear();
		for (const auto& [id, entity] : kept)
		{
			m_entities.TryAdd(id, entity, [](Entity) { return true; });
			m_keptPending.push_back(id);
		}
		m_lastKeptCount = kept.size();
		m_snapshotAcks.Reset();
		m_latency = {};
		m_transition = change.transition;
		m_sceneChangeExpected = true;
		m_confirmPending = true;
		++m_stats.sceneChanges;
		SetSceneSeedFromHost(change.sceneSeed);   // before the scene loads: its scripts' OnCreate sees it
		CRU_INFO("[Net] the host changes scene ({}): loading scene {:016X} (seed {}), keeping {} of its {} kept network objects",
			change.transition, change.sceneAsset, change.sceneSeed, kept.size(), change.keptCount);
		Post(NetworkEvents::SceneChangeStarted{ .sceneAsset = change.sceneAsset, .transition = change.transition });
	}

	bool NetworkSession::IsKeptPending(Net::NetworkId id) const noexcept
	{
		return std::ranges::find(m_keptPending, id) != m_keptPending.end();
	}

	bool NetworkSession::TakeKept(Net::NetworkId id) noexcept
	{
		const auto it = std::ranges::find(m_keptPending, id);
		if (it == m_keptPending.end()) return false;
		m_keptPending.erase(it);
		return true;
	}

	void NetworkSession::TakeUnboundKept(std::vector<Net::NetworkId>& out)
	{
		out = std::move(m_keptPending);
		m_keptPending.clear();
	}

	bool NetworkSession::AcceptSceneChange() noexcept
	{
		if (!m_sceneChangeExpected) return false;
		m_sceneChangeExpected = false;
		m_sceneLoadIssued = false;
		++m_sceneEpoch;
		return true;
	}

	void NetworkSession::CheckSceneLoad()
	{
		if (!m_sceneLoadIssued || !IsActive()) return;
		m_sceneLoadIssued = false;
		m_sceneChangeExpected = false;
		const std::string why = "the scene of the session's scene change could not be loaded";
		if (IsServer()) Stop(why);
		else CloseAndStop(CloseReason::SceneMismatch, why);
	}

	void NetworkSession::ConfirmSceneLoaded()
	{
		if (!IsClient() || !m_confirmPending || m_transport == nullptr || m_serverConnection == kInvalidConnection) return;
		m_confirmPending = false;
		if (SendReliable(m_serverConnection, SceneLoaded{ .transition = m_transition }) != SendResult::Ok)
			CloseAndStop(CloseReason::SendFailed, "could not send SceneLoaded");
	}

	// ---- starting from gameplay (Networking C# Phase 10) --------------------------------------------

	bool NetworkSession::RequestStart(const NetworkSettings& settings, std::string& error)
	{
		if (m_state != SessionState::Offline || m_transport != nullptr) { error = "a session is already running; leave it first"; return false; }
		if (m_startRequested) { error = "a session start is already waiting for the next frame"; return false; }
		if (settings.mode == NetMode::Offline) { error = "choose Host or Client"; return false; }
		if (!Validate(settings, error)) return false;
		m_startSettings = settings;
		m_startRequested = true;
		return true;
	}

	void NetworkSession::ApplyStartRequest()
	{
		if (!m_startRequested) return;
		m_startRequested = false;
		if (m_prepareContent) m_prepareContent();   // may set the content error Start refuses with
		// Start(settings) makes its settings the ones the NEXT Play uses (Settings()). A session a
		// script asked for must not: the Editor's Network panel (Offline) would then host or join
		// at every later Play. The running session keeps its own copy (ActiveSettings()).
		const NetworkSettings playSettings = m_settings;
		std::string error;
		const bool started = Start(m_startSettings, error);
		m_settings = playSettings;
		if (started) return;
		CRU_ERROR("[Net] could not start the {} session: {}", ToString(m_startSettings.mode), error);
		// Gameplay asked: it hears the outcome like any other end of a session.
		m_lastEndCode = 0;
		Post(NetworkEvents::SessionEnded{ .reason = (m_startSettings.mode == NetMode::Host ? "could not host: " : "could not join: ") + error });
	}

	// ---- client time and responsiveness --------------------------------------------------------

	// The one instrument: key down to the first rendered frame in which the
	// own player moved. Expected with v1 (no prediction): one input interval + delivery + one
	// snapshot interval + the 100 ms interpolation delay, ~110 to 150 ms on a LAN.
	void NetworkSession::AbandonLatencyProbe() noexcept
	{
		m_latency.pending = false;
		++m_stats.inputLatencyAbandoned;
	}

	void NetworkSession::NoteMoveStarted(Net::NetworkId player, const Math::vec3& position)
	{
		if (m_latency.pending && SteadyClock::now() - m_latency.started > kLatencyProbeTimeout) AbandonLatencyProbe();
		if (!IsClient() || m_latency.pending) return;
		m_latency = LatencyProbe{ .pending = true, .started = SteadyClock::now(), .player = player, .from = position };
	}

	void NetworkSession::NoteRenderedPosition(Net::NetworkId player, const Math::vec3& position)
	{
		if (!m_latency.pending || player != m_latency.player) return;
		const auto elapsed = SteadyClock::now() - m_latency.started;
		if (elapsed > kLatencyProbeTimeout) { AbandonLatencyProbe(); return; }
		const Math::vec3 moved = position - m_latency.from;
		if (glm::dot(moved, moved) < 0.01f * 0.01f) return;
		m_latency.pending = false;
		const auto ms = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
		m_stats.lastInputLatencyMs = ms;
		m_stats.minInputLatencyMs = m_stats.inputLatencySamples == 0 ? ms : std::min(m_stats.minInputLatencyMs, ms);
		m_stats.maxInputLatencyMs = std::max(m_stats.maxInputLatencyMs, ms);
		m_stats.totalInputLatencyMs += ms;
		++m_stats.inputLatencySamples;
		CRU_INFO("[Net] key-to-motion latency {} ms (min {}, max {}, average {} over {})", ms, m_stats.minInputLatencyMs,
			m_stats.maxInputLatencyMs, m_stats.totalInputLatencyMs / m_stats.inputLatencySamples, m_stats.inputLatencySamples);
	}

	bool NetworkSession::SendInput(const InputMessage& message)
	{
		if (!IsClient() || !IsJoined() || m_transport == nullptr) return false;
		std::array<std::byte, 64> buffer;   // 51 bytes at most
		ByteWriter writer(buffer);
		if (!Encode(writer, message)) return false;
		if (m_transport->Send(m_serverConnection, writer.Written(), Delivery::UnreliableNoDelay, Lane::Gameplay) != SendResult::Ok)
		{
			++m_stats.inputSendFailed;
			return false;
		}
		++m_stats.inputsSent;
		CountBytes(m_stats.bytesSentByType, writer.Written());
		return true;
	}

	void NetworkSession::Malformed(PeerState& peer, const char* what)
	{
		++m_stats.malformed;
		if (++peer.malformed <= kMaxMalformedPerPeer) return;
		++m_stats.closedMalformed;
		ClosePeer(peer, CloseReason::Malformed, std::format("too many malformed messages (last: {})", what));
	}

	void NetworkSession::ClosePeer(PeerState& peer, CloseReason reason, const std::string& debug)
	{
		CRU_INFO("[Net] closing peer {} ({}): {}", peer.peer, peer.remoteAddress, debug);
		m_transport->Close(peer.connection, reason, debug.c_str(), true);   // no Closed event follows our own close
		const bool wasJoined = peer.IsJoined();
		if (wasJoined)
		{
			++m_stats.peersLeft;
			Post(NetworkEvents::PeerLeft{ .peer = peer.peer, .endReason = static_cast<int>(reason), .debug = debug });
		}
		peer = PeerState{};
		if (wasJoined) RefreshRoster();
	}

	void NetworkSession::EnforceQuotas()
	{
		for (PeerState& peer : m_peers)
		{
			if (peer.phase == PeerState::Phase::Free) continue;
			if (peer.messagesThisFrame <= kMaxMessagesPerPeerPerFrame) { peer.quotaStrikes = 0; continue; }
			if (++peer.quotaStrikes < kMaxQuotaStrikes) continue;
			++m_stats.closedMalformed;
			ClosePeer(peer, CloseReason::Malformed, std::format("flooding: over {} messages a frame for {} frames", kMaxMessagesPerPeerPerFrame, kMaxQuotaStrikes));
		}
	}

	void NetworkSession::EnforceJoinTimeouts(SteadyClock::time_point now)
	{
		for (PeerState& peer : m_peers)
		{
			if (peer.phase == PeerState::Phase::AwaitingJoin && now >= peer.joinDeadline)
			{
				++m_stats.joinTimeouts;
				ClosePeer(peer, CloseReason::Timeout, std::format("no JoinRequest within {} ms", m_active.joinTimeoutMs));
			}
			else if (peer.phase == PeerState::Phase::Loading && now >= peer.loadDeadline)
			{
				++m_stats.sceneLoadTimeouts;
				ClosePeer(peer, CloseReason::Timeout, std::format("did not load the scene within {} ms", kSceneLoadTimeout.count()));
			}
			else if (peer.phase == PeerState::Phase::Synchronizing && now >= peer.syncDeadline)
			{
				++m_stats.syncTimeouts;
				ClosePeer(peer, CloseReason::Timeout, std::format("no ClientReady within {} ms", kSynchronizeTimeout.count()));
			}
		}
	}

	// ---- client --------------------------------------------------------------------------------

	void NetworkSession::HandleClientEvent(const ConnectionEvent& event)
	{
		if (event.connection != m_serverConnection) return;
		switch (event.kind)
		{
		case ConnectionEvent::Kind::Connected:
		{
			JoinRequest request;
			request.protocolVersion = kProtocolVersion;
			request.contentHash = m_contentHash;
			request.SetName(m_active.playerName);   // validated by Start
			m_clientPhase = ClientPhase::Joining;
			m_joinDeadline = SteadyClock::now() + std::chrono::milliseconds(m_active.joinTimeoutMs);   // monotonic
			if (SendReliable(m_serverConnection, request) != SendResult::Ok) CloseAndStop(CloseReason::SendFailed, "could not send JoinRequest");
			return;
		}
		case ConnectionEvent::Kind::Closed:
		{
			const std::string why = std::format("{} ({})", event.debug, event.endReason);
			m_endCode = event.endReason;
			if (m_clientPhase != ClientPhase::Joined)
			{
				CRU_WARN("[Net] join refused or failed: {}", why);
				Post(NetworkEvents::JoinRefused{ .reason = event.endReason, .debug = event.debug });
			}
			if (event.endReason == static_cast<int>(CloseReason::Kicked))
				RequestStop("kicked by the host: " + event.debug);
			else
				RequestStop(m_clientPhase == ClientPhase::Joined ? "host ended the session: " + why : "could not join: " + why);
			return;
		}
		default:
			return;
		}
	}

	void NetworkSession::HandleClientMessage(ReceivedMessage& message)
	{
		if (message.Connection() != m_serverConnection) { ++m_stats.unknownSender; return; }
		const std::span<const std::byte> data = message.Data();
		ByteReader reader(data);
		MessageType type{};
		if (!ReadMessageType(reader, type) || data.size() > MaxMessageBytes(type)) { ++m_stats.malformed; return; }

		switch (type)
		{
		case MessageType::JoinAccepted:
		{
			if (m_clientPhase != ClientPhase::Joining) { ++m_stats.unexpected; return; }
			JoinAccepted accepted;
			if (!Decode(reader, accepted))
			{
				++m_stats.malformed;
				CloseAndStop(CloseReason::Malformed, "unreadable JoinAccepted");
				return;
			}
			m_localPeer = accepted.peer;
			m_appliedLifecycle = accepted.lifecycleSeq;
			m_serverTickAtJoin = accepted.serverTick;
			m_clientPhase = ClientPhase::Joined;
			m_clientWorld = ClientWorld::AwaitingWorld;
			CRU_INFO("[Net] joined {}:{} as peer {}", m_active.connectAddress, m_active.connectPort, m_localPeer);
			Post(NetworkEvents::SessionStarted{ .role = Net::NetRole::Client, .localPeer = m_localPeer });
			return;
		}
		case MessageType::PeerPings:
		{
			if (m_clientPhase != ClientPhase::Joined) { ++m_stats.unexpected; return; }
			PeerPings pings;
			if (!Decode(reader, pings)) { ++m_stats.malformed; return; }   // unreliable: the next one replaces it
			m_peerPings.fill(-1);
			for (const PeerPings::Entry& e : pings.Entries())
				m_peerPings[e.peer] = e.pingMs == PeerPings::kUnknown ? -1 : static_cast<int>(e.pingMs);
			++m_stats.pingsReceived;
			return;
		}
		case MessageType::JoinRequest:
		case MessageType::Input:
		case MessageType::ClientReady:
		case MessageType::Command:
		case MessageType::SceneLoaded:
			++m_stats.malformed;   // client-to-host types never come from the host
			return;
		default:
			// The world stream, spawns, despawns, events and snapshots need the World:
			// NetReceiveSystem applies them, in this order, right after Update. Nothing is
			// applied once the session is leaving.
			if (m_clientPhase != ClientPhase::Joined) { ++m_stats.unexpected; return; }
			if (m_stopRequested) return;
			m_worldMessages.push_back(std::move(message));   // capacity: a whole frame (reserved)
			return;
		}
	}

	void NetworkSession::CloseAndStop(CloseReason reason, const std::string& debug)
	{
		if (!IsClient()) return;
		CRU_WARN("[Net] leaving the session: {}", debug);
		m_endCode = static_cast<int>(reason);
		if (m_serverConnection != kInvalidConnection && m_transport != nullptr) m_transport->Close(m_serverConnection, reason, debug.c_str(), true);
		m_serverConnection = kInvalidConnection;
		RequestStop(debug);
	}
}
