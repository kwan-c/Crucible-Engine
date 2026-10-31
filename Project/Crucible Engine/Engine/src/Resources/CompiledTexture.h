/*!
* @file			CompiledTexture.h
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		The .bill file: a texture already compressed for the GPU, with all its mipmaps.
*
* Written by the texture compiler (Tools/TextureCompiler) from a .png/.jpg in Assets, into
* Resources. The engine uploads the blocks as they are (Renderer::CreateTextureCompressed) -
* no image decoding at runtime, so the game needs no image library (stb), and the texture
* takes 4-8x less GPU memory than RGBA8.
*
* Layout, little-endian, fixed-width fields (never C++ object layouts):
*   0  u32 magic "CIMG"        24 u32 width              44 u32 layers (0 or 1: a 2D texture, 6: a cube)
*   4  u32 version             28 u32 height             48 u64 FNV-1a hash of the data
*   8  u64 asset GUID          32 u32 format             56 mip table: mipCount x
*  16  u64 source hash         36 u32 flags                   { u32 width, u32 height, u64 offset, u64 size }
*                              40 u32 mipCount           then the data (offsets are from its start)
*
* With CompiledTextureFlag_Billet the data after the mip table is a Billet stream: the same
* bytes, packed losslessly to take less disk space, and unpacked when the texture is loaded.
* The header and mip table stay readable either way; the hash is of the unpacked data.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "EngineExport.h"

namespace Crucible {

	/// The file extension of compiled textures (named after Billet, which packs their data).
	/// Every .bill is a texture: other files packed with Billet need extensions of their own.
	inline constexpr const char* CompiledTextureExtension = ".bill";

	/// ModelImage::mimeType of an image stored inside a .cmodel as compiled texture bytes
	/// (the same bytes a .bill file holds), by CrucibleModelCompiler.
	inline constexpr const char* CompiledTextureMimeType = "image/x-crucible-bill";

	inline constexpr uint32_t CompiledTextureVersion = 1;
	// Limits apply to both compiler output and runtime input, before allocation.
	inline constexpr size_t   MaxCompiledTextureBytes = 1024ull * 1024 * 1024;

	/// How the texels are stored. The values are the file's, so never renumber them.
	/// BC = "block compression": every 4x4 texels become one fixed-size block the GPU reads directly.
	enum class CompiledTextureFormat : uint32_t {
		RGBA8 = 0,   ///< uncompressed, 4 bytes per texel
		BC1   = 1,   ///< RGB (+1-bit alpha), 8 bytes per block - smallest, lower quality
		BC3   = 3,   ///< RGBA, 16 bytes per block
		BC4   = 4,   ///< one channel (masks, roughness...), 8 bytes per block
		BC5   = 5,   ///< two channels (normal maps: X and Y, Z rebuilt in the shader), 16 bytes per block
		BC6H  = 6,   ///< HDR colour (half floats), 16 bytes per block
		BC7   = 7,   ///< RGBA, best quality, 16 bytes per block - the default for colour
	};

	/// CompiledTexture::flags bits.
	enum CompiledTextureFlags : uint32_t {
		CompiledTextureFlag_Srgb      = 1u << 0,   ///< colour data: mipmaps were averaged in linear light
		CompiledTextureFlag_HasAlpha  = 1u << 1,   ///< some texel is not fully opaque
		CompiledTextureFlag_Billet    = 1u << 2,   ///< on disk the data is packed with Billet (Compression/Billet.h);
		                                           ///< DecodeCompiledTexture unpacks it, so `data` is always unpacked
	};

	struct CompiledTextureMip {
		uint32_t width  = 0;
		uint32_t height = 0;
		uint64_t offset = 0;   ///< into CompiledTexture::data
		uint64_t size   = 0;   ///< bytes
	};

	struct CompiledTexture {
		uint32_t version    = CompiledTextureVersion;
		uint64_t assetGuid  = 0;   ///< the source image's GUID (the .bill's .meta has the same one)
		uint64_t sourceHash = 0;   ///< source bytes + import settings, to skip unchanged sources
		uint32_t width      = 0;
		uint32_t height     = 0;
		CompiledTextureFormat format = CompiledTextureFormat::RGBA8;
		uint32_t flags      = 0;
		uint32_t layers     = 1;   ///< 1 (2D) or 6 (cube faces +X -X +Y -Y +Z -Z); a mip holds every layer, one after another
		std::vector<CompiledTextureMip> mips;   ///< level 0 first, each half the size of the one before
		std::vector<unsigned char>      data;   ///< every level's blocks, back to back
	};

	/// Bytes one mip level of `format` takes (whole 4x4 blocks for the BC formats).
	ENGINE_API uint64_t CompiledTextureLevelBytes(CompiledTextureFormat format, uint32_t width, uint32_t height);

	/// Readable name, e.g. "BC7".
	ENGINE_API const char* ToString(CompiledTextureFormat format);

	/// Throws std::runtime_error if `texture` is inconsistent (sizes, offsets, mip chain).
	/// billet: pack the data with Billet (kept unpacked if that would not make it smaller).
	ENGINE_API std::vector<unsigned char> EncodeCompiledTexture(const CompiledTexture& texture, bool billet = true);

	/// Checks the header, data hash and every mip before filling `out`. expectedGuid == 0 skips the
	/// GUID check (offline inspection only). False with `error` set on any problem.
	ENGINE_API bool DecodeCompiledTexture(std::span<const unsigned char> bytes, uint64_t expectedGuid,
	                           CompiledTexture& out, std::string& error);

}
