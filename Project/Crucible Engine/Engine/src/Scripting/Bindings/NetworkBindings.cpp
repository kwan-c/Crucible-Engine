/*************************************************************************
* \file:         NetworkBindings.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          16 November 2026
* \version       1.14
* \brief         Internal calls behind the C# Network class
*                (ManagedScripts/Runtime/Networking/Network.cs) and the network part of
*                Entity (NetworkId, Owner, IsMine), and the queue of network
*                events for the scripts' callbacks.
*
* \details
*    Everything here reads or asks the engine's one NetworkSession
*    (Core::GetNetwork); no gameplay lives in this file.
*
*    Roles as C# sees them: 0 offline, 1 host, 2 client. Offline counts as
*    "the only player, with authority": the local peer is 0, the roster is
*    that one player, and a command is delivered locally - so a script
*    written for multiplayer runs unchanged in a single-player game.
*
*    Events: SetNetwork subscribes to the network bus (NetworkEvents.h) and
*    turns each event a script should hear into a ScriptNetEvent
*    (NetworkContext.h). The bus delivers at the end of a stage, the
*    session posts in PreUpdate, and EntityScriptSystem hands the queue out
*    in Update - so a script hears an event in the frame it happened.
*      SessionStarted (host)   -> Started
*      WorldReady (client)     -> Started   (joined AND the world is here)
*      SessionEnded            -> Stopped
*      PeerJoined/Ready/Left   -> the same, on every machine
*      CommandReceived         -> Command, to the target's scripts only when
*                                 it names a network object
*      GameplayEventReceived   -> Event, the same way
*
*    Events from C# (Network.SendEvent): the authority only, through
*    NetworkSession::SendGameplayEvent - to every machine, itself included.
*
*    Spawning from C# (Network.Spawn): the host performs the spawn at once
*    (NetworkSession::BeginSpawn + NetReceiveSystem::PerformSpawn - scripts
*    run inside an Exclusive system, so the World may change), and the send
*    system announces it the same frame. Offline it is a plain instantiate.
*
*    Commands from C# (Network.SendCommand): a client's goes through
*    NetworkSession::SendCommand to the host. The authority's own (host or
*    offline) is delivered locally next frame; when it is aimed at an
*    entity that is not a network object - every entity of an offline game -
*    it is queued here directly, because no NetworkId can name it.
*
*    Entities cross as (index, generation) and are checked with
*    EntityContext::ResolveEntity, like every other binding.
*
* \history
*    -v1.0       session state, roster, entity identity and the session and
*                player callbacks (Networking C# Phase 1); SendCommand and
*                OnNetworkCommand (Networking C# Phase 2)
*    -v1.1       SendEvent and OnNetworkEvent (Networking C# Phase 5A);
*                Spawn (Networking C# Phase 3)
*    -v1.2       NetVars Get/Set and OnNetVarChanged (Networking C# Phase 4);
*                events: everyone except one peer, unreliable (Networking C# Phase 5B)
*    -v1.3       GetPlayerEntity: a peer's character (found missing in Networking C# Phase 6)
*    -v1.4       LoadScene, OnNetworkSceneLoading, OnAllPeersReady (Networking C# Phase 9);
*                StartHost, Join, Leave (Networking C# Phase 10)
*    -v1.5       lobby list: Browse, LobbyCount, GetLobby, GetLobbyText, SetLobby;
*                LastEndCode, LastEndReason (Networking C# lobby browser)
*    -v1.6       the LAN connection server: GetLobby source flags (4 LAN, 8 server), LobbyServerInfo,
*                LobbyServerAddress, LobbyListingProblem, LobbyBrowseInfo, UseLanDiscovery
*    -v1.7       ObjectOf: the network object an entity belongs to, e.g. a client's raycast hit
*                on a query body (Networking C# Phase 8)
*    -v1.8       the scene seed: LoadSceneSeeded, SceneSeed (protocol 9)
*    -v1.9       SetOwner and OnOwnershipChanged
*    -v1.10      player input: SetLocalInput and GetInput (protocol 10)
*    -v1.11      the shared clock (GetTime), pings (GetPing), Kick; NetVars 64 slots; NetData Get/Set and
*                DataChanged (protocol 11)
*    -v1.12      KeepAcrossScenes, KeepPlayersAcrossScenes, IsKeptAcrossScenes (protocol 12)
*    -v1.13      lag compensation: ViewTick, MaxRewindSeconds, RaycastHistory (Network.RaycastAt)
*    -v1.14      GetPlayerEntity: a player of any player prefab
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "Scripting/Bindings/NetworkContext.h"
#include "Core.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"   // Transform
#include "ECS/Systems/EventBus/EventBuses.h"
#include "ECS/Systems/EventBus/Network/NetworkEvents.h"
#include "Network/NetAnimator.h"   // FindNetworkRoot
#include "Network/NetComponents.h"
#include "Network/NetContent.h"
#include "Network/NetworkSession.h"
#include "Network/Protocol.h"
#include "Network/Systems/NetReceiveSystem.h"   // PerformSpawn
#include "Network/NetworkSettings.h"
#include "AssetPipe/Assetmanager.h"
#include "Scene/SceneManager.h"
#include <filesystem>
#if CRUCIBLE_WITH_MONO
#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <vector>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/loader.h>
#include <mono/metadata/object.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;
		using NetworkContext::ScriptNetEvent;

		// Roles for Network.cs. Must match Network.GetStateInternal's callers.
		constexpr int32_t kRoleOffline = 0;
		constexpr int32_t kRoleHost = 1;
		constexpr int32_t kRoleClient = 2;

		// Results of SendCommandInternal. Must match Network.cs.
		constexpr int32_t kSendOk = 0;
		constexpr int32_t kSendRefused = 1;     // not in the game yet, stopping, or the send buffer is full
		constexpr int32_t kSendBadArgument = 2; // kind outside 1..65535, or a payload over the limit
		constexpr int32_t kSendBadTarget = 3;   // a destroyed entity, or (on a client) one that is not a network object
		constexpr int32_t kSendNotAuthority = 4; // SendEvent on a client

		// Results of SetVarInternal. Must match NetVars.cs.
		constexpr int32_t kVarOk = 0;
		constexpr int32_t kVarBadEntity = 1;      // destroyed, or a slot outside 0..63
		constexpr int32_t kVarNotAuthority = 2;   // a client writing a network object's variable
		constexpr int32_t kVarNotReplicated = 3;  // a network object whose settings do not replicate NetVariables (or NetData)
		constexpr int32_t kDataTooLong = 4;       // NetData: more than NetData::kMaxBytes

		// Results of SpawnInternal. Must match Network.cs.
		constexpr int32_t kSpawnOk = 0;
		constexpr int32_t kSpawnNotAuthority = 1;   // a client
		constexpr int32_t kSpawnNotNetworkPrefab = 2;
		constexpr int32_t kSpawnBadOwner = 3;       // no player with that PeerId
		constexpr int32_t kSpawnFailed = 4;         // see the log

		// Commands wait here while the game is paused; a client that keeps sending cannot grow
		// this without limit. Session and player events are few and always kept.
		constexpr std::size_t kMaxWaitingEvents = 4096;

		Core* core = nullptr;
		std::vector<ScriptNetEvent> waiting;   // received since the scripts last ran
		std::vector<ScriptNetEvent> batch;     // being handed out by EntityScriptSystem
		int32_t nextSerial = 1;
		bool overflowLogged = false;
		// The listeners; a Subscription only holds a weak link to its bus, so releasing these
		// after the bus is gone is safe.
		Subscription onStarted, onWorldReady, onEnded, onJoined, onReady, onLeft, onCommand, onEvent, onSceneLoading,
		    onAllReady, onOwnership;

		NetworkSession* Session()
		{
			return core ? &core->GetNetwork() : nullptr;
		}

		void Queue(ScriptNetEvent&& event)
		{
			const bool message = event.kind == ScriptNetEvent::Kind::Command || event.kind == ScriptNetEvent::Kind::Event;
			if (message && waiting.size() >= kMaxWaitingEvents)
			{
				if (!overflowLogged)
					CRU_WARN("[Net] more than {} network commands and events are waiting for the scripts (is the game paused?) - "
					         "new ones are dropped until they run",
					         kMaxWaitingEvents);
				overflowLogged = true;
				return;
			}
			waiting.push_back(std::move(event));
		}

		void QueueMessage(ScriptNetEvent::Kind what, int32_t sender, int32_t kind, bool targeted, Entity target,
		                  std::span<const std::byte> payload)
		{
			ScriptNetEvent event;
			event.kind = what;
			event.peer = sender;
			event.code = kind;
			event.serial = nextSerial;
			nextSerial = nextSerial == INT32_MAX ? 1 : nextSerial + 1;
			event.targeted = targeted;
			event.target = target;
			event.payload.assign(payload.begin(), payload.end());
			Queue(std::move(event));
		}

		// The network object `entity` is in the running session, or null: it must carry a
		// NetworkIdentity AND be the entity the session maps that id to.
		const NetworkIdentity* IdentityOf(const NetworkSession& session, const World& world, Entity entity)
		{
			const NetworkIdentity* identity = world.Read<NetworkIdentity>(entity);
			if (!identity || identity->id == Net::kInvalidNetworkId || !session.IsActive())
				return nullptr;
			return session.Entities().Find(identity->id) == entity ? identity : nullptr;
		}

		MonoString* NewString(const std::string& text)
		{
			std::wstring wide;
			if (!ToUtf16(text, wide))
				return nullptr;
			return mono_string_new_utf16(mono_domain_get(), reinterpret_cast<const mono_unichar2*>(wide.data()),
			                             static_cast<int32_t>(wide.size()));
		}

		/*********************************************************************
		 \brief  C# Network.IsHost / IsClient / LocalPeerId / IsReady: the role
		         (0 offline, 1 host, 2 client), this machine's PeerId (-1 for a
		         client that has not joined yet) and whether it is in the game.
		 *********************************************************************/
		int32_t ManagedNetworkGetState(int32_t* role, int32_t* localPeer, int32_t* ready) noexcept
		{
			try
			{
				if (!role || !localPeer || !ready)
					return 0;
				*role = kRoleOffline;
				*localPeer = Net::kServerPeer;
				*ready = 1;
				const NetworkSession* session = Session();
				if (!session || !session->IsActive())
					return 1;
				if (session->IsServer())
				{
					*role = kRoleHost;
					return 1;
				}
				*role = kRoleClient;
				*localPeer = session->IsJoined() ? static_cast<int32_t>(session->LocalPeer()) : -1;
				*ready = session->IsJoined() && session->WorldState() == ClientWorld::Ready ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.Peers / PlayerCount: bit i set = PeerId i is in
		         the session. Offline: bit 0 (the only player).
		 *********************************************************************/
		int32_t ManagedNetworkGetPeerMask() noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				if (!session || !session->IsActive())
					return 1;
				int32_t mask = 0;
				for (const NetPeerInfo& player : session->Roster())
					if (player.Present())
						mask |= 1 << player.peer;
				return mask;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.IsPeerConnected / IsPeerReady: 1 if `peer` is in
		         the session; `inGame` says whether its world is complete.
		 *********************************************************************/
		int32_t ManagedNetworkGetPeerInfo(int32_t peer, int32_t* inGame) noexcept
		{
			try
			{
				if (!inGame)
					return 0;
				*inGame = 0;
				const NetworkSession* session = Session();
				if (!session || !session->IsActive())
				{
					*inGame = peer == Net::kServerPeer ? 1 : 0;
					return peer == Net::kServerPeer ? 1 : 0;
				}
				if (peer < 0 || peer >= static_cast<int32_t>(Net::kMaxPlayers))
					return 0;
				const NetPeerInfo* player = session->FindPlayer(static_cast<Net::PeerId>(peer));
				if (!player)
					return 0;
				*inGame = player->inGame ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.GetPeerName: the player's name, or null if nobody
		         holds that PeerId. Offline, peer 0 is this instance's name.
		 *********************************************************************/
		MonoString* ManagedNetworkGetPeerName(int32_t peer) noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				if (!session)
					return nullptr;
				if (!session->IsActive())
					return peer == Net::kServerPeer ? NewString(session->Settings().playerName) : nullptr;
				if (peer < 0 || peer >= static_cast<int32_t>(Net::kMaxPlayers))
					return nullptr;
				const NetPeerInfo* player = session->FindPlayer(static_cast<Net::PeerId>(peer));
				return player ? NewString(player->name) : nullptr;
			}
			catch (...)
			{
				return nullptr;
			}
		}

		/*********************************************************************
		 \brief  C# Entity.IsNetworked / NetworkId / Owner: 1 if the entity is
		         a network object of the running session, with its NetworkId
		         and the PeerId that owns it.
		 *********************************************************************/
		int32_t ManagedNetworkGetIdentity(int32_t index, int32_t generation, uint32_t* networkId, int32_t* owner) noexcept
		{
			try
			{
				if (!networkId || !owner)
					return 0;
				*networkId = Net::kInvalidNetworkId;
				*owner = Net::kServerPeer;
				Entity entity;
				const NetworkSession* session = Session();
				const World* world = EntityContext::GetWorld();
				if (!session || !world || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const NetworkIdentity* identity = IdentityOf(*session, *world, entity);
				if (!identity)
					return 0;
				*networkId = identity->id;
				*owner = identity->owner;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.FindEntity: the local entity of a network object,
		         by its NetworkId (the same number on every machine).
		 *********************************************************************/
		int32_t ManagedNetworkFindEntity(uint32_t networkId, int32_t* outIndex, int32_t* outGeneration) noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				if (!outIndex || !outGeneration || !session || !session->IsActive() || networkId == Net::kInvalidNetworkId)
					return 0;
				const Entity found = session->Entities().Find(networkId);
				Entity checked;   // ResolveEntity also rejects calls outside a Play session
				if (!found.IsValid() || !EntityContext::ResolveEntity(static_cast<int32_t>(found.index),
				                                                      static_cast<int32_t>(found.generation), checked))
					return 0;
				*outIndex = static_cast<int32_t>(found.index);
				*outGeneration = static_cast<int32_t>(found.generation);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.SendCommand. `data` may be null when `length` is 0.
		         Returns one of the kSend* codes above.
		 *********************************************************************/
		int32_t ManagedNetworkSendCommand(int32_t kind, MonoArray* data, int32_t length, int32_t hasTarget, int32_t targetIndex,
		                                  int32_t targetGeneration) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				const World* world = EntityContext::GetWorld();
				if (!session || !world || !EntityContext::InPlaySession())
					return kSendRefused;
				if (kind < 1 || kind > 0xFFFF || length < 0 || static_cast<std::size_t>(length) > kMaxCommandPayload)
					return kSendBadArgument;

				// Copied out at once: nothing below holds a pointer into the C# array.
				std::vector<std::byte> payload;
				if (length > 0)
				{
					GcRoot root(reinterpret_cast<MonoObject*>(data));
					auto* bytes = reinterpret_cast<MonoArray*>(root.Get());
					if (!bytes || mono_array_length(bytes) < static_cast<uintptr_t>(length))
						return kSendBadArgument;
					payload.resize(static_cast<std::size_t>(length));
					std::memcpy(payload.data(), mono_array_addr(bytes, uint8_t, 0), payload.size());
				}

				Entity target;
				Net::NetworkId targetId = Net::kInvalidNetworkId;
				if (hasTarget != 0)
				{
					if (!EntityContext::ResolveEntity(targetIndex, targetGeneration, target))
						return kSendBadTarget;
					if (const NetworkIdentity* identity = IdentityOf(*session, *world, target))
						targetId = identity->id;
					else if (session->IsClient())
						return kSendBadTarget;   // the host could not tell which entity is meant
					else
					{
						// The authority's own command about a local entity: no id can name it, and
						// it never leaves this machine anyway.
						++session->MutableStats().commandsLocal;
						QueueMessage(ScriptNetEvent::Kind::Command, Net::kServerPeer, kind, true, target, payload);
						return kSendOk;
					}
				}
				return session->SendCommand(static_cast<std::uint16_t>(kind), payload, targetId) ? kSendOk : kSendRefused;
			}
			catch (...)
			{
				return kSendRefused;
			}
		}

		/*********************************************************************
		 \brief  C# Network (internal): the payload of the command with this
		         serial in the batch being handed out, as a new byte[] (empty
		         if it has none); null if the serial is not in the batch.
		 *********************************************************************/
		MonoArray* ManagedNetworkGetEventPayload(int32_t serial) noexcept
		{
			try
			{
				const auto found = std::ranges::find_if(batch, [serial](const ScriptNetEvent& e) {
					return (e.kind == ScriptNetEvent::Kind::Command || e.kind == ScriptNetEvent::Kind::Event) && e.serial == serial;
				});
				if (found == batch.end())
					return nullptr;
				MonoArray* bytes = mono_array_new(mono_domain_get(), mono_get_byte_class(), found->payload.size());
				if (bytes && !found->payload.empty())
					std::memcpy(mono_array_addr(bytes, uint8_t, 0), found->payload.data(), found->payload.size());
				return bytes;
			}
			catch (...)
			{
				return nullptr;
			}
		}

		// Copies `length` bytes out of a C# byte[] (null allowed when length is 0). False if the
		// array is shorter than that.
		bool CopyBytes(MonoArray* data, int32_t length, std::vector<std::byte>& out)
		{
			out.clear();
			if (length <= 0)
				return length == 0;
			GcRoot root(reinterpret_cast<MonoObject*>(data));
			auto* bytes = reinterpret_cast<MonoArray*>(root.Get());
			if (!bytes || mono_array_length(bytes) < static_cast<uintptr_t>(length))
				return false;
			out.resize(static_cast<std::size_t>(length));
			std::memcpy(out.data(), mono_array_addr(bytes, uint8_t, 0), out.size());
			return true;
		}

		/*********************************************************************
		 \brief  C# Network.SendEvent: the authority tells every machine (or
		         one player, toPeer >= 0) that something happened. Returns one
		         of the kSend* codes.
		 *********************************************************************/
		int32_t ManagedNetworkSendEvent(int32_t kind, MonoArray* data, int32_t length, int32_t toPeer, int32_t exceptPeer,
		                                int32_t unreliable, int32_t hasTarget, int32_t targetIndex,
		                                int32_t targetGeneration) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				const World* world = EntityContext::GetWorld();
				if (!session || !world || !EntityContext::InPlaySession())
					return kSendRefused;
				if (session->IsClient())
					return kSendNotAuthority;
				const std::size_t limit = unreliable != 0 ? kMaxUnreliableEventPayload : kMaxGameplayEventPayload;
				if (kind < 1 || kind > 0xFFFF || length < 0 || static_cast<std::size_t>(length) > limit ||
				    toPeer < -1 || toPeer > static_cast<int32_t>(Net::kMaxClients) ||
				    exceptPeer < -1 || exceptPeer > static_cast<int32_t>(Net::kMaxClients) ||
				    (unreliable != 0 && toPeer != -1))   // an unreliable event goes to everyone
					return kSendBadArgument;
				std::vector<std::byte> payload;
				if (!CopyBytes(data, length, payload))
					return kSendBadArgument;

				const Net::PeerId to = toPeer < 0 ? NetworkSession::kEveryone : static_cast<Net::PeerId>(toPeer);
				const Net::PeerId except = exceptPeer < 0 ? Net::kInvalidPeer : static_cast<Net::PeerId>(exceptPeer);
				Net::NetworkId about = Net::kInvalidNetworkId;
				if (hasTarget != 0)
				{
					Entity target;
					if (!EntityContext::ResolveEntity(targetIndex, targetGeneration, target))
						return kSendBadTarget;
					if (const NetworkIdentity* identity = IdentityOf(*session, *world, target))
						about = identity->id;
					else if (session->IsServer())
						return kSendBadTarget;   // the clients could not tell which entity is meant
					else
					{
						// Offline: no ids exist and this machine is the only listener.
						if (except != Net::kServerPeer && (to == NetworkSession::kEveryone || to == Net::kServerPeer))
							QueueMessage(ScriptNetEvent::Kind::Event, Net::kServerPeer, kind, true, target, payload);
						return kSendOk;
					}
				}
				const bool sent = unreliable != 0
					? session->SendUnreliableEvent(static_cast<std::uint16_t>(kind), payload, about, except)
					: session->SendGameplayEvent(static_cast<std::uint16_t>(kind), payload, to, about, except);
				return sent ? kSendOk : kSendRefused;
			}
			catch (...)
			{
				return kSendRefused;
			}
		}

		/*********************************************************************
		 \brief  C# Network.Spawn: the authority spawns a network prefab (by
		         asset guid) at a position and rotation, owned by `owner`, and
		         gets its root at once. Offline: an ordinary instantiate.
		         Returns one of the kSpawn* codes.
		 *********************************************************************/
		int32_t ManagedNetworkSpawn(int32_t idLow, int32_t idHigh, const ScriptVec3* position, const ScriptQuat* rotation,
		                            int32_t owner, int32_t* outIndex, int32_t* outGeneration) noexcept
		{
			try
			{
				if (!position || !rotation || !outIndex || !outGeneration)
					return kSpawnFailed;
				*outIndex = 0;
				*outGeneration = 0;
				NetworkSession* session = Session();
				World* world = EntityContext::GetWorld();
				SceneManager* scenes = EntityContext::GetScenes();
				if (!session || !world || !scenes || !EntityContext::InPlaySession())
					return kSpawnFailed;
				if (session->IsClient())
					return kSpawnNotAuthority;
				if (!std::isfinite(position->x) || !std::isfinite(position->y) || !std::isfinite(position->z) ||
				    !std::isfinite(rotation->x) || !std::isfinite(rotation->y) || !std::isfinite(rotation->z) ||
				    !std::isfinite(rotation->w))
					return kSpawnFailed;
				const uint64_t assetId = (static_cast<uint64_t>(static_cast<uint32_t>(idHigh)) << 32) | static_cast<uint32_t>(idLow);
				const Math::vec3 where(position->x, position->y, position->z);
				const Math::quat facing(rotation->w, rotation->x, rotation->y, rotation->z);   // glm: w first

				Entity root = kNullEntity;
				if (!session->IsActive())
				{
					// Offline: nothing to tell anyone. The same call still works.
					if (owner != Net::kServerPeer)
						return kSpawnBadOwner;
					root = EntityContext::SpawnPrefab(assetId);
					if (!root.IsValid() || !world->IsAlive(root))
						return kSpawnFailed;
					if (Transform* t = world->Write<Transform>(root))
					{
						t->positions = where;
						t->rotation = facing;
					}
				}
				else
				{
					const NetContent& content = core->GetNetContent();
					const Net::NetPrefabId prefab = content.Prefabs().FindByGuid(assetId);
					if (prefab == Net::kInvalidNetPrefab)
						return kSpawnNotNetworkPrefab;
					if (owner < 0 || owner >= static_cast<int32_t>(Net::kMaxPlayers) ||
					    !session->FindPlayer(static_cast<Net::PeerId>(owner)))
						return kSpawnBadOwner;
					PendingSpawn spawn;
					spawn.id = session->BeginSpawn(prefab, static_cast<Net::PeerId>(owner));
					if (spawn.id == Net::kInvalidNetworkId)
						return kSpawnFailed;
					spawn.prefab = prefab;
					spawn.owner = static_cast<Net::PeerId>(owner);
					spawn.position = where;
					spawn.rotation = facing;
					std::vector<Entity> scratch;
					root = NetReceiveSystem::PerformSpawn(*world, *session, content, *scenes, spawn, scratch);
					if (!root.IsValid())
						return kSpawnFailed;   // NotifySpawnFailed logged why
				}
				*outIndex = static_cast<int32_t>(root.index);
				*outGeneration = static_cast<int32_t>(root.generation);
				return kSpawnOk;
			}
			catch (const std::exception& error)
			{
				CRU_USER_ERROR("[Script] Network.Spawn failed: {}", error.what());
				return kSpawnFailed;
			}
			catch (...)
			{
				return kSpawnFailed;
			}
		}

		/*********************************************************************
		 \brief  C# NetVars.Get*: the raw 32 bits of one slot (0 if the entity
		         has no variables yet). Returns 0 for a destroyed entity or a
		         slot outside 0..63.
		 *********************************************************************/
		int32_t ManagedNetVarsGet(int32_t index, int32_t generation, int32_t slot, uint32_t* bits) noexcept
		{
			try
			{
				if (!bits)
					return 0;
				*bits = 0;
				Entity entity;
				const World* world = EntityContext::GetWorld();
				if (!world || slot < 0 || slot >= static_cast<int32_t>(NetVariables::kSlots) ||
				    !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				if (const NetVariables* variables = world->Read<NetVariables>(entity))
					*bits = variables->slots[static_cast<std::size_t>(slot)];
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# NetVars.Set*: writes one slot. On a network object only
		         the authority may, and only if the object replicates
		         NetVariables (it then has the component). Any other entity
		         gets the component on first use: its values stay local.
		         Returns one of the kVar* codes.
		 *********************************************************************/
		int32_t ManagedNetVarsSet(int32_t index, int32_t generation, int32_t slot, uint32_t bits) noexcept
		{
			try
			{
				Entity entity;
				NetworkSession* session = Session();
				World* world = EntityContext::GetWorld();
				if (!session || !world || slot < 0 || slot >= static_cast<int32_t>(NetVariables::kSlots) ||
				    !EntityContext::ResolveEntity(index, generation, entity))
					return kVarBadEntity;
				const bool networked = IdentityOf(*session, *world, entity) != nullptr;
				if (networked && session->IsClient())
					return kVarNotAuthority;
				NetVariables* variables = world->Write<NetVariables>(entity);
				if (!variables)
				{
					if (networked)
						return kVarNotReplicated;
					world->AddComponent(entity, NetVariables{});   // scripts run in an Exclusive system
					variables = world->Write<NetVariables>(entity);
					if (!variables)
						return kVarBadEntity;
				}
				std::uint32_t& value = variables->slots[static_cast<std::size_t>(slot)];
				if (value != bits)
				{
					value = bits;
					variables->changed |= std::uint64_t{ 1 } << slot;
				}
				return kVarOk;
			}
			catch (...)
			{
				return kVarBadEntity;
			}
		}

		/*********************************************************************
		 \brief  C# Network.GetPlayerEntity: the character the session spawned
		         for `peer` - the network object made from the player prefab
		         (spawnPerPlayer) that this peer owns. 0 if there is none (no
		         session, no player prefab, or that player is not in the game).
		 *********************************************************************/
		int32_t ManagedNetworkGetPlayerEntity(int32_t peer, int32_t* outIndex, int32_t* outGeneration) noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				const World* world = EntityContext::GetWorld();
				if (!outIndex || !outGeneration || !session || !world || !session->IsActive() || peer < 0 ||
				    peer >= static_cast<int32_t>(Net::kMaxPlayers))
					return 0;
				const Net::NetPrefabId playerPrefab = core->GetNetContent().PlayerPrefab();
				if (playerPrefab == Net::kInvalidNetPrefab)
					return 0;
				// A few dozen network objects at most; the lowest id wins, so the answer is stable.
				Entity found;
				Net::NetworkId foundId = Net::kInvalidNetworkId;
				for (const auto& [id, entity] : session->Entities())
				{
					const NetworkIdentity* identity = world->Read<NetworkIdentity>(entity);
					if (!identity || !core->GetNetContent().IsPlayerPrefab(identity->prefab) || identity->owner != static_cast<Net::PeerId>(peer))
						continue;
					if (foundId == Net::kInvalidNetworkId || id < foundId)
					{
						found = entity;
						foundId = id;
					}
				}
				Entity checked;
				if (foundId == Net::kInvalidNetworkId ||
				    !EntityContext::ResolveEntity(static_cast<int32_t>(found.index), static_cast<int32_t>(found.generation), checked))
					return 0;
				*outIndex = static_cast<int32_t>(found.index);
				*outGeneration = static_cast<int32_t>(found.generation);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.ObjectOf: the network object `index`/`generation`
		         belongs to - itself, or its nearest ancestor with a
		         NetworkIdentity (a client's raycast hits a query body, a child
		         of the object). 0 if it belongs to none, or no longer exists.
		 *********************************************************************/
		int32_t ManagedNetworkObjectOf(int32_t index, int32_t generation, int32_t* outIndex, int32_t* outGeneration) noexcept
		{
			try
			{
				Entity entity;
				const World* world = EntityContext::GetWorld();
				if (!outIndex || !outGeneration || !world || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const Entity root = FindNetworkRoot(*world, entity);
				if (!root.IsValid())
					return 0;
				*outIndex = static_cast<int32_t>(root.index);
				*outGeneration = static_cast<int32_t>(root.generation);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// A C# string as UTF-8; empty for null.
		std::string ReadString(MonoString* value)
		{
			GcRoot root(reinterpret_cast<MonoObject*>(value));
			std::string text;
			if (root.Get())
				FromManagedString(reinterpret_cast<MonoString*>(root.Get()), text);
			return text;
		}

		/*********************************************************************
		 \brief  C# Network.StartHost (mode 1) / Join (mode 2): asks for a
		         session, started at the next frame's safe point. Settings not
		         given (port 0, null address or name) come from this
		         instance's (EngineConfig.json, or the Editor's Network panel).
		         Returns null when the request was accepted, else why not.
		 *********************************************************************/
		MonoString* ManagedNetworkStart(int32_t mode, MonoString* address, int32_t port, MonoString* playerName) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				if (!session || !EntityContext::InPlaySession())
					return NewString("only while the game is playing");
				NetworkSettings settings = session->Settings();
				settings.mode = mode == 1 ? NetMode::Host : NetMode::Client;
				if (port < 0 || port > 65535)
					return NewString("the port must be 1 to 65535");
				const std::string name = ReadString(playerName);
				if (!name.empty())
					settings.playerName = name;
				if (settings.mode == NetMode::Host)
				{
					if (port > 0)
						settings.hostPort = static_cast<std::uint16_t>(port);
				}
				else
				{
					const std::string host = ReadString(address);
					if (!host.empty())
						settings.connectAddress = host;
					if (port > 0)
						settings.connectPort = static_cast<std::uint16_t>(port);
				}
				std::string error;
				return session->RequestStart(settings, error) ? nullptr : NewString(error);
			}
			catch (...)
			{
				return NewString("the request failed");
			}
		}

		/*********************************************************************
		 \brief  C# Network.Leave: ends this machine's session at its next
		         update (a host's ends it for everyone). 0 if none runs.
		 *********************************************************************/
		int32_t ManagedNetworkLeave(MonoString* reason) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				if (!session || !session->IsActive())
					return 0;
				const std::string why = ReadString(reason);
				session->RequestStop(why.empty() ? "left the session" : why);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.LoadScene("Scenes/Arena.scene"): host - the whole
		         session moves there; offline - an ordinary Scene.Load; client -
		         refused (only the host decides). 0 ok, 1 refused, 2 not a scene.
		 *********************************************************************/
		int32_t LoadSceneWithSeed(MonoString* path, const std::uint32_t* seed) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				SceneManager* scenes = EntityContext::GetScenes();
				AssetManager* assets = EntityContext::GetAssets();
				const std::string virtualPath = ReadString(path);
				if (!session || !scenes || !assets || !EntityContext::InPlaySession())
					return 1;
				if (virtualPath.empty() || std::filesystem::path(virtualPath).extension() != ".scene")
					return 2;
				if (session->IsClient())
					return 1;
				if (!session->IsActive())
					return scenes->RequestLoad(assets->Resolve(virtualPath)) ? 0 : 2;
				const std::uint64_t asset = assets->GetGuid(virtualPath).m_Value;
				if (asset == 0)
					return 2;
				return (seed != nullptr ? session->RequestSceneChange(asset, *seed) : session->RequestSceneChange(asset)) ? 0 : 1;
			}
			catch (...)
			{
				return 1;
			}
		}

		/*********************************************************************
		 \brief  C# Network.SetOwner(entity, peer): the
		         host gives a network object to a player. 0 asked for (it changes
		         next frame), 1 not the running host, 2 not a network object,
		         3 no such player in the session.
		 *********************************************************************/
		int32_t ManagedNetworkSetOwner(int32_t index, int32_t generation, int32_t peer) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				const World* world = EntityContext::GetWorld();
				Entity entity;
				if (!session || !world || !session->IsServer() || session->State() != SessionState::Running)
					return 1;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 2;
				const NetworkIdentity* identity = IdentityOf(*session, *world, entity);
				if (!identity)
					return 2;
				if (peer < 0 || peer > static_cast<int32_t>(Net::kMaxClients))
					return 3;
				return session->RequestOwnerChange(identity->id, static_cast<Net::PeerId>(peer)) ? 0 : 3;
			}
			catch (...)
			{
				return 1;
			}
		}

		/*********************************************************************
		 \brief  C# Network.SetLocalInput / SetLocalActions: this machine's
		         player input for this frame (NetworkSession::SetLocalInput).
		         Works offline too.
		 *********************************************************************/
		void ManagedNetworkSetLocalInput(float moveX, float moveZ, float yaw, float pitch, uint32_t actions,
			int32_t hasMotion) noexcept
		{
			try
			{
				if (NetworkSession* session = Session())
					session->SetLocalInput(Math::vec3(moveX, 0.f, moveZ), yaw, pitch, actions, hasMotion != 0);
			}
			catch (...)
			{
			}
		}

		/*********************************************************************
		 \brief  C# Network.GetInput(peer) / LocalInput: what a player does this
		         frame (NetworkSession::GetPlayerInput). peer < 0 = this machine's
		         own. 1 known (the host: any player in the game; a client: its
		         own; offline: its own), 0 otherwise (and neutral).
		 *********************************************************************/
		int32_t ManagedNetworkGetInput(int32_t peer, float* moveX, float* moveZ, float* yaw, float* pitch,
			uint32_t* held, uint32_t* pressed, int32_t* live) noexcept
		{
			try
			{
				if (!moveX || !moveZ || !yaw || !pitch || !held || !pressed || !live)
					return 0;
				NetPlayerInput input;
				bool known = false;
				if (const NetworkSession* session = Session())
				{
					if (peer < 0)
					{
						input = session->LocalPlayerInput();
						known = true;
					}
					else if (peer <= static_cast<int32_t>(Net::kMaxClients))
					{
						known = session->GetPlayerInput(static_cast<Net::PeerId>(peer), input);
					}
				}
				*moveX = input.move.x;
				*moveZ = input.move.z;
				*yaw = input.yawDegrees;
				*pitch = input.pitchDegrees;
				*held = input.held;
				*pressed = input.pressed;
				*live = input.live ? 1 : 0;
				return known ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.Time / Tick: the shared clock, in ticks
		         (NetworkSession::SharedTicks). 1 shared, 0 none (offline,
		         joining): the C# side uses this machine's own time then.
		 *********************************************************************/
		int32_t ManagedNetworkGetTime(double* ticks) noexcept
		{
			try
			{
				if (!ticks)
					return 0;
				*ticks = 0.0;
				const NetworkSession* session = Session();
				return session && session->SharedTicks(*ticks) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.GetPing(peer): milliseconds, -1 unknown
		         (NetworkSession::PingOf).
		 *********************************************************************/
		int32_t ManagedNetworkGetPing(int32_t peer) noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				if (!session || peer < 0 || peer >= static_cast<int32_t>(Net::kMaxPlayers))
					return -1;
				return session->PingOf(static_cast<Net::PeerId>(peer));
			}
			catch (...)
			{
				return -1;
			}
		}

		/*********************************************************************
		 \brief  C# Network.Kick(peer, reason): 0 removed, 1 not the running
		         host, 2 no such client (or the host itself).
		 *********************************************************************/
		int32_t ManagedNetworkKick(int32_t peer, MonoString* reason) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				if (!session || !session->IsServer() || session->State() != SessionState::Running)
					return 1;
				if (peer <= 0 || peer > static_cast<int32_t>(Net::kMaxClients))
					return 2;
				return session->Kick(static_cast<Net::PeerId>(peer), ReadString(reason)) ? 0 : 2;
			}
			catch (...)
			{
				return 1;
			}
		}

		/*********************************************************************
		 \brief  C# NetData.Read: a copy of the entity's byte block (empty if
		         it has none yet); null for a destroyed entity.
		 *********************************************************************/
		MonoArray* ManagedNetDataGet(int32_t index, int32_t generation) noexcept
		{
			try
			{
				Entity entity;
				const World* world = EntityContext::GetWorld();
				if (!world || !EntityContext::ResolveEntity(index, generation, entity))
					return nullptr;
				const NetData* data = world->Read<NetData>(entity);
				const std::size_t length = data ? data->length : 0u;
				MonoArray* bytes = mono_array_new(mono_domain_get(), mono_get_byte_class(), length);
				if (bytes && length > 0)
					std::memcpy(mono_array_addr(bytes, uint8_t, 0), data->bytes.data(), length);
				return bytes;
			}
			catch (...)
			{
				return nullptr;
			}
		}

		/*********************************************************************
		 \brief  C# NetData.Set: replaces the entity's byte block with the
		         first `length` bytes of `data`. The same rules as NetVars.Set:
		         on a network object only the authority, and only if it
		         replicates NetData; any other entity keeps a local block.
		         Returns one of the kVar* codes, or kDataTooLong.
		 *********************************************************************/
		int32_t ManagedNetDataSet(int32_t index, int32_t generation, MonoArray* bytes, int32_t length) noexcept
		{
			try
			{
				Entity entity;
				NetworkSession* session = Session();
				World* world = EntityContext::GetWorld();
				if (!session || !world || !EntityContext::ResolveEntity(index, generation, entity))
					return kVarBadEntity;
				if (length < 0 || length > static_cast<int32_t>(NetData::kMaxBytes))
					return kDataTooLong;
				std::vector<std::byte> copy;
				if (!CopyBytes(bytes, length, copy))
					return kVarBadEntity;
				const bool networked = IdentityOf(*session, *world, entity) != nullptr;
				if (networked && session->IsClient())
					return kVarNotAuthority;
				NetData* data = world->Write<NetData>(entity);
				if (!data)
				{
					if (networked)
						return kVarNotReplicated;
					world->AddComponent(entity, NetData{});   // scripts run in an Exclusive system
					data = world->Write<NetData>(entity);
					if (!data)
						return kVarBadEntity;
				}
				const bool same = data->length == copy.size() && std::equal(copy.begin(), copy.end(), data->bytes.begin());
				if (!same)
				{
					data->bytes = {};
					std::copy(copy.begin(), copy.end(), data->bytes.begin());
					data->length = static_cast<std::uint16_t>(copy.size());
					data->changed = true;
				}
				return kVarOk;
			}
			catch (...)
			{
				return kVarBadEntity;
			}
		}

		/*********************************************************************
		 \brief  C# Network.KeepAcrossScenes(entity): the host keeps a spawned
		         network object through the next scene changes (it becomes
		         DontDestroyOnLoad; SceneChange lists it). 0 kept, 1 not the
		         running host, 2 not a network object, 3 a scene object (it
		         belongs to its scene), 4 attached under another object (keep
		         that one), 5 SceneManager refused.
		 *********************************************************************/
		int32_t ManagedNetworkKeepAcrossScenes(int32_t index, int32_t generation) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				World* world = EntityContext::GetWorld();
				SceneManager* scenes = EntityContext::GetScenes();
				if (!session || !world || !scenes || !session->IsServer() || session->State() != SessionState::Running)
					return 1;
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 2;
				const NetworkIdentity* identity = IdentityOf(*session, *world, entity);
				if (!identity)
					return 2;
				if (session->SceneIdOf(identity->id) != 0)
					return 3;
				if (world->GetParent(entity).IsValid())
					return 4;
				return scenes->DontDestroyOnLoad(entity) ? 0 : 5;
			}
			catch (...)
			{
				return 1;
			}
		}

		/*********************************************************************
		 \brief  C# Network.KeepPlayersAcrossScenes: get (value < 0) or set.
		         Returns the setting.
		 *********************************************************************/
		int32_t ManagedNetworkKeepPlayers(int32_t value) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				if (!session)
					return 0;
				if (value >= 0)
					session->SetKeepPlayers(value != 0);
				return session->KeepPlayers() ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.IsKeptAcrossScenes(entity): a network object a
		         scene change keeps (DontDestroyOnLoad, itself or an ancestor).
		 *********************************************************************/
		int32_t ManagedNetworkIsKept(int32_t index, int32_t generation) noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				const World* world = EntityContext::GetWorld();
				SceneManager* scenes = EntityContext::GetScenes();
				Entity entity;
				if (!session || !world || !scenes || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				return IdentityOf(*session, *world, entity) && scenes->IsPersistent(entity) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.ViewTick: the tick this machine shows other objects
		         at (NetworkSession::ViewTicks). 0 offline / joining.
		 *********************************************************************/
		int32_t ManagedNetworkViewTick(double* ticks) noexcept
		{
			try
			{
				if (!ticks)
					return 0;
				*ticks = 0.0;
				const NetworkSession* session = Session();
				return session && session->ViewTicks(*ticks) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.MaxRewindSeconds: get (seconds < 0) or set (0 to 1).
		 *********************************************************************/
		float ManagedNetworkMaxRewind(float seconds) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				if (!session)
					return 0.f;
				if (seconds >= 0.f && std::isfinite(seconds))
					session->LagHistory().SetMaxRewindTicks(std::min(seconds, 1.f) * static_cast<double>(NetClock::kTickRate));
				return static_cast<float>(session->LagHistory().MaxRewindTicks() / NetClock::kTickRate);
			}
			catch (...)
			{
				return 0.f;
			}
		}

		/*********************************************************************
		 \brief  C# Network.RaycastAt, the network objects' part: the ray
		         against the shapes as they were at `tick` (the host's lag
		         history). `ignore`'s network object is skipped. 1 with the
		         entity hit (the part carrying the shape) and the tick used.
		 *********************************************************************/
		int32_t ManagedNetworkRaycastHistory(double tick, const ScriptVec3* origin, const ScriptVec3* direction, float maxDistance,
			int32_t ignoreIndex, int32_t ignoreGeneration, int32_t* outIndex, int32_t* outGeneration, ScriptVec3* outPoint,
			ScriptVec3* outNormal, float* outDistance, double* outTick) noexcept
		{
			try
			{
				if (!origin || !direction || !outIndex || !outGeneration || !outPoint || !outNormal || !outDistance || !outTick)
					return 0;
				*outIndex = 0;
				*outGeneration = 0;
				const NetworkSession* session = Session();
				const World* world = EntityContext::GetWorld();
				if (!session || !world || !session->IsServer() || session->State() != SessionState::Running)
					return 0;
				Net::NetworkId ignore = Net::kInvalidNetworkId;
				Entity shooter;
				if (ignoreIndex != 0 || ignoreGeneration != 0)
					if (EntityContext::ResolveEntity(ignoreIndex, ignoreGeneration, shooter))
						if (const Entity root = FindNetworkRoot(*world, shooter); world->IsAlive(root))
							if (const NetworkIdentity* identity = world->Read<NetworkIdentity>(root))
								ignore = identity->id;
				LagHit hit;
				if (!session->LagHistory().Raycast(tick, Math::vec3(origin->x, origin->y, origin->z),
				        Math::vec3(direction->x, direction->y, direction->z), maxDistance, ignore, hit))
					return 0;
				if (!world->IsAlive(hit.entity))
					return 0;   // destroyed since
				*outIndex = static_cast<int32_t>(hit.entity.index);
				*outGeneration = static_cast<int32_t>(hit.entity.generation);
				*outPoint = { hit.point.x, hit.point.y, hit.point.z };
				*outNormal = { hit.normal.x, hit.normal.y, hit.normal.z };
				*outDistance = hit.distance;
				*outTick = hit.tick;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedNetworkLoadScene(MonoString* path) noexcept
		{
			return LoadSceneWithSeed(path, nullptr);
		}

		/*********************************************************************
		 \brief  C# Network.LoadScene(path, seed): as LoadScene, the new scene's
		         seed chosen by the host's script (protocol 9).
		 *********************************************************************/
		int32_t ManagedNetworkLoadSceneSeeded(MonoString* path, int32_t seed) noexcept
		{
			const std::uint32_t value = static_cast<std::uint32_t>(seed);
			return LoadSceneWithSeed(path, &value);
		}

		/*********************************************************************
		 \brief  C# Network.SceneSeed / HasSceneSeed: 1 and the seed when the
		         session knows the current scene's seed, else 0.
		 *********************************************************************/
		int32_t ManagedNetworkSceneSeed(int32_t* outSeed) noexcept
		{
			if (outSeed) *outSeed = 0;
			NetworkSession* session = Session();
			if (!outSeed || !session || !session->HasSceneSeed()) return 0;
			*outSeed = static_cast<int32_t>(session->SceneSeed());
			return 1;
		}

		/*********************************************************************
		 \brief  C# Network.StartBrowsing / StopBrowsing (on = 1 / 0). Null on
		         success, else why not.
		 *********************************************************************/
		MonoString* ManagedNetworkBrowse(int32_t on) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				if (!session)
					return NewString("no network session");
				if (on == 0)
				{
					session->StopBrowsing();
					return nullptr;
				}
				std::string error;
				return session->StartBrowsing(error) ? nullptr : NewString(error);
			}
			catch (...)
			{
				return NewString("the request failed");
			}
		}

		/*********************************************************************
		 \brief  C# Network.Lobbies: how many hosts are listed now.
		 *********************************************************************/
		int32_t ManagedNetworkLobbyCount() noexcept
		{
			const NetworkSession* session = Session();
			return session ? static_cast<int32_t>(session->Lobbies().size()) : 0;
		}

		/*********************************************************************
		 \brief  C# Network.Lobbies: one host's numbers. flags: 1 open, 2 same
		         protocol version (joinable by this build), 4 seen on LAN, 8 listed
		         by the connection server. 0 if no such index.
		 *********************************************************************/
		int32_t ManagedNetworkGetLobby(int32_t index, int32_t* port, int32_t* players, int32_t* maxPlayers, int32_t* flags,
		                               float* age) noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				if (!session || !port || !players || !maxPlayers || !flags || !age || index < 0 ||
				    static_cast<std::size_t>(index) >= session->Lobbies().size())
					return 0;
				const LobbyInfo& lobby = session->Lobbies()[static_cast<std::size_t>(index)];
				*port = lobby.advert.gamePort;
				*players = lobby.advert.players;
				*maxPlayers = lobby.advert.maxPlayers;
				*flags = (lobby.advert.open ? 1 : 0) | (lobby.advert.protocolVersion == kProtocolVersion ? 2 : 0) |
				         ((lobby.sources & kLobbyFromLan) != 0 ? 4 : 0) | ((lobby.sources & kLobbyFromServer) != 0 ? 8 : 0);
				*age = lobby.Age(std::chrono::steady_clock::now());
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.Lobbies: one host's texts. which: 0 address,
		         1 name, 2 status, 3 instance id (hex). Null if no such index.
		 *********************************************************************/
		MonoString* ManagedNetworkGetLobbyText(int32_t index, int32_t which) noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				if (!session || index < 0 || static_cast<std::size_t>(index) >= session->Lobbies().size())
					return nullptr;
				const LobbyInfo& lobby = session->Lobbies()[static_cast<std::size_t>(index)];
				switch (which)
				{
				case 0: return NewString(lobby.address);
				case 1: return NewString(lobby.advert.name);
				case 2: return NewString(lobby.advert.status);
				case 3: return NewString(std::format("{:016X}", lobby.advert.instance));
				default: return nullptr;
				}
			}
			catch (...)
			{
				return nullptr;
			}
		}

		/*********************************************************************
		 \brief  C# Network.LobbyServerState and friends: the connection server
		         as this session sees it. status: LobbyServerStatus; mode: the
		         browser's LobbyDiscovery; flags: 1 the list is the server's,
		         2 it was truncated, 4 the host is registered. 0 without a session.
		 *********************************************************************/
		int32_t ManagedNetworkLobbyServerInfo(int32_t* status, int32_t* mode, int32_t* total, int32_t* flags) noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				if (!session || !status || !mode || !total || !flags)
					return 0;
				*status = static_cast<int32_t>(session->LobbyServerState());
				*mode = static_cast<int32_t>(session->IsBrowsing() ? session->BrowseDiscovery() : session->Settings().lobbyDiscovery);
				*total = session->LobbyListTotal();
				*flags = (session->LobbyListFromServer() ? 1 : 0) | (session->LobbyListTruncated() ? 2 : 0) |
				         (session->LobbyRegistered() ? 4 : 0);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.LobbyServerFailedAttempts / IsUsingLanDiscovery /
		         CanUseLanDiscovery. Returns flags: 1 LAN discovery chosen,
		         2 it can be chosen now. 0 without a session.
		 *********************************************************************/
		int32_t ManagedNetworkLobbyBrowseInfo(int32_t* attempts, int32_t* maxAttempts) noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				if (!session || !attempts || !maxAttempts)
					return 0;
				*attempts = session->LobbyServerFailedAttempts();
				*maxAttempts = LobbyServer::kListAttempts;
				return (session->LanDiscoveryChosen() ? 1 : 0) | (session->CanUseLanDiscovery() ? 2 : 0);
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.UseLanDiscovery: null on success, else why not.
		 *********************************************************************/
		MonoString* ManagedNetworkUseLanDiscovery() noexcept
		{
			try
			{
				NetworkSession* session = Session();
				if (!session)
					return NewString("no network session");
				std::string error;
				return session->UseLanDiscovery(error) ? nullptr : NewString(error);
			}
			catch (...)
			{
				return NewString("the request failed");
			}
		}

		/*********************************************************************
		 \brief  C# Network.LobbyListingProblem: why the hosted session is not
		         in lobby lists, or "" (listed, still trying, or not hosting).
		 *********************************************************************/
		MonoString* ManagedNetworkLobbyListingProblem() noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				return NewString(session ? session->LobbyListingProblem() : std::string());
			}
			catch (...)
			{
				return nullptr;
			}
		}

		/*********************************************************************
		 \brief  C# Network.LobbyServerAddress: "192.168.0.5:27018" of the
		         connection server in use (else the one set in the settings),
		         or "" if none.
		 *********************************************************************/
		MonoString* ManagedNetworkLobbyServerAddress() noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				if (!session)
					return NewString("");
				std::string endpoint = session->LobbyServerEndpoint();
				if (endpoint.empty() && !session->Settings().lobbyServerAddress.empty())
					endpoint = std::format("{}:{}", session->Settings().lobbyServerAddress, session->Settings().lobbyServerPort);
				return NewString(endpoint);
			}
			catch (...)
			{
				return nullptr;
			}
		}

		/*********************************************************************
		 \brief  C# Network.SetLobby: host - open or closed to new players,
		         and the status lobby lists show. 0 unless hosting.
		 *********************************************************************/
		int32_t ManagedNetworkSetLobby(int32_t open, MonoString* status) noexcept
		{
			try
			{
				NetworkSession* session = Session();
				if (!session || !session->IsServer())
					return 0;
				session->SetLobby(open != 0, ReadString(status));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Network.LastEndCode / LastEndReason: why the latest session
		         ended (NetworkSession::LastEndCode).
		 *********************************************************************/
		int32_t ManagedNetworkLastEndCode() noexcept
		{
			const NetworkSession* session = Session();
			return session ? session->LastEndCode() : 0;
		}

		MonoString* ManagedNetworkLastEndReason() noexcept
		{
			try
			{
				const NetworkSession* session = Session();
				return session ? NewString(session->LastEndReason()) : nullptr;
			}
			catch (...)
			{
				return nullptr;
			}
		}

		template <typename Fn>
		void Add(const char* name, Fn* function)
		{
			mono_add_internal_call(name, reinterpret_cast<const void*>(function));
		}
	}

	void SetNetwork(Core* engine, EventBuses* events)
	{
		core = engine;
		onStarted = {};
		onWorldReady = {};
		onEnded = {};
		onJoined = {};
		onReady = {};
		onLeft = {};
		onCommand = {};
		onEvent = {};
		onSceneLoading = {};
		onAllReady = {};
		onOwnership = {};
		waiting.clear();
		batch.clear();
		overflowLogged = false;
		if (!engine || !events)
		{
			core = nullptr;
			return;
		}

		const auto queue = [](ScriptNetEvent::Kind kind, int32_t peer = 0, std::string text = {}) {
			ScriptNetEvent event;
			event.kind = kind;
			event.peer = peer;
			event.text = std::move(text);
			Queue(std::move(event));
		};
		// A client has "started" for gameplay once the host's world is here, not at JoinAccepted.
		onStarted = events->network.Subscribe<NetworkEvents::SessionStarted>(
		    [queue](const NetworkEvents::SessionStarted& e)
		    {
			    if (e.role == Net::NetRole::Server)
				    queue(ScriptNetEvent::Kind::Started);
		    });
		onWorldReady = events->network.Subscribe<NetworkEvents::WorldReady>(
		    [queue](const NetworkEvents::WorldReady&) { queue(ScriptNetEvent::Kind::Started); });
		onEnded = events->network.Subscribe<NetworkEvents::SessionEnded>(
		    [queue](const NetworkEvents::SessionEnded& e) { queue(ScriptNetEvent::Kind::Stopped, 0, e.reason); });
		onJoined = events->network.Subscribe<NetworkEvents::PeerJoined>(
		    [queue](const NetworkEvents::PeerJoined& e) { queue(ScriptNetEvent::Kind::PeerJoined, e.peer); });
		onReady = events->network.Subscribe<NetworkEvents::PeerReady>(
		    [queue](const NetworkEvents::PeerReady& e) { queue(ScriptNetEvent::Kind::PeerReady, e.peer); });
		onLeft = events->network.Subscribe<NetworkEvents::PeerLeft>(
		    [queue](const NetworkEvents::PeerLeft& e) { queue(ScriptNetEvent::Kind::PeerLeft, e.peer); });
		onCommand = events->network.Subscribe<NetworkEvents::CommandReceived>(
		    [](const NetworkEvents::CommandReceived& e)
		    {
			    // An aimed command goes to that object's scripts only. Gone since it arrived (or
			    // since the host asked itself): nobody is left to hear it.
			    Entity target;
			    if (e.target != Net::kInvalidNetworkId)
			    {
				    NetworkSession* session = Session();
				    const World* world = EntityContext::GetWorld();
				    target = session ? session->Entities().Find(e.target) : Entity{};
				    if (!session || !world || !target.IsValid() || !world->IsAlive(target))
				    {
					    if (session)
						    ++session->MutableStats().commandsNoTarget;
					    return;
				    }
			    }
			    QueueMessage(ScriptNetEvent::Kind::Command, e.sender, e.kind, e.target != Net::kInvalidNetworkId, target, e.payload);
		    });
		onSceneLoading = events->network.Subscribe<NetworkEvents::SceneChangeStarted>(
		    [queue](const NetworkEvents::SceneChangeStarted&) { queue(ScriptNetEvent::Kind::SceneLoading); });
		onAllReady = events->network.Subscribe<NetworkEvents::AllPeersReady>(
		    [queue](const NetworkEvents::AllPeersReady&) { queue(ScriptNetEvent::Kind::AllPeersReady); });
		// Ownership: to the scripts on that object only, as VarChanged.
		onOwnership = events->network.Subscribe<NetworkEvents::OwnershipChanged>(
		    [](const NetworkEvents::OwnershipChanged& e)
		    {
			    NetworkSession* session = Session();
			    const World* world = EntityContext::GetWorld();
			    const Entity target = session ? session->Entities().Find(e.id) : Entity{};
			    if (!world || !target.IsValid() || !world->IsAlive(target))
				    return;   // despawned since: nobody left to hear it
			    ScriptNetEvent event;
			    event.kind = ScriptNetEvent::Kind::OwnershipChanged;
			    event.peer = e.owner;
			    event.code = e.previous;
			    event.targeted = true;
			    event.target = target;
			    Queue(std::move(event));
		    });
		onEvent = events->network.Subscribe<NetworkEvents::GameplayEventReceived>(
		    [](const NetworkEvents::GameplayEventReceived& e)
		    {
			    // The same rule as commands: about one object -> that object's scripts only.
			    Entity target;
			    if (e.target != Net::kInvalidNetworkId)
			    {
				    NetworkSession* session = Session();
				    const World* world = EntityContext::GetWorld();
				    target = session ? session->Entities().Find(e.target) : Entity{};
				    if (!session || !world || !target.IsValid() || !world->IsAlive(target))
				    {
					    if (session)
						    ++session->MutableStats().gameplayEventsNoTarget;
					    return;
				    }
			    }
			    QueueMessage(ScriptNetEvent::Kind::Event, Net::kServerPeer, e.kind, e.target != Net::kInvalidNetworkId, target, e.payload);
		    });
	}

	namespace NetworkContext
	{
		void TrimEvents()
		{
			batch.clear();
			const auto newestStart = std::find_if(waiting.rbegin(), waiting.rend(), [](const ScriptNetEvent& e) {
				return e.kind == ScriptNetEvent::Kind::Started;
			});
			if (newestStart == waiting.rend())
				waiting.clear();
			else
				waiting.erase(waiting.begin(), std::prev(newestStart.base()));
			overflowLogged = false;
		}

		void GatherVariableChanges(World& world)
		{
			// Collected first: nothing is queued while the query walks the components.
			std::vector<std::pair<Entity, std::uint64_t>> changes;
			world.BuildQuery<const NetVariables>().Build().ForEach([&](Entity entity, const NetVariables& variables) {
				if (variables.changed != 0)
					changes.emplace_back(entity, variables.changed);
			});
			for (const auto& [entity, mask] : changes)
			{
				if (NetVariables* variables = world.Write<NetVariables>(entity))
					variables->changed = 0;
				for (std::uint32_t slot = 0; slot < NetVariables::kSlots; ++slot)
				{
					if ((mask & (std::uint64_t{ 1 } << slot)) == 0)
						continue;
					ScriptNetEvent event;
					event.kind = ScriptNetEvent::Kind::VarChanged;
					event.code = static_cast<int32_t>(slot);
					event.targeted = true;
					event.target = entity;
					waiting.push_back(std::move(event));
				}
			}
			GatherDataChanges(world);   // NetData -> OnNetDataChanged, in the same batch
		}

		// NetData blocks that changed (a record on a client, a script's Set on the authority): one
		// DataChanged each, for the scripts on that entity.
		void GatherDataChanges(World& world)
		{
			std::vector<Entity> changed;
			world.BuildQuery<const NetData>().Build().ForEach([&](Entity entity, const NetData& data) {
				if (data.changed)
					changed.push_back(entity);
			});
			for (const Entity entity : changed)
			{
				if (NetData* data = world.Write<NetData>(entity))
					data->changed = false;
				ScriptNetEvent event;
				event.kind = ScriptNetEvent::Kind::DataChanged;
				event.targeted = true;
				event.target = entity;
				waiting.push_back(std::move(event));
			}
		}

		const std::vector<ScriptNetEvent>& BeginDispatch()
		{
			batch.clear();
			batch.swap(waiting);
			return batch;
		}

		void EndDispatch()
		{
			batch.clear();
			overflowLogged = false;
		}
	}

	void RegisterNetworkBindings()
	{
		Add("Crucible.Network::GetStateInternal", &ManagedNetworkGetState);
		Add("Crucible.Network::GetPeerMaskInternal", &ManagedNetworkGetPeerMask);
		Add("Crucible.Network::GetPeerInfoInternal", &ManagedNetworkGetPeerInfo);
		Add("Crucible.Network::GetPeerNameInternal", &ManagedNetworkGetPeerName);
		Add("Crucible.Network::GetIdentityInternal", &ManagedNetworkGetIdentity);
		Add("Crucible.Network::FindEntityInternal", &ManagedNetworkFindEntity);
		Add("Crucible.Network::SendCommandInternal", &ManagedNetworkSendCommand);
		Add("Crucible.Network::GetEventPayloadInternal", &ManagedNetworkGetEventPayload);
		Add("Crucible.Network::SendEventInternal", &ManagedNetworkSendEvent);
		Add("Crucible.Network::SpawnInternal", &ManagedNetworkSpawn);
		Add("Crucible.Network::GetPlayerEntityInternal", &ManagedNetworkGetPlayerEntity);
		Add("Crucible.Network::ObjectOfInternal", &ManagedNetworkObjectOf);
		Add("Crucible.Network::StartInternal", &ManagedNetworkStart);
		Add("Crucible.Network::LeaveInternal", &ManagedNetworkLeave);
		Add("Crucible.Network::LoadSceneInternal", &ManagedNetworkLoadScene);
		Add("Crucible.Network::LoadSceneSeededInternal", &ManagedNetworkLoadSceneSeeded);
		Add("Crucible.Network::SetOwnerInternal", &ManagedNetworkSetOwner);
		Add("Crucible.Network::SetLocalInputInternal", &ManagedNetworkSetLocalInput);
		Add("Crucible.Network::GetInputInternal", &ManagedNetworkGetInput);
		Add("Crucible.Network::GetTimeInternal", &ManagedNetworkGetTime);
		Add("Crucible.Network::GetPingInternal", &ManagedNetworkGetPing);
		Add("Crucible.Network::KickInternal", &ManagedNetworkKick);
		Add("Crucible.Network::KeepAcrossScenesInternal", &ManagedNetworkKeepAcrossScenes);
		Add("Crucible.Network::ViewTickInternal", &ManagedNetworkViewTick);
		Add("Crucible.Network::MaxRewindInternal", &ManagedNetworkMaxRewind);
		Add("Crucible.Network::RaycastHistoryInternal", &ManagedNetworkRaycastHistory);
		Add("Crucible.Network::KeepPlayersInternal", &ManagedNetworkKeepPlayers);
		Add("Crucible.Network::IsKeptInternal", &ManagedNetworkIsKept);
		Add("Crucible.NetData::GetInternal", &ManagedNetDataGet);
		Add("Crucible.NetData::SetInternal", &ManagedNetDataSet);
		Add("Crucible.Network::SceneSeedInternal", &ManagedNetworkSceneSeed);
		Add("Crucible.Network::BrowseInternal", &ManagedNetworkBrowse);
		Add("Crucible.Network::LobbyCountInternal", &ManagedNetworkLobbyCount);
		Add("Crucible.Network::GetLobbyInternal", &ManagedNetworkGetLobby);
		Add("Crucible.Network::GetLobbyTextInternal", &ManagedNetworkGetLobbyText);
		Add("Crucible.Network::SetLobbyInternal", &ManagedNetworkSetLobby);
		Add("Crucible.Network::LobbyServerInfoInternal", &ManagedNetworkLobbyServerInfo);
		Add("Crucible.Network::LobbyServerAddressInternal", &ManagedNetworkLobbyServerAddress);
		Add("Crucible.Network::LobbyListingProblemInternal", &ManagedNetworkLobbyListingProblem);
		Add("Crucible.Network::LobbyBrowseInfoInternal", &ManagedNetworkLobbyBrowseInfo);
		Add("Crucible.Network::UseLanDiscoveryInternal", &ManagedNetworkUseLanDiscovery);
		Add("Crucible.Network::LastEndCodeInternal", &ManagedNetworkLastEndCode);
		Add("Crucible.Network::LastEndReasonInternal", &ManagedNetworkLastEndReason);
		Add("Crucible.NetVars::GetInternal", &ManagedNetVarsGet);
		Add("Crucible.NetVars::SetInternal", &ManagedNetVarsSet);
	}
}
#endif
