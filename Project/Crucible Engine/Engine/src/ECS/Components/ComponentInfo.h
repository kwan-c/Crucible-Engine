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
		//constuct T at dest
		template <typename T>
		void DefaultConstructImpl(void* dst)
		{
			std::construct_at(static_cast<T*>(dst));
		}

		template<typename T>
		void DestroyImpl(void* ptr)
		{
			std::destroy_at(static_cast<T*>(ptr));
		}

		template <typename T>
		void MoveConstructImpl(void* dst, void* src)
		{
			std::construct_at(static_cast<T*>(dst), std::move(*static_cast<T*>(src)));
		}

		template <typename T>
		void CopyConstructImpl(void* dst, const void* src)
		{
			std::construct_at(static_cast<T*>(dst), *static_cast< const T*>(src));
		}
	}


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

		bool IsValid()const { return id != kInvalidComponentId; }
		ComponentKind Kind()const { return ToComponentKind(id); }

	};

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