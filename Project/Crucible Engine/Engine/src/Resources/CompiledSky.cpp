/*!
* @file			CompiledSky.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		Reading and writing .sky files (see CompiledSky.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "CompiledSky.h"

#include <bit>
#include <stdexcept>

namespace Crucible {

	namespace {

		constexpr uint32_t Magic      = 0x594B5343u;   // "CSKY" read as a little-endian u32
		constexpr size_t   HeaderSize = 24 + 9 * 3 * 4 + 16;

		void Put32(std::vector<unsigned char>& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(v >> (8 * i))); }
		void Put64(std::vector<unsigned char>& out, uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<unsigned char>(v >> (8 * i))); }
		uint32_t Get32(const unsigned char* p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t{ p[i] } << (8 * i); return v; }
		uint64_t Get64(const unsigned char* p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= uint64_t{ p[i] } << (8 * i); return v; }

	}

	std::vector<unsigned char> EncodeCompiledSky(const CompiledSky& sky, bool billet) {
		if (sky.sky.layers != 6 || sky.environment.layers != 6) throw std::runtime_error("Cannot write .sky: both parts must be cubes");
		const std::vector<unsigned char> skyBytes = EncodeCompiledTexture(sky.sky, billet);
		const std::vector<unsigned char> envBytes = EncodeCompiledTexture(sky.environment, billet);

		std::vector<unsigned char> out;
		out.reserve(HeaderSize + skyBytes.size() + envBytes.size());
		Put32(out, Magic);
		Put32(out, CompiledSkyVersion);
		Put64(out, sky.assetGuid);
		Put64(out, sky.sourceHash);
		for (const auto& rgb : sky.sh) for (float c : rgb) Put32(out, std::bit_cast<uint32_t>(c));
		Put64(out, skyBytes.size());
		Put64(out, envBytes.size());
		out.insert(out.end(), skyBytes.begin(), skyBytes.end());
		out.insert(out.end(), envBytes.begin(), envBytes.end());
		return out;
	}

	bool DecodeCompiledSky(std::span<const unsigned char> bytes, uint64_t expectedGuid, CompiledSky& out, std::string& error) {
		const auto fail = [&](const std::string& why) { error = ".sky: " + why; return false; };
		if (bytes.size() < HeaderSize || Get32(bytes.data()) != Magic) return fail("not a .sky file");
		if (Get32(bytes.data() + 4) != CompiledSkyVersion) return fail("made by another texture compiler version; compile it again");

		CompiledSky sky;
		const unsigned char* p = bytes.data();
		sky.assetGuid  = Get64(p + 8);
		sky.sourceHash = Get64(p + 16);
		if (expectedGuid != 0 && sky.assetGuid != expectedGuid) return fail("belongs to another asset (GUID mismatch)");
		size_t at = 24;
		for (auto& rgb : sky.sh) for (float& c : rgb) { c = std::bit_cast<float>(Get32(p + at)); at += 4; }
		const uint64_t skySize = Get64(p + at), envSize = Get64(p + at + 8);
		at += 16;
		if (skySize > bytes.size() - at || envSize != bytes.size() - at - skySize) return fail("bad part sizes");

		std::string why;
		if (!DecodeCompiledTexture(bytes.subspan(at, skySize), sky.assetGuid, sky.sky, why)) return fail("sky: " + why);
		if (!DecodeCompiledTexture(bytes.subspan(at + skySize), sky.assetGuid, sky.environment, why)) return fail("environment: " + why);
		if (sky.sky.layers != 6 || sky.environment.layers != 6) return fail("a part is not a cube");

		out = std::move(sky);
		return true;
	}

}
