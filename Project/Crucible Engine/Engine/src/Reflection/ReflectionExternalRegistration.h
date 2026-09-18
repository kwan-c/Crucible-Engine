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

RTTR_REGISTRATION
{
	using namespace Crucible::Math;

    // -----------------
    // Registration for Math Library
    // -----------------

	rttr::registration::class_<vec2>("vec2")
		.property("x", &vec2::m_X)
		.property("y", &vec2::m_Y)
	;
	
	rttr::registration::class_<vec3>("vec3")
		.property("x", &vec3::m_X)
		.property("y", &vec3::m_Y)
		.property("z", &vec3::m_Z)
	;
	
	rttr::registration::class_<vec4>("vec4")
		.property("w", &vec3::m_W)
		.property("x", &vec3::m_X)
		.property("y", &vec3::m_Y)
		.property("z", &vec3::m_Z)
	;
	
    rttr::registration::class_<matrix3>("matrix3")
        .property("m00", &matrix3::m_00)
        .property("m10", &matrix3::m_10)
        .property("m20", &matrix3::m_20)
        .property("m01", &matrix3::m_01)
        .property("m11", &matrix3::m_11)
        .property("m21", &matrix3::m_21)
        .property("m02", &matrix3::m_02)
        .property("m12", &matrix3::m_12)
        .property("m22", &matrix3::m_22)
    ;
	
	rttr::registration::class_<matrix4>("matrix4")
        .property("m00", &matrix4::m_00)
        .property("m10", &matrix4::m_10)
        .property("m20", &matrix4::m_20)
        .property("m30", &matrix4::m_30)
        .property("m01", &matrix4::m_01)
        .property("m11", &matrix4::m_11)
        .property("m21", &matrix4::m_21)
        .property("m31", &matrix4::m_31)
        .property("m02", &matrix4::m_02)
        .property("m12", &matrix4::m_12)
        .property("m22", &matrix4::m_22)
        .property("m32", &matrix4::m_32)
	;
	
    rttr::registration::class_<quaternion>("quaternion")
        .property("x", &quaternion::m_X)
        .property("y", &quaternion::m_Y)
        .property("z", &quaternion::m_Z)
        .property("w", &quaternion::m_W)
    ;
}
