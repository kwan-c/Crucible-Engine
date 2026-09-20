/*************************************************************************
* \file:         ComponentKind.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Declares the fixed ComponentKind enum (one entry per
*                engine component type) and the CRUCIBLE_COMPONENT /
*                CRUCIBLE_COMPONENT_LIST macros used to bind each C++
*                component struct to its ComponentKind/name.
*
* \details
*                This is THE file to edit when adding a new component
*                type: add an enum value here (before MAXCOMPONENT),
*                add it to CRUCIBLE_COMPONENT_LIST, define the actual
*                component struct in Components.h, and bind it there
*                with CRUCIBLE_COMPONENT(Type, KindValue). Everything
*                else (ComponentTypeId.h, ComponentInfo.h,
*                ComponentRegistry.cpp's RegisterAll) picks the new
*                type up automatically via the macro list.
*
* \history
*    -v1.0       initial fixed component-kind enum + binding macros
*
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <string_view>

namespace Crucible
{

	/*************************************************************************
	 \brief
		Fixed enumeration giving every engine component type a stable
		numeric id.

	 \details
		The numeric value of each enumerator IS the component's ComponentId
		and its bit position in a ComponentSignature. MAXCOMPONENT is a
		sentinel one past the last real entry, used to size fixed-length
		arrays such as ComponentSignature's bitset and ComponentRegistry's
		storage.

	 \note
		This is the file to edit when adding a new component type: add an
		enumerator here (before MAXCOMPONENT) and add it to
		CRUCIBLE_COMPONENT_LIST below.
	 *************************************************************************/
	//fixed enum id for each component
	enum class ComponentKind :uint32_t
	{
		Transform = 0,
		RigidBody = 1,
		MeshRenderer = 2,
		Audio = 3,

		MAXCOMPONENT = 4
	};


	/*************************************************************************
	 \brief
		Primary template for per-component-type metadata; deliberately left
		undefined.

	 \details
		Only explicit specialisations produced by CRUCIBLE_COMPONENT are
		valid to use. A type with no specialisation fails the
		RegisteredComponent concept at compile time.
	 *************************************************************************/
	template <typename T>
	struct ComponentTraits;


/*************************************************************************
 \brief
	Binds a component struct to its ComponentKind and a display name.
 \param Type
	The component struct being bound (e.g. Transform).
 \param KindValue
	The matching ComponentKind enumerator name (e.g. Transform).

 \details
	Expands to an explicit specialisation of ComponentTraits<Type> with
	a `kind` and a `name` (stringised from Type). Used once per
	component struct in Components.h, right after its definition.
*************************************************************************/
#define CRUCIBLE_COMPONENT(Type, KindValue)                         \
    template<>                                                       \
	struct ComponentTraits<Type>                                      \
	{                                                                  \
		static constexpr ComponentKind kind = ComponentKind::KindValue; \
		static constexpr std::string_view name = #Type;                  \
	};

/*************************************************************************
  \brief
	 X-macro listing every registered component type, in registration
	 order.
  \param X
	 A macro/function-like name invoked once per component type, as
	 X(Type).
  \details
	 Expanding this with X = a per-type action is how
	 ComponentRegistry::RegisterAll() registers every component type
	 without a hand-maintained list of Register<T>() calls - add a new
	 component here (matching its ComponentKind entry above) and
	 RegisterAll() picks it up automatically.
*************************************************************************/
#define CRUCIBLE_COMPONENT_LIST(X)                                         \
 X(Transform)                                                                \
 X(RigidBody)                                                                \
 X(MeshRenderer)                                                               \
 X(Audio)                                                                      \

}