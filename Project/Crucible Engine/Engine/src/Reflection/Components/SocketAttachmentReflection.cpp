#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"

RTTR_REGISTRATION {
    using namespace Crucible;
    rttr::registration::class_<SocketAttachment>("SocketAttachment")
        .constructor<>()
        .property("enabled", &SocketAttachment::enabled)
        .property("socketName", &SocketAttachment::socketName)
        .property("modelAsset", &SocketAttachment::modelAsset)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("node", &SocketAttachment::node)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true));
}
