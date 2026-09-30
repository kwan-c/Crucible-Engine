#pragma once

/*!
* @file			ReflectionRegistration.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		This file wraps registration for RTTR.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include <rttr/registration.h>
#include <rttr/registration_friend.h>
#include <rttr/rttr_enable.h>
#include <rttr/type.h>

namespace Crucible {

}

enum class MetaDataTypes {

	/*!
	* @brief    This metadata type lets the serialization know that this
	*			value should NOT be saved.
	* 
	* @values	true,  Data is not serialized
	*			false, Data is serialized
	* 
	* @usage	.property("m_resourcePath", &Renderer::m_resourcePath)
	*			(
	*				rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true)
	*			)
	*/
	DO_NOT_SERIALIZE,

	/*!
	* @brief    This metadata type lets the Editor know to not display this
	*			value on the ImGui
	*
	* @values	true,  Data is not displayed
	*			false, Data is displayed
	*
	* @usage	.property("m_Type", &System::m_Type)
				(
					rttr::metadata(MetaDataTypes::DO_NOT_SHOW_ON_IMGUI, true)
				)
	*/
	NOT_ON_IMGUI,

	/*!
	* @brief    This metadata type lets the Editor know to display on ImGui
	*			but is disabled.
	*
	* @values	true,  Data is not editable
	*			false, Data is editable
	*
	* @usage	.property("m_Type", &System::m_Type)
				(
					rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true)
				)
	*/
	DISABLED_ON_IMGUI,

	/*!
	* @brief    This metadata type lets the Editor know that this value
	*			is modifiable with a drag and drop field.
	*
	* @values	true,  Data is editable with drag and drop field
	*			false, Data is editable without a drag and drop field
	*
	* @usage	.property("m_Type", &System::m_Type)
				(
					rttr::metadata(MetaDataTypes::DRAG_AND_DROP, true)
				)
	*/
	DRAG_AND_DROP,

	/*!
	* @brief    This metadata type lets the Editor know that this value
	*			is modifiable with a slider field.
	*
	* @values	value does not matter
	*
	* @usage	.property("volume", &Audioerer::volume)
				(
					rttr::metadata(MetaDataTypes::SLIDER, glm::vec2(0.f, 1.f))
				)
	*/
	SLIDER,

	/*!
	* @brief    This metadata type lets the Editor know that this value
	*			is modifiable with a combobox field.
	*
	* @values	value input thyself
	*
	* @usage	std::vector<std::string> Audioerer::m_AllAudioClipPaths
	* 
				.property("audioClipPath", &Audioerer::audioClipPath)
				(
					rttr::metadata(MetaDataTypes::COMBOBOX, &Audioerer::m_AllAudioClipPaths)
				)
	*/
	COMBOBOX,

	/*!
	* @brief    Draw this vec3/vec4 with a colour picker instead of drag fields.
	*/
	COLOUR
};