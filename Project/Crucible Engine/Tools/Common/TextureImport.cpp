/*!
* @file			TextureImport.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			31 October 2026
*
* @brief		Importing one image (see TextureImport.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "TextureImport.h"
#include "TextureBuild.h"

#include "AssetPipe/AssetRegistry.h"
#include "Graphics/EnvironmentBake.h"
#include "Resources/CompiledSky.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX   // keep Windows.h's min/max macros away from std::min / std::max (the editor builds this file too)
#endif
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace Crucible::TextureImport {

	namespace {

		// Bump when the output for the same image and settings changes, so every texture is rebuilt.
		constexpr uint64_t ImportVersion = 2;   // 2: Billet packing, maxSize

		void Check(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }

		std::vector<unsigned char> Read(const fs::path& path) {
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			Check(bool(file), "Cannot open " + path.string());
			const auto size = file.tellg();
			Check(size >= 0 && uint64_t(size) <= MaxCompiledTextureBytes, "File exceeds 1 GiB limit: " + path.string());
			std::vector<unsigned char> bytes(static_cast<size_t>(size));
			file.seekg(0);
			Check(bool(file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))), "Cannot read " + path.string());
			return bytes;
		}

		void Replace(const fs::path& temporary, const fs::path& destination) {
#ifdef _WIN32
			Check(MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
			      "Cannot replace " + destination.string() + " (Windows error " + std::to_string(GetLastError()) + ")");
#else
			fs::rename(temporary, destination);
#endif
		}

		const char* const kUsageNames[]  = { "Color", "Linear", "Mask", "Normal" };
		const char* const kFormatNames[] = { "Auto", "BC1", "BC3", "BC4", "BC5", "BC7" };

		std::string Kb(uint64_t bytes) { return std::to_string(bytes / 1024) + " KB"; }

	}

	const char* ToString(Usage usage)   { return kUsageNames[static_cast<size_t>(usage)]; }
	const char* ToString(Format format) { return kFormatNames[static_cast<size_t>(format)]; }

	CompiledTextureFormat ResolveFormat(const Settings& settings) {
		switch (settings.format) {
			case Format::BC1: return CompiledTextureFormat::BC1;
			case Format::BC3: return CompiledTextureFormat::BC3;
			case Format::BC4: return CompiledTextureFormat::BC4;
			case Format::BC5: return CompiledTextureFormat::BC5;
			case Format::BC7: return CompiledTextureFormat::BC7;
			case Format::Auto: break;
		}
		switch (settings.usage) {
			case Usage::Mask:   return CompiledTextureFormat::BC4;
			case Usage::Normal: return CompiledTextureFormat::BC5;
			default:            return CompiledTextureFormat::BC7;
		}
	}

	fs::path SettingsPathFor(const fs::path& image) {
		fs::path file = image;
		file += ".texsettings";
		return file;
	}

	Settings ReadSettings(const fs::path& image, std::string& text) {
		Settings settings;
		text.clear();
		const fs::path file = SettingsPathFor(image);
		if (!fs::exists(file)) return settings;
		const auto bytes = Read(file);
		text.assign(bytes.begin(), bytes.end());

		rapidjson::Document doc;
		doc.Parse(text.c_str());
		Check(!doc.HasParseError() && doc.IsObject(), file.filename().string() + " is not a JSON object");
		const auto pick = [&](const char* key, const char* const* names, size_t count, auto& out) {
			if (!doc.HasMember(key)) return;
			Check(doc[key].IsString(), std::string("\"") + key + "\" must be a string");
			const std::string value = doc[key].GetString();
			for (size_t i = 0; i < count; ++i)
				if (value == names[i]) { out = static_cast<std::remove_reference_t<decltype(out)>>(i); return; }
			std::string choices;
			for (size_t i = 0; i < count; ++i) choices += (i ? ", " : "") + std::string(names[i]);
			Check(false, std::string("\"") + key + "\" must be one of " + choices + ", not \"" + value + "\"");
		};
		pick("usage", kUsageNames, std::size(kUsageNames), settings.usage);
		pick("format", kFormatNames, std::size(kFormatNames), settings.format);
		if (doc.HasMember("mipmaps")) { Check(doc["mipmaps"].IsBool(), "\"mipmaps\" must be true or false"); settings.mipmaps = doc["mipmaps"].GetBool(); }
		if (doc.HasMember("quality")) {
			Check(doc["quality"].IsNumber(), "\"quality\" must be a number");
			settings.quality = std::clamp(doc["quality"].GetFloat(), 0.0f, 1.0f);
		}
		if (doc.HasMember("maxSize")) {
			Check(doc["maxSize"].IsUint() && doc["maxSize"].GetUint() >= 1 && doc["maxSize"].GetUint() <= 16384, "\"maxSize\" must be a whole number from 1 to 16384");
			settings.maxSize = doc["maxSize"].GetUint();
		}
		if (doc.HasMember("billet")) { Check(doc["billet"].IsBool(), "\"billet\" must be true or false"); settings.billet = doc["billet"].GetBool(); }
		if (doc.HasMember("faceSize")) {
			Check(doc["faceSize"].IsUint() && doc["faceSize"].GetUint() >= 16 && doc["faceSize"].GetUint() <= 4096, "\"faceSize\" must be a whole number from 16 to 4096");
			settings.faceSize = doc["faceSize"].GetUint();
		}
		return settings;
	}

	std::string WriteSettingsText(const Settings& settings) {
		rapidjson::StringBuffer buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
		w.SetIndent(' ', 2);
		w.StartObject();
		w.Key("usage");    w.String(ToString(settings.usage));
		w.Key("format");   w.String(ToString(settings.format));
		w.Key("mipmaps");  w.Bool(settings.mipmaps);
		w.Key("quality");  w.Double(static_cast<double>(settings.quality));
		w.Key("maxSize");  w.Uint(settings.maxSize);
		w.Key("billet");   w.Bool(settings.billet);
		w.Key("faceSize"); w.Uint(settings.faceSize);
		w.EndObject();
		return std::string(buffer.GetString(), buffer.GetSize()) + "\n";
	}

	uint64_t SourceHash(const std::vector<unsigned char>& sourceBytes, const std::string& settingsText) {
		uint64_t hash = 1469598103934665603ull;
		const auto mix = [&](unsigned char byte) { hash ^= byte; hash *= 1099511628211ull; };
		for (auto byte : sourceBytes) mix(byte);
		for (auto byte : settingsText) mix(static_cast<unsigned char>(byte));
		for (int i = 0; i < 8; ++i) mix(static_cast<unsigned char>(ImportVersion >> (8 * i)));
		for (int i = 0; i < 8; ++i) mix(static_cast<unsigned char>(TextureBuild::Version >> (8 * i)));
		return hash;
	}

	Compiled CompileImage(const std::vector<unsigned char>& sourceBytes, const Settings& settings,
	                      uint64_t guid, uint64_t sourceHash, bool skybox) {
		Compiled out;
		if (skybox) {
			// A panorama -> six cube faces and the sky's lighting, baked the same way the renderer
			// would at load time (Graphics/EnvironmentBake), all BC6H.
			const TextureBuild::ImageHdr image = TextureBuild::DecodeImageHdr(sourceBytes);
			const std::vector<glm::vec3> faces = EnvironmentBake::EquirectToCube(image.rgba.data(), static_cast<int>(image.width),
			                                                                     static_cast<int>(image.height), settings.faceSize);
			const EnvironmentBake::Lighting lighting = EnvironmentBake::BakeLighting(faces, settings.faceSize);

			CompiledSky sky;
			for (size_t i = 0; i < 9; ++i) sky.sh[i] = { lighting.sh[i].x, lighting.sh[i].y, lighting.sh[i].z };
			sky.sky         = TextureBuild::BuildHdrCube({ faces }, { settings.faceSize });
			sky.environment = TextureBuild::BuildHdrCube(lighting.levels, lighting.sizes);
			sky.assetGuid = sky.sky.assetGuid = sky.environment.assetGuid = guid;
			sky.sourceHash = sourceHash;
			out.bytes = EncodeCompiledSky(sky, settings.billet);

			// Read it back exactly as the engine will: must give the same blocks.
			CompiledSky check; std::string error;
			const bool decoded = DecodeCompiledSky(out.bytes, guid, check, error);   // before the message reads `error`
			Check(decoded, "Compiled round trip failed: " + error);
			Check(check.sky.data == sky.sky.data && check.environment.data == sky.environment.data, "Compiled round trip changed the sky data");

			const uint64_t gpu = sky.sky.data.size() + sky.environment.data.size();
			const uint64_t halfFloat = uint64_t{ settings.faceSize } * settings.faceSize * 6 * 8 + sky.environment.data.size() * 8;   // RGBA16F, 8x BC6H
			out.summary = std::to_string(settings.faceSize) + " per face BC6H + " + std::to_string(sky.environment.mips.size()) +
			              " reflection levels | file " + Kb(out.bytes.size()) + " | GPU " + Kb(gpu) + " (baked at load time before: " + Kb(halfFloat) + ")";
			return out;
		}

		TextureBuild::Options options;
		options.format    = ResolveFormat(settings);
		options.srgb      = settings.usage == Usage::Color;
		options.normalMap = settings.usage == Usage::Normal;
		options.mipmaps   = settings.mipmaps;
		options.quality   = settings.quality;
		options.maxSize   = settings.maxSize;
		CompiledTexture texture = TextureBuild::Build(TextureBuild::DecodeImage(sourceBytes), options);
		texture.assetGuid  = guid;
		texture.sourceHash = sourceHash;
		out.bytes = EncodeCompiledTexture(texture, settings.billet);

		// Read it back exactly as the engine will (Billet unpacked too): must give the same blocks.
		CompiledTexture check; std::string error;
		const bool decoded = DecodeCompiledTexture(out.bytes, guid, check, error);   // before the message reads `error`
		Check(decoded, "Compiled round trip failed: " + error);
		Check(check.data == texture.data, "Compiled round trip changed the texture data");

		out.summary = std::to_string(texture.width) + "x" + std::to_string(texture.height) + ' ' + ToString(texture.format) + ", " +
		              std::to_string(texture.mips.size()) + " mips | file " + Kb(out.bytes.size()) + " (" +
		              ((check.flags & CompiledTextureFlag_Billet) ? "Billet" : "unpacked") + "), GPU " + Kb(texture.data.size()) +
		              " | source file " + Kb(sourceBytes.size()) + ", as RGBA8 on the GPU " +
		              Kb(uint64_t{ texture.width } * texture.height * 4 * 4 / 3);
		return out;
	}

	Comparison CompareFormats(const std::vector<unsigned char>& sourceBytes, const Settings& settings, uint32_t previewMax) {
		const bool srgb   = settings.usage == Usage::Color;
		const bool normal = settings.usage == Usage::Normal;
		const TextureBuild::Image image = TextureBuild::DecodeImage(sourceBytes);

		// GPU memory of the real compiled texture (after maxSize), mips included.
		Comparison out;
		out.compiledWidth = image.width;
		out.compiledHeight = image.height;
		while (out.compiledWidth > settings.maxSize || out.compiledHeight > settings.maxSize) {
			out.compiledWidth = std::max(out.compiledWidth / 2, 1u);
			out.compiledHeight = std::max(out.compiledHeight / 2, 1u);
		}
		const auto chainBytes = [&](CompiledTextureFormat format) {
			uint64_t total = 0;
			for (uint32_t w = out.compiledWidth, h = out.compiledHeight;; w = std::max(w / 2, 1u), h = std::max(h / 2, 1u)) {
				total += CompiledTextureLevelBytes(format, w, h);
				if (!settings.mipmaps || (w == 1 && h == 1)) break;
			}
			return total;
		};

		// The comparison itself runs on a smaller copy: quick, and enough to judge.
		const TextureBuild::Image preview = TextureBuild::Shrink(image, std::min(settings.maxSize, previewMax), srgb, normal);
		out.width = preview.width;
		out.height = preview.height;
		bool hasAlpha = false;
		for (size_t i = 3; i < preview.rgba.size(); i += 4) { if (preview.rgba[i] != 255) { hasAlpha = true; break; } }

		out.formats.push_back({ CompiledTextureFormat::RGBA8, preview.rgba, 0.0, chainBytes(CompiledTextureFormat::RGBA8) });
		for (CompiledTextureFormat format : { CompiledTextureFormat::BC1, CompiledTextureFormat::BC3, CompiledTextureFormat::BC7,
		                                      CompiledTextureFormat::BC4, CompiledTextureFormat::BC5 }) {
			TextureBuild::Options options;
			options.format    = format;
			options.srgb      = srgb;
			options.normalMap = normal;
			options.mipmaps   = false;
			options.quality   = settings.quality;
			options.maxSize   = 16384;   // already shrunk
			ComparedFormat compared{ format, TextureBuild::DecodeLevel(TextureBuild::Build(preview, options), 0), 0.0, chainBytes(format) };

			// PSNR over the channels the format keeps (alpha only where the image has some):
			// 10 log10(255^2 / mean squared error); identical shows as 99.
			bool channels[4] = { true, true, true, hasAlpha };
			if (format == CompiledTextureFormat::BC4) { channels[1] = channels[2] = channels[3] = false; }
			if (format == CompiledTextureFormat::BC5) { channels[2] = channels[3] = false; }
			double sum = 0.0;
			size_t count = 0;
			for (size_t i = 0; i + 3 < preview.rgba.size(); i += 4) {
				for (int c = 0; c < 4; ++c) {
					if (!channels[c]) continue;
					const double d = static_cast<double>(preview.rgba[i + c]) - static_cast<double>(compared.rgba[i + c]);
					sum += d * d;
					++count;
				}
			}
			const double mse = count ? sum / static_cast<double>(count) : 0.0;
			compared.psnr = mse <= 1e-12 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
			out.formats.push_back(std::move(compared));
		}

		// The result with these exact settings, every mip level decoded back.
		TextureBuild::Options options;
		options.format    = ResolveFormat(settings);
		options.srgb      = srgb;
		options.normalMap = normal;
		options.mipmaps   = settings.mipmaps;
		options.quality   = settings.quality;
		options.maxSize   = 16384;   // already shrunk
		const CompiledTexture result = TextureBuild::Build(preview, options);
		out.resultFormat = result.format;
		for (size_t level = 0; level < result.mips.size(); ++level)
			out.resultLevels.push_back({ result.mips[level].width, result.mips[level].height, TextureBuild::DecodeLevel(result, level) });
		return out;
	}

	bool IsUpToDate(const fs::path& file, uint64_t guid, uint64_t sourceHash, bool skybox) {
		std::error_code ec;
		if (!fs::exists(file, ec)) return false;
		std::string error;
		try {
			const auto bytes = Read(file);
			if (skybox) { CompiledSky cached;     return DecodeCompiledSky(bytes, guid, cached, error) && cached.sourceHash == sourceHash; }
			else        { CompiledTexture cached; return DecodeCompiledTexture(bytes, guid, cached, error) && cached.sourceHash == sourceHash; }
		} catch (const std::exception&) {
			return false;
		}
	}

	void WriteCompiledFile(const fs::path& path, const std::vector<unsigned char>& bytes, uint64_t guid, const std::string& virtualPath) {
		fs::create_directories(path.parent_path());
		const auto temporary     = path.parent_path() / ("." + path.filename().string() + ".tmp");
		const auto meta          = AssetRegistry::MetaPathFor(path);
		const auto temporaryMeta = path.parent_path() / ("." + path.filename().string() + ".meta.tmp");
		try {
			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				Check(bool(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))), "Cannot write " + temporary.string());
				file.close(); Check(!file.fail(), "Cannot flush " + temporary.string());
			}
			Check(Read(temporary) == bytes, "Compiled file read-back differs");
			AssetMeta metadata; metadata.guid = xresource::instance_guid{ guid };
			metadata.contenthash = AssetRegistry::HashFileContents(temporary);
			Check(AssetRegistry::WriteMeta(temporaryMeta, metadata, virtualPath), "Cannot write compiled metadata");
			Replace(temporary, path);
			Replace(temporaryMeta, meta);
		} catch (...) {
			std::error_code ignored; fs::remove(temporary, ignored); fs::remove(temporaryMeta, ignored);
			throw;
		}
	}

}
