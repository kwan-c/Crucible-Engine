/*************************************************************************
* \file:         NetAttachments.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                Attachments (see the header).
*
* \history
*    -v1.0       attachments: the Attachment serializer and the client's side
*
**************************************************************************/
#include "Network/NetAttachments.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Network/ByteStream.h"
#include "Network/NetComponents.h"
#include "Network/NetQueryBodies.h"
#include "Network/NetSyncedChildren.h"
#include "Network/ReplicationRegistry.h"

namespace Crucible
{
	namespace
	{
		struct AttachmentState
		{
			std::uint32_t parent;   // 0: not attached
			std::uint32_t mount;
		};

		void WarnHostOnce(Entity root, const char* why)
		{
			static std::unordered_set<std::uint64_t> warned;
			if (!warned.insert(static_cast<std::uint64_t>(root.index) << 32 | root.generation).second) return;
			CRU_WARN("[Net] network object {}:{} is attached {}: clients see it unattached", root.index, root.generation, why);
		}

		// The host: what `root` hangs under, as the wire names it. False: not attached (or not to
		// anything a client can find).
		bool Describe(const World& world, Entity root, AttachmentState& out)
		{
			out = { 0, 0 };
			const Entity mount = world.GetParent(root);
			if (!mount.IsValid()) return false;
			Entity owner = mount;
			while (owner.IsValid() && !world.HasComponent<NetworkIdentity>(owner)) owner = world.GetParent(owner);
			if (!owner.IsValid()) { WarnHostOnce(root, "to an entity that is not part of a network object"); return false; }
			const NetworkIdentity* identity = world.Read<NetworkIdentity>(owner);
			if (identity == nullptr || identity->id == Net::kInvalidNetworkId) { WarnHostOnce(root, "to a network object with no id yet"); return false; }
			std::string path;
			if (!ChildPathOf(world, owner, mount, path)) { WarnHostOnce(root, "to a mount with no name (or an unnamed entity above it)"); return false; }
			if (!path.empty())
			{
				Entity resolved;
				if (ResolveChildPath(world, owner, path, resolved) != ChildPathResult::Found || resolved != mount)
				{
					WarnHostOnce(root, "to a mount whose name path is not unique (rename its siblings)");
					return false;
				}
			}
			out.parent = identity->id;
			out.mount = ChildPathHash(path);
			return true;
		}

		bool WriteAttachment(const World& world, Entity root, const ReplicationContext&, ByteWriter& out)
		{
			AttachmentState state;
			if (!Describe(world, root, state)) { out.U8(0); return out.Ok(); }
			out.U8(1);
			out.VarU32(state.parent);
			out.U32(state.mount);
			return out.Ok();
		}

		bool DecodeAttachment(ByteReader& in, const ReplicationContext&, AttachmentState& out)
		{
			out = { 0, 0 };
			const std::uint8_t attached = in.U8();
			if (!in.Ok() || attached > 1) return false;
			if (attached == 0) return true;
			out.parent = in.VarU32();
			out.mount = in.U32();
			return in.Ok() && out.parent != Net::kInvalidNetworkId;
		}

		bool CommitAttachment(NetAttachment& a, const AttachmentState& state, const ReplicationContext&)
		{
			if (a.parent != state.parent || a.mount != state.mount) a.warned = false;   // a new problem may be reported
			a.parent = state.parent;
			a.mount = state.mount;
			return true;
		}

		// The client: the entity below `owner` whose name path hashes to `mount` (0: the owner).
		// Missing or more than one: kNullEntity, with `ambiguous` telling which.
		Entity FindMount(const World& world, Entity owner, std::uint32_t mount, bool& ambiguous)
		{
			ambiguous = false;
			if (mount == 0) return owner;
			Entity found = kNullEntity;
			int matches = 0;
			std::vector<std::pair<Entity, std::string>> open{ { owner, std::string{} } };
			for (std::size_t k = 0; k < open.size(); ++k)
			{
				const Entity at = open[k].first;
				const std::string base = open[k].second;   // a copy: `open` grows below
				world.ForEachChild(at, [&](Entity child) {
					if (IsClientQueryBody(world, child) || world.HasComponent<NetworkIdentity>(child)) return;
					const EntityName* name = world.Read<EntityName>(child);
					if (name == nullptr || name->name.empty()) return;
					std::string path = base.empty() ? name->name : base + "/" + name->name;
					if (ChildPathHash(path) == mount) { found = child; ++matches; }
					open.emplace_back(child, std::move(path));
				});
			}
			if (matches > 1) { ambiguous = true; return kNullEntity; }
			return found;
		}

		void WarnClientOnce(World& world, Entity root, const char* why)
		{
			const NetAttachment* a = world.Read<NetAttachment>(root);
			if (a == nullptr || a->warned) return;
			world.Write<NetAttachment>(root)->warned = true;
			CRU_WARN("[Net] network object {}:{} cannot be attached here yet: {}", root.index, root.generation, why);
		}

		void SetWaiting(World& world, Entity root, bool waiting)
		{
			const NetAttachment* a = world.Read<NetAttachment>(root);
			if (a != nullptr && a->waiting != waiting) world.Write<NetAttachment>(root)->waiting = waiting;
		}
	}

	ReplicatedComponent AttachmentSerializer()
	{
		ReplicatedComponent serializer = MakeSerializer<NetAttachment, AttachmentState, &DecodeAttachment, &CommitAttachment>({
			.id = NetComponentId::Attachment, .name = "Attachment", .schemaVersion = 1,
			.maxBytes = 1 + 5 + 4, .write = &WriteAttachment });
		serializer.addedAtRuntime = true;
		serializer.ensure = [](World& world, Entity entity) { world.AddComponent(entity, NetAttachment{}); };
		return serializer;
	}

	void RestartInterpolation(World& world, Entity root)
	{
		if (const NetInterpolation* buffer = world.Read<NetInterpolation>(root); buffer != nullptr && buffer->count > 0)
		{
			NetInterpolation* b = world.Write<NetInterpolation>(root);
			b->samples[0] = b->samples[b->count - 1];
			b->count = 1;
			Transform* t = world.Write<Transform>(root);
			if (t != nullptr)
			{
				t->positions = b->samples[0].position;
				t->rotation = b->samples[0].rotation;
			}
		}
		if (world.HasComponent<NetChildTransforms>(root)) RestartChildHistory(*world.Write<NetChildTransforms>(root));
	}

	bool ApplyAttachment(World& world, Entity root, const NetObjectFinder& find)
	{
		const NetAttachment* a = world.IsAlive(root) ? world.Read<NetAttachment>(root) : nullptr;
		if (a == nullptr) return true;
		const Entity current = world.GetParent(root);

		Entity mount = kNullEntity;
		if (a->parent != Net::kInvalidNetworkId)
		{
			const Entity owner = find ? find(a->parent) : kNullEntity;
			if (!world.IsAlive(owner)) { SetWaiting(world, root, true); return false; }   // not spawned here yet: normal while joining
			bool ambiguous = false;
			mount = FindMount(world, owner, a->mount, ambiguous);
			if (!mount.IsValid())
			{
				WarnClientOnce(world, root, ambiguous ? "its mount's name path names more than one entity here" : "its mount is not in the parent object here");
				SetWaiting(world, root, true);
				return false;
			}
		}

		if (current == mount) { SetWaiting(world, root, false); return true; }   // already as the host has it

		// The host's Transform is local to the new parent (or the world): keep the local values.
		const ParentResult result = world.TrySetParent(root, mount, false);
		if (result != ParentResult::Ok)
		{
			WarnClientOnce(world, root, ToString(result));
			SetWaiting(world, root, true);
			return false;
		}
		RestartInterpolation(world, root);
		RefreshClientQueryBodies(world, root);   // a new parent can change its world scale
		SetWaiting(world, root, false);
		return true;
	}

	std::size_t DetachNetworkDescendants(World& world, Entity root)
	{
		if (!world.IsAlive(root)) return 0;
		std::vector<Entity> attached;
		std::vector<Entity> open{ root };
		for (std::size_t k = 0; k < open.size(); ++k)
		{
			world.ForEachChild(open[k], [&](Entity child) {
				if (world.HasComponent<NetworkIdentity>(child)) attached.push_back(child);   // not below it: it moves as one
				else open.push_back(child);
			});
		}
		for (const Entity child : attached)
		{
			if (world.TrySetParent(child, kNullEntity, true) != ParentResult::Ok) world.TrySetParent(child, kNullEntity, false);
			// Its Transform is now the world pose; its samples were local to the mount. Start the
			// buffer again from where it is drawn.
			if (const NetInterpolation* buffer = world.Read<NetInterpolation>(child); buffer != nullptr && buffer->count > 0)
			{
				const Transform* t = world.Read<Transform>(child);
				NetInterpolation* b = world.Write<NetInterpolation>(child);
				b->samples[0] = b->samples[b->count - 1];
				b->samples[0].position = t->positions;
				b->samples[0].rotation = t->rotation;
				b->count = 1;
			}
			if (world.HasComponent<NetChildTransforms>(child)) RestartChildHistory(*world.Write<NetChildTransforms>(child));
			SetWaiting(world, child, true);
		}
		return attached.size();
	}
}
