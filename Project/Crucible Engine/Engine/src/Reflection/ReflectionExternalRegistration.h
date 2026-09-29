#pragma once

/*!
* @file			ReflectionExternalRegistration.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		Registration of the external data types.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "Reflection/ReflectionRegistration.h"
#include "Math/Math.h"

namespace rttr::detail {

    template<bool V> struct template_type_trait<xmath::fvec3_t<V>> : std::true_type
    {
        static std::vector<::rttr::type> get_template_arguments() { return { ::rttr::type::get<bool>() }; }
    };

    template<bool V> struct template_type_trait<xmath::fquat_t<V>> : std::true_type
    {
        static std::vector<::rttr::type> get_template_arguments() { return { ::rttr::type::get<bool>() }; }
    };

    template<bool V> struct template_type_trait<xmath::fmat3_t<V>> : std::true_type
    {
        static std::vector<::rttr::type> get_template_arguments() { return { ::rttr::type::get<bool>() }; }
    };

    template<bool V> struct template_type_trait<xmath::fmat4_t<V>> : std::true_type
    {
        static std::vector<::rttr::type> get_template_arguments() { return { ::rttr::type::get<bool>() }; }
    };
}

// NOTE (Nelson, 29 Sep 2026): the RTTR_REGISTRATION block that used to sit
// here has moved to Reflection/ReflectionComponentRegistration.cpp.
//
// RTTR_REGISTRATION expands to a fixed-name struct and function, so a
// translation unit may only contain one. Living in a header, this one was
// pulled into both ImGuiPanel_Inspector.cpp and ImGuiManager.cpp - two
// copies in the Editor - and could never be combined with any other
// registration. It also meant the math types were registered by the Editor
// only, so Game.exe ran without them.
//
// The template_type_trait specialisations above must stay in this header:
// they are compile-time traits and every translation unit needs to see them.
// The registration is runtime data and belongs in exactly one .cpp.
