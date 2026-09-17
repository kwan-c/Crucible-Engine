#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <utility>

#include "ComponentKind.h"

namespace Crucible
{
	
	using ComponentId = uint32_t;

	//upperbod for maz comp ,and size for bitset
	inline constexpr uint32_t kMaxComponents = static_cast<uint32_t>(ComponentKind::MAXCOMPONENT);
	inline constexpr ComponentId kInvalidComponentId = ~0u;//0 reserved for invalid comp

	//checks at complie time that traits matched the component kind and name
	template <typename T>
	concept RegisteredComponent = requires
	{
		{ComponentTraits<T>::kind } -> std::convertible_to<ComponentKind>;
		{ ComponentTraits<T>::name } -> std::convertible_to<std::string_view>;
	};

	constexpr bool IsValidComponentId(ComponentId id)
	{
		return id != kInvalidComponentId && id < kMaxComponents;
	}

	//invalid or missing traits that cause complie time error
	template <typename T>
	constexpr ComponentId ComponentTypeId()
	{
		static_assert(RegisteredComponent<T>,
			"T is not a registered ECS component. ADD a curcible compoenent in ComponentKind.h and add in CRUCIBLE_COMPONENT_LIST ");

		if constexpr (RegisteredComponent<T>)
		{

			constexpr ComponentId id =std::to_underlying(ComponentTraits<T>::kind);// c++23 update to for static cast only for enums for int values "1,2etc"

			static_assert(IsValidComponentId(id), "component Trait uses an invalid component id");

			return id;


		}
	}

	//enum to numeric 
	constexpr ComponentId ToComponentId(ComponentKind kind)
	{
		return std::to_underlying(kind);
	}

	// numeric to enum
	constexpr ComponentKind ToComponentKind(ComponentId id)
	{
		return static_cast<ComponentKind>(id);
	}

	

}