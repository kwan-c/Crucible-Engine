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
	COLOUR,

	/*!
	* @brief    This uint64 is an asset id (ResourceManager AssetId). The Inspector
	*			draws it as a picker of that kind of asset (combo + drag-drop
	*			target) instead of a raw hex field. Serialization is unchanged:
	*			the id is still saved as a plain number.
	*
	* @values	a Crucible::ResourceKind - which assets the picker lists
	*
	* @usage	.property("meshAsset", &MeshRenderer::meshAsset)
				(
					rttr::metadata(MetaDataTypes::ASSET_REF, Crucible::ResourceKind::Mesh)
				)
	*/
	ASSET_REF,

	/*!
	* @brief    This uint64 is another entity's scene id (Scene::IdOf). The
	*			Inspector draws it as "Entity N" with a drop target for an
	*			entity dragged from the Hierarchy, and a button to clear it,
	*			instead of a raw hex field. Serialization is unchanged: the
	*			id is saved as a plain number, and stays valid across reloads
	*			because scene ids do.
	*
	* @values	true
	*
	* @usage	.property("connectedBody", &PhysicsJoint::m_connectedBody)
				(
					rttr::metadata(MetaDataTypes::ENTITY_REF, true)
				)
	*/
	ENTITY_REF,

	/*!
	* @brief    Read from files but never written: a key an older file format
	*			used, kept so those files still load (the setter moves the
	*			value to where it lives now). Saving writes only the current
	*			format, so a file is upgraded the next time it is saved.
	*
	* @values	true
	*
	* @usage	.property("class", &ScriptComponent::LegacyClass, &ScriptComponent::SetLegacyClass)
				(
					rttr::metadata(MetaDataTypes::LOAD_ONLY, true)
				)
	*/
	LOAD_ONLY
};