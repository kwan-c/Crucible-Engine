/*!
* @file			FontImportSettings.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			21 October 2026
*
* @brief		FontImportSettings: limits, .fontsettings JSON, settings hash.
*/

#include "Fonts/FontImportSettings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace Crucible {

	namespace {

		// Allowed ranges. Outside them a font either looks broken or makes an absurd atlas.
		constexpr int   kMinPixelSize  = 4,   kMaxPixelSize  = 256;
		constexpr int   kMaxBitmapSizes = 8;
		constexpr float kMinPixelRange = 1.0f, kMaxPixelRange = 32.0f;
		constexpr int   kMaxPadding    = 16;
		constexpr int   kMinAtlas      = 64,  kMaxAtlas      = 16384;
		constexpr std::uint32_t kMaxCodepoint = 0x10FFFF;

		std::string Hex(std::uint32_t cp) {
			char buffer[16] = {};
			std::snprintf(buffer, sizeof(buffer), "%04X", static_cast<unsigned>(cp));
			return buffer;
		}

	}	// anonymous namespace

	bool ParseCodepointRange(std::string const& input, std::pair<std::uint32_t, std::uint32_t>& out) {
		std::string text = input;
		text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; }), text.end());
		auto parseOne = [](std::string part, std::uint32_t& value) {
			if (part.size() > 2 && (part[0] == 'U' || part[0] == 'u') && part[1] == '+') { part = part.substr(2); }
			else if (part.size() > 2 && part[0] == '0' && (part[1] == 'x' || part[1] == 'X')) { part = part.substr(2); }
			if (part.empty() || part.size() > 6) { return false; }
			char* end = nullptr;
			const unsigned long v = std::strtoul(part.c_str(), &end, 16);
			if (end == nullptr || *end != '\0' || v > kMaxCodepoint) { return false; }
			value = static_cast<std::uint32_t>(v);
			return true;
		};
		const std::size_t dash = text.find('-');
		std::uint32_t a = 0, b = 0;
		if (dash == std::string::npos) {
			if (!parseOne(text, a)) { return false; }
			b = a;
		} else if (!parseOne(text.substr(0, dash), a) || !parseOne(text.substr(dash + 1), b)) {
			return false;
		}
		out = { std::min(a, b), std::max(a, b) };
		return true;
	}

	std::string FormatCodepointRange(std::pair<std::uint32_t, std::uint32_t> const& range) {
		return range.first == range.second ? Hex(range.first) : Hex(range.first) + "-" + Hex(range.second);
	}

	namespace {

		constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
		constexpr std::uint64_t kFnvPrime  = 1099511628211ull;

		std::uint64_t Fnv1a(std::uint64_t hash, void const* data, std::size_t size) {
			auto const* bytes = static_cast<unsigned char const*>(data);
			for (std::size_t i = 0; i < size; ++i) { hash = (hash ^ bytes[i]) * kFnvPrime; }
			return hash;
		}

	}	// anonymous namespace

	// ----------------------------------------------------------------------------------------- //

	std::vector<std::string> FontImportSettings::Sanitize() {
		std::vector<std::string> changes;

		auto clampInt = [&](int& value, int lo, int hi, char const* name) {
			const int clamped = std::clamp(value, lo, hi);
			if (clamped != value) {
				changes.push_back(std::string(name) + " " + std::to_string(value) + " is out of range, using " + std::to_string(clamped));
				value = clamped;
			}
		};

		if (static_cast<std::uint8_t>(outputType) > static_cast<std::uint8_t>(FontOutputType::Mtsdf)) {
			changes.push_back("unknown output type, using Bitmap");
			outputType = FontOutputType::Bitmap;
		}

		for (int& size : bitmapSizes) { clampInt(size, kMinPixelSize, kMaxPixelSize, "bitmap size"); }
		std::sort(bitmapSizes.begin(), bitmapSizes.end());
		bitmapSizes.erase(std::unique(bitmapSizes.begin(), bitmapSizes.end()), bitmapSizes.end());
		if (static_cast<int>(bitmapSizes.size()) > kMaxBitmapSizes) {
			changes.push_back("more than " + std::to_string(kMaxBitmapSizes) + " bitmap sizes, keeping the first " + std::to_string(kMaxBitmapSizes));
			bitmapSizes.resize(kMaxBitmapSizes);
		}
		if (bitmapSizes.empty()) {
			changes.push_back("no bitmap sizes, using 16 and 32");
			bitmapSizes = { 16, 32 };
		}

		clampInt(glyphSize, kMinPixelSize, kMaxPixelSize, "glyph size");
		clampInt(padding, 0, kMaxPadding, "padding");
		clampInt(atlasMaxSize, kMinAtlas, kMaxAtlas, "atlas max size");

		if (!(pixelRange >= kMinPixelRange && pixelRange <= kMaxPixelRange)) {   // also catches NaN
			const float fixed = std::isnan(pixelRange) ? 4.0f : std::clamp(pixelRange, kMinPixelRange, kMaxPixelRange);
			changes.push_back("pixel range " + std::to_string(pixelRange) + " is out of range, using " + std::to_string(fixed));
			pixelRange = fixed;
		}

		// Ranges: sorted, overlapping or touching ones merged.
		for (auto& [a, b] : ranges) {
			if (a > b) { std::swap(a, b); }
			b = std::min(b, kMaxCodepoint);
			a = std::min(a, b);
		}
		std::sort(ranges.begin(), ranges.end());
		std::vector<std::pair<std::uint32_t, std::uint32_t>> merged;
		for (auto const& range : ranges) {
			if (!merged.empty() && range.first <= merged.back().second + 1) {
				merged.back().second = std::max(merged.back().second, range.second);
			} else {
				merged.push_back(range);
			}
		}
		ranges = std::move(merged);

		std::vector<std::string> files;
		for (std::string& file : sampleTextFiles) {
			std::replace(file.begin(), file.end(), '\\', '/');
			if (!file.empty() && std::find(files.begin(), files.end(), file) == files.end()) { files.push_back(file); }
		}
		sampleTextFiles = std::move(files);

		return changes;
	}

	std::string FontSettingsPathFor(std::string const& fontVirtualPath) {
		return fontVirtualPath + ".fontsettings";
	}

	std::string CompiledFontPathFor(std::string const& fontVirtualPath) {
		const std::size_t slash = fontVirtualPath.find_last_of("/\\");
		const std::size_t dot   = fontVirtualPath.find_last_of('.');
		const bool hasExtension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
		return (hasExtension ? fontVirtualPath.substr(0, dot) : fontVirtualPath) + ".cfont";
	}

	bool ParseFontOutputType(std::string const& text, FontOutputType& out) {
		std::string lower = text;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lower == "bitmap") { out = FontOutputType::Bitmap; return true; }
		if (lower == "sdf")    { out = FontOutputType::Sdf;    return true; }
		if (lower == "mtsdf")  { out = FontOutputType::Mtsdf;  return true; }
		return false;
	}

	// ----------------------------------------------------------------------------------------- //

	bool ParseFontImportSettings(std::string const& json, FontImportSettings& out,
	                             std::vector<std::string>& warnings, std::string& error) {
		out = FontImportSettings{};
		warnings.clear();
		error.clear();

		rapidjson::Document doc;
		doc.Parse(json.c_str(), json.size());
		if (doc.HasParseError()) {
			error = std::string("not valid JSON: ") + rapidjson::GetParseError_En(doc.GetParseError())
			      + " (at byte " + std::to_string(doc.GetErrorOffset()) + ")";
			return false;
		}
		if (!doc.IsObject()) { error = "not a JSON object"; return false; }

		auto bad = [&](char const* key, char const* expected) {
			warnings.push_back(std::string("\"") + key + "\" should be " + expected + " - using the default");
		};
		auto member = [&](char const* key) -> rapidjson::Value const* {
			auto it = doc.FindMember(key);
			return it != doc.MemberEnd() ? &it->value : nullptr;
		};
		auto readInt = [&](char const* key, int& value) {
			if (rapidjson::Value const* v = member(key)) {
				if (v->IsNumber()) { value = static_cast<int>(v->GetDouble()); } else { bad(key, "a number"); }
			}
		};

		if (rapidjson::Value const* v = member("outputType")) {
			FontOutputType type{};
			if (v->IsString() && ParseFontOutputType(v->GetString(), type)) { out.outputType = type; }
			else { bad("outputType", "\"Bitmap\", \"SDF\" or \"MTSDF\""); }
		}
		if (rapidjson::Value const* v = member("bitmapSizes")) {
			if (v->IsArray()) {
				std::vector<int> sizes;
				for (auto const& item : v->GetArray()) {
					if (item.IsNumber()) { sizes.push_back(static_cast<int>(item.GetDouble())); }
					else { bad("bitmapSizes", "a list of numbers"); sizes.clear(); break; }
				}
				if (!sizes.empty()) { out.bitmapSizes = std::move(sizes); }
			} else {
				bad("bitmapSizes", "a list of numbers");
			}
		}
		readInt("glyphSize", out.glyphSize);
		if (rapidjson::Value const* v = member("pixelRange")) {
			if (v->IsNumber()) { out.pixelRange = static_cast<float>(v->GetDouble()); } else { bad("pixelRange", "a number"); }
		}
		readInt("padding", out.padding);
		if (rapidjson::Value const* v = member("includeAscii")) {
			if (v->IsBool()) { out.includeAscii = v->GetBool(); } else { bad("includeAscii", "true or false"); }
		}
		if (rapidjson::Value const* v = member("ranges")) {
			if (v->IsArray()) {
				for (auto const& item : v->GetArray()) {
					std::pair<std::uint32_t, std::uint32_t> range;
					if (item.IsString() && ParseCodepointRange(item.GetString(), range)) { out.ranges.push_back(range); }
					else { warnings.push_back("\"ranges\": skipped an entry that isn't \"XXXX\" or \"XXXX-YYYY\" (hex)"); }
				}
			} else {
				bad("ranges", "a list of strings like \"00C0-00FF\"");
			}
		}
		if (rapidjson::Value const* v = member("sampleTextFiles")) {
			if (v->IsArray()) {
				for (auto const& item : v->GetArray()) {
					if (item.IsString()) { out.sampleTextFiles.emplace_back(item.GetString()); }
				}
			} else {
				bad("sampleTextFiles", "a list of file paths");
			}
		}
		readInt("atlasMaxSize", out.atlasMaxSize);

		for (std::string& change : out.Sanitize()) { warnings.push_back(std::move(change)); }
		return true;
	}

	std::string SerializeFontImportSettings(FontImportSettings const& settings) {
		rapidjson::StringBuffer buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
		w.SetIndent(' ', 2);
		w.StartObject();
		w.Key("outputType");   w.String(ToString(settings.outputType));
		w.Key("bitmapSizes");  w.StartArray(); for (int size : settings.bitmapSizes) { w.Int(size); } w.EndArray();
		w.Key("glyphSize");    w.Int(settings.glyphSize);
		w.Key("pixelRange");   w.Double(static_cast<double>(settings.pixelRange));
		w.Key("padding");      w.Int(settings.padding);
		w.Key("includeAscii"); w.Bool(settings.includeAscii);
		w.Key("ranges");       w.StartArray();
		for (auto const& [a, b] : settings.ranges) {
			const std::string text = FormatCodepointRange({ a, b });
			w.String(text.c_str());
		}
		w.EndArray();
		w.Key("sampleTextFiles"); w.StartArray(); for (auto const& file : settings.sampleTextFiles) { w.String(file.c_str()); } w.EndArray();
		w.Key("atlasMaxSize"); w.Int(settings.atlasMaxSize);
		w.EndObject();
		return std::string(buffer.GetString(), buffer.GetSize()) + "\n";
	}

	std::uint64_t ComputeFontSettingsHash(FontImportSettings const& settings, std::span<const std::string> sampleTexts) {
		// The canonical JSON covers every setting; the sample texts' contents are added after it
		// (each with its length, so moving text from one file to the next still changes the hash).
		const std::string json = SerializeFontImportSettings(settings);
		std::uint64_t hash = Fnv1a(kFnvOffset, json.data(), json.size());
		for (std::string const& text : sampleTexts) {
			const std::uint64_t size = text.size();
			hash = Fnv1a(hash, &size, sizeof(size));
			hash = Fnv1a(hash, text.data(), text.size());
		}
		return hash == 0 ? 1 : hash;   // 0 means "unknown" in FontData
	}

}	// NAMESPACE CRUCIBLE
