/*************************************************************************
* \file:         EntityBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         Internal calls behind the C# Entity class
*                (ManagedScripts/Runtime/Entity.cs).
*
* \details
*    Entities cross as (index, generation) and are checked with
*    EntityContext::ResolveEntity, so a destroyed entity, or a call outside
*    Play, fails instead of touching the wrong slot.
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
#include <mono/metadata/appdomain.h>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		/*********************************************************************
		 \brief  C# Entity.IsAlive. *out = 1 while the entity exists in a Play
		         session, 0 once it is destroyed (or outside Play).
		 \return 1 unless out is null.
		 *********************************************************************/
		int32_t ManagedEntityIsAlive(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				if (!out)
					return 0;
				Entity entity;
				*out = EntityContext::ResolveEntity(index, generation, entity) ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Entity.Name: the name shown in the Hierarchy. Returns a new
		         C# string, or null if the entity is gone or has no name.
		 *********************************************************************/
		MonoString* ManagedEntityGetName(int32_t index, int32_t generation) noexcept
		{
			try
			{
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return nullptr;
				const EntityName* name = EntityContext::GetWorld()->Read<EntityName>(entity);
				if (!name || name->name.empty())
					return nullptr;
				std::wstring wide;
				if (!ToUtf16(name->name, wide))
					return nullptr;
				return mono_string_new_utf16(mono_domain_get(), reinterpret_cast<const mono_unichar2*>(wide.data()),
				                             static_cast<int32_t>(wide.size()));
			}
			catch (...)
			{
				return nullptr;
			}
		}

		/*********************************************************************
		 \brief  C# Entity.Find(name): an entity whose Hierarchy name matches
		         exactly. Names need not be unique; if several match, which
		         one is returned is unspecified.
		 \return 1 and the entity's (index, generation) if found during Play,
		         otherwise 0.
		 \note   Visits every named entity - call it in OnCreate and keep the
		         result, not every frame.
		 *********************************************************************/
		int32_t ManagedEntityFind(MonoString* name, int32_t* outIndex, int32_t* outGeneration) noexcept
		{
			try
			{
				if (!outIndex || !outGeneration)
					return 0;
				*outIndex = 0;
				*outGeneration = 0;
				GcRoot root(reinterpret_cast<MonoObject*>(name));
				std::string wanted;
				World* world = EntityContext::GetWorld();
				if (!world || !root.Get() || !FromManagedString(reinterpret_cast<MonoString*>(root.Get()), wanted) ||
				    wanted.empty())
					return 0;
				Entity match{};
				bool found = false;
				world->BuildQuery<const EntityName>().Build().ForEach(
				    [&](Entity entity, const EntityName& entityName)
				    {
					    if (!found && entityName.name == wanted)
					    {
						    match = entity;
						    found = true;
					    }
				    });
				// ResolveEntity also rejects calls outside a Play session.
				Entity checked;
				if (!found || !EntityContext::ResolveEntity(static_cast<int32_t>(match.index),
				                                            static_cast<int32_t>(match.generation), checked))
					return 0;
				*outIndex = static_cast<int32_t>(match.index);
				*outGeneration = static_cast<int32_t>(match.generation);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// Component kinds for HasComponentInternal. Must match ComponentKind in Entity.cs.
		enum class ScriptComponentKind : int32_t
		{
			Transform = 0,
			RigidBody = 1,
			Animator = 2, // the AnimatedModel component
			Script = 3,
			AudioSource = 4,
			CharacterController = 5,
			Camera = 6,       // the CameraComponent
			CameraOrbit = 7,
			Light = 8,
			ParticleEmitter = 9,
			UIText = 10,
			UIImage = 11,
			UISlider = 12,
			UIButton = 13,
			UIRect = 14,
			UICanvas = 15,
			Collider = 16,
			MeshRenderer = 17,
			PlayerController = 18,
			WorldText = 19,
			MaterialParams = 20,
		};

		/*********************************************************************
		 \brief  C# Entity.HasComponent<T>() / GetComponent<T>(): does the
		         entity have the native component behind a C# wrapper?
		 \return 1 with *out = 0/1, or 0 for a null pointer or unknown kind.
		 *********************************************************************/
		int32_t ManagedEntityHasComponent(int32_t index, int32_t generation, int32_t kind, int32_t* out) noexcept
		{
			try
			{
				if (!out)
					return 0;
				*out = 0;
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return 1; // a dead entity simply has nothing
				const World& world = *EntityContext::GetWorld();
				switch (static_cast<ScriptComponentKind>(kind))
				{
				case ScriptComponentKind::Transform:
					*out = world.HasComponent<Transform>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::RigidBody:
					*out = world.HasComponent<RigidBody>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::Animator:
					*out = world.HasComponent<AnimatedModel>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::Script:
					*out = world.HasComponent<ScriptComponent>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::UIText:
					*out = world.HasComponent<Crucible::UIText>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::UIImage:
					*out = world.HasComponent<Crucible::UIImage>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::UISlider:
					*out = world.HasComponent<Crucible::UISlider>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::UIButton:
					*out = world.HasComponent<Crucible::UIButton>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::UIRect:
					*out = world.HasComponent<Crucible::UIRect>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::UICanvas:
					*out = world.HasComponent<Crucible::UICanvas>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::ParticleEmitter:
					*out = world.HasComponent<Crucible::ParticleEmitter>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::Light:
					*out = world.HasComponent<Crucible::Light>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::Camera:
					*out = world.HasComponent<CameraComponent>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::CameraOrbit:
					*out = world.HasComponent<Crucible::CameraOrbit>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::CharacterController:
					*out = world.HasComponent<Crucible::CharacterController>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::AudioSource:
					*out = world.HasComponent<Crucible::AudioSource>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::Collider:
					*out = world.HasComponent<Crucible::Collider>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::MeshRenderer:
					*out = world.HasComponent<MeshRenderer>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::PlayerController:
					*out = world.HasComponent<Crucible::PlayerController>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::WorldText:
					*out = world.HasComponent<Crucible::WorldText>(entity) ? 1 : 0;
					return 1;
				case ScriptComponentKind::MaterialParams:
					*out = world.HasComponent<MaterialParams>(entity) ? 1 : 0;
					return 1;
				}
				return 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Entity.GetComponent<SomeScript>(): the C# script object
		         running on the entity, or null if it has none (no
		         ScriptComponent, disabled before it ever started, or failed).
		         C# then checks the object is actually a SomeScript.
		 *********************************************************************/
		MonoObject* ManagedEntityGetScript(int32_t index, int32_t generation) noexcept
		{
			try
			{
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return nullptr;
				const uint32_t handle = EntityContext::FindScriptHandle(entity);
				return handle ? mono_gchandle_get_target(handle) : nullptr;
			}
			catch (...)
			{
				return nullptr;
			}
		}

		/*********************************************************************
		 \brief  Depth-first search below `parent` for an entity named `name`.
		         Children are visited in Hierarchy order, each one's own children
		         before its next sibling.
		 *********************************************************************/
		bool FindDescendant(const World& world, Entity parent, const std::string& name, Entity& found)
		{
			const HierarchyNode* node = world.Read<HierarchyNode>(parent);
			for (Entity child = node ? node->firstChild : Entity{}; child.IsValid();)
			{
				const EntityName* childName = world.Read<EntityName>(child);
				if (childName && childName->name == name)
				{
					found = child;
					return true;
				}
				if (FindDescendant(world, child, name, found))
					return true;
				const HierarchyNode* childNode = world.Read<HierarchyNode>(child);
				child = childNode ? childNode->nextSibling : Entity{};
			}
			return false;
		}

		/*********************************************************************
		 \brief  C# Entity.FindChild(name): a child, grandchild, ... of the
		         entity whose Hierarchy name matches exactly. How a script reaches
		         the parts of a prefab it spawned.
		 \return 1 with its (index, generation), or 0 if none matches.
		 *********************************************************************/
		int32_t ManagedEntityFindChild(int32_t index, int32_t generation, MonoString* name, int32_t* outIndex,
		                               int32_t* outGeneration) noexcept
		{
			try
			{
				if (!outIndex || !outGeneration)
					return 0;
				*outIndex = 0;
				*outGeneration = 0;
				GcRoot root(reinterpret_cast<MonoObject*>(name));
				std::string wanted;
				Entity parent;
				if (!EntityContext::ResolveEntity(index, generation, parent) || !root.Get() ||
				    !FromManagedString(reinterpret_cast<MonoString*>(root.Get()), wanted) || wanted.empty())
					return 0;
				Entity found;
				if (!FindDescendant(*EntityContext::GetWorld(), parent, wanted, found))
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
	}

	void RegisterEntityBindings()
	{
		mono_add_internal_call("Crucible.Entity::FindChildInternal", reinterpret_cast<const void*>(ManagedEntityFindChild));
		mono_add_internal_call("Crucible.Entity::IsAliveInternal", reinterpret_cast<const void*>(ManagedEntityIsAlive));
		mono_add_internal_call("Crucible.Entity::GetNameInternal", reinterpret_cast<const void*>(ManagedEntityGetName));
		mono_add_internal_call("Crucible.Entity::FindInternal", reinterpret_cast<const void*>(ManagedEntityFind));
		mono_add_internal_call("Crucible.Entity::HasComponentInternal",
		                       reinterpret_cast<const void*>(ManagedEntityHasComponent));
		mono_add_internal_call("Crucible.Entity::GetScriptInternal", reinterpret_cast<const void*>(ManagedEntityGetScript));
	}
}
#endif
