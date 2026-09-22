/*************************************************************************
* \file:         ComponentInfo.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Declares ComponentInfo: the type-erased description of
*                a single component type (size, alignment, and
*                construct/destroy function pointers) that lets
*                ComponentColumn store any component type without being
*                a template itself.
*
* \details
*                MakeComponentInfo<T>() below is the only way a
*                ComponentInfo gets built - it fills in every field
*                from T via type traits/std::construct_at, and is
*                called once per type by ComponentRegistry::Register<T>()
*                (see ComponentRegistry.h) during
*                ComponentRegistry::RegisterAll() (ComponentRegistry.cpp).
*                See ComponentColumn.h/.cpp for how these function
*                pointers get used.
*
* \history
*    -v1.0       initial type-erased component metadata + MakeComponentInfo<T>
*
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "ComponentTypeId.h"

namespace Crucible
{
	// helper functions
	namespace Detail
	{
		/*********************************************************************
		 \brief  Default-constructs a T in already-allocated storage.
		 \param[out] dst Pointer to (aligned, sized) storage for one T.
		 \note  Used as ComponentInfo::defaultConstruct for type T.
		 *********************************************************************/
		template <typename T>
		void DefaultConstructImpl(void* dst)
		{
			std::construct_at(static_cast<T*>(dst));
		}

		/*********************************************************************
		 \brief  Destroys a live T in place, without freeing its storage.
		 \param[in] ptr Pointer to a live T.
		 \note  Used as ComponentInfo::destroy for type T.
		 *********************************************************************/
		template<typename T>
		void DestroyImpl(void* ptr)
		{
			std::destroy_at(static_cast<T*>(ptr));
		}

		/*********************************************************************
		\brief
		   Move-constructs a T from src into dst, leaving src moved-from
		   (still alive - the caller must destroy it separately).
		\param[out] dst Pointer to storage for the new T.
		\param[in]  src Pointer to the live T being moved from.
		\note  Used as ComponentInfo::moveConstruct for type T.
		*********************************************************************/
		template <typename T>
		void MoveConstructImpl(void* dst, void* src)
		{
			std::construct_at(static_cast<T*>(dst), std::move(*static_cast<T*>(src)));
		}

		/*********************************************************************
		 \brief  Copy-constructs a T from src into dst.
		 \param[out] dst Pointer to storage for the new T.
		 \param[in]  src Pointer to the T being copied.
		 \note  Used as ComponentInfo::copyConstruct for type T.
		 *********************************************************************/
		template <typename T>
		void CopyConstructImpl(void* dst, const void* src)
		{
			std::construct_at(static_cast<T*>(dst), *static_cast< const T*>(src));
		}
	}

	/*************************************************************************
	 \brief
		Type-erased description of one component type: its size, alignment,
		relocation/destruction traits, and construct/destroy function
		pointers.

	 \details
		Built exactly once per type by MakeComponentInfo<T>() and stored by
		ComponentRegistry, indexed by ComponentId. This is what lets
		ComponentColumn manage arbitrary component types without itself
		being a template - every byte-level operation goes through one of
		the four function pointers here instead of calling T's members
		directly.

	 \note
		A default-constructed ComponentInfo is invalid (id ==
		kInvalidComponentId, all function pointers null) - see IsValid().
	 *************************************************************************/
	struct ComponentInfo
	{
		//start comp id from invalid
		ComponentId id = kInvalidComponentId;

		//comp name
		std::string_view name;

		//number of bytes occupied by on comp
		std::size_t size = 0;
		
		//required alignment of the component memory add
		std::size_t alignment = 0;

		//indicates that the columns byte-copy relocation path can be used 
		// for this comp type 
		bool isTriviallyRelocatable = false;

		//true when destruction requires no clean up
		//lets the column skip per object destructor calls
		bool isTriviallyDestructible = false;

		//comments to written
		void(*defaultConstruct)(void* dst) = nullptr;
		void (*destroy)(void* ptr) = nullptr;
		void (*moveConstruct)(void* dst, void* src) = nullptr;
		void (*copyConstruct)(void* dst, const void* src) = nullptr;


		/*********************************************************************
		 \brief  Whether this describes a real, registered component type.
		 \return true if id is not kInvalidComponentId.
		 *********************************************************************/
		bool IsValid()const { return id != kInvalidComponentId; }

		/*********************************************************************
		 \brief  The ComponentKind this info describes.
		 \return id converted back to a ComponentKind.
		 *********************************************************************/
		ComponentKind Kind()const { return ToComponentKind(id); }

	};

	/*************************************************************************
	 \brief
		Builds a fully-populated ComponentInfo for component type T.

	 \tparam T
		The component type; must satisfy RegisteredComponent and be
		default-constructible.
	 \return
		A ComponentInfo describing T: its id/name/size/alignment, whether it
		is trivially relocatable/destructible, and its four type-erased
		construct/destroy function pointers.

	 \details
		The only place a ComponentInfo is constructed. Called once per type
		by ComponentRegistry::Register<T>() during RegisterAll().
		isTriviallyRelocatable is true only when T is both trivially
		copyable and trivially destructible, which is what lets
		ComponentColumn use a plain memcpy instead of move-construct+destroy
		for that type.
	 *************************************************************************/
	template<typename T>
	ComponentInfo MakeComponentInfo()
	{
		static_assert(RegisteredComponent<T>, "T is no a registered ECS component");
		static_assert(std::is_default_constructible_v<T>, "components must be default constructable");


		ComponentInfo info;
		info.id = ComponentTypeId<T>();
		info.name = ComponentTraits<T>::name;
		info.size = sizeof(T);
		info.alignment = alignof(T);

		info.isTriviallyRelocatable = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;

		info.isTriviallyDestructible = std::is_trivially_destructible_v<T>;

		info.defaultConstruct = &Detail::DefaultConstructImpl<T>;
		info.destroy = &Detail::DestroyImpl<T>;
		info.moveConstruct = &Detail::MoveConstructImpl<T>;
		info.copyConstruct = &Detail::CopyConstructImpl<T>;
		return info;
	}
}