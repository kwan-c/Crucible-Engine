#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"
#include "Resources/ResourceManager.h"

RTTR_REGISTRATION {
    using namespace Crucible;
    rttr::registration::class_<AnimatedModel>("AnimatedModel")
        .constructor<>()
        .property("clip", &AnimatedModel::clip)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("speed", &AnimatedModel::speed)
        .property("loop", &AnimatedModel::loop)
        .property("previewInEditor", &AnimatedModel::previewInEditor)
        .property("playOnStart", &AnimatedModel::playOnStart);
}
