/*!
* @file			ImageDecoders.h
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		An optional way to read image files (.png/.jpg/.hdr ...) that the engine
*				itself cannot read.
*
* The engine has no image library: the course allows stb_image in tools and the editor only,
* never in the game. Everything the game shows is compiled first (.bill, .sky, .cmodel,
* .cfont). The editor, though, should still show an image the moment it is added - before
* the next build compiles it - so it installs decoders here (Editor: EditorImageDecoders.cpp,
* using stb) and the ResourceManager falls back to them for files that are not compiled yet.
* In the game nothing is installed, so those files simply do not load (Cook reports them).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "EngineExport.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Crucible {

	struct DecodedImage {
		uint32_t width  = 0;
		uint32_t height = 0;
		std::vector<uint8_t> pixels;   ///< width * height * the channels asked for, top row first
	};

	struct DecodedImageHdr {
		uint32_t width  = 0;
		uint32_t height = 0;
		std::vector<float> rgba;       ///< width * height * 4 linear floats, top row first
	};

	struct ImageDecoders {
		/// An image file's bytes -> 8-bit pixels with `channels` (1-4) channels.
		bool (*decode)(std::span<const uint8_t> file, int channels, DecodedImage& out, std::string& error) = nullptr;
		/// An image file's bytes -> linear RGBA floats (.hdr as stored, other images through a 2.2 gamma).
		bool (*decodeHdr)(std::span<const uint8_t> file, DecodedImageHdr& out, std::string& error) = nullptr;
	};

	/// Installs the decoders (the editor, at startup). Pass {} to remove them.
	ENGINE_API void SetImageDecoders(const ImageDecoders& decoders);

	/// The installed decoders; both null in the game.
	ENGINE_API const ImageDecoders& GetImageDecoders();

}
