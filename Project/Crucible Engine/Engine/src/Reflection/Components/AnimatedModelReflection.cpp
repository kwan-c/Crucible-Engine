#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"
#include "Resources/ResourceManager.h"

RTTR_REGISTRATION {
    using namespace Crucible;
    rttr::registration::class_<AnimatedModel>("AnimatedModel")
        .constructor<>()
        .property("modelAsset", &AnimatedModel::modelAsset)
            (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Model))
        .property("clip", &AnimatedModel::clip)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("speed", &AnimatedModel::speed)
        .property("loop", &AnimatedModel::loop)
        .property("previewInEditor", &AnimatedModel::previewInEditor)
        .property("visible", &AnimatedModel::visible)
        .property("playing", &AnimatedModel::playing)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("time", &AnimatedModel::time)
            (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true), rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true));
}
