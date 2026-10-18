/*************************************************************************
  \file       UIComponents.h
  \brief
    The UI components (UICanvas, UIRect, UIImage, UIButton, UIText): the
    structs, their ECS bindings and their RTTR registration, in one file.
  \details
    ECS: ComponentKind.h gives them kinds 17-21 and lists them in
    CRUCIBLE_COMPONENT_LIST; Components.h includes this file, so the
    component registry, the Inspector's Add Component menu and scene
    save / load all pick them up.

    RTTR: the registration is at the bottom of this file, but OFF by
    default. RTTR_REGISTRATION defines a static object that runs at start-
    up, so compiling it into every .cpp that includes this header (which
    is most of the engine, through Components.h) would register every UI
    type dozens of times. Exactly one .cpp switches it on - UIComponents.cpp:

        #define CRU_UI_COMPONENTS_REGISTRATION
        #include "UI/UIComponents.h"

    The block is outside the include guard on purpose, so it still compiles
    in that .cpp even when something it included earlier already pulled
    this header in. (That is also why this file uses an include guard and
    not #pragma once: #pragma once would skip the whole file the second time.)

    Property names are scene-file keys: renaming one breaks saved scenes.
    Runtime state follows CharacterControllerReflection.cpp: UIRect::computed
    and UIButton::state are registered with DO_NOT_SERIALIZE (never saved or
    loaded, and so never in an undo snapshot either - those reuse the same
    writer) plus DISABLED_ON_IMGUI (shown read-only, handy for checking
    layout and hover while playing). UIImage::texture is not registered at
    all: a TextureHandle has no RTTR type and its id means nothing to a
    person, same as MeshRenderer::mesh.

    Conventions (Graphics/UITypes.h): canvas pixels with the origin at the
    top-left and +y down; colours are sRGB, 0-1 per channel
    (PackUIColour(vec4) turns them into UIVertex colours).
*************************************************************************/
#ifndef CRUCIBLE_UI_COMPONENTS_H
#define CRUCIBLE_UI_COMPONENTS_H

// STL
#include <cstdint>
#include <string>

// Crucible engine
#include "ECS/Components/ComponentKind.h"
#include "Graphics/TextureHandle.h"   // no Vulkan in it, like MeshHandle.h
#include "Math/Math.h"

namespace Crucible {

	/*************************************************************************
	 \brief  Root of a UI tree. Its child entities (with UIRect) lay out
			 inside it.
	 *************************************************************************/
	struct UICanvas {
		enum class RenderMode : std::uint8_t {
			ScreenSpace,   // over the game screen, always on top (Unity: Screen Space - Overlay)
			WorldSpace,    // a panel in the 3D world, placed by this entity's Transform (not drawn yet)
		};

		bool       enabled             = true;                           // false: not drawn and ignores the mouse (Unity: Canvas.enabled)
		bool       toggleWithEscape    = false;                          // an in-game menu: hidden when Play starts, Escape shows /
		                                                                 //   hides it and frees / locks the cursor (UIMenuSystem)
		bool       showsCursor         = false;                          // while shown, the cursor is free (a title screen, game over):
		                                                                 //   locked again once no shown canvas needs it
		RenderMode renderMode          = RenderMode::ScreenSpace;
		Math::vec2 referenceResolution = Math::vec2(1920.0f, 1080.0f);   // the page you lay out on (the UI tab's page)
		bool       scaleWithScreenSize = true;                           // scale the canvas to fit the screen
		int        sortOrder           = 0;                              // with several canvases, higher draws on top
	};
	CRUCIBLE_COMPONENT(UICanvas, UICanvas)

	/*************************************************************************
	 \brief  Where a UI element sits, relative to its parent's rect (the
			 canvas, for top-level elements).
	 \note   Every entity also has the engine's mandatory Transform. Screen-
			 space UI ignores it; a world-space canvas will use its own
			 Transform to place itself in the world.
	 *************************************************************************/
	struct UIRect {
		Math::vec2 anchorMin{ 0.5f };              // 0..1 of the parent rect. anchorMin == anchorMax: fixed size;
		Math::vec2 anchorMax{ 0.5f };              //   apart: stretches with the parent
		Math::vec2 pivot{ 0.5f };                  // 0..1 of this rect: the point that `position` places
		Math::vec2 position{ 0.0f };               // pivot's offset from the anchors, in reference pixels
		Math::vec2 size{ 100.0f, 100.0f };         // in reference pixels (extra size when the anchors stretch)
		float      rotation = 0.0f;                // degrees, counter-clockwise (like Unity), around the pivot.
		                                           //   Children turn with their parent.
		bool       raycastTarget = true;           // can it block / receive clicks

		// Runtime only (DO_NOT_SERIALIZE, read-only in the Inspector): filled in by layout every frame.
		Math::vec4 computed{ 0.0f };               // min x, min y, max x, max y in screen pixels: the upright
		                                           //   box around it (rotation included)
	};
	CRUCIBLE_COMPONENT(UIRect, UIRect)

	/*************************************************************************
	 \brief  A coloured and/or textured rectangle filling its UIRect. Also
			 the picture of a button (UIButton sits next to it).
	 *************************************************************************/
	struct UIImage {
		Math::vec4    colour{ 1.0f };      // sRGB, 0-1. Multiplies the texture (or is the colour, with no texture)
		std::uint64_t textureAsset = 0;    // the texture's asset id (.meta GUID); 0 = none, a solid colour. Saved.

		// Runtime only (not registered): resolved from textureAsset with ResourceManager::GetTexture(),
		// the way MeshRenderer::mesh is. A GPU handle means nothing after a reload, so it is never saved.
		TextureHandle texture{};
	};
	CRUCIBLE_COMPONENT(UIImage, UIImage)

	/*************************************************************************
	 \brief  Makes an element clickable. Behaviour only: put it on an entity
			 with a UIImage, which is the button's picture.
	 \details
		When drawn, the current state's tint multiplies UIImage::colour.
		UIImage::colour itself is never changed, so the designer's colour is
		what gets saved. The tints' alpha multiplies too: keep it 1 unless
		the button should fade (disabled does, by default).
	 *************************************************************************/
	struct UIButton {
		/// What a click does by itself, before / besides any UIEvents::ButtonClicked listener.
		enum class ClickAction : std::uint8_t {
			None,        // nothing built in: game code reacts to UIEvents::ButtonClicked
			LoadScene,   // load sceneAsset (a level switch; in the editor, Stop still restores the original scene)
			CloseMenu,   // close the in-game menu (a "Resume" button): UI::SetMenuOpen(false)
		};

		ClickAction   onClick    = ClickAction::None;
		std::uint64_t sceneAsset = 0;          // LoadScene: the scene's asset id (.meta GUID), so renaming the file keeps it

		Math::vec4 normal{ 1.0f };
		Math::vec4 hovered{ 0.9f, 0.9f, 0.9f, 1.0f };
		Math::vec4 pressed{ 0.7f, 0.7f, 0.7f, 1.0f };
		Math::vec4 disabled{ 0.5f, 0.5f, 0.5f, 0.5f };
		bool       interactable = true;

		// Runtime only (DO_NOT_SERIALIZE, read-only in the Inspector): set by the UI input system each frame.
		enum class State : std::uint8_t { Normal, Hovered, Pressed, Disabled };
		State state = State::Normal;
	};
	CRUCIBLE_COMPONENT(UIButton, UIButton)

	/*************************************************************************
	 \brief  Text inside its UIRect.
	 \note   Not drawn yet: waits for the font system (Font / TextLayout).
	 *************************************************************************/
	struct UIText {
		Math::vec4    colour{ 1.0f };      // sRGB, 0-1
		std::string   text;                // UTF-8
		std::uint64_t fontAsset = 0;       // the font's asset id; 0 = the default font. Shown as a raw id until
		                                   //   ResourceManager has a Font kind for the Inspector's picker.
		float         fontSize = 16.0f;    // pixel height, in reference pixels
	};
	CRUCIBLE_COMPONENT(UIText, UIText)

}   // NAMESPACE CRUCIBLE

#endif // CRUCIBLE_UI_COMPONENTS_H


// ------------------------------------------------------------------------------------------------ //
//  RTTR REGISTRATION
//  Compiled only where CRU_UI_COMPONENTS_REGISTRATION is defined: UIComponents.cpp, and nowhere else.
//  See the file header for why.
// ------------------------------------------------------------------------------------------------ //
#if defined(CRU_UI_COMPONENTS_REGISTRATION) && !defined(CRU_UI_COMPONENTS_REGISTERED)
#define CRU_UI_COMPONENTS_REGISTERED

#include "Reflection/ReflectionRegistration.h"
#include "Resources/ResourceManager.h"   // ResourceKind, for the texture picker (ASSET_REF)

RTTR_REGISTRATION
{
	using namespace Crucible;

	// By name, so a scene file says "WorldSpace" rather than 1 and reordering the enum is safe.
	rttr::registration::enumeration<UICanvas::RenderMode>("UIRenderMode")
	(
		rttr::value("ScreenSpace", UICanvas::RenderMode::ScreenSpace),
		rttr::value("WorldSpace",  UICanvas::RenderMode::WorldSpace)
	);

	rttr::registration::class_<UICanvas>("UICanvas")
		.constructor<>()
		.property("enabled",             &UICanvas::enabled)
		.property("toggleWithEscape",    &UICanvas::toggleWithEscape)
		.property("showsCursor",         &UICanvas::showsCursor)
		.property("renderMode",          &UICanvas::renderMode)
		.property("referenceResolution", &UICanvas::referenceResolution)
		.property("scaleWithScreenSize", &UICanvas::scaleWithScreenSize)
		.property("sortOrder",           &UICanvas::sortOrder)
	;

	rttr::registration::class_<UIRect>("UIRect")
		.constructor<>()
		.property("anchorMin",     &UIRect::anchorMin)
		.property("anchorMax",     &UIRect::anchorMax)
		.property("pivot",         &UIRect::pivot)
		.property("position",      &UIRect::position)
		.property("size",          &UIRect::size)
		.property("rotation",      &UIRect::rotation)
		.property("raycastTarget", &UIRect::raycastTarget)
		.property("computed",      &UIRect::computed)
			(rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
			 rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true))
	;

	rttr::registration::class_<UIImage>("UIImage")
		.constructor<>()
		.property("colour",       &UIImage::colour)
			(rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("textureAsset", &UIImage::textureAsset)
			(rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Texture))
	;

	rttr::registration::enumeration<UIButton::State>("UIButtonState")
	(
		rttr::value("Normal",   UIButton::State::Normal),
		rttr::value("Hovered",  UIButton::State::Hovered),
		rttr::value("Pressed",  UIButton::State::Pressed),
		rttr::value("Disabled", UIButton::State::Disabled)
	);

	rttr::registration::enumeration<UIButton::ClickAction>("UIClickAction")
	(
		rttr::value("None",      UIButton::ClickAction::None),
		rttr::value("LoadScene", UIButton::ClickAction::LoadScene),
		rttr::value("CloseMenu", UIButton::ClickAction::CloseMenu)
	);

	rttr::registration::class_<UIButton>("UIButton")
		.constructor<>()
		.property("onClick",      &UIButton::onClick)
		.property("sceneAsset",   &UIButton::sceneAsset)
			// Drawn as a scene dropdown by the Inspector (DrawComponentExtras(UIButton&)): a raw
			// id means nothing to a person. Still saved like any other property.
			(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("normal",       &UIButton::normal)
			(rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("hovered",      &UIButton::hovered)
			(rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("pressed",      &UIButton::pressed)
			(rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("disabled",     &UIButton::disabled)
			(rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("interactable", &UIButton::interactable)
		.property("state",        &UIButton::state)
			(rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
			 rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true))
	;

	rttr::registration::class_<UIText>("UIText")
		.constructor<>()
		.property("colour",    &UIText::colour)
			(rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("text",      &UIText::text)
		.property("fontAsset", &UIText::fontAsset)
		.property("fontSize",  &UIText::fontSize)
	;
}

#endif // CRU_UI_COMPONENTS_REGISTRATION
