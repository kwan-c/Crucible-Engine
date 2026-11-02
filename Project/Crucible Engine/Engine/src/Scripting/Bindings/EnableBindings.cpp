/*************************************************************************
* \file:         EnableBindings.cpp
* \author:       <author>
* \par:          Email: <email>
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind switching components and whole
*                entities on and off from C#: Entity.SetEnabled<T> /
*                IsEnabled<T>, EntityScript.Enabled, Entity.SetActive /
*                IsActive / IsActiveSelf.
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
#if CRUCIBLE_WITH_MONO
#include <algorithm>
#include <map>
#include <utility>
#include <vector>
#include <mono/metadata/loader.h>

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
	}

	void RegisterEnableBindings()
	{
		mono_add_internal_call("Crucible.Entity::SetComponentEnabledInternal",
		                       reinterpret_cast<const void*>(ManagedSetComponentEnabled));
		mono_add_internal_call("Crucible.Entity::IsComponentEnabledInternal",
		                       reinterpret_cast<const void*>(ManagedIsComponentEnabled));
		mono_add_internal_call("Crucible.Entity::SetActiveInternal", reinterpret_cast<const void*>(ManagedSetActive));
		mono_add_internal_call("Crucible.Entity::IsActiveInternal", reinterpret_cast<const void*>(ManagedIsActive));
	}
}
#endif
