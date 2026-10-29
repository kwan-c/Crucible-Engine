/*************************************************************************
  \file       UIComponents.h
  \brief
    The UI components (UICanvas, UIRect, UIImage, UIButton, UIText, UISlider): the
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
#include "Text/TextLayout.h"       // TextAlignH / TextAlignV / TextOverflow (no ECS or GPU in it)
#include "Text/WorldText.h"        // WorldBillboard (a world-space canvas turns like a WorldText)

namespace Crucible {

	/*************************************************************************
	 \brief  Root of a UI tree. Its child entities (with UIRect) lay out
			 inside it.
	 \details
		ScreenSpace: over the game screen, scaled to it, clickable.
		WorldSpace: a flat page in the 3D world - a sign, a nameplate, a
		health bar over an enemy. Its Transform places it, pixelsPerUnit
		sizes it, and it is drawn with the scene (fogged, depth-tested,
		optionally turned to the camera), like a WorldText. Display only:
		buttons and sliders on it don't react to the mouse; code sets a
		slider's value (a progress or health bar).
	 *************************************************************************/
	struct UICanvas {
		enum class RenderMode : std::uint8_t {
			ScreenSpace,   // over the game screen, always on top (Unity: Screen Space - Overlay)
			WorldSpace,    // a panel in the 3D world, placed by this entity's Transform (Unity: World Space)
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

		// ---- WorldSpace only. The page is referenceResolution canvas pixels, laid out as on screen
		// (anchors, pivots, sibling order) but never scaled to a screen; this entity's Transform puts
		// it in the world, the same way as a WorldText's box. Nothing on it reacts to the mouse.
		float          pixelsPerUnit = 100.0f;                           // canvas pixels per metre: a 400 x 100 page is 4 x 1 m
		Math::vec2     pivot{ 0.5f, 0.5f };                              // the page's point on this entity; (0, 0) = top-left, as UIRect
		WorldBillboard billboard     = WorldBillboard::Off;              // Off: the Transform's rotation; or turn to face the camera
		bool           alwaysOnTop   = false;                            // drawn over everything, not hidden behind walls
		bool           doubleSided   = true;                             // also drawn (mirrored) when seen from behind
		float          intensity     = 1.0f;                             // brightness in the scene: > 1 glows with bloom
		bool           fade          = false;                            // fade out with distance from the camera
		float          fadeStart     = 20.0f;                            // world units: fully visible up to here
		float          fadeEnd       = 30.0f;                            // gone from here
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
		Math::vec2 position{ 0.0f };               // pivot's offset from the anchors, in reference pixels. +y is DOWN
		                                           //   (screen pixels); the Inspector shows it +y up by default (display only)
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
			Quit,        // quit the game: sends UIEvents::QuitRequested. The game closes its window;
			             //   the editor stops Play mode instead (like Unity's Application.Quit).
			Custom,      // sends UIEvents::CustomAction with actionId, for game code (GameApp's action
			             //   table, later scripts) to run - "OpenSettings", "StartCoOp", ...
		};

		ClickAction   onClick    = ClickAction::None;
		std::uint64_t sceneAsset = 0;          // LoadScene: the scene's asset id (.meta GUID), so renaming the file keeps it
		std::string   actionId;                // Custom: the action's name (case-sensitive)

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
	 \brief  Text inside its UIRect, laid out by Text/TextLayout.h and drawn
			 by UISystem (after the element's UIImage, so it sits on top).
	 \details
		- fontAsset: a .cfont, or a .ttf/.otf (its compiled .cfont is used).
		  0, or a font that isn't compiled: the default font,
		  ResourceManager::m_defaultFontPath.
		- fontSize: the em size in reference pixels (it scales with the
		  canvas like everything else). Bitmap fonts are crisp only at their
		  baked sizes - see the Font Importer.
		- The rect is the text box: wrapping, alignment and overflow are
		  against its size.
		- richText: Unity's <b>, <u> and <color=#RRGGBB / name> tags (see
		  Text/TextLayout.h). Off: '<' is just a character.
		- bold / underline: the whole text (<b> / <u> do a part).
		- bestFit: fontSize is ignored; the largest size between minFontSize
		  and maxFontSize at which all the text fits the rect is used.
		- Effects (outline, glow, shadow), sizes in reference pixels like
		  fontSize. Outline and glow need an SDF / MTSDF font and can only
		  reach as far as its distance field (UI::MaxTextEffectWidth - raise
		  the font's Pixel Range for more); wider is clamped. Shadow works
		  with every font (a bitmap one gets a flat offset copy, no blur).
	 *************************************************************************/
	struct UIText {
		Math::vec4    colour{ 1.0f };      // sRGB, 0-1
		std::string   text;                // UTF-8; "\n" starts a new line
		std::uint64_t fontAsset = 0;       // a compiled font (.cfont) asset id; 0 = the default font
		float         fontSize = 16.0f;    // em size, in reference pixels

		TextAlignH    horizontalAlign  = TextAlignH::Left;
		TextAlignV    verticalAlign    = TextAlignV::Top;
		bool          wrap             = true;                    // break lines at spaces to stay inside the rect's width
		TextOverflow  overflow         = TextOverflow::Overflow;  // Overflow: draw past the rect; Truncate: cut at its edges; Ellipsis: cut with "..."
		float         lineSpacing      = 1.0f;                    // times the font's line height
		float         characterSpacing = 0.0f;                    // extra space after each character, in ems

		bool          richText         = true;                    // read <b> <u> <color> tags (Unity's default)
		bool          bold             = false;                   // bold everything (synthetic bold, any font)
		bool          underline        = false;                   // underline everything
		bool          bestFit          = false;                   // size the text to fit the rect (fontSize ignored)
		float         minFontSize      = 10.0f;                   // best fit's smallest size, reference pixels
		float         maxFontSize      = 40.0f;                   // best fit's largest size, reference pixels

		bool          outline          = false;
		Math::vec4    outlineColour{ 0.0f, 0.0f, 0.0f, 1.0f };    // sRGB, 0-1
		float         outlineWidth     = 2.0f;                    // reference pixels outside the letters' edge

		bool          glow             = false;
		Math::vec4    glowColour{ 1.0f, 0.85f, 0.4f, 0.8f };      // sRGB, 0-1
		float         glowWidth        = 3.0f;                    // reference pixels outside the outline (or the letters)
		float         glowSoftness     = 0.5f;                    // 0 = an even band, 1 = fades quickly

		bool          shadow           = false;
		Math::vec4    shadowColour{ 0.0f, 0.0f, 0.0f, 0.6f };     // sRGB, 0-1
		Math::vec2    shadowOffset{ 2.0f, 2.0f };                 // reference pixels, +y down
		float         shadowSoftness   = 0.0f;                    // 0 = crisp, 1 = as blurred as the font allows
	};
	CRUCIBLE_COMPONENT(UIText, UIText)

	/*************************************************************************
	 \brief  A draggable value (volume, sensitivity, brightness...). Draws
			 itself inside its UIRect: a track, the filled part up to the
			 value, and a handle - no child entities needed.
	 \details
		Press anywhere on it to jump there, drag to change it (the drag keeps
		going outside the slider until release). Each change during a drag
		sends UIEvents::SliderChanged. Code can set value directly; it is
		clamped (and rounded with wholeNumbers) when drawn and dragged.
	 *************************************************************************/
	struct UISlider {
		enum class Direction : std::uint8_t {
			LeftToRight,   // min at the left
			RightToLeft,
			BottomToTop,   // min at the bottom: a vertical slider
			TopToBottom,
		};

		float      minValue     = 0.0f;
		float      maxValue     = 1.0f;
		float      value        = 0.5f;
		bool       wholeNumbers = false;           // value snaps to integers (a 0-10 setting)
		bool       interactable = true;
		Direction  direction    = Direction::LeftToRight;

		Math::vec4 trackColour  { 0.25f, 0.25f, 0.28f, 1.0f };   // sRGB, 0-1
		Math::vec4 fillColour   { 0.30f, 0.60f, 1.00f, 1.0f };
		Math::vec4 handleColour { 1.0f };
		float      handleSize     = 20.0f;         // the handle's length along the slider, reference pixels
		float      trackThickness = 0.4f;          // the track's share of the slider's height (width, if vertical): 0-1

		// Runtime only (DO_NOT_SERIALIZE, read-only in the Inspector): set by the UI input system.
		// Hovered / Pressed (dragging) tint the handle a little darker, like a button.
		UIButton::State state = UIButton::State::Normal;
	};
	CRUCIBLE_COMPONENT(UISlider, UISlider)

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
		.property("renderMode",          &UICanvas::renderMode)
		.property("referenceResolution", &UICanvas::referenceResolution)
		// Screen Space and World Space settings: drawn by the Inspector (DrawComponentExtras(UICanvas&))
		// in two groups, the one the render mode doesn't use greyed out.
		.property("toggleWithEscape",    &UICanvas::toggleWithEscape)    (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("showsCursor",         &UICanvas::showsCursor)         (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("scaleWithScreenSize", &UICanvas::scaleWithScreenSize) (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("sortOrder",           &UICanvas::sortOrder)           (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("pixelsPerUnit",       &UICanvas::pixelsPerUnit)       (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("pivot",               &UICanvas::pivot)               (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("billboard",           &UICanvas::billboard)           (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // WorldBillboard: registered in WorldTextReflection.cpp
		.property("alwaysOnTop",         &UICanvas::alwaysOnTop)         (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("doubleSided",         &UICanvas::doubleSided)         (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("intensity",           &UICanvas::intensity)           (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("fade",                &UICanvas::fade)                (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("fadeStart",           &UICanvas::fadeStart)           (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("fadeEnd",             &UICanvas::fadeEnd)             (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
	;

	rttr::registration::class_<UIRect>("UIRect")
		.constructor<>()
		.property("anchorMin",     &UIRect::anchorMin)
		.property("anchorMax",     &UIRect::anchorMax)
		.property("pivot",         &UIRect::pivot)
		// position .. computed: drawn by the Inspector (DrawComponentExtras(UIRect&)) in this order,
		// so position's y can be shown +y up (display only) without changing what is saved.
		.property("position",      &UIRect::position)      (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("size",          &UIRect::size)          (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("rotation",      &UIRect::rotation)      (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("raycastTarget", &UIRect::raycastTarget) (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("computed",      &UIRect::computed)
			(rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
			 rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
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
		rttr::value("CloseMenu", UIButton::ClickAction::CloseMenu),
		rttr::value("Quit",      UIButton::ClickAction::Quit),
		rttr::value("Custom",    UIButton::ClickAction::Custom)
	);

	rttr::registration::class_<UIButton>("UIButton")
		.constructor<>()
		.property("onClick",      &UIButton::onClick)
		.property("sceneAsset",   &UIButton::sceneAsset)
			// Drawn as a scene dropdown by the Inspector (DrawComponentExtras(UIButton&)): a raw
			// id means nothing to a person. Still saved like any other property.
			(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("actionId",     &UIButton::actionId)
			// Drawn by DrawComponentExtras(UIButton&), only when onClick is Custom.
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

	rttr::registration::enumeration<UISlider::Direction>("UISliderDirection")
	(
		rttr::value("LeftToRight", UISlider::Direction::LeftToRight),
		rttr::value("RightToLeft", UISlider::Direction::RightToLeft),
		rttr::value("BottomToTop", UISlider::Direction::BottomToTop),
		rttr::value("TopToBottom", UISlider::Direction::TopToBottom)
	);

	rttr::registration::class_<UISlider>("UISlider")
		.constructor<>()
		.property("minValue",       &UISlider::minValue)
		.property("maxValue",       &UISlider::maxValue)
		.property("value",          &UISlider::value)
		.property("wholeNumbers",   &UISlider::wholeNumbers)
		.property("interactable",   &UISlider::interactable)
		.property("direction",      &UISlider::direction)
			// Drawn by the Inspector (DrawComponentExtras(UISlider&)): switching between horizontal
			// and vertical there also swaps the UIRect's width and height, like Unity's Slider.
			(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("trackColour",    &UISlider::trackColour)
			(rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("fillColour",     &UISlider::fillColour)
			(rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("handleColour",   &UISlider::handleColour)
			(rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("handleSize",     &UISlider::handleSize)
		.property("trackThickness", &UISlider::trackThickness)
		.property("state",          &UISlider::state)
			(rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
			 rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true))
	;

	rttr::registration::enumeration<TextAlignH>("UITextAlignH")
	(
		rttr::value("Left",   TextAlignH::Left),
		rttr::value("Center", TextAlignH::Center),
		rttr::value("Right",  TextAlignH::Right)
	);
	rttr::registration::enumeration<TextAlignV>("UITextAlignV")
	(
		rttr::value("Top",    TextAlignV::Top),
		rttr::value("Middle", TextAlignV::Middle),
		rttr::value("Bottom", TextAlignV::Bottom)
	);
	rttr::registration::enumeration<TextOverflow>("UITextOverflow")
	(
		rttr::value("Overflow", TextOverflow::Overflow),
		rttr::value("Truncate", TextOverflow::Truncate),
		rttr::value("Ellipsis", TextOverflow::Ellipsis)
	);

	rttr::registration::class_<UIText>("UIText")
		.constructor<>()
		.property("text",      &UIText::text)
			// Drawn by the Inspector (DrawComponentExtras(UIText&)) as a multi-line box.
			(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("colour",    &UIText::colour)
			(rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("fontAsset", &UIText::fontAsset)
			(rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Font))
		.property("fontSize",  &UIText::fontSize)
		.property("horizontalAlign", &UIText::horizontalAlign)
			// Both alignments are drawn by DrawComponentExtras(UIText&) as one 3 x 3 grid, like Unity.
			(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("verticalAlign",   &UIText::verticalAlign)
			(rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("wrap",             &UIText::wrap)
		.property("overflow",         &UIText::overflow)
		.property("lineSpacing",      &UIText::lineSpacing)
		.property("characterSpacing", &UIText::characterSpacing)
		.property("richText",         &UIText::richText)
		.property("bold",             &UIText::bold)
		.property("underline",        &UIText::underline)
		.property("bestFit",          &UIText::bestFit)
		.property("minFontSize",      &UIText::minFontSize)
		.property("maxFontSize",      &UIText::maxFontSize)
		// Effects: drawn by DrawComponentExtras(UIText&) in an "Effects" group, each with its checkbox,
		// the sizes greyed out while it is off, and the font's maximum width shown.
		.property("outline",          &UIText::outline)        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("outlineColour",    &UIText::outlineColour)  (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true), rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("outlineWidth",     &UIText::outlineWidth)   (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("glow",             &UIText::glow)           (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("glowColour",       &UIText::glowColour)     (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true), rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("glowWidth",        &UIText::glowWidth)      (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("glowSoftness",     &UIText::glowSoftness)   (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("shadow",           &UIText::shadow)         (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("shadowColour",     &UIText::shadowColour)   (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true), rttr::metadata(MetaDataTypes::COLOUR, true))
		.property("shadowOffset",     &UIText::shadowOffset)   (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
		.property("shadowSoftness",   &UIText::shadowSoftness) (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))
	;
}

#endif // CRU_UI_COMPONENTS_REGISTRATION
