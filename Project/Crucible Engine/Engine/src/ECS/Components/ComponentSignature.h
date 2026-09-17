#pragma once


#include <bitset>
#include <functional>

#include "ComponentTypeId.h"

namespace Crucible
{
	// on bit per comp id
	//bit pos = comp id
	using ComponentSignature = std::bitset<kMaxComponents>;

	// building integer mask by setting one bit per suppplied comp type
	//
	template <typename... Components>
	constexpr unsigned long long SignatureMask()
	{

		return ((1ull << ComponentTypeId<Components>()) | ... | 0ull);
	}

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
	//check whether the archetypes bitset(signature) matches the required bitset(signature)ll extra comps are allowed
	constexpr bool SignatureMatches(const ComponentSignature& archetype, const ComponentSignature& required)
	{
		return (archetype & required) == required;
	}


	//the following computes the hash for using signatures that will be used as unordered map keys
	struct SignatureHash
	{
		std::size_t operator()(const ComponentSignature& signature)const noexcept
		{
			return std::hash<ComponentSignature>{}(signature);
		}
	};
}