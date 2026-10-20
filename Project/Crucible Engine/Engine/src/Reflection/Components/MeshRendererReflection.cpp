/*************************************************************************
  \file       MeshRendererReflection.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
  \brief
    RTTR registration for MeshRenderer.
  \details
    Saves the asset ids, never the runtime handles. MeshRenderer::mesh and
    MeshRenderer::material are GPU handles that mean nothing after a
    reload; meshAsset and materialAsset are resolved back to handles by
    ResourceManager every frame (see MeshRenderer's doc comment), so they
    are what the file stores and what the Inspector's pickers edit.

    One RTTR_REGISTRATION block per .cpp is legal and is the intended way to
    split registration up: the macro defines a fixed-name struct inside an
    anonymous namespace, so each translation unit gets its own copy. What is
    NOT legal is two blocks in the same .cpp - which is what happens if one
    is ever put in a header. Never put RTTR_REGISTRATION in a header.

    Property names are what the scene file uses as JSON keys. Renaming one
    breaks every scene saved with the old name, so treat them as a file
    format, not as code.
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h, Resources/ResourceManager.h
  \see
    Reflection/MathReflection.cpp, Reflection/Serialization.h, Scene/EntityIO.h
  \history
    - 1.0 (1 October 2026): Split out of the old single registration file
      ReflectionComponentRegistration.cpp, one file per component, following
      the layout of the reference engine.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"
#include "Resources/ResourceManager.h"   // ResourceKind, for ASSET_REF metadata

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<MeshRenderer>("MeshRenderer")
        .constructor<>()
        .property("visible",       &MeshRenderer::visible)
        .property("modelAsset",    &MeshRenderer::modelAsset)
            (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("modelRoot", &MeshRenderer::modelRoot)
            (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("materialOverrides", &MeshRenderer::materialOverrides)
            (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("colour",        &MeshRenderer::colour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("meshAsset",     &MeshRenderer::meshAsset)
            (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("materialAsset", &MeshRenderer::materialAsset)
            (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Material),
             // Picked in the Inspector's Material section instead (with New / Make Unique), so it
             // is not drawn twice. Still saved and loaded like any other property.
             rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
    ;
}
