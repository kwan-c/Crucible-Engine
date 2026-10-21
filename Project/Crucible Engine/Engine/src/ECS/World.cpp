/*************************************************************************
* \file:         World.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.7
* \brief
*                Implementation of World: entity creation/destruction
*                and the archetype-move logic used by
*                AddComponent/RemoveComponent. See World.h for the
*                public interface and member comments.
*
* \details
*                World's constructor wires the ArchetypeRegistry to the
*                ComponentRegistry and registers every known component
*                type (see ComponentRegistry::RegisterAll in
*                ComponentRegistry.cpp). CreateEntity/DestroyEntity are
*                thin wrappers around EntityManager (see ECSCore.h) and
*                Archetype (see Archetype.cpp). MoveEntity() is the
*                shared relocation routine used whenever an entity's
*                component set changes (Add/RemoveComponent in
*                World.h). The hierarchy members (SetParent, DetachChildren,
*                sibling order, destroy-with-children
*                and the link-keeping hook) are at the end of this file.
*
*                Failure handling: every structural operation does all of
*                its throwing work (allocation, component construction)
*                before it changes anything observable, and undoes partial
*                work on failure. Lifecycle hooks are isolated: a throwing
*                hook is logged and counted, never propagated.
*
* \history
*    -v1.0       initial CreateEntity/DestroyEntity/GetSignature/MoveEntity
*    -v1.1       hook symmetry (creation fires add hooks, destruction fires
*                per-component remove hooks), DestroyAllEntities, hooks
*                cannot be registered mid-iteration, MoveEntity relocates
*                the added value instead of default-construct + assign
*    -v1.2       DestroyEntity body moved into DestroyEntityImpl(entity,
*                fireHooks) so CommandBuffer can silently roll back spawns
*                whose add hooks never ran
*    -v1.3       exception-safe creation (slot released on failure),
*                all-or-nothing CreateEntities, reserve-then-move
*                MoveEntity, signature/count validation, catch-log-continue
*                hook dispatch, structural-change enforcement for running
*                systems
*    -v1.4       mandatory base components ORed into every creation
*                signature, MoveEntity guard, DestroyEntity destroys
*                descendants, hierarchy hook installed by the constructor;
*                Unity-style hierarchy members (SetParent, DetachChildren,
*                sibling order)
*    -v1.5       GLM math fixed and sped up: DecomposeTRS wraps
*                glm::decompose (validated first, unit-length axes, outputs
*                written only on success); glm::affineInverse; no
*                negative scales (mirrored keep-world
*                solves are refused); shared PlaceNewEntity (single + batch
*                creation) and UnlinkFromSiblings (detach + destroy hook)
*    -v1.6       entity names: every creation path names an unnamed entity
*                "Entity N" (spawns once their With<T>() values are in)
*    -v1.7       NormalizeName applies both naming rules on every path
*                (blank -> default, default-shaped -> reserved);
*                ReissueDefaultName, DeferredNaming; SetEntityName refused
*                inside declared systems (allowed while a Query iterates)
*
**************************************************************************/

#include "World.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

#include <glm/gtc/matrix_inverse.hpp>   // glm::affineInverse
#include <glm/gtx/matrix_decompose.hpp>  // glm::decompose (GLM_ENABLE_EXPERIMENTAL is set by Math.h)
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Diagnostics/Logger.h>

namespace Crucible
{
	namespace
	{
		// Empty or only whitespace: not a name, so the entity gets a default.
		bool IsBlankName(const std::string& name) noexcept
		{
			for (const char c : name)
			{
				if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v') return false;
			}
			return true;
		}

		// N for a name shaped exactly like a default ("Entity N", N >= 1); 0 for anything else.
		std::uint32_t DefaultNameNumber(std::string_view name) noexcept
		{
			constexpr std::string_view prefix = "Entity ";
			if (!name.starts_with(prefix)) return 0;
			name.remove_prefix(prefix.size());
			if (name.empty() || name.size() > 9 || name.front() == '0') return 0;
			std::uint32_t number = 0;
			for (const char c : name)
			{
				if (c < '0' || c > '9') return 0;
				number = number * 10 + static_cast<std::uint32_t>(c - '0');
			}
			return number;
		}
	}

	//build archetype registry over m_components
	World::World() :m_archetypes(m_components)
	{
		m_components.RegisterAll();
		InstallHierarchyHook();   // keeps HierarchyNode links valid on destroy (see the hierarchy section below)
	}

	void World::RequireStructuralChange(const char* operation) const
	{
		// Structural changes during iteration would invalidate the rows being walked.
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error(std::string("World::") + operation +
				" called while a Query is iterating - use a CommandBuffer instead (see CommandBuffer.h)");
		}
		// Declared systems may run alongside others, so they must defer structural changes.
		// The message is only built when such a system is running (no allocation on the normal path).
		if (Detail::RestrictedSystem())
		{
			CheckStructuralAccess((std::string("World::") + operation).c_str());
		}
	}

	EntityRecord& World::RequireMutableEntity(Entity entity, const char* operation)
	{
		RequireStructuralChange(operation);
		if (!m_entities.IsAlive(entity))
		{
			throw std::invalid_argument(std::string("World::") + operation + " called with a dead/stale Entity");
		}
		return m_entities.Record(entity);
	}

	Entity World::CreateEntity()
	{
		return CreateEntityImpl(ComponentSignature{}, true);
	}

	Entity World::CreateEntity(const ComponentSignature& signature)
	{
		return CreateEntityImpl(signature, true);
	}

	Entity World::CreateEntityImpl(const ComponentSignature& requested, bool fireAddHooks)
	{
		RequireStructuralChange("CreateEntity");

		// Every creation path (CreateEntity, CreateEntity(signature), CommandBuffer spawns)
		// comes through here: add the mandatory base BEFORE choosing the archetype, so the
		// entity is born in its final archetype (one placement, no migrations).
		const ComponentSignature signature = requested | kMandatoryComponents;
		m_components.ValidateSignature(signature, "World::CreateEntity");

		// Resolve the archetype before taking a slot, so a failure here leaks nothing.
		Archetype* archetype = m_archetypes.FindOrCreate(signature);
		const Entity entity = PlaceNewEntity(*archetype);

		// Without hooks (a CommandBuffer spawn) the caller names it after its With<T>() values.
		if (fireAddHooks)
		{
			try { NormalizeName(entity); }
			catch (...) { DestroyEntityImpl(entity, false); throw; }
			FireAddHooksForRow(*archetype, m_entities.RecordAt(entity.index).row, entity);
		}

		return entity;
	}

	Entity World::CreateNamedEntity(std::string name, const ComponentSignature& signature)
	{
		const Entity entity = CreateEntityImpl(signature, false);
		try { ApplyName(entity, std::move(name)); }
		catch (...) { DestroyEntityImpl(entity, false); throw; }
		NotifyComponentsAdded(entity);   // hooks see the name
		return entity;
	}

	void World::SetEntityName(Entity entity, std::string name)
	{
		const EntityRecord& record = RequireRenamableEntity(entity, "SetEntityName");
		ApplyName(entity, std::move(name));
		record.archetype->GetColumn(ComponentTypeId<EntityName>()).Touch(record.row, m_currentTick);
	}

	void World::ReissueDefaultName(Entity entity)
	{
		const EntityRecord& record = RequireRenamableEntity(entity, "ReissueDefaultName");
		std::string& name = NameOf(entity);
		if (!IsBlankName(name) && DefaultNameNumber(name) == 0) return;   // a custom name: keep it
		name.clear();
		NormalizeName(entity);
		record.archetype->GetColumn(ComponentTypeId<EntityName>()).Touch(record.row, m_currentTick);
	}

	void World::RecalculateEntityNameCounter()
	{
		const ComponentId id = ComponentTypeId<EntityName>();

		// Pass 1: the highest default number in use, so pass 2 cannot repeat one.
		std::uint32_t highest = 0;
		bool anyBlank = false;
		for (Archetype* archetype : m_archetypes.All())
		{
			const std::uint32_t count = archetype->EntityCount();
			for (std::uint32_t row = 0; row < count; ++row)
			{
				const std::string& name = static_cast<const EntityName*>(archetype->GetComponentPtr(id, row))->name;
				if (IsBlankName(name)) anyBlank = true;
				else highest = std::max(highest, DefaultNameNumber(name));
			}
		}
		m_nextEntityNumber = highest + 1;

		// Pass 2: name the blanks (inside a DeferredNaming, its own rescan at the end does).
		if (!anyBlank || m_deferNamingDepth > 0) return;
		for (Archetype* archetype : m_archetypes.All())
		{
			const std::uint32_t count = archetype->EntityCount();
			for (std::uint32_t row = 0; row < count; ++row)
			{
				std::string& name = static_cast<EntityName*>(archetype->GetComponentPtr(id, row))->name;
				if (!IsBlankName(name)) continue;
				name = "Entity " + std::to_string(m_nextEntityNumber);
				++m_nextEntityNumber;
				archetype->GetColumn(id).Touch(row, m_currentTick);
			}
		}
	}

	// --- naming internals -------------------------------------------------------------------

	const EntityRecord& World::RequireRenamableEntity(Entity entity, const char* operation) const
	{
		// No archetype changes, so a rename is fine mid-iteration (the editor renames from inside
		// its Hierarchy loop). But it can move the shared default counter, so a declared system -
		// which may run alongside others - must rename through its CommandBuffer.
		if (Detail::RestrictedSystem())
		{
			CheckStructuralAccess((std::string("World::") + operation).c_str());
		}
		if (!m_entities.IsAlive(entity))
		{
			throw std::invalid_argument(std::string("World::") + operation + " called with a dead/stale Entity");
		}
		return m_entities.Record(entity);
	}

	std::string& World::NameOf(Entity entity)
	{
		const EntityRecord& record = m_entities.Record(entity);
		return static_cast<EntityName*>(record.archetype->GetComponentPtr(ComponentTypeId<EntityName>(), record.row))->name;
	}

	void World::NormalizeName(Entity entity)
	{
		std::string& name = NameOf(entity);
		if (IsBlankName(name))
		{
			if (m_deferNamingDepth > 0) return;   // named when the DeferredNaming ends
			name = "Entity " + std::to_string(m_nextEntityNumber);
			++m_nextEntityNumber;                  // only once the name is in (to_string may throw)
			return;
		}
		// A name shaped like a default: no later default may repeat it.
		const std::uint32_t number = DefaultNameNumber(name);
		if (number >= m_nextEntityNumber) m_nextEntityNumber = number + 1;
	}

	void World::ApplyName(Entity entity, std::string name)
	{
		NameOf(entity) = std::move(name);
		NormalizeName(entity);
	}

	void World::EndDeferredNaming() noexcept
	{
		if (--m_deferNamingDepth > 0) return;
		try
		{
			RecalculateEntityNameCounter();
		}
		catch (const std::exception& e)
		{
			// Out of memory (a destructor cannot throw): some loaded entities may stay unnamed.
			CRU_ERROR("World: naming the loaded entities failed: {}", e.what());
		}
	}

	Entity World::PlaceNewEntity(Archetype& archetype)
	{
		const Entity entity = m_entities.Create();

		uint32_t row = 0;
		try
		{
			row = archetype.AddEntityDefault(entity);
		}
		catch (...)
		{
			// The archetype is unchanged (strong guarantee); give the slot back.
			m_entities.Release(entity);
			throw;
		}

		EntityRecord& record = m_entities.RecordAt(entity.index);
		record.archetype = &archetype;
		record.row = row;
		archetype.TouchAll(row, m_currentTick);
		return entity;
	}

	std::vector<Entity> World::CreateEntities(uint32_t count, const ComponentSignature& requested)
	{
		RequireStructuralChange("CreateEntities");
		const ComponentSignature signature = requested | kMandatoryComponents;   // same rule as CreateEntityImpl
		m_components.ValidateSignature(signature, "World::CreateEntities");

		std::vector<Entity> created;
		if (count == 0) return created;

		if (count > std::numeric_limits<uint32_t>::max() - m_entities.AliveCount())
		{
			throw std::length_error("World::CreateEntities: count " + std::to_string(count) + " exceeds the remaining entity index space");
		}

		created.reserve(count);
		Archetype* archetype = m_archetypes.FindOrCreate(signature);
		archetype->Reserve(count);
		m_entities.Reserve(count);

		// Build every entity first; hooks fire only once the whole batch exists.
		try
		{
			for (uint32_t i = 0; i < count; ++i)
			{
				created.push_back(PlaceNewEntity(*archetype));   // capacity reserved above
				NormalizeName(created.back());
			}
		}
		catch (...)
		{
			// All-or-nothing: remove this call's entities newest first (no hooks have run).
			for (auto it = created.rbegin(); it != created.rend(); ++it) DestroyEntityImpl(*it, false);
			throw;
		}

		for (Entity entity : created)
		{
			const EntityRecord& record = m_entities.Record(entity);
			FireAddHooksForRow(*record.archetype, record.row, entity);
		}
		return created;
	}

	void World::NotifyComponentsAdded(Entity entity)
	{
		if (!m_entities.IsAlive(entity)) return;
		NormalizeName(entity);   // a spawn's With<EntityName>() value, or a default if it had none
		const EntityRecord& record = m_entities.Record(entity);
		FireAddHooksForRow(*record.archetype, record.row, entity);
	}

	void World::OnEntityDestroyed(std::function<void(World&, Entity)> callback)
	{
		if (m_archetypes.IsIterating())
		{
			throw std::logic_error("World::OnEntityDestroyed: register hooks outside queries and hook dispatch");
		}
		m_onDestroyHooks.push_back(std::move(callback));
	}

	void World::ReportHookFailure(const char* hookKind, ComponentId id, Entity entity, const char* what) noexcept
	{
		++m_hookFailures;
		try
		{
			const char* component = id == kInvalidComponentId ? "-" : m_components.Get(id).name.data();
			CRU_ERROR("World: {} hook threw for entity {}:{} (component {}) - {}; the operation completed, the hook's own side effects were not undone",
				hookKind, entity.index, entity.generation, component, what);
		}
		catch (...)
		{
			// Logging must never turn a contained hook failure into a new one.
		}
	}

	void World::FireAddHooks(ComponentId id, Entity entity, void* component) noexcept
	{
		if (m_onAddHooks[id].empty()) return;
		ArchetypeRegistry::IterationGuard guard(m_archetypes);   // hooks may read, never restructure

		// Each hook is isolated: one failing hook doesn't stop the others.
		for (auto& hook : m_onAddHooks[id])
		{
			try { hook(*this, entity, component); }
			catch (const std::exception& e) { ReportHookFailure("OnComponentAdded", id, entity, e.what()); }
			catch (...) { ReportHookFailure("OnComponentAdded", id, entity, "non-std exception"); }
		}
	}

	void World::FireRemoveHooks(ComponentId id, Entity entity, void* component) noexcept
	{
		if (m_onRemoveHooks[id].empty()) return;
		ArchetypeRegistry::IterationGuard guard(m_archetypes);

		for (auto& hook : m_onRemoveHooks[id])
		{
			try { hook(*this, entity, component); }
			catch (const std::exception& e) { ReportHookFailure("OnComponentRemoved", id, entity, e.what()); }
			catch (...) { ReportHookFailure("OnComponentRemoved", id, entity, "non-std exception"); }
		}
	}

	void World::FireDestroyHooks(Entity entity) noexcept
	{
		if (m_onDestroyHooks.empty()) return;
		ArchetypeRegistry::IterationGuard guard(m_archetypes);

		for (auto& hook : m_onDestroyHooks)
		{
			try { hook(*this, entity); }
			catch (const std::exception& e) { ReportHookFailure("OnEntityDestroyed", kInvalidComponentId, entity, e.what()); }
			catch (...) { ReportHookFailure("OnEntityDestroyed", kInvalidComponentId, entity, "non-std exception"); }
		}
	}

	void World::FireAddHooksForRow(Archetype& archetype, uint32_t row, Entity entity) noexcept
	{
		for (ComponentId id : archetype.ComponentIds())
		{
			if (!m_onAddHooks[id].empty()) FireAddHooks(id, entity, archetype.GetComponentPtr(id, row));
		}
	}

	void World::FireRemoveHooksForRow(Archetype& archetype, uint32_t row, Entity entity) noexcept
	{
		for (ComponentId id : archetype.ComponentIds())
		{
			if (!m_onRemoveHooks[id].empty()) FireRemoveHooks(id, entity, archetype.GetComponentPtr(id, row));
		}
	}


	void World::DestroyEntity(Entity entity)
	{
		DestroyEntityImpl(entity, true);
	}

	void World::DestroyEntityImpl(Entity entity, bool fireHooks)
	{
		RequireStructuralChange("DestroyEntity");

		if (!m_entities.IsAlive(entity)) return;

		// Unity semantics on EVERY destroy path: children go with their parent, so no
		// entity is ever left pointing at a dead parent.
		DestroyDescendants(entity, fireHooks);

		EntityRecord& record = m_entities.Record(entity);

		if (fireHooks)
		{
			FireDestroyHooks(entity);

			// Every component the entity still owns counts as removed, so
			// resource-owning OnComponentRemoved hooks fire here too - not only
			// on RemoveComponent().
			FireRemoveHooksForRow(*record.archetype, record.row, entity);
		}

		// Nothing below can throw: swap-removal and slot release are noexcept.
		const Entity relocated = record.archetype->RemoveEntity(record.row);
		if (relocated.IsValid())
			m_entities.RecordAt(relocated.index).row = record.row;
		m_entities.Destroy(entity);
	}

	void World::DestroyAllEntities()
	{
		RequireStructuralChange("DestroyAllEntities");

		std::vector<Entity> alive;
		alive.reserve(m_entities.AliveCount());
		for (Archetype* archetype : m_archetypes.All())
		{
			alive.insert(alive.end(), archetype->Entities().begin(), archetype->Entities().end());
		}

		for (Entity entity : alive) DestroyEntity(entity);
		m_nextEntityNumber = 1;   // nothing left to collide with
	}


	ComponentSignature World::GetSignature(Entity entity) const
	{
		if (!m_entities.IsAlive(entity)) return ComponentSignature{};

		return m_entities.Record(entity).archetype->Signature();
	}


	void World::MoveEntity(EntityRecord& record, Entity entity, Archetype& destination, ComponentId addedId, void* addedValue)
	{
		Archetype& source = *record.archetype;
		assert(&source != &destination);

		// Mandatory base components are never removed (checked before anything moves).
		if ((destination.Signature() & kMandatoryComponents) != kMandatoryComponents)
		{
			throw std::logic_error("World: Transform, WorldTransform and HierarchyNode are mandatory and can't be removed - destroy the entity instead");
		}

		// 1. Allocate everything up front: afterwards only constructors can throw.
		destination.ReserveForAppend();

		// 2. Build the destination-only columns (no source value, not the added one) - undone on failure.
		std::array<ComponentId, kMaxComponents> built{};
		std::size_t builtCount = 0;
		try
		{
			for (ComponentId id : destination.ComponentIds())
			{
				if (source.Contains(id) || (id == addedId && addedValue != nullptr)) continue;
				destination.GetColumn(id).PushBackDefault();
				built[builtCount++] = id;
			}
		}
		catch (...)
		{
			while (builtCount > 0) destination.GetColumn(built[--builtCount]).PopBack();
			throw;
		}

		// 3. Non-throwing relocation of shared components and the added value.
		for (ComponentId id : destination.ComponentIds())
		{
			ComponentColumn& column = destination.GetColumn(id);
			if (source.Contains(id))
			{
				source.GetColumn(id).MoveElementTo(record.row, column);
			}
			else if (id == addedId && addedValue != nullptr)
			{
				column.PushBackMoved(addedValue);   // one relocation instead of default-construct + move-assign
			}
		}

		const uint32_t destinationRow = destination.AppendEntityReserved(entity);

		// 4. Drop the old row (noexcept) and fix up whichever entity was swapped into it.
		const Entity relocated = source.RemoveEntity(record.row);
		if (relocated.IsValid())
		{
			m_entities.RecordAt(relocated.index).row = record.row;
		}

		record.archetype = &destination;
		record.row = destinationRow;
	}

	// =====================================================================
	// Hierarchy - Unity Transform semantics (SetParent, DetachChildren,
	// sibling order, root/IsChildOf, keep-world reparenting, destroy-with-
	// children, and the hook that keeps links valid). World is the only
	// writer of HierarchyNode links: everything here writes through
	// HierarchyLinks(). Every live entity has a Transform, WorldTransform
	// and HierarchyNode, so a missing one means the invariant is broken.
	// =====================================================================

	namespace
	{
		// Longer than this means the links are corrupted - fail loudly instead of looping.
		constexpr std::size_t kMaxHierarchySize = 1u << 20;

		// Below this a scale axis counts as zero (the matrix is treated as not invertible).
		constexpr float kMinScale = 1e-6f;
		// Relative tolerance for an axis collapsing into the plane of the previous ones.
		constexpr float kPlanarTolerance = 1e-4f;

		const Math::vec3 kUnitScale{ 1.f, 1.f, 1.f };

		// Local T*R*S; t is only null for a dead entity (a live one always has a Transform).
		Math::mat4 LocalMatrix(const Transform* t)
		{
			if (!t) return Math::mat4(1.0f);
			return Detail::ComposeTRS(t->positions, t->rotation, t->scale);
		}

		// The entity's frame: parent world x local T*R, without its own scale.
		Math::mat4 FrameMatrix(const Math::mat4& parentWorld, const Transform& local)
		{
			return parentWorld * Detail::ComposeTRS(local.positions, local.rotation, kUnitScale);
		}

		bool AllFinite(const Math::vec3& v) noexcept
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		enum class Basis { Proper, Mirrored, Degenerate };

		// Classifies an affine matrix's 3x3 basis (bottom row ignored). Degenerate: a
		// non-finite value, an (almost) zero axis, or axes (almost) in one plane - not
		// invertible. axisLengths gets each column's length.
		Basis ClassifyBasis(const Math::mat4& m, Math::vec3& axisLengths) noexcept
		{
			const Math::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
			if (!(AllFinite(c0) && AllFinite(c1) && AllFinite(c2) && AllFinite(Math::vec3(m[3])))) return Basis::Degenerate;

			axisLengths = Math::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
			if (!(axisLengths.x > kMinScale && axisLengths.y > kMinScale && axisLengths.z > kMinScale)) return Basis::Degenerate;

			// Volume of the unit-length axes: +-1 when orthogonal, 0 when flat, < 0 when
			// mirrored. Scale-independent (and can't overflow like det / (l0 * l1 * l2)).
			const float volume = glm::dot(glm::cross(c0 / axisLengths.x, c1 / axisLengths.y), c2 / axisLengths.z);
			if (!(std::fabs(volume) > kPlanarTolerance)) return Basis::Degenerate;
			return volume > 0.f ? Basis::Proper : Basis::Mirrored;
		}

		// glm::affineInverse of m when m is invertible (mirrored is fine: the handedness
		// check belongs to the decomposition of the solved matrix).
		bool TryAffineInverse(const Math::mat4& m, Math::mat4& inverse) noexcept
		{
			Math::vec3 lengths;
			if (ClassifyBasis(m, lengths) == Basis::Degenerate) return false;
			inverse = glm::affineInverse(m);
			return true;
		}
	}

	bool Detail::DecomposeTRS(const Math::mat4& m, Math::vec3& translation, Math::quat& rotation,
		Math::vec3& scale) noexcept
	{
		// No negative scales: a mirrored matrix is refused, like a singular one.
		Math::vec3 lengths;
		if (ClassifyBasis(m, lengths) != Basis::Proper) return false;

		// glm::decompose's singularity test is an absolute |det| < FLT_EPSILON, which
		// would refuse small but valid scales. Give it unit-length axes (|det| is then the
		// volume checked above) and a clean affine bottom row, and scale back afterwards:
		// dividing an axis by its length changes neither its direction nor, after
		// Gram-Schmidt, anything but that axis' scale.
		const Math::mat4 unit(Math::vec4(Math::vec3(m[0]) / lengths.x, 0.f), Math::vec4(Math::vec3(m[1]) / lengths.y, 0.f),
			Math::vec4(Math::vec3(m[2]) / lengths.z, 0.f), Math::vec4(Math::vec3(m[3]), 1.f));

		Math::vec3 t, s, skew;
		Math::vec4 perspective;
		Math::quat q;
		if (!glm::decompose(unit, s, q, t, skew, perspective)) return false;
		s *= lengths;
		if (!(AllFinite(s) && std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z)))
			return false;

		if (q.w < 0.f) q = -q;   // canonical of q / -q (same rotation)
		translation = t;
		rotation = SafeNormalize(q);
		scale = s;
		return true;
	}

	const char* ToString(ParentResult result) noexcept
	{
		switch (result)
		{
		case ParentResult::Ok:         return "Ok";
		case ParentResult::DeadChild:  return "child is dead or null";
		case ParentResult::DeadParent: return "parent is dead";
		case ParentResult::SelfParent: return "an entity can't be its own parent";
		case ParentResult::WouldCycle: return "parent is a descendant of child (would create a cycle)";
		case ParentResult::UnsupportedTransform:
			return "can't keep the world transform: a parent's world transform has a zero or non-finite scale, or the parents differ in handedness (would need a negative scale; use worldPositionStays = false)";
		default:                       return "unknown";
		}
	}

	// ---------------------------------------------------------------- internals

	HierarchyNode& World::HierarchyLinks(Entity entity)
	{
		constexpr ComponentId id = ComponentTypeId<HierarchyNode>();
		if (!m_entities.IsAlive(entity)) throw std::logic_error("World: hierarchy link to a dead entity - links are corrupted");
		const EntityRecord& record = m_entities.Record(entity);
		if (!record.archetype->Contains(id)) throw std::logic_error("World: entity has no HierarchyNode - the mandatory-component invariant is broken");

		// No change stamp: editing a parent's or sibling's links moves nothing. Only the
		// entity whose parent changes is stamped (StampReparented).
		return *static_cast<HierarchyNode*>(record.archetype->GetComponentPtr(id, record.row));
	}

	void World::StampReparented(Entity entity)
	{
		// Its world transform changed, so HierarchyTransformSystem recomputes its subtree.
		constexpr ComponentId id = ComponentTypeId<HierarchyNode>();
		const EntityRecord& record = m_entities.Record(entity);
		record.archetype->GetColumn(id).Touch(record.row, m_currentTick);
	}

	const HierarchyNode* World::NodeOf(Entity entity) const
	{
		if (!m_entities.IsAlive(entity)) return nullptr;
		const EntityRecord& record = m_entities.Record(entity);
		// Every live entity has one (mandatory base component): no Contains() check.
		return static_cast<const HierarchyNode*>(
			static_cast<const Archetype*>(record.archetype)->GetColumn(ComponentTypeId<HierarchyNode>()).At(record.row));
	}

	const Transform* World::LocalOf(Entity entity) const
	{
		if (!m_entities.IsAlive(entity)) return nullptr;
		const EntityRecord& record = m_entities.Record(entity);
		return static_cast<const Transform*>(
			static_cast<const Archetype*>(record.archetype)->GetColumn(ComponentTypeId<Transform>()).At(record.row));
	}

	void World::CheckHierarchyRead(const char* operation) const
	{
		CheckDeclaredAccess(MakeSignature<HierarchyNode>(), ComponentSignature{}, operation);
	}

	Entity World::ChildAt(const HierarchyNode& parent, std::uint32_t index) const
	{
		if (index >= parent.childCount) return kNullEntity;

		// Walk from whichever end is closer.
		const std::uint32_t fromBack = parent.childCount - 1u - index;
		Entity c;
		if (index <= fromBack)
		{
			c = parent.firstChild;
			for (std::uint32_t i = 0; i < index; ++i) c = NodeOf(c)->nextSibling;
		}
		else
		{
			c = parent.lastChild;
			for (std::uint32_t i = 0; i < fromBack; ++i) c = NodeOf(c)->prevSibling;
		}
		return c;
	}

	std::uint32_t World::SiblingIndexOf(const HierarchyNode& node) const
	{
		if (!node.parent.IsValid()) return 0;   // roots have no sibling list
		const std::uint32_t count = NodeOf(node.parent)->childCount;

		// Step backwards and forwards together; whichever list ends first gives the index.
		Entity back = node.prevSibling, front = node.nextSibling;
		for (std::uint32_t k = 0; k < count; ++k)
		{
			if (!back.IsValid()) return k;                // k siblings before it
			if (!front.IsValid()) return count - 1u - k;  // k siblings after it
			back = NodeOf(back)->prevSibling;
			front = NodeOf(front)->nextSibling;
		}
		throw std::logic_error("World: sibling list longer than childCount - hierarchy links are corrupted");
	}

	bool World::ReachesUp(Entity from, Entity target, Math::mat4* worldOut) const
	{
		// world = L(root) * ... * L(from), built upward as world = L(ancestor) * world.
		Math::mat4 world = Math::mat4(1.0f);
		std::size_t depth = 0;
		for (Entity e = from; e.IsValid(); )
		{
			if (e == target) return true;
			const HierarchyNode* node = NodeOf(e);
			if (!node) break;   // dead: contributes nothing
			if (worldOut) world = LocalMatrix(LocalOf(e)) * world;
			if (++depth > kMaxHierarchySize) throw std::logic_error("World: parent chain too long - hierarchy links are corrupted");
			e = node->parent;
		}
		if (worldOut) *worldOut = world;
		return false;
	}

	void World::DetachFromParent(Entity child)
	{
		HierarchyNode& c = HierarchyLinks(child);
		const Entity parent = c.parent;
		if (!parent.IsValid()) return;

		UnlinkFromSiblings(c, HierarchyLinks(parent));
		c.parent = kNullEntity;
		c.prevSibling = kNullEntity;
		c.nextSibling = kNullEntity;
	}

	void World::UnlinkFromSiblings(const HierarchyNode& child, HierarchyNode& parent)
	{
		if (child.prevSibling.IsValid()) HierarchyLinks(child.prevSibling).nextSibling = child.nextSibling;
		else                             parent.firstChild = child.nextSibling;
		if (child.nextSibling.IsValid()) HierarchyLinks(child.nextSibling).prevSibling = child.prevSibling;
		else                             parent.lastChild = child.prevSibling;
		--parent.childCount;
	}

	void World::InsertChildAt(Entity child, Entity parent, std::uint32_t index)
	{
		HierarchyNode& c = HierarchyLinks(child);
		HierarchyNode& p = HierarchyLinks(parent);

		// The sibling that will come after child (nearest-end walk); null means append.
		const Entity after = ChildAt(p, index);
		const Entity before = after.IsValid() ? NodeOf(after)->prevSibling : p.lastChild;

		c.parent = parent;
		c.prevSibling = before;
		c.nextSibling = after;

		if (before.IsValid()) HierarchyLinks(before).nextSibling = child;
		else                  p.firstChild = child;
		if (after.IsValid())  HierarchyLinks(after).prevSibling = child;
		else                  p.lastChild = child;
		++p.childCount;
	}

	void World::DestroyDescendants(Entity root, bool fireHooks)
	{
		const HierarchyNode* rootNode = NodeOf(root);
		if (!rootNode || rootNode->childCount == 0) return;

		// Post-order with no stack or buffer: go down first children to a leaf, destroy it,
		// carry on from its parent. Each edge is walked down once and up once: O(subtree).
		for (Entity current = root;;)
		{
			const HierarchyNode* node = NodeOf(current);
			if (!node) throw std::logic_error("World: dead entity linked in the hierarchy - links are corrupted");
			if (node->firstChild.IsValid()) { current = node->firstChild; continue; }
			if (current == root) break;   // root has no children left: the caller destroys it

			const Entity parent = node->parent;
			// Without hooks the HierarchyNode hook doesn't unlink the leaf, so do it here.
			if (!fireHooks) DetachFromParent(current);
			DestroyEntityImpl(current, fireHooks);
			if (NodeOf(parent)->firstChild == current)   // still linked: would loop forever
				throw std::logic_error("World: destroyed child still linked to its parent - hierarchy links are corrupted");
			current = parent;
		}
	}

	void World::InstallHierarchyHook()
	{
		// Fires BEFORE the HierarchyNode is torn down. It is mandatory, so that only
		// happens in DestroyEntity. Hooks may only write fields (no structural
		// changes), which is all this does.
		OnComponentRemoved<HierarchyNode>([](World& w, Entity, HierarchyNode& h)
			{
				// 1. Unlink self from its parent's child list (no stamps: nothing else moves).
				if (h.parent.IsValid() && w.IsAlive(h.parent))
					w.UnlinkFromSiblings(h, w.HierarchyLinks(h.parent));

				// 2. No children can be left: DestroyEntityImpl destroys every descendant
				//    first, so nothing is ever orphaned. Reaching here with children means
				//    the links are corrupted - report it (logged and counted as a hook failure).
				const bool hadChildren = h.firstChild.IsValid();
				h = HierarchyNode{};
				if (hadChildren)
					throw std::logic_error("World: entity destroyed with live children - hierarchy links are corrupted");
			});
	}

	// ------------------------------------------------------------------ queries
	// Each public query checks declared access once, then walks with NodeOf().

	bool World::IsAncestorOf(Entity ancestor, Entity entity) const
	{
		CheckHierarchyRead("reads through World::IsAncestorOf and");
		const HierarchyNode* h = NodeOf(entity);
		if (!ancestor.IsValid() || !h) return false;
		return ReachesUp(h->parent, ancestor, nullptr);   // strict: starts at the parent
	}

	bool World::IsChildOf(Entity entity, Entity ancestor) const
	{
		CheckHierarchyRead("reads through World::IsChildOf and");
		const HierarchyNode* h = NodeOf(entity);
		if (!h) return false;
		return entity == ancestor || (ancestor.IsValid() && ReachesUp(h->parent, ancestor, nullptr));
	}

	Entity World::GetParent(Entity entity) const
	{
		CheckHierarchyRead("reads through World::GetParent and");
		const HierarchyNode* h = NodeOf(entity);
		return h ? h->parent : kNullEntity;   // dead entity: no parent
	}

	std::uint32_t World::ChildCount(Entity entity) const
	{
		CheckHierarchyRead("reads through World::ChildCount and");
		const HierarchyNode* h = NodeOf(entity);
		return h ? h->childCount : 0u;   // dead entity: no children
	}

	Entity World::GetChild(Entity parent, std::uint32_t index) const
	{
		CheckHierarchyRead("reads through World::GetChild and");
		const HierarchyNode* h = NodeOf(parent);
		return h ? ChildAt(*h, index) : kNullEntity;
	}

	Entity World::GetRoot(Entity entity) const
	{
		CheckHierarchyRead("reads through World::GetRoot and");
		const HierarchyNode* h = NodeOf(entity);
		if (!h) return kNullEntity;
		Entity current = entity;
		for (std::size_t depth = 0; h->parent.IsValid(); ++depth)
		{
			if (depth == kMaxHierarchySize) throw std::logic_error("World: parent chain too long - hierarchy links are corrupted");
			current = h->parent;
			h = NodeOf(current);
		}
		return current;
	}

	std::uint32_t World::GetSiblingIndex(Entity entity) const
	{
		CheckHierarchyRead("reads through World::GetSiblingIndex and");
		const HierarchyNode* h = NodeOf(entity);
		return h ? SiblingIndexOf(*h) : 0u;   // dead entity: 0
	}

	void World::CollectSubtree(Entity root, std::vector<Entity>& out) const
	{
		CheckHierarchyRead("reads through World::CollectSubtree and");
		const HierarchyNode* rootNode = NodeOf(root);
		if (!rootNode) return;
		const std::size_t start = out.size();
		out.reserve(start + 1u + rootNode->childCount);   // at least the root and its children
		out.push_back(root);
		// out doubles as the work list: each entity appends its children after itself.
		for (std::size_t i = start; i < out.size(); ++i)
		{
			if (out.size() - start > kMaxHierarchySize) throw std::logic_error("World: subtree too large - hierarchy links are corrupted");
			for (Entity c = NodeOf(out[i])->firstChild; c.IsValid(); c = NodeOf(c)->nextSibling)
				out.push_back(c);
		}
	}

	// ------------------------------------------------------------ world matrix

	Math::mat4 World::ComputeWorldMatrix(Entity entity) const
	{
		// One pass upward, no buffer: L(root) * ... * L(entity).
		Math::mat4 world;
		ReachesUp(entity, kNullEntity, &world);   // kNullEntity is never reached: full chain
		return world;
	}

	// ---------------------------------------------------------------- mutations

	ParentResult World::SolveKeepWorld(const Transform& local, const Math::mat4& oldParentWorld,
		const Math::mat4* newParentInverse, Transform& out) const
	{
		// Solve for the child's frame WITHOUT its own scale, then put the local scale back:
		// a zero local scale is carried over exactly instead of being recovered from a
		// (singular) matrix.
		const Math::mat4 world = FrameMatrix(oldParentWorld, local);
		const Math::mat4 frame = newParentInverse ? *newParentInverse * world : world;
		Math::vec3 frameScale;
		if (!Detail::DecomposeTRS(frame, out.positions, out.rotation, frameScale))
			return ParentResult::UnsupportedTransform;   // zero-scale chain, or the parents differ in handedness

		out.scale = frameScale * local.scale;
		return ParentResult::Ok;
	}

	ParentResult World::TrySetParent(Entity child, Entity parent, bool worldPositionStays)
	{
		// No archetype change, but it edits links on up to five entities that other
		// systems may be reading: main thread, outside iteration.
		RequireStructuralChange("SetParent");

		if (!IsAlive(child)) return ParentResult::DeadChild;
		if (parent.IsValid() && !IsAlive(parent)) return ParentResult::DeadParent;
		if (parent == child) return ParentResult::SelfParent;
		const Entity oldParent = NodeOf(child)->parent;
		if (oldParent == parent) return ParentResult::Ok;   // nothing to do (and no cycle possible)

		// One walk up from the new parent both detects a cycle and, for keep-world,
		// accumulates the new parent's world matrix.
		Math::mat4 newParentWorld = Math::mat4(1.0f);
		if (parent.IsValid() && ReachesUp(parent, child, worldPositionStays ? &newParentWorld : nullptr))
			return ParentResult::WouldCycle;

		// Everything that can throw or fail (reading and solving transforms) happens before
		// any link or Transform changes. Both entities already have a HierarchyNode and a
		// Transform (mandatory), so nothing is added and no archetype changes.
		Transform solved{};
		if (worldPositionStays)
		{
			// The new parent must be invertible, or no local Transform reaches the old world
			// spot. A root needs no inverse (identity).
			Math::mat4 newParentInverse;
			if (parent.IsValid() && !TryAffineInverse(newParentWorld, newParentInverse)) return ParentResult::UnsupportedTransform;
			const ParentResult solve = SolveKeepWorld(*LocalOf(child), ComputeWorldMatrix(oldParent),
				parent.IsValid() ? &newParentInverse : nullptr, solved);
			if (solve != ParentResult::Ok) return solve;
		}

		// Field writes only from here on: cannot fail.
		DetachFromParent(child);
		if (parent.IsValid()) InsertChildAt(child, parent, ~0u);   // append: Unity puts it last
		if (worldPositionStays) *Write<Transform>(child) = solved;
		StampReparented(child);
		return ParentResult::Ok;
	}

	void World::SetParent(Entity child, Entity parent, bool worldPositionStays)
	{
		const ParentResult result = TrySetParent(child, parent, worldPositionStays);
		if (result != ParentResult::Ok)
			throw std::invalid_argument(std::string("World::SetParent: ") + ToString(result));
	}

	void World::DetachChildren(Entity parent)
	{
		RequireStructuralChange("DetachChildren");
		const HierarchyNode* node = NodeOf(parent);
		if (!node || node->childCount == 0) return;

		std::vector<Entity> children;
		children.reserve(node->childCount);
		for (Entity c = node->firstChild; c.IsValid(); c = NodeOf(c)->nextSibling)
			children.push_back(c);   // copy first: detaching edits the list

		// All-or-nothing: solve every child's root Transform before detaching any. They
		// share the old parent, so its world matrix is computed once; the new parent is
		// the root (nothing to invert).
		const Math::mat4 parentWorld = ComputeWorldMatrix(parent);
		std::vector<Transform> solved(children.size());
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			if (SolveKeepWorld(*LocalOf(children[i]), parentWorld, nullptr, solved[i]) != ParentResult::Ok)
				throw std::invalid_argument(std::string("World::DetachChildren: ") + ToString(ParentResult::UnsupportedTransform));
		}

		for (std::size_t i = 0; i < children.size(); ++i)
		{
			DetachFromParent(children[i]);
			*Write<Transform>(children[i]) = solved[i];
			StampReparented(children[i]);
		}
	}

	void World::SetSiblingIndex(Entity entity, std::uint32_t index)
	{
		RequireStructuralChange("SetSiblingIndex");   // edits links on several entities, like SetParent
		const HierarchyNode* node = NodeOf(entity);
		if (!node || !node->parent.IsValid()) return;   // dead or root: no sibling list

		const Entity parent = node->parent;
		const std::uint32_t count = NodeOf(parent)->childCount;
		const std::uint32_t target = index < count ? index : count - 1u;   // clamped, like InsertChildAt
		if (target == SiblingIndexOf(*node)) return;                      // already there: touch nothing

		DetachFromParent(entity);
		InsertChildAt(entity, parent, index);   // index past the end appends (clamp)
		// No stamp: the parent didn't change, so no world transform did.
	}

	void World::SetAsFirstSibling(Entity entity) { SetSiblingIndex(entity, 0); }
	void World::SetAsLastSibling(Entity entity)  { SetSiblingIndex(entity, ~0u); }
}
