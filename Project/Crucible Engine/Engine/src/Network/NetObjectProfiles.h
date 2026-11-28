/*************************************************************************
* \file:         NetObjectProfiles.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          6 November 2026
* \version       1.3
* \brief
*                From authored settings to replication profiles: turns an
*                object's NetworkObjectAuthoring into a NetPrefabDesc, and
*                finds every network prefab in the project.
*
* \details
*                A network prefab is any .cprefab whose ROOT carries
*                NetworkObjectAuthoring. Nobody registers it by hand:
*                before every Host or Client session NetContent offers this
*                code every prefab of the project (NetPrefabSource), and
*                each network prefab is registered from its own settings.
*
*                Ids are derived, never stored: the network prefabs are
*                sorted by asset GUID and numbered from `firstId` up. Ids
*                exist only on the wire and in the session's table - never
*                in a scene, a prefab or any other file - so they only have
*                to agree between two machines with the same content, and
*                the content hash proves they do (it covers every prefab in
*                id order). The same set of prefabs gives the same ids in
*                any scan order.
*
*                What happens to a prefab:
*                  renamed, moved   same GUID: same place in the order. Its
*                                   name (its asset path) is part of the
*                                   content hash, so both machines need the
*                                   renamed version.
*                  duplicated       a new GUID: a new network prefab with
*                                   the copied settings.
*                  deleted          not offered any more: gone from the
*                                   table; its scene instances are reported
*                                   by scene validation (a later step).
*
*                The content hash covers each authored prefab's whole file
*                except its sendPriority and critical (SchemaHash), so -
*                as for code-registered prefabs - two machines may differ
*                in scheduling only.
*
*                BuildProfile only resolves names (component and serializer
*                names to ids) and copies the scheduling values. Every rule
*                about what may be combined is NetPrefabTable's (Register,
*                then Validate against the prefab itself), so an authored
*                prefab is checked exactly like one registered in code.
*
*                Rules of its own:
*                  - NetworkObjectAuthoring only on the root: an entry below
*                    the root that has one (a network object inside another,
*                    e.g. a nested network prefab) is refused, because a
*                    spawn of the outer one could not give the inner one an
*                    identity of its own;
*                  - at most one prefab with spawnPerPlayer;
*                  - every name must resolve: an unknown or misspelt one is
*                    refused with the field and the name, never ignored.
*
* \history
*    -v1.0       Networking Phase 14.2
*    -v1.1       AuthoringOptions and CheckAuthoring: what the Inspector may
*                offer for an object, and what is wrong with its settings
*                (Networking Phase 14.3)
*    -v1.2       AuthoringComponentOption::belowRoot
*    -v1.3       several spawnPerPlayer prefabs (playerPrefabs): each scene's player is the one it places
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "EngineExport.h"
#include "Network/NetComponents.h"
#include "Network/NetPrefabTable.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	class ComponentRegistry;
	class Prefab;
	class ReplicationRegistry;
	class World;

	/// One prefab offered to discovery: its asset identity and its contents, read fresh.
	struct NetPrefabSource
	{
		std::uint64_t                 guid = 0;   // asset GUID (renames and moves keep it)
		std::string                   path;       // asset path, e.g. "Prefabs/Grunt.cprefab": its name
		std::shared_ptr<const Prefab> prefab;
	};

	/// `prefab`'s content for the content hash: its document without the root authoring's
	/// sendPriority and critical (never 0).
	ENGINE_API std::uint64_t SchemaHash(const Prefab& prefab);

	/// What DiscoverNetworkPrefabs registered.
	struct NetPrefabDiscovery
	{
		std::vector<Net::NetPrefabId> ids;                           // in id (= asset GUID) order
		Net::NetPrefabId              playerPrefab = Net::kInvalidNetPrefab;   // the default player: the first spawnPerPlayer one
		std::vector<Net::NetPrefabId> playerPrefabs;                            // every spawnPerPlayer one (a scene picks by its placed players)
		std::size_t                   offered = 0;                   // prefabs looked at
	};

	/// Resolves `authoring`'s names into `out`'s lists (out.name, prefabPath and prefabGuid are left
	/// to the caller). False with the field and the name if a component or serializer name is not
	/// registered.
	ENGINE_API bool BuildProfile(const NetworkObjectAuthoring& authoring, const ComponentRegistry& components,
		const ReplicationRegistry& serializers, NetPrefabDesc& out, std::string& error);

	/// The root's NetworkObjectAuthoring of `prefab`, if it has one: Found (in `out`), None, or
	/// Invalid (with `error`): an entry below the root has one too, or the root's cannot be read.
	enum class AuthoringRead : std::uint8_t { None, Found, Invalid };
	ENGINE_API AuthoringRead ReadPrefabAuthoring(const Prefab& prefab, NetworkObjectAuthoring& out, std::string& error);

	/// One component of an object, as the Inspector may configure it (AuthoringOptions).
	struct AuthoringComponentOption
	{
		std::string              name;              // its registry name: what the authoring lists store
		bool                     onRoot = false;    // on the object's root (replicated, enabledReplicated need it)
		bool                     belowRoot = false; // on an entity below the root (childEnabledReplicated needs it on a synced child)
		bool                     toggleable = false;
		std::vector<std::string> serializers;       // registered serializers for it: the variants
		std::string              noReplication;     // why it cannot be Replicated (empty: it can)
		std::string              noPolicy;          // why it cannot be host-only / owner-only (empty: it can)
		std::string              noEnabledSync;     // why its on/off state cannot be replicated (empty: it can)
	};

	/// Every configurable component on `root` and below (each once, registry order), with what it may
	/// be set to. Left out: the network runtime components, NetworkObjectAuthoring itself, and any
	/// component with nothing to choose (no serializer and cannot be switched off: always Local).
	ENGINE_API std::vector<AuthoringComponentOption> AuthoringOptions(const World& world, Entity root,
		const ComponentRegistry& components, const ReplicationRegistry& serializers);

	/// Everything discovery and session validation would refuse about `authoring` on `root`, one
	/// message each (empty: valid): unknown names, the table's combination rules, components the
	/// object does not have or that cannot be switched off, and a network object inside another.
	/// Project-wide rules (one spawnPerPlayer prefab) are checked at discovery only.
	ENGINE_API std::vector<std::string> CheckAuthoring(const World& world, Entity root, const NetworkObjectAuthoring& authoring,
		const ComponentRegistry& components, const ReplicationRegistry& serializers);

	/// Registers (and attaches) every source whose root carries NetworkObjectAuthoring, numbered
	/// from `firstId` in asset-GUID order. Sources without one are skipped. False - with the
	/// prefab's path and the reason - on the first refused one, a GUID offered twice, a second
	/// spawnPerPlayer prefab, or more prefabs than ids; `table` then holds the ones registered
	/// before it (NetContent removes them again).
	ENGINE_API bool DiscoverNetworkPrefabs(std::span<const NetPrefabSource> sources, const ComponentRegistry& components,
		const ReplicationRegistry& serializers, NetPrefabTable& table, Net::NetPrefabId firstId,
		NetPrefabDiscovery& out, std::string& error);
}
