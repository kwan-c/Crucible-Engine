#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"

RTTR_REGISTRATION {
    using namespace Crucible;
    rttr::registration::enumeration<ScriptFieldType>("ScriptFieldType")(
        rttr::value("Bool", ScriptFieldType::Boolean), rttr::value("Int", ScriptFieldType::Int32),
        rttr::value("Float", ScriptFieldType::Single), rttr::value("Double", ScriptFieldType::Double),
        rttr::value("String", ScriptFieldType::String), rttr::value("Entity", ScriptFieldType::Entity),
        rttr::value("Prefab", ScriptFieldType::Prefab),
        rttr::value("AudioClip", ScriptFieldType::AudioClip));
    rttr::registration::class_<ScriptFieldValue>("ScriptFieldValue")
        .constructor<>()
        .property("declaringType", &ScriptFieldValue::declaringType)
        .property("name", &ScriptFieldValue::name).property("type", &ScriptFieldValue::type)
        .property("boolean", &ScriptFieldValue::boolean).property("integer", &ScriptFieldValue::integer)
        .property("single", &ScriptFieldValue::single).property("real", &ScriptFieldValue::real)
        .property("text", &ScriptFieldValue::text).property("stringIsNull", &ScriptFieldValue::stringIsNull)
        .property("entityId", &ScriptFieldValue::entityId)  // missing in older scenes - stays 0
        .property("assetId", &ScriptFieldValue::assetId);
    rttr::registration::class_<ScriptComponent>("ScriptComponent")
        .constructor<>()
        .property("enabled", &ScriptComponent::enabled)
        .property("assembly", &ScriptComponent::assembly)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("namespace", &ScriptComponent::nameSpace)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("class", &ScriptComponent::className)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("fields", &ScriptComponent::fields)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true));
}
