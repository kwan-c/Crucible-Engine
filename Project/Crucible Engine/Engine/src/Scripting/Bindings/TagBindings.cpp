/*************************************************************************
* \file:         TagBindings.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 November 2026
* \version       1.0
* \brief         Internal calls behind the C# entity tag API
*                (ManagedScripts/Runtime/Entity.cs): Entity.Tag,
*                Entity.CompareTag, Entity.FindWithTag,
*                Entity.FindAllWithTag, Entity.IsTagDefined.
*
* \details
*    Every call goes through World's tag functions, so C# gets exactly the
*    native rules: a tag must be defined in the project's tag list
*    (TagRegistry) to be assigned, and comparing/searching are exact,
*    case-sensitive matches. Entities cross as (index, generation) and are
*    checked with EntityContext::ResolveEntity (a destroyed entity, or a
*    call outside Play, fails instead of touching the wrong slot).
*
*    A tag set by a script is a Play-time change: Stop restores the
*    scene's saved tags (Play snapshot), and nothing here is replicated
*    over the network.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "ECS/TagRegistry.h"
#if CRUCIBLE_WITH_MONO
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

		// Results of SetTagInternal. Must match Entity.cs.
		constexpr int32_t kTagSet = 0;
		constexpr int32_t kTagDeadEntity = 1;
		constexpr int32_t kTagUndefined = 2;
		constexpr int32_t kTagFailed = 3;   // bad string, no world, or an engine error

		// A rooted C# string as UTF-8; false for null or invalid text.
		bool ReadTag(MonoString* value, std::string& out)
		{
			GcRoot root(reinterpret_cast<MonoObject*>(value));
			return root.Get() && FromManagedString(reinterpret_cast<MonoString*>(root.Get()), out);
		}

		/*********************************************************************
		 \brief  C# Entity.Tag (get): the entity's tag, or null if it is gone.
		 *********************************************************************/
		MonoString* ManagedEntityGetTag(int32_t index, int32_t generation) noexcept
		{
			try
			{
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return nullptr;
				const std::string tag(EntityContext::GetWorld()->GetEntityTag(entity));
				std::wstring wide;
				if (!ToUtf16(tag, wide))
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
		 \brief  C# Entity.Tag (set), through World::TrySetEntityTag.
		 \return kTagSet, kTagDeadEntity, kTagUndefined or kTagFailed.
		 *********************************************************************/
		int32_t ManagedEntitySetTag(int32_t index, int32_t generation, MonoString* tag) noexcept
		{
			try
			{
				std::string wanted;
				if (!ReadTag(tag, wanted))
					return kTagFailed;
				Entity entity;
				if (!EntityContext::ResolveEntity(index, generation, entity))
					return kTagDeadEntity;
				switch (EntityContext::GetWorld()->TrySetEntityTag(entity, wanted))
				{
				case TagResult::Ok:           return kTagSet;
				case TagResult::DeadEntity:   return kTagDeadEntity;
				case TagResult::UndefinedTag: return kTagUndefined;
				default:                      return kTagFailed;
				}
			}
			catch (...)
			{
				return kTagFailed;
			}
		}

		/*********************************************************************
		 \brief  C# Entity.CompareTag. *out = 1 if the entity is alive and its
		         tag is exactly tag, else 0.
		 \return 1 unless out is null.
		 *********************************************************************/
		int32_t ManagedEntityCompareTag(int32_t index, int32_t generation, MonoString* tag, int32_t* out) noexcept
		{
			try
			{
				if (!out)
					return 0;
				*out = 0;
				std::string wanted;
				Entity entity;
				if (!ReadTag(tag, wanted) || !EntityContext::ResolveEntity(index, generation, entity))
					return 1;
				*out = EntityContext::GetWorld()->CompareTag(entity, wanted) ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Entity.FindWithTag: the first entity with the tag (storage
		         order). 1 and its (index, generation) if found during Play,
		         otherwise 0.
		 *********************************************************************/
		int32_t ManagedEntityFindWithTag(MonoString* tag, int32_t* outIndex, int32_t* outGeneration) noexcept
		{
			try
			{
				if (!outIndex || !outGeneration)
					return 0;
				*outIndex = 0;
				*outGeneration = 0;
				std::string wanted;
				World* world = EntityContext::GetWorld();
				if (!world || !ReadTag(tag, wanted))
					return 0;
				const Entity match = world->FindWithTag(wanted);
				// ResolveEntity also rejects calls outside a Play session.
				Entity checked;
				if (!match.IsValid() || !EntityContext::ResolveEntity(static_cast<int32_t>(match.index),
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

		/*********************************************************************
		 \brief  C# Entity.FindAllWithTag: every entity with the tag, as an
		         int[] of (index, generation) pairs. Empty if none; null
		         outside Play or on an error.
		 *********************************************************************/
		MonoArray* ManagedEntityFindAllWithTag(MonoString* tag) noexcept
		{
			try
			{
				std::string wanted;
				World* world = EntityContext::GetWorld();
				if (!world || !ReadTag(tag, wanted))
					return nullptr;
				std::vector<Entity> found;
				world->FindAllWithTag(wanted, found);
				if (!found.empty())
				{
					Entity checked;   // outside Play nothing is returned (same as FindWithTag)
					if (!EntityContext::ResolveEntity(static_cast<int32_t>(found.front().index),
					                                  static_cast<int32_t>(found.front().generation), checked))
						return nullptr;
				}
				MonoArray* pairs = mono_array_new(mono_domain_get(), mono_get_int32_class(), found.size() * 2);
				if (!pairs)
					return nullptr;
				for (std::size_t i = 0; i < found.size(); ++i)
				{
					mono_array_set(pairs, int32_t, i * 2, static_cast<int32_t>(found[i].index));
					mono_array_set(pairs, int32_t, i * 2 + 1, static_cast<int32_t>(found[i].generation));
				}
				return pairs;
			}
			catch (...)
			{
				return nullptr;
			}
		}

		/*********************************************************************
		 \brief  C# Entity.IsTagDefined: 1 if the project's tag list has the
		         tag (exact, case-sensitive), else 0.
		 *********************************************************************/
		int32_t ManagedIsTagDefined(MonoString* tag) noexcept
		{
			try
			{
				std::string wanted;
				return ReadTag(tag, wanted) && TagRegistry::Get().IsDefined(wanted) ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}
	}

	void RegisterTagBindings()
	{
		mono_add_internal_call("Crucible.Entity::GetTagInternal", reinterpret_cast<const void*>(ManagedEntityGetTag));
		mono_add_internal_call("Crucible.Entity::SetTagInternal", reinterpret_cast<const void*>(ManagedEntitySetTag));
		mono_add_internal_call("Crucible.Entity::CompareTagInternal", reinterpret_cast<const void*>(ManagedEntityCompareTag));
		mono_add_internal_call("Crucible.Entity::FindWithTagInternal", reinterpret_cast<const void*>(ManagedEntityFindWithTag));
		mono_add_internal_call("Crucible.Entity::FindAllWithTagInternal",
		                       reinterpret_cast<const void*>(ManagedEntityFindAllWithTag));
		mono_add_internal_call("Crucible.Entity::IsTagDefinedInternal", reinterpret_cast<const void*>(ManagedIsTagDefined));
	}
}
#endif
