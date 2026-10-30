#pragma once
/*************************************************************************
  \file       WorldText.h
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    WorldText: text placed in the 3D world by its entity's Transform - signs,
    labels, nameplates, damage numbers. Unity's TextMeshPro (3D) / TextMesh.
  \details
    The same text, layout and effects as UIText (UI/UIComponents.h), so the
    field names match it and Text/TextBuilder.h's TextLayoutSettingsOf /
    TextStyleOf work on both. The differences:

    Where it is
      - The text sits in a box `size` wide and tall, in the entity's local XY
        plane: +X to the right, +Y up, read from the +Z side (the side the
        default camera, looking down -Z, sees). `pivot` is the point of the
        box on the entity's position: (0, 0) its top-left corner, (1, 1) its
        bottom-right, (0.5, 0.5) its centre - the same as UIRect::pivot.
      - The Transform's rotation turns it and its scale scales it, children
        follow parents, like any mesh.
      - billboard turns it to face the camera instead (every camera sees it
        facing them - the Scene and Game views at once). Upright only turns
        it around Y, so a nameplate never tips back when seen from above.

    Units
      - fontSize, size and every effect width are WORLD UNITS (metres), before
        the Transform's scale. fontSize is the em: a capital letter is roughly
        0.7 of it, so fontSize 0.5 makes letters about 35 cm tall.

    Drawing
      - Drawn with the 3D scene, after water and particles: lit by nothing
        (like a particle), but fogged, bloomed and tonemapped like the rest
        of the world. intensity > 1 makes it glow through bloom (neon signs).
      - Hidden behind walls, unless alwaysOnTop.
      - Seen from behind: mirrored, or not drawn without doubleSided.
      - fade: fully visible up to fadeStart from the camera, gone by fadeEnd.
      - Use an SDF or MTSDF font (the Font Importer's Output Type). A bitmap
        font works, but is blocky close up and shimmers far away.
*************************************************************************/

// STL
#include <cstdint>
#include <string>

// Crucible engine
#include "ECS/Components/ComponentKind.h"
#include "Math/Math.h"
#include "Text/TextLayout.h"   // TextAlignH / TextAlignV / TextOverflow (no ECS or GPU in it)

namespace Crucible {

	/// How a flat thing in the world turns toward the camera: WorldText::billboard, and a
	/// world-space UICanvas's billboard.
	enum class WorldBillboard : std::uint8_t {
		Off,           // the Transform's rotation
		FaceCamera,    // always square on to the camera
		Upright,       // turns around Y only, toward the camera
	};

	struct WorldText {
		using Billboard = WorldBillboard;

		Math::vec4    colour{ 1.0f };      // sRGB, 0-1
		std::string   text;                // UTF-8; "\n" starts a new line
		std::uint64_t fontAsset = 0;       // a compiled font (.cfont) asset id; 0 = the default font
		float         fontSize = 0.5f;     // em size, world units

		// ---- Placement ----
		Math::vec2    size{ 4.0f, 1.0f };          // the box: width, height in world units
		Math::vec2    pivot{ 0.5f, 0.5f };         // the box's point on the entity's position; (0, 0) = top-left
		Billboard     billboard   = Billboard::Off;
		bool          alwaysOnTop = false;         // drawn over everything, not hidden behind walls
		bool          doubleSided = true;          // also drawn (mirrored) when seen from behind
		float         intensity   = 1.0f;          // brightness in the scene: > 1 glows with bloom
		bool          fade        = false;         // fade out with distance from the camera
		float         fadeStart   = 20.0f;         // world units: fully visible up to here
		float         fadeEnd     = 30.0f;         // gone from here

		// ---- Layout (as UIText) ----
		TextAlignH    horizontalAlign  = TextAlignH::Center;
		TextAlignV    verticalAlign    = TextAlignV::Middle;
		bool          wrap             = true;                    // break lines at spaces to stay inside the box's width
		TextOverflow  overflow         = TextOverflow::Overflow;  // Overflow: draw past the box; Truncate / Ellipsis: cut at its edges
		float         lineSpacing      = 1.0f;                    // times the font's line height
		float         characterSpacing = 0.0f;                    // extra space after each character, in ems

		bool          richText         = true;                    // read <b> <u> <color> tags
		bool          bold             = false;                   // bold everything (synthetic bold, any font)
		bool          underline        = false;                   // underline everything
		bool          bestFit          = false;                   // size the text to fit the box (fontSize ignored)
		float         minFontSize      = 0.1f;                    // best fit's smallest size, world units
		float         maxFontSize      = 1.0f;                    // best fit's largest size, world units

		// ---- Effects (as UIText; widths in world units, SDF / MTSDF fonts) ----
		bool          outline          = false;
		Math::vec4    outlineColour{ 0.0f, 0.0f, 0.0f, 1.0f };    // sRGB, 0-1
		float         outlineWidth     = 0.02f;                   // outside the letters' edge

		bool          glow             = false;
		Math::vec4    glowColour{ 1.0f, 0.85f, 0.4f, 0.8f };      // sRGB, 0-1
		float         glowWidth        = 0.03f;                   // outside the outline (or the letters)
		float         glowSoftness     = 0.5f;                    // 0 = an even band, 1 = fades quickly

		bool          shadow           = false;
		Math::vec4    shadowColour{ 0.0f, 0.0f, 0.0f, 0.6f };     // sRGB, 0-1
		Math::vec2    shadowOffset{ 0.02f, 0.02f };               // in the text's plane: +x right, +y DOWN (as UIText)
		float         shadowSoftness   = 0.0f;                    // 0 = crisp, 1 = as blurred as the font allows
	};
	CRUCIBLE_COMPONENT(WorldText, WorldText)

}   // NAMESPACE CRUCIBLE
