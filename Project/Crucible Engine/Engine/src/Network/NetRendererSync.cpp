/*************************************************************************
* \file:         NetRendererSync.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                Renderer sync (see the header).
*
* \history
*    -v1.0       renderer sync: the Renderers serializer and the client's side
*
**************************************************************************/
#include "Network/NetRendererSync.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Network/ByteStream.h"
#include "Network/NetComponents.h"
#include "Network/NetPrefabTable.h"
#include "Network/NetSyncedChildren.h"
#include "Network/ReplicationRegistry.h"

namespace Crucible
{
	namespace
	{
		constexpr std::uint32_t kSlots = NetRenderers::kSlots;
		constexpr std::uint32_t kMaterialSlots = NetRenderers::kMaterialSlots;

		// The field mask of one slot.
		enum Field : std::uint8_t
		{
			kVisible   = 1u << 0,
			kColour    = 1u << 1,
			kModel     = 1u << 2,
			kModelRoot = 1u << 3,
			kMaterial  = 1u << 4,
			kMesh      = 1u << 5,
			kOverrides = 1u << 6,
			kParams    = 1u << 7,
		};

		// Slots this object has: the root and its first kSlots - 1 synced children.
		std::uint32_t SlotCount(const NetPrefabDesc& desc) noexcept
		{
			return 1u + static_cast<std::uint32_t>(std::min<std::size_t>(desc.syncedChildren.size(), kSlots - 1));
		}

		float Finite(float v, float fallback) noexcept { return std::isfinite(v) ? v : fallback; }

		struct RenderersState
		{
			std::uint8_t present;
			std::array<NetRenderers::Slot, kSlots> slots;
		};

		void WarnOverridesOnce(Entity e, std::size_t count)
		{
			static std::unordered_set<std::uint64_t> warned;
			if (!warned.insert(static_cast<std::uint64_t>(e.index) << 32 | e.generation).second) return;
			CRU_WARN("[Net] entity {}:{} overrides {} material slots: only the first {} are synchronized", e.index, e.generation, count, kMaterialSlots);
		}

		bool WriteRenderers(const World& world, Entity root, const ReplicationContext& ctx, ByteWriter& out)
		{
			if (ctx.desc == nullptr) return false;
			const std::uint32_t slots = SlotCount(*ctx.desc);
			std::array<Entity, kSlots> entity{};
			std::uint8_t present = 0;
			for (std::uint32_t s = 0; s < slots; ++s)
			{
				entity[s] = RendererSlotEntity(world, root, *ctx.desc, s);
				if (entity[s].IsValid() && world.Read<MeshRenderer>(entity[s]) != nullptr) present = static_cast<std::uint8_t>(present | (1u << s));
			}
			out.U8(present);
			for (std::uint32_t s = 0; s < slots; ++s)
			{
				if ((present & (1u << s)) == 0) continue;
				const MeshRenderer& r = *world.Read<MeshRenderer>(entity[s]);
				const MaterialParams* params = world.Read<MaterialParams>(entity[s]);
				const Math::vec4 colour(Finite(r.colour.x, 0.f), Finite(r.colour.y, 0.f), Finite(r.colour.z, 0.f), Finite(r.colour.w, 0.f));
				std::size_t overrides = r.materialOverrides.size();
				while (overrides > 0 && r.materialOverrides[overrides - 1] == 0) --overrides;   // trailing 0s inherit anyway
				if (overrides > kMaterialSlots) { WarnOverridesOnce(entity[s], overrides); overrides = kMaterialSlots; }

				std::uint8_t fields = 0;
				if (r.visible) fields |= kVisible;
				if (colour != Math::vec4(0.f)) fields |= kColour;
				if (r.modelAsset != 0) fields |= kModel;
				if (r.modelAsset != 0 && r.modelRoot != InvalidModelIndex) fields |= kModelRoot;
				if (r.materialAsset != 0) fields |= kMaterial;
				if (r.meshAsset != 0) fields |= kMesh;
				if (overrides > 0) fields |= kOverrides;
				if (params != nullptr) fields |= kParams;
				out.U8(fields);
				if (fields & kColour) { out.F32(colour.x); out.F32(colour.y); out.F32(colour.z); out.F32(colour.w); }
				if (fields & kModel) out.U64(r.modelAsset);
				if (fields & kModelRoot) out.U32(r.modelRoot);
				if (fields & kMaterial) out.U64(r.materialAsset);
				if (fields & kMesh) out.U64(r.meshAsset);
				if (fields & kOverrides)
				{
					out.U8(static_cast<std::uint8_t>(overrides));
					for (std::size_t i = 0; i < overrides; ++i) out.U64(r.materialOverrides[i]);
				}
				if (fields & kParams)
				{
					out.F32(Finite(params->emissiveScale, 1.f));
					out.F32(Finite(params->custom.x, 0.f));
					out.F32(Finite(params->custom.y, 0.f));
					out.F32(Finite(params->custom.z, 0.f));
				}
			}
			return out.Ok();
		}

		bool ReadFinite(ByteReader& in, float& out) { out = in.F32(); return in.Ok() && std::isfinite(out); }

		bool DecodeRenderers(ByteReader& in, const ReplicationContext& ctx, RenderersState& out)
		{
			if (ctx.desc == nullptr) return false;
			out.present = in.U8();
			const std::uint32_t slots = SlotCount(*ctx.desc);
			if (!in.Ok() || (out.present >> slots) != 0) return false;   // a slot this object does not have
			for (std::uint32_t s = 0; s < kSlots; ++s)
			{
				NetRenderers::Slot& slot = out.slots[s];
				slot = NetRenderers::Slot{};
				if ((out.present & (1u << s)) == 0) continue;
				slot.present = true;
				const std::uint8_t fields = in.U8();
				if (!in.Ok()) return false;
				slot.visible = (fields & kVisible) != 0;
				if (fields & kColour)
					if (!ReadFinite(in, slot.colour.x) || !ReadFinite(in, slot.colour.y) || !ReadFinite(in, slot.colour.z) || !ReadFinite(in, slot.colour.w)) return false;
				if (fields & kModel) { slot.model = in.U64(); if (slot.model == 0) return false; }   // one state, one encoding
				if (fields & kModelRoot) { if (!(fields & kModel)) return false; slot.modelRoot = in.U32(); if (slot.modelRoot == InvalidModelIndex) return false; }
				if (fields & kMaterial) { slot.material = in.U64(); if (slot.material == 0) return false; }
				if (fields & kMesh) { slot.mesh = in.U64(); if (slot.mesh == 0) return false; }
				if (fields & kOverrides)
				{
					slot.materialCount = in.U8();
					if (!in.Ok() || slot.materialCount == 0 || slot.materialCount > kMaterialSlots) return false;
					for (std::uint32_t i = 0; i < slot.materialCount; ++i) slot.materials[i] = in.U64();
					if (slot.materials[slot.materialCount - 1] == 0) return false;   // trailing 0s are never sent
				}
				if (fields & kParams)
				{
					slot.hasParams = true;
					if (!ReadFinite(in, slot.emissiveScale) || !ReadFinite(in, slot.custom.x) || !ReadFinite(in, slot.custom.y) || !ReadFinite(in, slot.custom.z)) return false;
				}
				if (!in.Ok()) return false;
			}
			return in.Ok();
		}

		bool CommitRenderers(NetRenderers& renderers, const RenderersState& state, const ReplicationContext&)
		{
			renderers.slots.assign(state.slots.begin(), state.slots.end());   // may allocate: the registry rolls back
			return true;
		}
	}

	ReplicatedComponent RenderersSerializer()
	{
		ReplicatedComponent serializer = MakeSerializer<NetRenderers, RenderersState, &DecodeRenderers, &CommitRenderers>({
			.id = NetComponentId::Renderers, .name = "Renderers", .schemaVersion = 1,
			.maxBytes = static_cast<std::uint16_t>(1 + kSlots * (1 + 16 + 8 + 4 + 8 + 8 + 1 + 8 * kMaterialSlots + 16)),
			.write = &WriteRenderers });
		serializer.addedAtRuntime = true;
		serializer.ensure = [](World& world, Entity entity) { world.AddComponent(entity, NetRenderers{}); };
		serializer.maxBytesFor = [](const NetPrefabDesc& desc) -> std::uint16_t {   // only this object's slots
			return static_cast<std::uint16_t>(1 + SlotCount(desc) * (1 + 16 + 8 + 4 + 8 + 8 + 1 + 8 * kMaterialSlots + 16));
		};
		return serializer;
	}

	Entity RendererSlotEntity(const World& world, Entity root, const NetPrefabDesc& desc, unsigned slot)
	{
		if (!world.IsAlive(root) || slot >= SlotCount(desc)) return kNullEntity;
		if (slot == 0) return root;
		Entity child;
		return ResolveChildPath(world, root, desc.syncedChildren[slot - 1], child) == ChildPathResult::Found ? child : kNullEntity;
	}

	unsigned ApplyRenderers(World& world, Entity root, const NetPrefabDesc& desc)
	{
		const NetRenderers* received = world.IsAlive(root) ? world.Read<NetRenderers>(root) : nullptr;
		if (received == nullptr) return 0;
		const std::vector<NetRenderers::Slot> slots = received->slots;   // a copy: writes below may move components
		unsigned changed = 0;
		for (std::uint32_t s = 0; s < slots.size() && s < SlotCount(desc); ++s)
		{
			const NetRenderers::Slot& want = slots[s];
			if (!want.present) continue;   // no MeshRenderer on the host: left as it is
			const Entity e = RendererSlotEntity(world, root, desc, s);
			const MeshRenderer* r = e.IsValid() ? world.Read<MeshRenderer>(e) : nullptr;
			if (r == nullptr) continue;

			std::vector<std::uint64_t> overrides(want.materials.begin(), want.materials.begin() + want.materialCount);
			std::vector<std::uint64_t> current = r->materialOverrides;
			while (!current.empty() && current.back() == 0) current.pop_back();
			const std::uint32_t modelRoot = want.model != 0 ? want.modelRoot : r->modelRoot;
			const bool modelChanged = r->modelAsset != want.model;
			const bool same = r->visible == want.visible && r->colour == want.colour && !modelChanged && r->modelRoot == modelRoot
				&& r->materialAsset == want.material && r->meshAsset == want.mesh && current == overrides;
			if (!same)
			{
				MeshRenderer* w = world.Write<MeshRenderer>(e);
				if (modelChanged)
				{
					// As C# MeshRenderer.Model and the Inspector: a new model drops the plain mesh, its part
					// root and the slot overrides (an AnimatedModel starts over on its own).
					w->modelAsset = want.model;
					w->modelRoot = InvalidModelIndex;
					w->materialOverrides.clear();
					if (want.model != 0) w->mesh = {};
				}
				w->modelRoot = modelRoot;
				w->visible = want.visible;
				w->colour = want.colour;
				if (w->materialAsset != want.material) { w->materialAsset = want.material; w->material = {}; }
				if (w->meshAsset != want.mesh) { w->meshAsset = want.mesh; if (want.mesh != 0) w->mesh = {}; }
				if (current != overrides) w->materialOverrides = overrides;
				++changed;
			}

			// MaterialParams: the host's values, or the defaults when the host's entity has none.
			const MaterialParams wanted = want.hasParams ? MaterialParams{ .emissiveScale = want.emissiveScale, .custom = want.custom } : MaterialParams{};
			if (const MaterialParams* p = world.Read<MaterialParams>(e))
			{
				if (p->emissiveScale != wanted.emissiveScale || p->custom != wanted.custom) { *world.Write<MaterialParams>(e) = wanted; ++changed; }
			}
			else if (want.hasParams)
			{
				world.AddComponent(e, wanted);
				++changed;
			}
		}
		return changed;
	}
}
