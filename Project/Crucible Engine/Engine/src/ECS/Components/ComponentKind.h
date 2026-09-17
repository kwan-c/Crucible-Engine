#pragma once

#include <cstdint>
#include <string_view>

namespace Crucible
{
	//fixed enum id for each component
	enum class ComponentKind :uint32_t
	{
		Transform = 0,
		RigidBody = 1,
		MeshRenderer = 2,
		Audio = 3,

		MAXCOMPONENT = 4
	};

	template <typename T>
	struct ComponentTraits;


	//name 
#define CRUCIBLE_COMPONENT(Type, KindValue)                         \
    template<>                                                       \
	struct ComponentTraits<Type>                                      \
	{                                                                  \
		static constexpr ComponentKind kind = ComponentKind::KindValue; \
		static constexpr std::string_view name = #Type;                  \
	};

#define CRUCIBLE_COMPONENT_LIST(X)                                         \
 X(Transform)                                                                \
 X(RigidBody)                                                                \
 X(MeshRenderer)                                                               \
 X(Audio)                                                                      \

}