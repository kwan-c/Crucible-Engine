/*************************************************************************
* \file:         ComponentSignature.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Defines ComponentSignature (a fixed-size bitset, one
*                bit per component id) and the helpers used to
*                build/compare signatures, plus a hash functor so
*                signatures can be unordered_map keys.
*
* \details
*                A signature is what identifies an Archetype (see
*                Archetype.h) and is how World::CreateQuery<T...>()
*                (see World.h) describes which components a query
*                requires. See ArchetypeRegistry.h/.cpp for where
*                signatures are used as unordered_map keys (via
*                SignatureHash below).
*
* \history
*    -v1.0       initial bitset-based signature + mask/match helpers
*
*
**************************************************************************/
#pragma once


#include <bitset>
#include <functional>

#include "ComponentTypeId.h"

namespace Crucible
{
	/*********************************************************************
	 \brief
		A fixed-width bitset identifying a set of component types, one bit
		per ComponentId.

	 \details
		This is what identifies an Archetype's exact component makeup and
		what QueryDescription uses to describe which components a query
		requires/excludes.
	 *********************************************************************/
	using ComponentSignature = std::bitset<kMaxComponents>;

	// building integer mask by setting one bit per supplied comp type
	/*********************************************************************
	 \brief
		Builds a compile-time integer bitmask with one bit set per given
		component type.

	 \tparam Components
		The component types to include in the mask.
	 \return
		An unsigned long long with bit ComponentTypeId<C>() set for each C.

	 \note
		Only correct while kMaxComponents <= 64 (the mask must fit in an
		unsigned long long) - see MakeSignature(), which falls back to a
		per-bit loop above that width.
	 *********************************************************************/
	template <typename... Components>
	constexpr unsigned long long SignatureMask()
	{

		return ((1ull << ComponentTypeId<Components>()) | ... | 0ull);
	}

	/*********************************************************************
	 \brief
		Builds a ComponentSignature with one bit set per given component
		type.

	 \tparam Components
		The component types the signature should require.
	 \return
		A ComponentSignature with bit ComponentTypeId<C>() set for every C.

	 \details
		Uses the fast single-integer-mask constructor (via SignatureMask())
		when kMaxComponents fits in 64 bits, otherwise sets each bit
		individually - callers never need to know which path ran.
	 *********************************************************************/
	//will take in the component types and convert them to signatures
	template <typename ... Components>
	constexpr ComponentSignature MakeSignature()
	{
		if constexpr (kMaxComponents <= 64)
		{
			return ComponentSignature(SignatureMask<Components ...>());
		}
		else
		{
			ComponentSignature signature;
			//set will turn on bits of comps
			(signature.set(ComponentTypeId<Components>()), ...);
			return signature;
		}
	}

	/*********************************************************************
	 \brief
		Checks whether an archetype's signature satisfies a required set of
		components.

	 \param[in]
		archetype The signature being tested (typically an Archetype's).
	 \param[in]
		required The set of components that must all be present.
	 \return
		true if every bit set in required is also set in archetype. Extra
		components on archetype beyond required are allowed.
	 *********************************************************************/
	//check whether the archetypes bitset(signature) matches the required bitset(signature)ll extra comps are allowed
	constexpr bool SignatureMatches(const ComponentSignature& archetype, const ComponentSignature& required)
	{
		return (archetype & required) == required;
	}

	/*********************************************************************
	 \brief
		Hash functor allowing ComponentSignature to be used as an
		unordered_map/unordered_set key.

	 \details
		Used by ArchetypeRegistry to key its signature -> Archetype map.
	 *********************************************************************/
	//the following computes the hash for using signatures that will be used as unordered map keys
	struct SignatureHash
	{

		/*********************************************************************
		 \brief  Computes a hash for a ComponentSignature.
		 \param[in] signature The signature to hash.
		 \return A hash value suitable for unordered associative containers.
		 *********************************************************************/
		std::size_t operator()(const ComponentSignature& signature)const noexcept
		{
			return std::hash<ComponentSignature>{}(signature);
		}
	};
}