/*!
* @file			TextureImport.h
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			31 October 2026
*
* @brief		Importing one image: its settings (.texsettings) and compiling it to a .bill or
*				.sky. Shared by CrucibleTextureCompiler (every image, on each build) and the
*				editor's Texture Importer (one image, on Compile), so both make the same file.
*
* "<image>.texsettings" (JSON, next to the image; every key optional):
*   { "usage": "Color", "format": "Auto", "mipmaps": true, "quality": 0.1,
*     "maxSize": 2048, "billet": true, "faceSize": 512 }
*   usage   Color  colour art (albedo, emissive, UI)          -> BC7, mipmaps in linear light
*           Linear data in RGB (metallic-roughness, AO...)     -> BC7
*           Mask   one channel (the image's red)               -> BC4
*           Normal tangent-space normal map                    -> BC5 (x, y; the shader rebuilds z)
*   format  Auto (the usage's default above) or BC1 / BC3 / BC4 / BC5 / BC7 to choose yourself
*   quality 0..1: higher is slightly better and much slower to compile
*   maxSize a larger image is halved until both sides fit
*   billet  false stores the blocks unpacked (bigger file, slightly faster load)
*   faceSize skyboxes only ("*_skybox" images and .hdr): cube face size
*
* Tools only (stb_image and Compressonator through TextureBuild).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "Resources/CompiledTexture.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Crucible::TextureImport {

	enum class Usage : uint8_t { Color, Linear, Mask, Normal };

	/// Format::Auto takes the usage's default; the others are CompiledTextureFormat values.
	enum class Format : uint8_t { Auto, BC1, BC3, BC4, BC5, BC7 };

	struct Settings {
		Usage    usage    = Usage::Color;
		Format   format   = Format::Auto;
		bool     mipmaps  = true;
		float    quality  = 0.1f;
		uint32_t maxSize  = 2048;
		bool     billet   = true;
		uint32_t faceSize = 512;

		bool operator==(const Settings&) const = default;
	};

	const char* ToString(Usage usage);
	const char* ToString(Format format);

	/// The format a texture with these settings is compiled to (Auto resolved by usage).
	CompiledTextureFormat ResolveFormat(const Settings& settings);

	/// "<image>.texsettings" next to an image.
	std::filesystem::path SettingsPathFor(const std::filesystem::path& image);

	/// Reads an image's settings file (defaults if there is none). `text` gets the file's contents,
	/// which are part of the source hash. Throws std::runtime_error for a broken file.
	Settings ReadSettings(const std::filesystem::path& image, std::string& text);

	/// The settings as .texsettings JSON text.
	std::string WriteSettingsText(const Settings& settings);

	/// Byte-exact fingerprint of an image, its settings text and the compilers' versions: a
	/// compiled file whose recorded source hash matches this is up to date.
	uint64_t SourceHash(const std::vector<unsigned char>& sourceBytes, const std::string& settingsText);

	/// One compiled image, ready to write.
	struct Compiled {
		std::vector<unsigned char> bytes;     ///< the .bill or .sky file
		std::string                summary;   ///< one line for the log
	};

	/// Compiles an image to a .bill, or a .sky when `skybox`, and reads the result back as the
	/// engine will (throws if that does not give the same data). Throws std::runtime_error.
	Compiled CompileImage(const std::vector<unsigned char>& sourceBytes, const Settings& settings,
	                      uint64_t guid, uint64_t sourceHash, bool skybox);

	/// One format in a comparison: the image after a round trip through it.
	struct ComparedFormat {
		CompiledTextureFormat      format = CompiledTextureFormat::RGBA8;   ///< RGBA8 = the original
		std::vector<unsigned char> rgba;          ///< what the GPU would show (preview size)
		double                     psnr = 0.0;    ///< dB vs the original, over the channels the format keeps (0 for the original)
		uint64_t                   gpuBytes = 0;  ///< GPU memory of the real compiled texture, mips included
	};

	/// One mip level of the result preview.
	struct PreviewLevel {
		uint32_t width = 0, height = 0;
		std::vector<unsigned char> rgba;
	};

	struct Comparison {
		uint32_t width = 0, height = 0;                  ///< preview size
		uint32_t compiledWidth = 0, compiledHeight = 0;  ///< the real compiled size (maxSize)
		std::vector<ComparedFormat> formats;             ///< [0] = the original, then BC1, BC3, BC7, BC4, BC5
		/// The image with exactly these settings (format, usage, mipmaps, quality) at the preview
		/// size: level 0 and every mip level the compile would make.
		CompiledTextureFormat     resultFormat = CompiledTextureFormat::BC7;
		std::vector<PreviewLevel> resultLevels;
	};

	/// Compresses an image in every 8-bit BC format and decodes each back, on a copy shrunk to at
	/// most previewMax per side, so they can be judged side by side (the Texture Importer, and
	/// CrucibleTextureCompiler --compare). Throws std::runtime_error.
	Comparison CompareFormats(const std::vector<unsigned char>& sourceBytes, const Settings& settings, uint32_t previewMax = 512);

	/// Whether `file` (a .bill or .sky) was compiled from exactly this source (SourceHash).
	bool IsUpToDate(const std::filesystem::path& file, uint64_t guid, uint64_t sourceHash, bool skybox);

	/// Writes a compiled file and its .meta (with the source's GUID) safely: a temporary file is
	/// written, read back and only then swapped in. Throws std::runtime_error.
	void WriteCompiledFile(const std::filesystem::path& path, const std::vector<unsigned char>& bytes,
	                       uint64_t guid, const std::string& virtualPath);

}
