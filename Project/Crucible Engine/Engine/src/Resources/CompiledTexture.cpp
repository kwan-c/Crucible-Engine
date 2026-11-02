/*!
* @file			CompiledTexture.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		Reading and writing .bill files (see CompiledTexture.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "CompiledTexture.h"
#include "Compression/Billet.h"

#include <algorithm>
#include <stdexcept>

namespace Crucible {

	namespace {

		constexpr uint32_t Magic         = 0x474D4943u;   // "CIMG" read as a little-endian u32
		constexpr size_t   HeaderSize    = 56;
		constexpr size_t   MipEntrySize  = 24;
		constexpr uint32_t MaxMipLevels  = 32;
		constexpr uint32_t MaxDimension  = 16384;

		uint64_t Hash(const unsigned char* bytes, size_t size) {
			uint64_t hash = 1469598103934665603ull;   // FNV-1a, same convention as AssetRegistry
			for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
			return hash;
		}

		void Put32(std::vector<unsigned char>& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(v >> (8 * i))); }
		void Put64(std::vector<unsigned char>& out, uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<unsigned char>(v >> (8 * i))); }
		uint32_t Get32(const unsigned char* p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t{ p[i] } << (8 * i); return v; }
		uint64_t Get64(const unsigned char* p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= uint64_t{ p[i] } << (8 * i); return v; }

		bool KnownFormat(uint32_t f) {
			switch (static_cast<CompiledTextureFormat>(f)) {
				case CompiledTextureFormat::RGBA8: case CompiledTextureFormat::BC1: case CompiledTextureFormat::BC3:
				case CompiledTextureFormat::BC4:   case CompiledTextureFormat::BC5: case CompiledTextureFormat::BC6H:
				case CompiledTextureFormat::BC7:   return true;
			}
			return false;
		}

		// Empty if the texture's mip chain and data are consistent, else what is wrong.
		std::string Check(const CompiledTexture& t) {
			if (!KnownFormat(static_cast<uint32_t>(t.format))) return "unknown format";
			if (t.width == 0 || t.height == 0 || t.width > MaxDimension || t.height > MaxDimension) return "bad size";
			if (t.mips.empty() || t.mips.size() > MaxMipLevels) return "bad mip count";
			if (t.layers != 1 && t.layers != 6) return "bad layer count";
			if (t.layers == 6 && t.width != t.height) return "cube faces must be square";
			uint64_t expectedOffset = 0;
			for (size_t level = 0; level < t.mips.size(); ++level) {
				const CompiledTextureMip& m = t.mips[level];
				const uint32_t w = std::max(t.width >> level, 1u), h = std::max(t.height >> level, 1u);
				if (m.width != w || m.height != h) return "mip " + std::to_string(level) + " has the wrong size";
				if (m.offset != expectedOffset || m.size != CompiledTextureLevelBytes(t.format, w, h) * t.layers)
					return "mip " + std::to_string(level) + " has the wrong offset or byte count";
				expectedOffset += m.size;
			}
			if (expectedOffset != t.data.size()) return "data size does not match the mips";
			return {};
		}

	}

	uint64_t CompiledTextureLevelBytes(CompiledTextureFormat format, uint32_t width, uint32_t height) {
		const uint64_t blocks = uint64_t{ (width + 3) / 4 } * ((height + 3) / 4);
		switch (format) {
			case CompiledTextureFormat::RGBA8: return uint64_t{ width } * height * 4;
			case CompiledTextureFormat::BC1:
			case CompiledTextureFormat::BC4:   return blocks * 8;
			default:                           return blocks * 16;
		}
	}

	const char* ToString(CompiledTextureFormat format) {
		switch (format) {
			case CompiledTextureFormat::RGBA8: return "RGBA8";
			case CompiledTextureFormat::BC1:   return "BC1";
			case CompiledTextureFormat::BC3:   return "BC3";
			case CompiledTextureFormat::BC4:   return "BC4";
			case CompiledTextureFormat::BC5:   return "BC5";
			case CompiledTextureFormat::BC6H:  return "BC6H";
			case CompiledTextureFormat::BC7:   return "BC7";
		}
		return "Unknown";
	}

	std::vector<unsigned char> EncodeCompiledTexture(const CompiledTexture& t, bool billet) {
		if (const std::string problem = Check(t); !problem.empty())
			throw std::runtime_error("Cannot write .bill: " + problem);

		std::vector<unsigned char> packed;
		if (billet) {
			packed = Billet::Compress(t.data);
			if (packed.size() >= t.data.size()) packed.clear();   // did not help: store the data as it is
		}
		uint32_t flags = t.flags & ~uint32_t{ CompiledTextureFlag_Billet };
		if (!packed.empty()) flags |= CompiledTextureFlag_Billet;
		const std::vector<unsigned char>& body = packed.empty() ? t.data : packed;

		std::vector<unsigned char> out;
		out.reserve(HeaderSize + t.mips.size() * MipEntrySize + body.size());
		Put32(out, Magic);
		Put32(out, CompiledTextureVersion);
		Put64(out, t.assetGuid);
		Put64(out, t.sourceHash);
		Put32(out, t.width);
		Put32(out, t.height);
		Put32(out, static_cast<uint32_t>(t.format));
		Put32(out, flags);
		Put32(out, static_cast<uint32_t>(t.mips.size()));
		Put32(out, t.layers);
		Put64(out, Hash(t.data.data(), t.data.size()));   // of the unpacked data, either way
		for (const CompiledTextureMip& m : t.mips) { Put32(out, m.width); Put32(out, m.height); Put64(out, m.offset); Put64(out, m.size); }
		out.insert(out.end(), body.begin(), body.end());
		if (out.size() > MaxCompiledTextureBytes) throw std::runtime_error("Cannot write .bill: larger than 1 GiB");
		return out;
	}

	bool DecodeCompiledTexture(std::span<const unsigned char> bytes, uint64_t expectedGuid,
	                           CompiledTexture& out, std::string& error) {
		const auto fail = [&](std::string message) { error = std::move(message); return false; };

		if (bytes.size() < HeaderSize || bytes.size() > MaxCompiledTextureBytes) return fail("not a .bill file (bad size)");
		const unsigned char* p = bytes.data();
		if (Get32(p) != Magic) return fail("not a .bill file");
		if (Get32(p + 4) != CompiledTextureVersion)
			return fail("made by another texture compiler version (" + std::to_string(Get32(p + 4)) + "); compile it again");

		CompiledTexture t;
		t.version    = Get32(p + 4);
		t.assetGuid  = Get64(p + 8);
		t.sourceHash = Get64(p + 16);
		t.width      = Get32(p + 24);
		t.height     = Get32(p + 28);
		t.format     = static_cast<CompiledTextureFormat>(Get32(p + 32));
		t.flags      = Get32(p + 36);
		const uint32_t mipCount = Get32(p + 40);
		t.layers                = std::max(Get32(p + 44), 1u);   // 0 in files from before cube support
		const uint64_t dataHash = Get64(p + 48);
		if (expectedGuid != 0 && t.assetGuid != expectedGuid) return fail("belongs to another asset (GUID mismatch)");
		if (mipCount == 0 || mipCount > MaxMipLevels) return fail("bad mip count");

		const size_t tableEnd = HeaderSize + size_t{ mipCount } * MipEntrySize;
		if (bytes.size() < tableEnd) return fail("truncated mip table");
		t.mips.resize(mipCount);
		for (uint32_t i = 0; i < mipCount; ++i) {
			const unsigned char* e = p + HeaderSize + size_t{ i } * MipEntrySize;
			t.mips[i] = { Get32(e), Get32(e + 4), Get64(e + 8), Get64(e + 16) };
		}

		const std::span<const unsigned char> body = bytes.subspan(tableEnd);
		if (t.flags & CompiledTextureFlag_Billet) {
			std::string why;
			if (!Billet::Decompress(body, t.data, why, MaxCompiledTextureBytes)) return fail(why);
		} else {
			t.data.assign(body.begin(), body.end());
		}
		if (Hash(t.data.data(), t.data.size()) != dataHash) return fail("data is damaged (hash mismatch)");
		if (const std::string problem = Check(t); !problem.empty()) return fail(problem);

		out = std::move(t);
		return true;
	}

}
