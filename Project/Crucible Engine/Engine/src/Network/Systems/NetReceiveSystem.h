/*************************************************************************
* \file:         NetReceiveSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.2
* \brief
*                The ECS system that runs the network session once per
*                frame, first in PreUpdate, and makes the World follow it:
*                spawns on the host, proxies on clients.
*
* \details
*                PreUpdate is the stage IECSSystems.h reserves for "input,
*                network receive"; priority -100 puts it before UI and input
*                systems, so everything later in the frame sees this frame's
*                network state.
*
*                Not SimulationOnly: it must keep running while Paused, or
*                messages pile up in GNS and a client leaving would go
*                unnoticed. In Edit the session is Offline and Update returns
*                at once.
*
*                Exclusive: creating and destroying entities (spawns,
*                proxies, despawns) are structural changes, which a declared
*                system may only make directly when it conflicts with every
*                other system (World::RequireStructuralChange). No Query is
*                iterating while it runs.
*
*                Host, each frame (Networking Guide 9.3):
*                  the session's Update, then the spawns gameplay requested
*                  last frame (RequestSpawn): instantiate, place, switch off
*                  owner-local components of a player the host does not own,
*                  add NetworkIdentity LAST. A failure destroys the whole
*                  subtree and is reported (NetworkEvents::SpawnFailed); the
*                  spawn is never broadcast.
*
*                Client, each frame (Networking Guide 9.3, 9.4, 18.3 issues
*                3, 4): every lifecycle message, in arrival order, is first
*                decoded and validated WHOLE (structure, lifecycle sequence,
*                prefab, owner, mask, every record), and only then applied.
*                A message that fails validation closes the connection with
*                Malformed: the reliable stream is corrupt, and skipping a
*                lifecycle message would leave this World permanently
*                different from the host's.
*                  WorldBegin    only first after joining
*                  SpawnBatch    one proxy per record (CreateProxy)
*                  WorldEnd      the world is complete: ClientReady
*                  DespawnBatch  destroys mapped proxies; unknown ids are fine
*                  GameplayEvent counted (handled in a later phase)
*                Each proxy is made in a fixed order - instantiate, switch off
*                server-only and (unless owned here) owner-local components,
*                apply the initial state as one transaction, add
*                NetInterpolation, add NetworkIdentity LAST - so the id map
*                only ever holds complete proxies. If a proxy fails, every
*                proxy of that message is destroyed again and the client
*                leaves with SpawnFailed.
*
*                Why switching components off in the same frame is enough:
*                physics builds Jolt bodies lazily in its own update (from
*                queries that skip disabled components), scripts start in
*                theirs, and both run after PreUpdate.
*
* \history
*    -v1.0       connection handling (Networking Guide PR 4)
*    -v1.1       destroys rejected identities; Debug id-map check (PR 6)
*    -v1.2       Exclusive; host spawns; client lifecycle stream and safe
*                proxy creation (PR 8)
*
**************************************************************************/
#pragma once

#include <span>
#include <vector>

#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"
#include "Network/NetworkSession.h"

namespace Crucible
{
	class NetContent;
	class SceneManager;
	struct NetPrefabDesc;
	class Prefab;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL member of an exported class: used only inside Engine.dll
#endif
	class ENGINE_API NetReceiveSystem final : public IECSSystem
	{
	public:
		/// `session`, `content` and `scenes` are owned by Core and outlive every system.
		NetReceiveSystem(NetworkSession& session, const NetContent& content, SceneManager& scenes) noexcept
			: m_session(session), m_content(content), m_scenes(scenes) {}
		NetReceiveSystem(const NetReceiveSystem&) = delete;   // holds received messages (move-only)
		NetReceiveSystem& operator=(const NetReceiveSystem&) = delete;

		SystemStage Stage() const override { return SystemStage::PreUpdate; }
		int         Priority() const override { return -100; }
		void        Describe(SystemDesc& desc) const override;
		void        Update(World& world, SystemContext& ctx) override;

		/// Switches the listed components off (or on) on `root` and every descendant that has
		/// them. Components an entity does not have are skipped.
		static void SetComponentsEnabled(World& world, Entity root, std::span<const ComponentId> ids, bool on,
			std::vector<Entity>& scratch);

	private:
		// Host
		void PerformPendingSpawns(World& world);

		// Client
		void ApplyWorldMessages(World& world);
		bool ApplyWorldMessage(World& world, std::span<const std::byte> data);   // false: the session is leaving
		bool CheckSequence(std::uint32_t seq, const char* what);
		bool ApplySpawnBatch(World& world, ByteReader& reader);
		bool ApplyDespawnBatch(World& world, ByteReader& reader);
		bool CreateProxy(World& world, const SpawnView& spawn, Net::NetTick tick, std::string& why);
		void Leave(CloseReason reason, const std::string& why);

		NetworkSession&           m_session;
		const NetContent&         m_content;
		SceneManager&             m_scenes;
		std::vector<Entity>       m_rejected;        // reused every frame
		std::vector<PendingSpawn> m_spawns;          // host: this frame's requests
		std::vector<ReceivedMessage> m_world;        // client: this frame's lifecycle messages
		SpawnBatchView            m_spawnBatch;      // client: reused decode targets
		DespawnBatchView          m_despawnBatch;
		std::vector<Entity>       m_created;         // client: proxies made by the current message
		std::vector<Entity>       m_scratch;         // subtree walks
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
