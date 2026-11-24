/*************************************************************************
* \file:         ScriptComponentLookup.h
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief         Does an entity have the native component behind a C#
*                component class ("Transform", "RigidBody", ...)?
*
* \details
*    Used by the Inspector for script component fields
*    (`public RigidBody body;`): only an entity that has the component can
*    be dropped onto the field. Works in Edit mode and Play (no Mono
*    needed), unlike the C# Entity.HasComponent binding.
*
*    The names are kScriptComponentTypes (Scripting/ScriptTypes.h). The
*    C# name and the native component differ for two of them:
*    Animator -> AnimatedModel, Camera -> CameraComponent - the same
*    pairing as ManagedEntityHasComponent in Bindings/EntityBindings.cpp.
*
**************************************************************************/
#pragma once
#include <string_view>
#include "ECS/Core/ECSCore.h"   // Entity
#include "EngineExport.h"

namespace Crucible
{
	class World;

	/*********************************************************************
	 \brief  true if `entity` is alive and has the component that the C#
	         class Crucible.<type> wraps.
	 \param  type  a name from kScriptComponentTypes, e.g. "RigidBody";
	               any other name gives false.
	 *********************************************************************/
	ENGINE_API bool HasScriptComponent(const World& world, Entity entity, std::string_view type);
}
