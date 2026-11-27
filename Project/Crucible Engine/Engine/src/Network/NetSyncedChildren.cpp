/*************************************************************************
* \file:         NetSyncedChildren.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.1
* \brief
*                Synced children (see the header).
*
* \history
*    -v1.0       synced children: paths, Children/Rotation and Children/Full
*    -v1.1       Children/OnOff, ApplyChildOnOff
*
**************************************************************************/
#include "Network/NetSyncedChildren.h"

#include <algorithm>
#include <array>
#include <format>
#include <unordered_set>

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Json/JsonWrapper.h"
#include "Network/ByteStream.h"
#include "Network/NetComponents.h"
#include "Network/NetPrefabTable.h"
#include "Network/NetQueryBodies.h"
#include "Network/Quantize.h"
#include "Network/ReplicationRegistry.h"
#include "Network/Systems/NetInterpolationSystem.h"
#include "Scene/Prefab.h"

namespace Crucible
{
	namespace
	{
		// Calls f(segment) for each '/'-separated segment; stops (false) when f returns false.
		template <typename F>
		bool ForEachSegment(std::string_view path, F&& f)
		{
			std::size_t start = 0;
			while (start <= path.size())
			{
				const std::size_t slash = path.find('/', start);
				const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
				if (!f(path.substr(start, end - start))) return false;
				if (slash == std::string_view::npos) break;
				start = slash + 1;
			}
			return true;
		}

		std::string_view NameOf(const World& world, Entity e)
		{
			const EntityName* name = world.Read<EntityName>(e);
			return name != nullptr ? std::string_view(name->name) : std::string_view();
		}

		// A child a path may match or enter: not a query body, not another network object.
		bool Matchable(const World& world, Entity child)
		{
			return !IsClientQueryBody(world, child) && !world.HasComponent<NetworkIdentity>(child);
		}
	}

	bool IsValidChildPath(std::string_view path, std::string* why)
	{
		const auto fail = [&](std::string text) { if (why) *why = std::move(text); return false; };
		if (path.empty()) return fail("an empty path");
		if (path.size() > kMaxChildPathLength) return fail(std::format("longer than {} characters", kMaxChildPathLength));
		bool emptySegment = false;
		ForEachSegment(path, [&](std::string_view segment) { emptySegment = emptySegment || segment.empty(); return !emptySegment; });
		if (emptySegment) return fail("an empty name in it (a leading, trailing or doubled '/')");
		return true;
	}

	bool CheckSyncedChildList(const std::vector<std::string>& paths, std::string& error)
	{
		if (paths.size() > kMaxSyncedChildren)
		{
			error = std::format("syncedChildren: {} children, at most {}", paths.size(), kMaxSyncedChildren);
			return false;
		}
		for (std::size_t i = 0; i < paths.size(); ++i)
		{
			std::string why;
			if (!IsValidChildPath(paths[i], &why)) { error = std::format("syncedChildren: '{}' is not a valid path: {}", paths[i], why); return false; }
			for (std::size_t j = i + 1; j < paths.size(); ++j)
				if (paths[i] == paths[j]) { error = std::format("syncedChildren: '{}' is listed twice", paths[i]); return false; }
		}
		return true;
	}

	ChildPathResult ResolveChildPath(const World& world, Entity root, std::string_view path, Entity& out)
	{
		out = kNullEntity;
		if (!world.IsAlive(root) || !IsValidChildPath(path)) return ChildPathResult::Missing;
		Entity current = root;
		ChildPathResult result = ChildPathResult::Found;
		ForEachSegment(path, [&](std::string_view segment) {
			Entity match = kNullEntity;
			int matches = 0;
			world.ForEachChild(current, [&](Entity child) {
				if (Matchable(world, child) && NameOf(world, child) == segment) { match = child; ++matches; }
			});
			if (matches == 0) { result = ChildPathResult::Missing; return false; }
			if (matches > 1) { result = ChildPathResult::Ambiguous; return false; }
			current = match;
			return true;
		});
		if (result == ChildPathResult::Found) out = current;
		return result;
	}

	ChildPathResult ResolveChildPath(const Prefab& prefab, std::size_t rootIndex, std::string_view path, std::size_t& out)
	{
		out = rootIndex;
		if (!prefab.IsValid() || rootIndex >= prefab.EntityCount() || !IsValidChildPath(path)) return ChildPathResult::Missing;
		std::vector<std::string> names(prefab.EntityCount());
		for (std::size_t i = 0; i < names.size(); ++i)
			names[i] = prefab.Document().Get<std::string>(prefab.ComponentsPath(i) + "/EntityName/name", std::string{});
		std::size_t current = rootIndex;
		ChildPathResult result = ChildPathResult::Found;
		ForEachSegment(path, [&](std::string_view segment) {
			std::size_t match = 0;
			int matches = 0;
			for (std::size_t i = 0; i < names.size(); ++i)
			{
				if (prefab.ParentAt(i) != static_cast<int>(current) || names[i] != segment) continue;
				match = i;
				++matches;
			}
			if (matches == 0) { result = ChildPathResult::Missing; return false; }
			if (matches > 1) { result = ChildPathResult::Ambiguous; return false; }
			current = match;
			return true;
		});
		if (result == ChildPathResult::Found) out = current;
		return result;
	}

	bool ChildPathOf(const World& world, Entity root, Entity entity, std::string& out)
	{
		out.clear();
		if (!world.IsAlive(root) || !world.IsAlive(entity)) return false;
		std::vector<std::string_view> segments;
		Entity e = entity;
		while (e != root)
		{
			if (!e.IsValid()) return false;   // reached the top without passing `root`
			const std::string_view name = NameOf(world, e);
			if (name.empty()) return false;
			segments.push_back(name);
			e = world.GetParent(e);
		}
		for (auto it = segments.rbegin(); it != segments.rend(); ++it)
		{
			if (!out.empty()) out += '/';
			out += *it;
		}
		return true;
	}

	std::uint32_t ChildPathHash(std::string_view path) noexcept
	{
		if (path.empty()) return 0;
		std::uint32_t h = 2166136261u;
		for (const char c : path) { h ^= static_cast<std::uint8_t>(c); h *= 16777619u; }
		return h == 0 ? 1u : h;
	}

	// ---- the serializers -------------------------------------------------------------------------

	namespace
	{
		struct ChildState
		{
			std::uint16_t present;
			bool          positions;
			std::array<Math::vec3, kMaxSyncedChildren> position;
			std::array<Math::quat, kMaxSyncedChildren> rotation;
		};

		// One warning per object and slot on the host (the write path has a const World).
		void WarnHostOnce(Entity root, std::size_t slot, std::string_view path, ChildPathResult result)
		{
			static std::unordered_set<std::uint64_t> warned;
			const std::uint64_t key = (static_cast<std::uint64_t>(root.index) << 32 | root.generation) * 31u + slot;
			if (!warned.insert(key).second) return;
			CRU_WARN("[Net] synced child '{}' of entity {}:{} {}: not synchronized", path, root.index, root.generation,
				result == ChildPathResult::Ambiguous ? "names more than one entity" : "is missing");
		}

		template <bool Positions>
		bool WriteChildren(const World& world, Entity root, const ReplicationContext& ctx, ByteWriter& out)
		{
			if (ctx.desc == nullptr) return false;
			const std::vector<std::string>& paths = ctx.desc->syncedChildren;
			std::array<const Transform*, kMaxSyncedChildren> found{};
			std::uint16_t present = 0;
			for (std::size_t i = 0; i < paths.size() && i < kMaxSyncedChildren; ++i)
			{
				Entity child;
				const ChildPathResult result = ResolveChildPath(world, root, paths[i], child);
				if (result != ChildPathResult::Found) { WarnHostOnce(root, i, paths[i], result); continue; }
				found[i] = world.Read<Transform>(child);
				if (found[i] != nullptr) present = static_cast<std::uint16_t>(present | (1u << i));
			}
			out.U16(present);
			for (std::size_t i = 0; i < kMaxSyncedChildren; ++i)
			{
				if ((present & (1u << i)) == 0) continue;
				if constexpr (Positions)
				{
					out.I32(Quantize::PositionToFixed(found[i]->positions.x));
					out.I32(Quantize::PositionToFixed(found[i]->positions.y));
					out.I32(Quantize::PositionToFixed(found[i]->positions.z));
				}
				out.U32(Quantize::PackQuat(found[i]->rotation));
			}
			return out.Ok();
		}

		// Fixed-point positions and packed rotations are finite whatever the bytes. A slot past the
		// object's list means the sender disagrees about the object: refused.
		template <bool Positions>
		bool DecodeChildren(ByteReader& in, const ReplicationContext& ctx, ChildState& out)
		{
			if (ctx.desc == nullptr) return false;
			out.present = in.U16();
			out.positions = Positions;
			const std::size_t listed = std::min<std::size_t>(ctx.desc->syncedChildren.size(), kMaxSyncedChildren);
			if (!in.Ok() || (listed < 16 && (out.present >> listed) != 0)) return false;
			for (std::size_t i = 0; i < kMaxSyncedChildren && in.Ok(); ++i)
			{
				out.position[i] = Math::vec3(0.f);
				out.rotation[i] = Math::quat(1.f, 0.f, 0.f, 0.f);
				if ((out.present & (1u << i)) == 0) continue;
				if constexpr (Positions)
				{
					const float x = Quantize::FixedToPosition(in.I32());
					const float y = Quantize::FixedToPosition(in.I32());
					const float z = Quantize::FixedToPosition(in.I32());
					out.position[i] = Math::vec3(x, y, z);
				}
				out.rotation[i] = Quantize::UnpackQuat(in.U32());
			}
			return in.Ok();
		}

		bool CommitChildren(NetChildTransforms& c, const ChildState& state, const ReplicationContext& ctx)
		{
			NetChildTransforms::Sample sample;
			sample.tick = ctx.tick;
			sample.present = state.present;
			sample.positions = state.position;
			sample.rotations = state.rotation;
			c.positions = state.positions;
			// Ticks only grow (NetReceiveSystem drops stale snapshots); one that does not (a spawn
			// record and a snapshot of the same tick) replaces the newest sample.
			if (!c.samples.empty() && c.samples.back().tick >= sample.tick) c.samples.back() = sample;
			else c.samples.push_back(sample);
			if (c.samples.size() > NetChildTransforms::kHistory) c.samples.erase(c.samples.begin());
			return true;
		}

		template <bool Positions>
		ReplicatedComponent ChildSerializer()
		{
			ReplicatedComponent serializer = MakeSerializer<NetChildTransforms, ChildState, &DecodeChildren<Positions>, &CommitChildren>({
				.id = Positions ? NetComponentId::ChildTransforms : NetComponentId::ChildRotations,
				.name = Positions ? "Children/Full" : "Children/Rotation",
				.schemaVersion = 1,
				.maxBytes = static_cast<std::uint16_t>(2 + (Positions ? 16 : 4) * kMaxSyncedChildren),
				.write = &WriteChildren<Positions> });
			serializer.addedAtRuntime = true;
			serializer.ensure = [](World& world, Entity entity) { world.AddComponent(entity, NetChildTransforms{}); };
			serializer.maxBytesFor = [](const NetPrefabDesc& desc) -> std::uint16_t {   // only the listed children
				const std::size_t listed = std::min<std::size_t>(desc.syncedChildren.size(), kMaxSyncedChildren);
				return static_cast<std::uint16_t>(2 + (Positions ? 16 : 4) * listed);
			};
			return serializer;
		}
	}

	ReplicatedComponent ChildRotationsSerializer() { return ChildSerializer<false>(); }
	ReplicatedComponent ChildTransformsSerializer() { return ChildSerializer<true>(); }

	// ---- Children/OnOff --------------------------------------------------

	namespace
	{
		struct OnOffState
		{
			std::uint16_t present;
			std::array<std::uint8_t, kMaxSyncedChildren> has;
			std::array<std::uint8_t, kMaxSyncedChildren> on;
		};

		bool WriteOnOff(const World& world, Entity root, const ReplicationContext& ctx, ByteWriter& out)
		{
			if (ctx.desc == nullptr) return false;
			const std::vector<std::string>& paths = ctx.desc->syncedChildren;
			const std::vector<ComponentId>& listed = ctx.desc->childEnabledReplicated;
			std::array<Entity, kMaxSyncedChildren> child{};
			std::uint16_t present = 0;
			for (std::size_t i = 0; i < paths.size() && i < kMaxSyncedChildren; ++i)
			{
				const ChildPathResult result = ResolveChildPath(world, root, paths[i], child[i]);
				if (result == ChildPathResult::Found) present = static_cast<std::uint16_t>(present | (1u << i));
				else WarnHostOnce(root, i, paths[i], result);
			}
			out.U16(present);
			for (std::size_t i = 0; i < kMaxSyncedChildren; ++i)
			{
				if ((present & (1u << i)) == 0) continue;
				std::uint8_t has = 0, on = 0;
				for (std::size_t j = 0; j < listed.size() && j < 8; ++j)
				{
					if (listed[j] >= kMaxComponents || !world.GetSignature(child[i]).test(listed[j])) continue;
					has = static_cast<std::uint8_t>(has | (1u << j));
					if (world.IsComponentEnabled(child[i], listed[j])) on = static_cast<std::uint8_t>(on | (1u << j));
				}
				out.U8(has);
				out.U8(on);
			}
			return out.Ok();
		}

		bool DecodeOnOff(ByteReader& in, const ReplicationContext& ctx, OnOffState& out)
		{
			if (ctx.desc == nullptr) return false;
			out.present = in.U16();
			out.has = {};
			out.on = {};
			const std::size_t children = std::min<std::size_t>(ctx.desc->syncedChildren.size(), kMaxSyncedChildren);
			const std::size_t listed = ctx.desc->childEnabledReplicated.size();
			if (!in.Ok() || listed > 8 || (children < 16 && (out.present >> children) != 0)) return false;
			for (std::size_t i = 0; i < kMaxSyncedChildren && in.Ok(); ++i)
			{
				if ((out.present & (1u << i)) == 0) continue;
				out.has[i] = in.U8();
				out.on[i] = in.U8();
				// Bits past the list, or "on" for a component the child does not have: malformed.
				if (listed < 8 && ((out.has[i] >> listed) != 0)) return false;
				if ((out.on[i] & ~out.has[i]) != 0) return false;
			}
			return in.Ok();
		}

		bool CommitOnOff(NetChildEnabled& c, const OnOffState& state, const ReplicationContext&)
		{
			c.present = state.present;
			c.has = state.has;
			c.on = state.on;
			return true;
		}
	}

	ReplicatedComponent ChildOnOffSerializer()
	{
		ReplicatedComponent serializer = MakeSerializer<NetChildEnabled, OnOffState, &DecodeOnOff, &CommitOnOff>({
			.id = NetComponentId::ChildOnOff, .name = "Children/OnOff", .schemaVersion = 1,
			.maxBytes = static_cast<std::uint16_t>(2 + 2 * kMaxSyncedChildren), .write = &WriteOnOff });
		serializer.addedAtRuntime = true;
		serializer.ensure = [](World& world, Entity entity) { world.AddComponent(entity, NetChildEnabled{}); };
		serializer.maxBytesFor = [](const NetPrefabDesc& desc) -> std::uint16_t {
			return static_cast<std::uint16_t>(2 + 2 * std::min<std::size_t>(desc.syncedChildren.size(), kMaxSyncedChildren));
		};
		return serializer;
	}

	unsigned ApplyChildOnOff(World& world, Entity root, const NetPrefabDesc& desc)
	{
		const NetChildEnabled* received = world.IsAlive(root) ? world.Read<NetChildEnabled>(root) : nullptr;
		if (received == nullptr) return 0;
		const NetChildEnabled state = *received;   // a copy: switching may move components
		unsigned switched = 0;
		for (std::size_t i = 0; i < desc.syncedChildren.size() && i < kMaxSyncedChildren; ++i)
		{
			if ((state.present & (1u << i)) == 0) continue;
			Entity child;
			if (ResolveChildPath(world, root, desc.syncedChildren[i], child) != ChildPathResult::Found) continue;   // reported by BindSyncedChildren
			for (std::size_t j = 0; j < desc.childEnabledReplicated.size() && j < 8; ++j)
			{
				if ((state.has[i] & (1u << j)) == 0) continue;
				Entity target = child;
				ComponentId id = desc.childEnabledReplicated[j];
				if (id >= kMaxComponents || !world.GetSignature(child).test(id)) continue;
				// A host-only Collider or CharacterController stays off here: its query body follows.
				if (std::ranges::find(desc.serverOnly, id) != desc.serverOnly.end())
				{
					if (id == ComponentTypeId<Collider>() && world.HasComponent<CharacterController>(child)) continue;   // no body of its own
					target = FindClientQueryBody(world, child);
					id = ComponentTypeId<Collider>();
					if (!target.IsValid()) continue;
				}
				const bool wanted = (state.on[i] & (1u << j)) != 0;
				if (world.IsComponentEnabled(target, id) == wanted) continue;
				if (world.TrySetComponentEnabled(target, id, wanted)) ++switched;
			}
		}
		return switched;
	}

	// ---- the client ------------------------------------------------------------------------------

	void BindSyncedChildren(World& world, Entity root, const NetPrefabDesc& desc)
	{
		const NetChildTransforms* current = world.IsAlive(root) ? world.Read<NetChildTransforms>(root) : nullptr;
		if (current == nullptr) return;
		std::array<Entity, kMaxSyncedChildren> children{};
		std::uint16_t bound = 0;
		std::uint16_t warned = current->warned;
		for (std::size_t i = 0; i < desc.syncedChildren.size() && i < kMaxSyncedChildren; ++i)
		{
			Entity child;
			const ChildPathResult result = ResolveChildPath(world, root, desc.syncedChildren[i], child);
			if (result == ChildPathResult::Found) { children[i] = child; bound = static_cast<std::uint16_t>(bound | (1u << i)); continue; }
			if ((warned & (1u << i)) != 0) continue;
			warned = static_cast<std::uint16_t>(warned | (1u << i));
			CRU_WARN("[Net] synced child '{}' of entity {}:{} {} on this machine: left as it is", desc.syncedChildren[i], root.index, root.generation,
				result == ChildPathResult::Ambiguous ? "names more than one entity" : "is missing");
		}
		if (current->bound == bound && current->warned == warned && current->children == children) return;   // no mutable access: no change stamp
		NetChildTransforms* c = world.Write<NetChildTransforms>(root);
		c->children = children;
		c->bound = bound;
		c->warned = warned;
	}

	bool EvaluateChild(const NetChildTransforms& transforms, std::uint32_t slot, double renderTick, Math::vec3& position, Math::quat& rotation) noexcept
	{
		if (slot >= kMaxSyncedChildren) return false;
		const std::uint16_t bit = static_cast<std::uint16_t>(1u << slot);
		const NetChildTransforms::Sample* before = nullptr;   // the newest carrying it at or before renderTick
		const NetChildTransforms::Sample* after = nullptr;    // the oldest carrying it after renderTick
		const NetChildTransforms::Sample* oldest = nullptr;
		for (const NetChildTransforms::Sample& s : transforms.samples)
		{
			if ((s.present & bit) == 0) continue;
			if (oldest == nullptr) oldest = &s;
			if (static_cast<double>(s.tick) <= renderTick) before = &s;
			else if (after == nullptr) after = &s;
		}
		if (oldest == nullptr) return false;
		if (before == nullptr) { position = oldest->positions[slot]; rotation = oldest->rotations[slot]; return true; }   // all ahead
		if (after == nullptr) { position = before->positions[slot]; rotation = before->rotations[slot]; return true; }    // hold the newest
		const Math::vec3 jump = after->positions[slot] - before->positions[slot];
		const float limit = NetInterpolationSystem::kTeleportMetres;
		if (glm::dot(jump, jump) > limit * limit) { position = before->positions[slot]; rotation = before->rotations[slot]; return true; }   // snaps at `after`
		const float alpha = static_cast<float>((renderTick - static_cast<double>(before->tick)) / static_cast<double>(after->tick - before->tick));
		position = glm::mix(before->positions[slot], after->positions[slot], alpha);
		rotation = glm::slerp(before->rotations[slot], after->rotations[slot], alpha);
		return true;
	}

	void ApplyChildTransforms(World& world, double renderTick)
	{
		world.CreateQuery<const NetChildTransforms, const NetworkIdentity>().ForEach(
			[&](Entity, const NetChildTransforms& transforms, const NetworkIdentity&) {
				for (std::uint32_t slot = 0; slot < kMaxSyncedChildren; ++slot)
				{
					if ((transforms.bound & (1u << slot)) == 0) continue;
					const Entity child = transforms.children[slot];
					const Transform* current = world.IsAlive(child) ? world.Read<Transform>(child) : nullptr;
					Math::vec3 position;
					Math::quat rotation;
					if (current == nullptr || !EvaluateChild(transforms, slot, renderTick, position, rotation)) continue;
					const bool movePosition = transforms.positions && current->positions != position;
					if (!movePosition && current->rotation == rotation) continue;   // no mutable access: no change stamp
					Transform* t = world.GetComponent<Transform>(child);
					if (transforms.positions) t->positions = position;
					t->rotation = rotation;
				}
			});
	}

	void RestartChildHistory(NetChildTransforms& transforms) noexcept
	{
		if (transforms.samples.size() > 1) transforms.samples.erase(transforms.samples.begin(), transforms.samples.end() - 1);
	}
}
