/*************************************************************************
* \file:         EnableBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          13 November 2026
* \version       1.1
* \brief         Internal calls behind switching components and whole
*                entities on and off from C#: Entity.SetEnabled<T> /
*                IsEnabled<T>, EntityScript.Enabled (one script of several:
*                its ScriptEntry::enabled), Entity.SetActive / IsActive /
*                IsActiveSelf.
*
* \details
*    Bindings only, on World's public per-component on/off API
*    (TrySetComponentEnabled / IsComponentEnabled by ComponentId). A
*    switched-off component keeps its data; queries skip it, so rendering,
*    physics, audio and scripts stop running it on their own. Mandatory
*    components (Transform, ...) can't be switched off.
*
*    The engine has no entity-level "active". SetActive(false) emulates
*    Unity's: it switches off every component on the entity and all its
*    descendants that is currently on, and remembers exactly which ones it
*    switched off (a record per entity it was called on). SetActive(true)
*    switches only those back on, so a component you had turned off
*    yourself stays off, and a child deactivated on its own stays inactive.
*    The records are Play-only: Stop / a scene change forget them (the
*    scene snapshot restores the real on/off state anyway).
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Audio/AudioComponents.h"
#include "Physics/PhysicsComponents.h"
#include "Player/PlayerComponents.h"
#include "Navigation/NavigationComponents.h"   // FlowFieldAgent, FlowFieldSurface
#include "AI/AIComponents.h"                   // StateGraphAgent
#if CRUCIBLE_WITH_MONO
#include <algorithm>
#include <map>
#include <utility>
#include <vector>
#include <mono/metadata/loader.h>
#include <mono/metadata/object.h>   // mono_gchandle_get_target (EntityScript.Enabled)

namespace Crucible::ScriptBindings
{
	namespace
	{
		// Component kinds as C# numbers them (ComponentKind in Entity.cs; same as
		// EntityBindings.cpp). -> the ECS ComponentId, or kInvalid for kinds that
		// can't be switched off (Transform).
		constexpr ComponentId kInvalid = static_cast<ComponentId>(-1);

		ComponentId KindToId(int32_t kind)
		{
			switch (kind)
			{
			case 1: return ComponentTypeId<RigidBody>();
			case 2: return ComponentTypeId<AnimatedModel>();
			case 3: return ComponentTypeId<ScriptComponent>();
			case 4: return ComponentTypeId<AudioSource>();
			case 5: return ComponentTypeId<CharacterController>();
			case 6: return ComponentTypeId<CameraComponent>();
			case 7: return ComponentTypeId<CameraOrbit>();
			case 8: return ComponentTypeId<Light>();
			case 9: return ComponentTypeId<ParticleEmitter>();
			case 10: return ComponentTypeId<UIText>();
			case 11: return ComponentTypeId<UIImage>();
			case 12: return ComponentTypeId<UISlider>();
			case 13: return ComponentTypeId<UIButton>();
			case 14: return ComponentTypeId<UIRect>();
			case 15: return ComponentTypeId<UICanvas>();
			case 16: return ComponentTypeId<Collider>();
			case 17: return ComponentTypeId<MeshRenderer>();
			case 18: return ComponentTypeId<PlayerController>();
			case 19: return ComponentTypeId<WorldText>();
			case 20: return ComponentTypeId<MaterialParams>();
			case 21: return ComponentTypeId<FlowFieldAgent>();     // Hugo, 7 Nov 2026: the AI components
			case 22: return ComponentTypeId<FlowFieldSurface>();
			case 23: return ComponentTypeId<StateGraphAgent>();
			case 24: return ComponentTypeId<Terrain>();             // Jean David, 10 Nov 2026: generated ground
			default: return kInvalid; // 0 = Transform: mandatory
			}
		}

		// SetActive(false) records: entity it was called on -> the components IT
		// switched off (on that entity and its descendants). Main thread only.
		std::map<uint64_t, std::vector<std::pair<Entity, ComponentId>>> inactive;

		uint64_t Key(Entity entity)
		{
			return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
		}

		/*********************************************************************
		 \brief  The entity and all its descendants, parents first.
		 *********************************************************************/
		std::vector<Entity> Subtree(const World& world, Entity root)
		{
			std::vector<Entity> subtree{root};
			for (std::size_t i = 0; i < subtree.size(); ++i)
			{
				const HierarchyNode* node = world.Read<HierarchyNode>(subtree[i]);
				for (Entity child = node ? node->firstChild : Entity{}; child.IsValid();)
				{
					subtree.push_back(child);
					const HierarchyNode* childNode = world.Read<HierarchyNode>(child);
					child = childNode ? childNode->nextSibling : Entity{};
				}
			}
			return subtree;
		}

		// ======================================================================
		// One component
		// ======================================================================

		/*********************************************************************
		 \brief  C# Entity.SetEnabled<T>(on). 0 if the entity has no such
		         component, it can't be switched off, or it is not Play.
		 *********************************************************************/
		int32_t ManagedSetComponentEnabled(int32_t index, int32_t generation, int32_t kind, int32_t enabled) noexcept
		{
			try
			{
				Entity entity;
				const ComponentId id = KindToId(kind);
				if (id == kInvalid || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				return EntityContext::GetWorld()->TrySetComponentEnabled(entity, id, enabled != 0) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Entity.IsEnabled<T>(): the component's own on/off choice.
		         Transform (always on) reads 1 if present.
		 *********************************************************************/
		int32_t ManagedIsComponentEnabled(int32_t index, int32_t generation, int32_t kind, int32_t* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const ComponentId id = kind == 0 ? ComponentTypeId<Transform>() : KindToId(kind);
				if (id == kInvalid)
					return 0;
				*out = EntityContext::GetWorld()->IsComponentEnabled(entity, id) ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// One script (EntityScript.Enabled)
		// ======================================================================

		/*********************************************************************
		 \brief  Which entry of the entity's ScriptComponent::scripts the C#
		         object `script` is (EntityScriptSystem's records line up with
		         that list). False if it is not one of the entity's running
		         scripts.
		 *********************************************************************/
		bool FindScriptSlot(Entity entity, MonoObject* script, uint32_t& slot)
		{
			if (!script)
				return false;
			std::vector<EntityContext::RunningScript> running;
			EntityContext::FindScripts(entity, running);
			for (const EntityContext::RunningScript& candidate : running)
			{
				if (mono_gchandle_get_target(candidate.handle) == script)
				{
					slot = candidate.slot;
					return true;
				}
			}
			return false;
		}

		/*********************************************************************
		 \brief  C# EntityScript.Enabled (get): that one script's own on/off
		         choice (ScriptEntry::enabled). Switching the whole
		         ScriptComponent off (SetActive, SetEnabled<EntityScript>)
		         pauses it too but does not change this.
		 *********************************************************************/
		int32_t ManagedScriptGetEnabled(MonoObject* script, int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				Entity entity;
				uint32_t slot = 0;
				if (!out || !EntityContext::ResolveEntity(index, generation, entity) || !FindScriptSlot(entity, script, slot))
					return 0;
				const ScriptComponent* component = EntityContext::GetWorld()->Read<ScriptComponent>(entity);
				if (!component || slot >= component->scripts.size())
					return 0;
				*out = component->scripts[slot].enabled ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# EntityScript.Enabled (set): switches only that script
		         (ScriptEntry::enabled). EntityScriptSystem pauses or resumes
		         its instance at its next Update, keeping its state; the
		         entity's other scripts carry on.
		 *********************************************************************/
		int32_t ManagedScriptSetEnabled(MonoObject* script, int32_t index, int32_t generation, int32_t enabled) noexcept
		{
			try
			{
				Entity entity;
				uint32_t slot = 0;
				if (!EntityContext::ResolveEntity(index, generation, entity) || !FindScriptSlot(entity, script, slot))
					return 0;
				World& world = *EntityContext::GetWorld();
				const ScriptComponent* current = world.Read<ScriptComponent>(entity);
				if (!current || slot >= current->scripts.size())
					return 0;
				if (current->scripts[slot].enabled == (enabled != 0))
					return 1; // already so: no write, so the component isn't marked changed
				world.Write<ScriptComponent>(entity)->scripts[slot].enabled = enabled != 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Whole entity (Unity's SetActive)
		// ======================================================================

		/*********************************************************************
		 \brief  C# Entity.SetActive(active). See the file comment.
		 \return 1 if applied (or already in that state), 0 if the entity is
		         gone or it is not Play.
		 *********************************************************************/
		int32_t ManagedSetActive(int32_t index, int32_t generation, int32_t active) noexcept
		{
			try
			{
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				World& world = *EntityContext::GetWorld();
				const auto found = inactive.find(Key(entity));

				if (active == 0)
				{
					if (found != inactive.end())
						return 1; // already inactive
					std::vector<std::pair<Entity, ComponentId>> switchedOff;
					for (const Entity member : Subtree(world, entity))
					{
						const ComponentSignature signature = world.GetSignature(member);
						for (std::size_t bit = 0; bit < signature.size(); ++bit)
						{
							const auto id = static_cast<ComponentId>(bit);
							// Only components that are on now - TrySet refuses mandatory ones.
							if (signature.test(bit) && world.IsComponentEnabled(member, id) &&
							    world.TrySetComponentEnabled(member, id, false))
								switchedOff.emplace_back(member, id);
						}
					}
					inactive.emplace(Key(entity), std::move(switchedOff));
					return 1;
				}

				if (found == inactive.end())
					return 1; // already active
				for (const auto& [member, id] : found->second)
					if (world.IsAlive(member))
						world.TrySetComponentEnabled(member, id, true); // gone since? TrySet just says no
				inactive.erase(found);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Entity.IsActiveSelf (self = 1) / IsActive (self = 0: the
		         entity AND every ancestor are active - Unity's activeInHierarchy).
		 *********************************************************************/
		int32_t ManagedIsActive(int32_t index, int32_t generation, int32_t self, int32_t* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const World& world = *EntityContext::GetWorld();
				*out = 1;
				for (Entity e = entity; e.IsValid(); e = self ? Entity{} : world.GetParent(e))
					if (inactive.count(Key(e)))
					{
						*out = 0;
						break;
					}
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}
	}

	namespace EntityContext
	{
		void ClearActiveStates()
		{
			inactive.clear();
		}

		void PruneActiveStates(const World& liveWorld)
		{
			std::erase_if(inactive, [&liveWorld](const auto& entry) {
				const Entity entity{ static_cast<uint32_t>(entry.first & 0xFFFFFFFFu),
				                     static_cast<uint32_t>(entry.first >> 32) };
				return !liveWorld.IsAlive(entity);
			});
		}
	}

	void RegisterEnableBindings()
	{
		mono_add_internal_call("Crucible.Entity::SetComponentEnabledInternal",
		                       reinterpret_cast<const void*>(ManagedSetComponentEnabled));
		mono_add_internal_call("Crucible.Entity::IsComponentEnabledInternal",
		                       reinterpret_cast<const void*>(ManagedIsComponentEnabled));
		mono_add_internal_call("Crucible.EntityScript::GetEnabledInternal", reinterpret_cast<const void*>(ManagedScriptGetEnabled));
		mono_add_internal_call("Crucible.EntityScript::SetEnabledInternal", reinterpret_cast<const void*>(ManagedScriptSetEnabled));
		mono_add_internal_call("Crucible.Entity::SetActiveInternal", reinterpret_cast<const void*>(ManagedSetActive));
		mono_add_internal_call("Crucible.Entity::IsActiveInternal", reinterpret_cast<const void*>(ManagedIsActive));
	}
}
#endif
