/*************************************************************************
* \file:         NetworkSession.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.12
* \brief
*                NetworkSession: start/stop, connection events, the join
*                handshake, bounded receive, quotas and timeouts, spawn
*                requests and the lifecycle stream.
*
* \history
*    -v1.0       Networking Guide PR 4 (see the header)
*    -v1.1       review fixes: refused Start changes nothing, FreeSlot
*                guarded, per-message receive budget, client join deadline
*    -v1.2       LastStartError, LastEndReason
*    -v1.3       identity hooks, rejection, despawn ownership (PR 6);
*                review fix: oversized messages count towards the frame's
*                message limit and their sender's quota, released at once
*    -v1.4       invalid network content refuses Start; client logs its
*                content hash (PR 7)
*    -v1.5       RequestSpawn, SendLifecycle, ClientReady, Synchronizing
*                timeout; the client hands lifecycle messages to
*                NetReceiveSystem (PR 8)
*    -v1.6       SendSnapshot, SendInput, HandleInput; ClientReady counts as
*                confirming the world (PR 9)
*    -v1.7       HandleInput takes commands (by sequence, newest wins, jumps
*                latched); TakePeerIntent with the input timeout; the
*                client's SnapshotAcks() reset by Start and Stop (PR 11)
*    -v1.8       gameplay events, ServerTime(), the input latency probe
*                (PR 12); review fix: a pending measurement is cancelled
*                when its entity is removed and expires in Update
*    -v1.9       bytes by message type, both directions (PR 13)
*    -v1.10      SceneObjectBatch is a lifecycle message (protocol 4,
*                Networking Phase 14.4)
*    -v1.11      scene objects: AdoptSceneObject, SceneIdOf, the scene
*                identity (Networking Phase 14.5)
*    -v1.12      PlayerSpawnPoints cleared with the entity state (Phase 14.6)
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
#include <format>

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
		m_stopRequested = false;
		m_stopReason.clear();
		m_state = SessionState::Running;
		++m_generation;

		if (m_active.mode == NetMode::Host)
		{
			m_role = Net::NetRole::Server;
			m_localPeer = Net::kServerPeer;
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
		++m_generation;
		m_state = SessionState::Offline;
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
		m_sceneObjects.clear();
		m_sceneAsset = 0;
		m_sceneHash = 0;
		m_playerSpawnPoints.clear();
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
			|| type == MessageType::WorldEnd || type == MessageType::GameplayEvent || type == MessageType::SceneObjectBatch;
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
		ClientReady ready;
		ready.worldEndSeq = worldEndSeq;
		if (SendReliable(m_serverConnection, ready) != SendResult::Ok)
		{
			CloseAndStop(CloseReason::SendFailed, "could not send ClientReady");
			return false;
		}
		CRU_INFO("[Net] world received: {} networked entities", entities);
		Post(NetworkEvents::WorldReady{ .entities = entities });
		return true;
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
		if (m_stopRequested)
		{
			const std::string reason = std::move(m_stopReason);
			Stop(reason);
			return;
		}
		if (m_transport == nullptr) return;
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
		}
		else if (m_clientPhase == ClientPhase::Joining && start >= m_joinDeadline)
		{
			// The host accepted the connection but never answered JoinRequest.
			const std::string why = std::format("no JoinAccepted within {} ms", m_active.joinTimeoutMs);
			Post(NetworkEvents::JoinRefused{ .reason = static_cast<int>(CloseReason::Timeout), .debug = why });
			CloseAndStop(CloseReason::Timeout, why);
		}
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
				if (peer->IsJoined())
				{
					++m_stats.peersLeft;
					Post(NetworkEvents::PeerLeft{ .peer = peer->peer, .endReason = event.endReason, .debug = event.debug });
				}
				*peer = PeerState{};   // the transport frees the handle at its next PollEvents
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
	}

	// The client's receipt for the world (issue 13): accepted only from a Synchronizing peer, and
	// only if it echoes the sequence of the WorldEnd it was sent. Sending a GNS message reliably
	// says nothing about whether the client's World holds the entities; this does.
	void NetworkSession::HandleClientReady(PeerState& peer, ByteReader& reader)
	{
		ClientReady ready;
		if (!Decode(reader, ready)) { Malformed(peer, "an unreadable ClientReady"); return; }
		if (peer.phase == PeerState::Phase::InGame) { ++m_stats.unexpected; return; }   // a duplicate
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
	}

	// Input is the client's heartbeat and carries both acknowledgements (Networking Guide 6.2,
	// issues 1 and 12) and its commands. appliedLifecycle is accepted while Synchronizing too, only
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

	// What a client's players do this frame (Networking Guide 6.2, server step 5). The intent stays
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

	bool NetworkSession::SendGameplayEvent(std::uint16_t kind, std::span<const std::byte> payload, Net::PeerId to)
	{
		const char* refusal = nullptr;
		if (kind == 0) refusal = "kind 0 is reserved";
		else if (payload.size() > kMaxGameplayEventPayload) refusal = "the payload is too large";
		else if (IsClient()) refusal = "only the host sends gameplay events";
		else if (IsServer() && m_state != SessionState::Running) refusal = "the session is stopping";
		else if (to != kEveryone && to > Net::kMaxClients) refusal = "the target is not a peer";
		else if (IsServer() && m_gameplayEvents.size() >= kMaxQueuedGameplayEvents) refusal = "too many events this frame";
		if (refusal != nullptr)
		{
			++m_stats.gameplayEventsRefused;
			CRU_ERROR("[Net] SendGameplayEvent(kind {}, {} bytes, to {}): {}", kind, payload.size(), to, refusal);
			return false;
		}

		// The host, and an offline game, hear their own events: one listener serves every role.
		if (to == kEveryone || to == Net::kServerPeer || !IsServer())
			Post(NetworkEvents::GameplayEventReceived{ .kind = kind, .payload = { payload.begin(), payload.end() }, .local = true });
		if (IsServer() && to != Net::kServerPeer)
		{
			m_gameplayEvents.push_back(QueuedGameplayEvent{ .to = to, .kind = kind, .payload = { payload.begin(), payload.end() } });
			++m_stats.gameplayEventsQueued;
		}
		return true;
	}

	void NetworkSession::TakeGameplayEvents(std::vector<QueuedGameplayEvent>& out)
	{
		out.clear();
		out.swap(m_gameplayEvents);
	}

	void NetworkSession::DeliverGameplayEvent(std::uint16_t kind, std::span<const std::byte> payload)
	{
		++m_stats.gameplayEventsReceived;
		Post(NetworkEvents::GameplayEventReceived{ .kind = kind, .payload = { payload.begin(), payload.end() }, .local = false });
	}

	// ---- client time and responsiveness --------------------------------------------------------

	// Networking Guide 11.7, the one instrument: key down to the first rendered frame in which the
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
		if (peer.IsJoined())
		{
			++m_stats.peersLeft;
			Post(NetworkEvents::PeerLeft{ .peer = peer.peer, .endReason = static_cast<int>(reason), .debug = debug });
		}
		peer = PeerState{};
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
			if (m_clientPhase != ClientPhase::Joined)
			{
				CRU_WARN("[Net] join refused or failed: {}", why);
				Post(NetworkEvents::JoinRefused{ .reason = event.endReason, .debug = event.debug });
			}
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
		case MessageType::JoinRequest:
		case MessageType::Input:
		case MessageType::ClientReady:
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
		if (m_serverConnection != kInvalidConnection && m_transport != nullptr) m_transport->Close(m_serverConnection, reason, debug.c_str(), true);
		m_serverConnection = kInvalidConnection;
		RequestStop(debug);
	}
}
