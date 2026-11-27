/*************************************************************************
* \file:         NetInputSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          6 November 2026
* \version       1.7
* \brief
*                NetInputSystem: client commands and heartbeat, the host's
*                per-client intents, and the auto-spawned players.
*
* \history
*    -v1.0       client commands, server intents, player spawns
*    -v1.1       starts the key-to-motion measurement
*    -v1.2       the player is the discovered spawnPerPlayer prefab when
*                there is one (Networking Phase 14.2)
*    -v1.3       players spawn at the scene's placed players (Phase 14.6)
*    -v1.4       players spawned again after each scene change (Networking C# Phase 9)
*    -v1.5       a client's commands carry the script's input (actions and pitch; its move
*                and yaw when it owns no PlayerInput), also without a player (protocol 10)
*    -v1.6       players kept through a scene change are adopted and moved to the new scene's
*                spawn point instead of spawned again (protocol 12)
*    -v1.7       the player is the scene's player prefab (the one it places), else the content's default
*
**************************************************************************/
#include "Network/Systems/NetInputSystem.h"

#include <algorithm>
#include <optional>

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Network/NetComponents.h"
#include "Network/NetContent.h"
#include "Network/Quantize.h"
#include "Network/Systems/LocalPlayerInputSystem.h"
#include "Network/Systems/NetReceiveSystem.h"

namespace Crucible
{
	NetInputSystem::NetInputSystem(NetworkSession& session, const NetContent& content, Net::NetPrefabId playerPrefab,
		SpawnPointFn spawnPoint)
		: m_session(session), m_content(content), m_playerPrefab(playerPrefab), m_spawnPoint(std::move(spawnPoint))
	{
		m_customSpawnPoint = static_cast<bool>(m_spawnPoint);
		if (!m_spawnPoint) m_spawnPoint = &DefaultSpawnPoint;
	}

	// One player for `peer`: at the game's spawn point if it gave one, else where the scene placed
	// a player (Phase 14.6), else DefaultSpawnPoint.
	Net::NetworkId NetInputSystem::RequestPlayer(Net::PeerId peer)
	{
		const std::span<const PlayerSpawnPoint> placed = m_session.PlayerSpawnPoints();
		if (m_customSpawnPoint || placed.empty()) return m_session.RequestSpawn(PlayerPrefab(), peer, m_spawnPoint(peer));
		const PlayerSpawnPoint& point = placed[peer % placed.size()];
		const float lap = static_cast<float>(peer / placed.size());
		return m_session.RequestSpawn(PlayerPrefab(), peer, point.position + Math::vec3(3.f * lap, 0.f, 0.f), point.rotation);
	}

	void NetInputSystem::Describe(SystemDesc& desc) const
	{
		// After this frame's messages (acks, lifecycle) and this frame's sampled input. Not
		// SimulationOnly: the client's Input is its heartbeat, sent while Paused too.
		desc.RunAfterIfPresent<NetReceiveSystem>()
			.RunAfterIfPresent<LocalPlayerInputSystem>()
			.Affinity(SystemAffinity::MainThread);   // the session is main-thread only
	}

	void NetInputSystem::Update(World& world, CommandBuffer& commands)
	{
		if (m_session.Generation() != m_generation) BeginSession();   // a new session (or none): forget the old one
		if (m_session.SceneEpoch() != m_sceneEpoch)
		{
			// The session moved to another scene: the old players went with it. Every player is
			// spawned again in the new one (the host now, each client once it has the new world).
			// The command sequence carries on: the host still has the newest one it took.
			m_sceneEpoch = m_session.SceneEpoch();
			m_hostSpawned = false;
			m_hostPlayer = Net::kInvalidNetworkId;
			m_slots = {};
			m_historyCount = 0;
			if (m_session.IsServer() && m_session.State() == SessionState::Running) AdoptKeptPlayers(world);
		}
		if (m_session.State() != SessionState::Running) return;     // Offline, or stopping
		if (m_session.IsClient()) UpdateClient(world);
		else if (m_session.IsServer()) UpdateServer(world, commands);
	}

	void NetInputSystem::BeginSession()
	{
		m_generation = m_session.Generation();
		m_sceneEpoch = m_session.SceneEpoch();
		m_sequence = 0;
		m_lastSentSerial = 0;
		m_historyCount = 0;
		m_wasMoving = false;
		m_hostSpawned = false;
		m_hostPlayer = Net::kInvalidNetworkId;
		m_slots = {};
	}

	// ---- client ----------------------------------------------------------------------------------

	// The message goes out every input interval whether or
	// not this client owns a player right now: before the host spawns it, while it is dead, after a
	// despawn there is simply no command in it. It runs after NetReceiveSystem in the same stage, so
	// the appliedLifecycle and acks it reports describe the World as it is now.
	void NetInputSystem::UpdateClient(World& world)
	{
		if (!m_session.IsJoined() || m_session.IsStopRequested()) return;

		// Every frame: the own player's input going from still to moving starts a key-to-motion
		// measurement; NetInterpolationSystem ends it when it draws the move.
		bool moving = false;
		world.CreateQuery<const NetworkIdentity, const PlayerInput>().ForEach([&](Entity e, const NetworkIdentity& identity, const PlayerInput& input) {
			if (moving || identity.owner != m_session.LocalPeer() || input.moveDirection == Math::vec3(0.f)) return;
			moving = true;
			if (!m_wasMoving)
				if (const Transform* t = world.Read<Transform>(e)) m_session.NoteMoveStarted(identity.id, t->positions);
		});
		m_wasMoving = moving;

		if (!m_session.Clock().InputDue(world.DeltaTime())) return;   // 30 Hz from frame time

		// 1. A command for the player this machine owns (the first, if it somehow owns several),
		//    then the script's input on top: its actions and pitch always; its move and yaw only
		//    when no PlayerInput gave them (a PlayerController keeps driving its own player). A
		//    client with script input but no PlayerInput still sends commands.
		InputCommand command;   // fields by name
		bool commanded = false;
		world.CreateQuery<const NetworkIdentity, PlayerInput>().ForEach([&](Entity e, const NetworkIdentity& identity, PlayerInput& input) {
			if (commanded || identity.owner != m_session.LocalPeer()) return;
			commanded = true;

			// Fresh: the sampler wrote since the last command. If it did not (it stopped running),
			// the command is neutral, so a held key is never repeated.
			const bool fresh = input.sampleSerial != m_lastSentSerial;
			m_lastSentSerial = input.sampleSerial;

			// No camera yaw: send where the mech faces now (the host's, from snapshots), so the
			// host keeps its facing instead of turning it to 0.
			float yaw = input.yawDegrees;
			if (!input.hasYaw)
				if (const Transform* t = world.Read<Transform>(e)) yaw = Quantize::YawDegrees(t->rotation);

			command.moveX = fresh ? Quantize::SignedUnitToI8(input.moveDirection.x) : std::int8_t{ 0 };
			command.moveZ = fresh ? Quantize::SignedUnitToI8(input.moveDirection.z) : std::int8_t{ 0 };
			command.yaw = Quantize::AngleToU16(yaw);
			command.buttons = fresh && input.jump ? InputCommand::kJump : std::uint8_t{ 0 };
			input.jump = false;   // consumed by this command (the proxy's controller is off: nothing else uses it)
		});
		InputCommand script;
		bool motion = false;
		const bool scripted = m_session.TakeLocalCommandInput(script, motion);
		if (scripted)
		{
			command.actions = script.actions;
			command.actionsPressed = script.actionsPressed;
			command.pitch = script.pitch;
			if (motion && !commanded) { command.moveX = script.moveX; command.moveZ = script.moveZ; command.yaw = script.yaw; }
		}
		if (commanded || scripted)
		{
			command.sequence = ++m_sequence;                                   // strictly increasing
			command.clientTick = m_session.Stats().lastSnapshotTick;           // the newest server tick seen (an estimator comes later)
			if (m_historyCount == m_history.size())
			{
				std::move(m_history.begin() + 1, m_history.end(), m_history.begin());
				--m_historyCount;
			}
			m_history[m_historyCount++] = command;
			++m_session.MutableStats().commandsSent;
		}
		else m_historyCount = 0;   // no player and no script input: never resend old commands

		// 2. The message: commands (oldest first), acks, lifecycle confirmation.
		InputMessage message;
		std::copy_n(m_history.begin(), m_historyCount, message.commands.begin());
		message.count = static_cast<std::uint8_t>(m_historyCount);
		message.ackedSnapshot = m_session.SnapshotAcks().Latest();
		message.ackBits = m_session.SnapshotAcks().Bits();
		message.appliedLifecycle = m_session.AppliedLifecycle();
		m_session.SendInput(message);   // a lost one is covered by the next two (redundancy)
	}

	// ---- host ------------------------------------------------------------------------------------

	// The authored player (the discovered prefab with spawnPerPlayer) when there is one, else the
	// prefab this system was given.
	Net::NetPrefabId NetInputSystem::PlayerPrefab() const
	{
		if (m_session.ScenePlayerPrefab() != Net::kInvalidNetPrefab) return m_session.ScenePlayerPrefab();   // the one the scene places
		return m_content.PlayerPrefab() != Net::kInvalidNetPrefab ? m_content.PlayerPrefab() : m_playerPrefab;
	}

	bool NetInputSystem::AutoSpawn() const
	{
		return m_content.Prefabs().Desc(PlayerPrefab()) != nullptr;
	}

	void NetInputSystem::UpdateServer(World& world, CommandBuffer& commands)
	{
		SpawnAndRemovePlayers(world, commands);

		// Each InGame client's intent, taken once per frame (it hands out a jump only once).
		std::array<std::optional<PeerIntent>, Net::kMaxClients + 1> intents;   // index = peer id
		for (const PeerState& peer : m_session.Peers())
		{
			if (peer.phase != PeerState::Phase::InGame || peer.peer == Net::kServerPeer || peer.peer > Net::kMaxClients) continue;
			PeerIntent intent;
			if (m_session.TakePeerIntent(peer.peer, intent)) intents[peer.peer] = intent;
		}

		// Into the PlayerInput of what each client owns. Owner 0 (the host) is local input; an
		// owner that is not InGame (still synchronizing, or gone) gets neutral input.
		const Net::NetTick tick = m_session.Clock().Tick();
		world.CreateQuery<const NetworkIdentity, PlayerInput>().ForEach([&](Entity entity, const NetworkIdentity& identity, PlayerInput& input) {
			if (identity.owner == Net::kServerPeer || identity.owner > Net::kMaxClients) return;
			const std::optional<PeerIntent>& intent = intents[identity.owner];
			// Custom gameplay can consume PlayerInput without a PlayerController. For the
			// built-in controller, do not buffer commands while it cannot consume them.
			const bool controllerReady = !world.HasComponent<PlayerController>(entity)
				|| (world.IsComponentActive<PlayerController>(entity)
					&& world.IsComponentActive<CharacterController>(entity));
			const bool acceptsMovement = intent && !intent->timedOut && controllerReady;
			input.moveDirection = acceptsMovement ? intent->moveDirection : Math::vec3(0.f);
			if (intent && intent->hasYaw) { input.yawDegrees = intent->yawDegrees; input.hasYaw = true; }
			input.jump = acceptsMovement && (input.jump || intent->jump);
			input.tick = tick;
			++input.sampleSerial;
		});
	}

	// After a scene change: a player kept through it (KeepAcrossScenes) is still its peer's player, so
	// it is not spawned again; it moves to where this scene would have spawned it (a Transform write
	// is a teleport for physics too).
	void NetInputSystem::AdoptKeptPlayers(World& world)
	{
		const Net::NetPrefabId playerPrefab = PlayerPrefab();
		if (playerPrefab == Net::kInvalidNetPrefab) return;
		const std::span<const PeerState> peers = m_session.Peers();
		const std::span<const PlayerSpawnPoint> placed = m_session.PlayerSpawnPoints();
		for (const auto& [id, entity] : m_session.Entities())
		{
			const NetworkIdentity* identity = world.IsAlive(entity) ? world.Read<NetworkIdentity>(entity) : nullptr;
			if (identity == nullptr || !m_content.IsPlayerPrefab(identity->prefab)) continue;
			const Net::PeerId peer = identity->owner;
			if (peer == Net::kServerPeer)
			{
				if (m_hostSpawned) continue;   // one player per peer
				m_hostSpawned = true;
				m_hostPlayer = id;
			}
			else if (peer <= Net::kMaxClients && peer <= peers.size() && peers[peer - 1].phase != PeerState::Phase::Free)
			{
				PlayerSlot& slot = m_slots[peer - 1];
				if (slot.player != Net::kInvalidNetworkId) continue;
				slot.connection = peers[peer - 1].connection;
				slot.player = id;
			}
			else continue;

			if (Transform* t = world.Write<Transform>(entity))
			{
				if (!m_customSpawnPoint && !placed.empty())
				{
					const PlayerSpawnPoint& point = placed[peer % placed.size()];
					t->positions = point.position + Math::vec3(3.f * static_cast<float>(peer / placed.size()), 0.f, 0.f);
					t->rotation = point.rotation;
				}
				else t->positions = m_spawnPoint(peer);
			}
			++m_session.MutableStats().playersKept;
			CRU_INFO("[Net] player {} of peer {} kept through the scene change", id, peer);
		}
	}

	// The host's player when the session starts; a client's when it reaches InGame;
	// a departed client's player destroyed. Only while the player prefab is registered.
	void NetInputSystem::SpawnAndRemovePlayers(World& world, CommandBuffer& commands)
	{
		const bool autoSpawn = AutoSpawn();
		if (autoSpawn && !m_hostSpawned)
		{
			m_hostSpawned = true;
			m_hostPlayer = RequestPlayer(Net::kServerPeer);
			if (m_hostPlayer != Net::kInvalidNetworkId) ++m_session.MutableStats().playersSpawned;
		}

		const std::span<const PeerState> peers = m_session.Peers();
		for (std::size_t i = 0; i < m_slots.size() && i < peers.size(); ++i)
		{
			PlayerSlot& slot = m_slots[i];
			const PeerState& peer = peers[i];

			// The client it was spawned for is gone (the slot is free, or already someone else's).
			if (slot.connection != kInvalidConnection && (peer.phase == PeerState::Phase::Free || peer.connection != slot.connection))
			{
				RemovePlayer(world, commands, slot.player);
				slot = PlayerSlot{};
			}
			if (autoSpawn && slot.connection == kInvalidConnection && peer.phase == PeerState::Phase::InGame)
			{
				slot.connection = peer.connection;   // once per connection, even if the spawn is refused
				slot.player = RequestPlayer(peer.peer);
				if (slot.player != Net::kInvalidNetworkId)
				{
					++m_session.MutableStats().playersSpawned;
					CRU_INFO("[Net] spawning player {} for peer {} '{}'", slot.player, peer.peer, peer.name);
				}
			}
		}
	}

	// A spawn requested in one frame is performed at the start of the next (NetReceiveSystem runs
	// before this system), so a player is always mapped by the time its client can be seen leaving.
	// Not found: gameplay destroyed it already, or its spawn failed.
	void NetInputSystem::RemovePlayer(World& world, CommandBuffer& commands, Net::NetworkId player)
	{
		const Entity e = m_session.Entities().Find(player);
		if (player == Net::kInvalidNetworkId || !world.IsAlive(e)) return;
		commands.DestroyEntity(e);   // the identity hook queues its despawn for every client
		++m_session.MutableStats().playersRemoved;
	}
}
