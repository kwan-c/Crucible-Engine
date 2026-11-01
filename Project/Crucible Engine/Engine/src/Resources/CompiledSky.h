/*!
* @file			CompiledSky.h
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		The .sky file: a skybox baked offline, ready to upload.
*
* Written by the texture compiler from a panorama image named "*_skybox" (or any .hdr), into
* Resources with the image's GUID. Everything the renderer used to work out at load time on
* the CPU is already done (Graphics/EnvironmentBake.h):
*   - sky:          the six cube faces, BC6H (HDR), what you see behind everything
*   - environment:  the sky blurred for reflections, one mip level per roughness, BC6H
*   - sh:           nine spherical-harmonic colours, the sky's soft diffuse light
* So loading a skybox is only an upload - no image library (stb) and no baking in the game.
*
* Layout, little-endian:
*   u32 magic "CSKY"   u32 version   u64 asset GUID   u64 source hash
*   27 x f32 sh (9 RGB)   u64 sky bytes   u64 environment bytes
*   then the two compiled textures (Resources/CompiledTexture.h, 6 layers each, Billet-packed)
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "Resources/CompiledTexture.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Crucible {

	/// The file extension of compiled skyboxes.
	inline constexpr const char* CompiledSkyExtension = ".sky";

	inline constexpr uint32_t CompiledSkyVersion = 1;

	/// Whether an Assets image is a skybox the texture compiler bakes into a .sky: its name
	/// contains "_skybox" (any case, e.g. "Textures/aurora_green_skybox.png"), or it is a .hdr.
	inline bool IsSkyboxSource(std::string_view virtualPath) {
		std::string lower(virtualPath);
		for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		const size_t slash = lower.find_last_of('/');
		const std::string_view name = std::string_view(lower).substr(slash == std::string::npos ? 0 : slash + 1);
		return name.find("_skybox") != std::string_view::npos || (name.size() >= 4 && name.substr(name.size() - 4) == ".hdr");
	}

	struct CompiledSky {
		uint64_t assetGuid  = 0;   ///< the source image's GUID (the .sky's .meta has the same one)
		uint64_t sourceHash = 0;   ///< source bytes + settings, to skip unchanged sources
		std::array<std::array<float, 3>, 9> sh{};   ///< diffuse light, as EnvironmentBake::Lighting::sh
		CompiledTexture sky;           ///< BC6H cube, 6 layers
		CompiledTexture environment;   ///< BC6H cube, 6 layers, one level per roughness
	};

	/// Throws std::runtime_error if either cube is inconsistent. billet: pack the cubes' data.
	ENGINE_API std::vector<unsigned char> EncodeCompiledSky(const CompiledSky& sky, bool billet = true);

	/// Checks everything (both cubes as DecodeCompiledTexture does) before filling `out`.
	/// expectedGuid == 0 skips the GUID check (offline inspection only).
	ENGINE_API bool DecodeCompiledSky(std::span<const unsigned char> bytes, uint64_t expectedGuid,
	                       CompiledSky& out, std::string& error);

}
