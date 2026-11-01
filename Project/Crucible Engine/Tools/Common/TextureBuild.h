/*!
* @file			TextureBuild.h
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		Image -> GPU texture, shared by the offline compilers: CrucibleTextureCompiler
*				(loose images -> .bill) and CrucibleModelCompiler (images inside models -> .cmodel).
*
* Uses stb_image and Compressonator, which the course allows in tools only: compile
* TextureBuild.cpp into tools, never into Engine.dll or the game.
*
*   1. DecodeImage: a .png/.jpg/.tga/.bmp file's bytes -> RGBA8 pixels (stb_image).
*   2. Build: shrink to maxSize, make the mipmaps (each level the 2x2 average of the one
*      before; colour is averaged in linear light, or the small levels come out too dark),
*      and compress every level with Compressonator, one 4x4 block at a time on every CPU core.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "Resources/CompiledTexture.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace Crucible::TextureBuild {

	/// Bump when Build's output for the same image and options changes; the compilers mix it
	/// into their "is this up to date?" hashes, so every texture is rebuilt.
	inline constexpr uint64_t Version = 2;   // 2: normal maps in models are BC5

	struct Image {
		uint32_t width  = 0;
		uint32_t height = 0;
		std::vector<unsigned char> rgba;   ///< width * height * 4 bytes, top row first
	};

	struct Options {
		CompiledTextureFormat format  = CompiledTextureFormat::BC7;   ///< BC1 / BC3 / BC7 (colour), BC4 (red), BC5 (red + green)
		bool     srgb    = true;    ///< colour data: averaged in linear light, flagged sRGB
		bool     mipmaps = true;
		float    quality = 0.1f;    ///< 0..1: higher is slightly better and much slower
		uint32_t maxSize = 2048;    ///< a larger image is halved until both sides fit
		bool     normalMap = false; ///< a tangent-space normal map: mipmaps average directions and stay unit length
	};

	/// Decodes an image file. Throws std::runtime_error if stb cannot read it.
	Image DecodeImage(std::span<const unsigned char> fileBytes);

	/// The compressed texture (no GUID or source hash yet: the caller fills those in).
	/// Throws std::runtime_error on failure.
	CompiledTexture Build(Image image, const Options& options);

	/// The image halved (the mipmap filter: linear light for sRGB, directions for normal maps)
	/// until both sides fit maxSize - what Build does first.
	Image Shrink(Image image, uint32_t maxSize, bool srgb, bool normalMap);

	/// One 2D level decoded back to RGBA8 - what the GPU will show (the Texture Importer's
	/// comparison). BC4 reads as grey; BC5 as (x, y, rebuilt z), like the shader.
	std::vector<unsigned char> DecodeLevel(const CompiledTexture& texture, size_t level);

	/// An image file as linear RGBA floats (stbi_loadf: .hdr as stored, LDR images through a
	/// 2.2 gamma - the same as the renderer's runtime skybox path). Throws if stb cannot read it.
	struct ImageHdr {
		uint32_t width  = 0;
		uint32_t height = 0;
		std::vector<float> rgba;   ///< width * height * 4 floats, top row first
	};
	ImageHdr DecodeImageHdr(std::span<const unsigned char> fileBytes);

	/// HDR cube faces -> a BC6H cube (layers = 6). levels[i] holds six faces of sizes[i]^2 linear
	/// RGB texels, face after face (+X -X +Y -Y +Z -Z); each level half the size of the one before.
	/// Negative values become 0 and big ones stop at 65504 (BC6H holds unsigned half floats).
	CompiledTexture BuildHdrCube(const std::vector<std::vector<glm::vec3>>& levels, const std::vector<uint32_t>& sizes, float quality = 0.05f);

}
