/*!
* @file			EditorImageDecoders.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		Gives the engine stb_image (allowed in the editor, not the game) for files that
*				are not compiled yet - see Resources/ImageDecoders.h.
*
* So an image added to Assets shows up in the editor right away, before the next build
* compiles it, and an old font with a PNG atlas still opens (until it is recompiled).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "Assets/EditorImageDecoders.h"
#include "Resources/ImageDecoders.h"

#include <stb_image.h>   // implemented in Tools/Common/TextureBuild.cpp, compiled into the editor too

namespace Crucible {

	namespace {

		bool Decode(std::span<const uint8_t> file, int channels, DecodedImage& out, std::string& error) {
			int w = 0, h = 0, fileChannels = 0;
			stbi_uc* pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &fileChannels, channels);
			if (!pixels) { error = std::string("image decode failed: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown"); return false; }
			out.width  = static_cast<uint32_t>(w);
			out.height = static_cast<uint32_t>(h);
			out.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * channels);
			stbi_image_free(pixels);
			return true;
		}

		bool DecodeHdr(std::span<const uint8_t> file, DecodedImageHdr& out, std::string& error) {
			int w = 0, h = 0, fileChannels = 0;
			float* pixels = stbi_loadf_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &fileChannels, STBI_rgb_alpha);
			if (!pixels) { error = std::string("image decode failed: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown"); return false; }
			out.width  = static_cast<uint32_t>(w);
			out.height = static_cast<uint32_t>(h);
			out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
			stbi_image_free(pixels);
			return true;
		}

	}

	void InstallEditorImageDecoders() {
		ImageDecoders decoders;
		decoders.decode    = &Decode;
		decoders.decodeHdr = &DecodeHdr;
		SetImageDecoders(decoders);
	}

}
