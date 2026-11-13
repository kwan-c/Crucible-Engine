#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"

RTTR_REGISTRATION {
    using namespace Crucible;
    rttr::registration::enumeration<ScriptFieldType>("ScriptFieldType")(
        rttr::value("Bool", ScriptFieldType::Boolean), rttr::value("Int", ScriptFieldType::Int32),
        rttr::value("Float", ScriptFieldType::Single), rttr::value("Double", ScriptFieldType::Double),
        rttr::value("String", ScriptFieldType::String), rttr::value("Entity", ScriptFieldType::Entity),
        rttr::value("Prefab", ScriptFieldType::Prefab),
        rttr::value("AudioClip", ScriptFieldType::AudioClip),
        rttr::value("Texture", ScriptFieldType::Texture), rttr::value("Font", ScriptFieldType::Font),
        rttr::value("Material", ScriptFieldType::Material), rttr::value("Model", ScriptFieldType::Model));
    rttr::registration::class_<ScriptFieldValue>("ScriptFieldValue")
        .constructor<>()
        .property("declaringType", &ScriptFieldValue::declaringType)
        .property("name", &ScriptFieldValue::name).property("type", &ScriptFieldValue::type)
        .property("boolean", &ScriptFieldValue::boolean).property("integer", &ScriptFieldValue::integer)
        .property("single", &ScriptFieldValue::single).property("real", &ScriptFieldValue::real)
        .property("text", &ScriptFieldValue::text).property("stringIsNull", &ScriptFieldValue::stringIsNull)
        .property("entityId", &ScriptFieldValue::entityId)  // missing in older scenes - stays 0
        .property("assetId", &ScriptFieldValue::assetId);

    // One script. The same keys the single-script ScriptComponent used to have.
    rttr::registration::class_<ScriptEntry>("ScriptEntry")
        .constructor<>()
        .property("enabled", &ScriptEntry::enabled)
        .property("assembly", &ScriptEntry::assembly)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("namespace", &ScriptEntry::nameSpace)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("class", &ScriptEntry::className)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("fields", &ScriptEntry::fields)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true));

    // Every script on the entity: { "scripts": [ {...}, {...} ] }. The Inspector
    // draws scripts itself (Inspector::DrawComponentExtras), so nothing here is
    // drawn by the generic property view.
    //
    // The LOAD_ONLY properties read files saved before multiple scripts, which
    // held one script's keys at the top level, into scripts[0]. They are never
    // written, so such a file is upgraded the next time it is saved.
    rttr::registration::class_<ScriptComponent>("ScriptComponent")
        .constructor<>()
        .property("scripts", &ScriptComponent::scripts)(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("enabled", &ScriptComponent::LegacyEnabled, &ScriptComponent::SetLegacyEnabled)(
            rttr::metadata(MetaDataTypes::LOAD_ONLY, true), rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("assembly", &ScriptComponent::LegacyAssembly, &ScriptComponent::SetLegacyAssembly)(
            rttr::metadata(MetaDataTypes::LOAD_ONLY, true), rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("namespace", &ScriptComponent::LegacyNamespace, &ScriptComponent::SetLegacyNamespace)(
            rttr::metadata(MetaDataTypes::LOAD_ONLY, true), rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("class", &ScriptComponent::LegacyClass, &ScriptComponent::SetLegacyClass)(
            rttr::metadata(MetaDataTypes::LOAD_ONLY, true), rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
        .property("fields", &ScriptComponent::LegacyFields, &ScriptComponent::SetLegacyFields)(
            rttr::metadata(MetaDataTypes::LOAD_ONLY, true), rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true));
}
