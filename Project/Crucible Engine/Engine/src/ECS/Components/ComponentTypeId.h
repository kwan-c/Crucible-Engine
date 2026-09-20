/*************************************************************************
* \file:         ComponentTypeId.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Defines ComponentId (the runtime integer id for a
*                component type) and ComponentTypeId<T>() (the
*                compile-time -> runtime bridge that maps a C++ type
*                to its numeric id), plus the RegisteredComponent
*                concept that enforces every component type has
*                proper traits.
*
* \details
*                Every other ECS file that needs to go from a C++
*                type T to a numeric id (ComponentColumn indexing,
*                archetype signatures, ComponentInfo, ...) goes
*                through ComponentTypeId<T>() here. See
*                ComponentKind.h for where ComponentTraits<T> gets
*                specialized per type (via CRUCIBLE_COMPONENT), which
*                is what makes T satisfy RegisteredComponent below.
*
* \history
*    -v1.0       initial ComponentId/ComponentTypeId<T>()/RegisteredComponent
*
*
**************************************************************************/
#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <utility>

#include "ComponentKind.h"

namespace Crucible
{
	/*********************************************************************
	 \brief  The runtime integer identifier for a component type.
	 \details
		Equal to std::to_underlying(ComponentTraits<T>::kind) for the type
		it was obtained from - see ComponentTypeId<T>().
	 *********************************************************************/
	using ComponentId = uint32_t;

	/*********************************************************************
	 \brief
		Upper bound on the number of distinct component types, and the bit
		width used by ComponentSignature.
	 \details
		Equal to ComponentKind::MAXCOMPONENT, i.e. one past the last real
		component id.
	 *********************************************************************/
	//upper bound for max comp ,and size for bitset
	inline constexpr uint32_t kMaxComponents = static_cast<uint32_t>(ComponentKind::MAXCOMPONENT);

	/*********************************************************************
	 \brief  Sentinel value meaning "no component" / "not a valid id".
	 *********************************************************************/
	inline constexpr ComponentId kInvalidComponentId = ~0u;//0 reserved for invalid comp

	/*********************************************************************
	 \brief
		Compile-time constraint requiring that ComponentTraits<T> has been
		specialized for T (i.e. T was bound via CRUCIBLE_COMPONENT).

	 \details
		Requires ComponentTraits<T>::kind to convert to ComponentKind and
		ComponentTraits<T>::name to convert to std::string_view. Any T that
		fails this cannot be used with ComponentTypeId<T>(),
		MakeComponentInfo<T>(), or as a Query<...>/MakeSignature<...>
		argument.
	 *********************************************************************/
	//checks at compile time that traits matched the component kind and name
	template <typename T>
	concept RegisteredComponent = requires
	{
		{ComponentTraits<T>::kind } -> std::convertible_to<ComponentKind>;
		{ ComponentTraits<T>::name } -> std::convertible_to<std::string_view>;
	};

	/*********************************************************************
	 \brief  Whether a numeric component id is in range and not the invalid
			 sentinel.
	 \param[in] id The id to check.
	 \return true if id != kInvalidComponentId and id < kMaxComponents.
	 *********************************************************************/
	constexpr bool IsValidComponentId(ComponentId id)
	{
		return id != kInvalidComponentId && id < kMaxComponents;
	}

	/*********************************************************************
	 \brief
		Maps a C++ component type to its runtime ComponentId.

	 \param T
		The component type; must satisfy RegisteredComponent.
	 \return
		The ComponentId corresponding to ComponentTraits<T>::kind.

	 \details
		The single bridge every other ECS file uses to go from a type T to
		a numeric id (ComponentColumn indexing, archetype signatures,
		ComponentInfo, ...). Fails to compile for any T not bound via
		CRUCIBLE_COMPONENT in ComponentKind.h.
	 *********************************************************************/
	//invalid or missing traits that cause compile time error
	template <typename T>
	constexpr ComponentId ComponentTypeId()
	{
		static_assert(RegisteredComponent<T>,
			"T is not a registered ECS component. ADD a crucible component in ComponentKind.h and add in CRUCIBLE_COMPONENT_LIST ");

		if constexpr (RegisteredComponent<T>)
		{

			constexpr ComponentId id =std::to_underlying(ComponentTraits<T>::kind);// c++23 update to for static cast only for enums for int values "1,2etc"

			static_assert(IsValidComponentId(id), "component Trait uses an invalid component id");

			return id;


		}
	}

	/*********************************************************************
	 \brief  Converts a ComponentKind enumerator to its numeric id.
	 \param[in] kind The enumerator to convert.
	 \return The underlying integer value of kind.
	 *********************************************************************/
	//enum to numeric 
	constexpr ComponentId ToComponentId(ComponentKind kind)
	{
		return std::to_underlying(kind);
	}

		
	/*********************************************************************
	\brief  Converts a numeric component id back to a ComponentKind.
	\param[in] id The id to convert.
	\return The corresponding enumerator.
	\note
	   Performs no validation - id should already be known valid (see
	   IsValidComponentId()).
	*********************************************************************/
	// numeric to enum
	constexpr ComponentKind ToComponentKind(ComponentId id)
	{
		return static_cast<ComponentKind>(id);
	}

	

}