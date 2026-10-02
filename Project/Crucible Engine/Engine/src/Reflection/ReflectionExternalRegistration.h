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

    //template<bool V> struct template_type_trait<xmath::fvec3_t<V>> : std::true_type
    //{
    //    static std::vector<::rttr::type> get_template_arguments() { return { ::rttr::type::get<bool>() }; }
    //};

    //template<bool V> struct template_type_trait<xmath::fquat_t<V>> : std::true_type
    //{
    //    static std::vector<::rttr::type> get_template_arguments() { return { ::rttr::type::get<bool>() }; }
    //};

    //template<bool V> struct template_type_trait<xmath::fmat3_t<V>> : std::true_type
    //{
    //    static std::vector<::rttr::type> get_template_arguments() { return { ::rttr::type::get<bool>() }; }
    //};

    //template<bool V> struct template_type_trait<xmath::fmat4_t<V>> : std::true_type
    //{
    //    static std::vector<::rttr::type> get_template_arguments() { return { ::rttr::type::get<bool>() }; }
    //};
}

// NOTE (Nelson, 29 Sep 2026): the RTTR_REGISTRATION block that used to sit
// here has moved to Reflection/MathReflection.cpp (components each have
// their own file under Reflection/Components/).
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
// Disabled, not deleted: kept for reference. The live math registrations
// are in MathReflection.cpp. Re-enabling this block causes
//   'rttr__auto__register__': 'struct' type redefinition
// because that .cpp includes this header and has its own block.
#if 0
RTTR_REGISTRATION
{
	using namespace Crucible::Math;

    // -----------------
    // Registration for Math Library
    // -----------------

	//rttr::registration::class_<vec2>("vec2")
	//	.property("x", &vec2::m_X)
	//	.property("y", &vec2::m_Y)
	//;
	//
	//rttr::registration::class_<vec3>("vec3")
	//	.property("x", &vec3::m_X)
	//	.property("y", &vec3::m_Y)
	//	.property("z", &vec3::m_Z)
	//;
	//
	//rttr::registration::class_<vec4>("vec4")
	//	.property("w", &vec4::m_W)
	//	.property("x", &vec4::m_X)
	//	.property("y", &vec4::m_Y)
	//	.property("z", &vec4::m_Z)
	//;
	//
 //   rttr::registration::class_<mat3>("mat3")
 //       .property("m00", &mat3::m_00)
 //       .property("m10", &mat3::m_10)
 //       .property("m20", &mat3::m_20)
 //       .property("m01", &mat3::m_01)
 //       .property("m11", &mat3::m_11)
 //       .property("m21", &mat3::m_21)
 //       .property("m02", &mat3::m_02)
 //       .property("m12", &mat3::m_12)
 //       .property("m22", &mat3::m_22)
 //   ;
	//
	//rttr::registration::class_<mat4>("mat4")
 //       .property("m00", &mat4::m_00)
 //       .property("m10", &mat4::m_10)
 //       .property("m20", &mat4::m_20)
 //       .property("m30", &mat4::m_30)
 //       .property("m01", &mat4::m_01)
 //       .property("m11", &mat4::m_11)
 //       .property("m21", &mat4::m_21)
 //       .property("m31", &mat4::m_31)
 //       .property("m02", &mat4::m_02)
 //       .property("m12", &mat4::m_12)
 //       .property("m22", &mat4::m_22)
 //       .property("m32", &mat4::m_32)
	//;
	//
 //   rttr::registration::class_<quat>("quat")
 //       .property("x", &quat::m_X)
 //       .property("y", &quat::m_Y)
 //       .property("z", &quat::m_Z)
 //       .property("w", &quat::m_W)
 //   ;
}
#endif
