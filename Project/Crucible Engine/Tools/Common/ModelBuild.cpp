/*!
* @file			ModelBuild.cpp
*
* @brief		See ModelBuild.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ModelBuild.h"

#include "AssetPipe/AssetRegistry.h"
#include "AssetPipe/GLTFLoader.h"
#include "Resources/CompiledModel.h"
#include "Resources/CompiledTexture.h"
#include "TextureBuild.h"

#include <fstream>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace Crucible::ModelBuild {
	namespace {
		void Check(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }

		void Replace(const fs::path& temporary, const fs::path& destination) {
#ifdef _WIN32
			Check(MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
			      "Cannot replace " + destination.string() + " (Windows error " + std::to_string(GetLastError()) + ")");
#else
			fs::rename(temporary, destination);
#endif
		}

		// Each image becomes a GPU-compressed texture (Tools/Common/TextureBuild, packed with Billet),
		// so the engine uploads it as it is - no image decoding (stb) at runtime. Images a material
		// uses for colour (base colour, emissive) get sRGB mipmaps (BC7); normal maps BC5 with
		// direction-averaged mipmaps (the shader rebuilds z); other data (metallic-roughness,
		// occlusion) BC7, averaged as stored.
		void CompileImages(ModelResource& m, uint64_t guid) {
			std::vector<bool> colour(m.images.size(), false), normal(m.images.size(), false);
			auto mark = [&](const ModelTextureInfo& s, std::vector<bool>& as) {
				if (s.texture < m.textures.size() && m.textures[s.texture].image < m.images.size()) as[m.textures[s.texture].image] = true;
			};
			for (const auto& material : m.materials) {
				mark(material.baseColor, colour); mark(material.emissive, colour); mark(material.normal, normal);
			}
			for (size_t i = 0; i < m.images.size(); ++i) {
				auto& image = m.images[i];
				TextureBuild::Options options; options.srgb = colour[i];
				if (normal[i] && !colour[i]) { options.format = CompiledTextureFormat::BC5; options.normalMap = true; }
				CompiledTexture texture = TextureBuild::Build(TextureBuild::DecodeImage(image.bytes), options);
				texture.assetGuid = guid;
				auto bytes = EncodeCompiledTexture(texture);
				CompiledTexture check; std::string error;
				const bool decoded = DecodeCompiledTexture(bytes, guid, check, error);   // before the message reads `error`
				Check(decoded && check.data == texture.data, "Embedded texture round trip failed: " + error);
				image.bytes = std::move(bytes);
				image.mimeType = CompiledTextureMimeType;
			}
		}
	}

	std::vector<unsigned char> ReadFile(const fs::path& path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		Check(bool(file), "Cannot open " + path.string());
		const auto size = file.tellg();
		Check(size >= 0 && uint64_t(size) <= MaxCompiledModelBytes, "File exceeds 1 GiB limit: " + path.string());
		std::vector<unsigned char> bytes(static_cast<size_t>(size));
		file.seekg(0);
		Check(bool(file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))), "Cannot read " + path.string());
		return bytes;
	}

	uint64_t SourceHash(std::span<const unsigned char> sourceBytes) {
		// Source fingerprints must be byte-exact: the registry's text-
		// tolerant hash deliberately folds CRLF, even in binary files.
		uint64_t hash = 1469598103934665603ull;
		for (auto byte : sourceBytes) { hash ^= byte; hash *= 1099511628211ull; }
		for (int i = 0; i < 8; ++i) { hash ^= uint8_t(TextureBuild::Version >> (8 * i)); hash *= 1099511628211ull; }   // texture output changed: rebuild
		return hash;
	}

	bool IsUpToDate(const fs::path& target, uint64_t guid, uint64_t sourceHash) {
		std::error_code ec;
		if (!fs::exists(target, ec)) return false;
		CompiledModelInfo cached;
		auto decoded = DecodeCompiledModel(ReadFile(target), guid, &cached);
		return decoded.ok && cached.sourceHash == sourceHash;
	}

	Output Compile(std::span<const unsigned char> sourceBytes, uint64_t guid, uint64_t sourceHash) {
		auto imported = GLTFLoader::LoadModelGLB(sourceBytes);
		Check(imported.ok, imported.error);
		CompileImages(imported.model, guid);
		const CompiledModelInfo info{ CompiledModelVersion, guid, sourceHash };
		Output output;
		output.bytes = EncodeCompiledModel(imported.model, info);
		auto decoded = DecodeCompiledModel(output.bytes, guid);
		Check(decoded.ok, "Compiled round trip failed: " + decoded.error);
		Check(EncodeCompiledModel(decoded.model, info) == output.bytes, "Compiled round trip changed model fields");
		output.model = std::move(decoded.model);
		return output;
	}

	void Write(const fs::path& path, const std::vector<unsigned char>& bytes, uint64_t guid, const std::string& virtualPath) {
		fs::create_directories(path.parent_path());
		auto temporary = path.parent_path() / ("." + path.filename().string() + ".tmp");
		auto meta = AssetRegistry::MetaPathFor(path);
		auto temporaryMeta = path.parent_path() / ("." + path.filename().string() + ".meta.tmp");
		try {
			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				Check(bool(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))), "Cannot write " + temporary.string());
				file.close(); Check(!file.fail(), "Cannot flush " + temporary.string());
			}
			Check(ReadFile(temporary) == bytes, "Compiled file read-back differs");
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

	std::string Summary(const ModelResource& m) {
		size_t primitives = 0, vertices = 0, indices = 0;
		for (const auto& mesh : m.meshes) for (const auto& p : mesh.primitives) {
			++primitives; vertices += p.vertices.size(); indices += p.indices.size();
		}
		return "nodes=" + std::to_string(m.nodes.size()) + " meshes=" + std::to_string(m.meshes.size())
			+ " primitives=" + std::to_string(primitives) + " vertices=" + std::to_string(vertices)
			+ " indices=" + std::to_string(indices) + " materials=" + std::to_string(m.materials.size())
			+ " skins=" + std::to_string(m.skins.size()) + " clips=" + std::to_string(m.animations.size());
	}
}
