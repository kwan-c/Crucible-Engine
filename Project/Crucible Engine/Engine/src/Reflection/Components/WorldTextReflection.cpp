/*************************************************************************
  \file       WorldTextReflection.cpp
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    RTTR registration for WorldText (Text/WorldText.h).
  \details
    Every field is a setting someone types in, so every field is saved and
    editable; nothing is runtime state. The text, the alignments, the
    distance fade and the effects are NOT_ON_IMGUI: the Inspector draws
    them itself (DrawComponentExtras(WorldText&)), as it does for UIText. The alignment and overflow enums are
    already registered by UIComponents.h (UITextAlignH / UITextAlignV /
    UITextOverflow) and are shared, not registered again.

    Property names are scene-file keys: renaming one breaks saved scenes.
    The text, layout and effect names match UIText's on purpose.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "Resources/ResourceManager.h"   // ResourceKind, for the font picker (ASSET_REF)
#include "Text/WorldText.h"

RTTR_REGISTRATION
{
	using namespace Crucible;

	rttr::registration::enumeration<WorldText::Billboard>("WorldTextBillboard")
	(
		rttr::value("Off",         WorldText::Billboard::Off),
		rttr::value("Face Camera", WorldText::Billboard::FaceCamera),
		rttr::value("Upright",     WorldText::Billboard::Upright)
	);

	rttr::registration::class_<WorldText>("WorldText")
		.constructor<>()
		.property("text",             &WorldText::text)            (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // the Inspector's multi-line box
		.property("colour",           &WorldText::colour)          (rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("fontAsset",        &WorldText::fontAsset)       (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Font))
		.property("fontSize",         &WorldText::fontSize)

		.property("size",             &WorldText::size)
		.property("pivot",            &WorldText::pivot)
		.property("billboard",        &WorldText::billboard)
		.property("alwaysOnTop",      &WorldText::alwaysOnTop)
		.property("doubleSided",      &WorldText::doubleSided)
		.property("intensity",        &WorldText::intensity)
		.property("fade",             &WorldText::fade)            (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // the Inspector's Distance Fade group
		.property("fadeStart",        &WorldText::fadeStart)       (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("fadeEnd",          &WorldText::fadeEnd)         (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))

		.property("horizontalAlign",  &WorldText::horizontalAlign) (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // the Inspector's 3 x 3 grid
		.property("verticalAlign",    &WorldText::verticalAlign)   (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("wrap",             &WorldText::wrap)
		.property("overflow",         &WorldText::overflow)
		.property("lineSpacing",      &WorldText::lineSpacing)
		.property("characterSpacing", &WorldText::characterSpacing)
		.property("richText",         &WorldText::richText)
		.property("bold",             &WorldText::bold)
		.property("underline",        &WorldText::underline)
		.property("bestFit",          &WorldText::bestFit)
		.property("minFontSize",      &WorldText::minFontSize)
		.property("maxFontSize",      &WorldText::maxFontSize)

		.property("outline",          &WorldText::outline)         (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // effects: the Inspector's Effects group
		.property("outlineColour",    &WorldText::outlineColour)   (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true), rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("outlineWidth",     &WorldText::outlineWidth)    (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("glow",             &WorldText::glow)            (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("glowColour",       &WorldText::glowColour)      (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true), rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("glowWidth",        &WorldText::glowWidth)       (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("glowSoftness",     &WorldText::glowSoftness)    (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("shadow",           &WorldText::shadow)          (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("shadowColour",     &WorldText::shadowColour)    (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true), rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("shadowOffset",     &WorldText::shadowOffset)    (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("shadowSoftness",   &WorldText::shadowSoftness)  (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
	;
}
