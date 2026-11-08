/*!
* @file			main.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		CrucibleTextureCompiler: images in Assets (.png .jpg .jpeg .tga .bmp, .hdr skies) ->
*				GPU-compressed .bill / .sky files in Resources.
*
* For each image (Tools/Common/TextureImport does the work, shared with the editor's Texture
* Importer, so both make the same file):
*   1. Load it with stb_image (allowed in tools, not in the game).
*   2. Build the mipmaps: each level is the 2x2 average of the one before. Colour images are
*      averaged in linear light (sRGB decoded first), normal maps as directions.
*   3. Compress every level with Compressonator (CMP_Core), one 4x4 block at a time, on every
*      CPU core: BC7 for colour, BC4 for masks, BC5 for normal maps (or the format chosen).
*   4. Pack the compressed blocks with Billet (Compression/Billet.h, lossless) so the file
*      takes less disk space; the engine unpacks them when it loads the texture.
*   5. Write Resources/<same path>.bill and its .meta with the image's own GUID, so the engine
*      loads the .bill wherever the image's GUID is used.
*
* Skyboxes: an image whose name contains "_skybox" (e.g. aurora_green_skybox.png), or any .hdr,
* is a panorama, compiled to Resources/<same path>.sky instead (Resources/CompiledSky.h): six
* cube faces and the sky's lighting, baked here instead of at load, all BC6H.
*
* An optional "<image>.texsettings" next to the image changes how it is compiled - see
* TextureImport.h (the editor's Texture Importer writes it).
* Unchanged images are skipped: the .bill / .sky records a hash of the image and its settings.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "AssetPipe/AssetManager.h"
#include "AssetPipe/AssetRegistry.h"
#include "Resources/CompiledTexture.h"
#include "Resources/CompiledSky.h"
#include "TextureImport.h"   // Tools/Common
#include "AssetBuildReport.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

namespace fs = std::filesystem;
using namespace Crucible;

namespace {

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

    bool IsCompilable(const std::string& extension) {
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp";
    }

    // Removes this GUID's other compiled outputs - e.g. the .bill left from before the image was
    // renamed "*_skybox" - so only one compiled file claims the GUID.
    void RemoveOtherOutputs(const AssetManager& outputs, const AssetEntry& source, const fs::path& keep) {
        for (const AssetEntry& entry : outputs.GetAll()) {
            if (entry.guid != source.guid || (entry.extension != CompiledTextureExtension && entry.extension != CompiledSkyExtension)) continue;
            const fs::path file(entry.absolutePath);
            std::error_code ec;
            if (fs::equivalent(file, keep, ec)) continue;
            fs::remove(file, ec);
            fs::remove(AssetRegistry::MetaPathFor(file), ec);
            std::cout << "Removed " << entry.virtualPath << " (" << source.virtualPath << " is compiled to another file now)\n";
        }
    }

    // problems: each image that failed, for the --report file.
    int CompileTree(const fs::path& sourceRoot, const fs::path& outputRoot, std::vector<AssetBuildReport::Problem>& problems) {
        Check(fs::is_directory(sourceRoot) && fs::is_directory(outputRoot), "Source and runtime asset roots must exist");
        const auto sourcePath = fs::weakly_canonical(sourceRoot), outputPath = fs::weakly_canonical(outputRoot);
        auto within = [](const fs::path& a, const fs::path& b) {
            auto relative = a.lexically_relative(b);
            return !relative.empty() && *relative.begin() != "..";
        };
        Check(!within(sourcePath, outputPath) && !within(outputPath, sourcePath), "Source and runtime roots must be separate, non-nested directories");
        AssetManager sources, outputs;
        Check(sources.Init(sourcePath.string()) && outputs.Init(outputPath.string()), "Cannot index asset roots");

        size_t built = 0, unchanged = 0, failed = 0;
        std::unordered_set<std::string> destinations;
        for (const auto* source : sources.GetByType(AssetType::Texture)) {
            // "*_skybox" images and .hdr panoramas become .sky; other images .bill.
            const bool skybox = IsSkyboxSource(source->virtualPath);
            if (!IsCompilable(source->extension) && !(skybox && source->extension == ".hdr")) continue;
            const char* extension = skybox ? CompiledSkyExtension : CompiledTextureExtension;
            try {
                fs::path relative = fs::path(source->virtualPath); relative.replace_extension(extension);
                if (const auto* existing = outputs.FindByGuid(source->guid); existing && existing->extension == extension)
                    relative = existing->virtualPath;   // stable runtime path survives a source rename
                const auto target = fs::weakly_canonical(outputPath / relative);
                Check(within(target, outputPath), "Output escapes runtime asset root");
                Check(destinations.insert(AssetManager::ToLower(target.string())).second, "Multiple sources map to " + target.string());
                if (fs::exists(target)) {
                    AssetMeta meta;
                    Check(AssetRegistry::ReadMeta(AssetRegistry::MetaPathFor(target), meta) && meta.guid == source->guid,
                          "Refusing to overwrite a compiled asset with a different GUID: " + target.string());
                }

                const auto sourceBytes = Read(source->absolutePath);
                std::string settingsText;
                const TextureImport::Settings settings = TextureImport::ReadSettings(source->absolutePath, settingsText);
                const uint64_t sourceHash = TextureImport::SourceHash(sourceBytes, settingsText);

                if (TextureImport::IsUpToDate(target, source->guid.m_Value, sourceHash, skybox)) {
                    RemoveOtherOutputs(outputs, *source, target);
                    ++unchanged;
                    continue;
                }

                const auto start = std::chrono::steady_clock::now();
                const TextureImport::Compiled compiled = TextureImport::CompileImage(sourceBytes, settings, source->guid.m_Value, sourceHash, skybox);
                TextureImport::WriteCompiledFile(target, compiled.bytes, source->guid.m_Value, relative.generic_string());
                RemoveOtherOutputs(outputs, *source, target);

                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                std::cout << "Compiled " << relative.generic_string() << ": " << compiled.summary << " | " << seconds << " s\n";
                ++built;
            } catch (const std::exception& e) {
                std::cerr << "[TextureCompiler] " << source->virtualPath << ": " << e.what() << '\n'; ++failed;
                problems.push_back({ source->virtualPath, e.what() });
            }
        }
        std::cout << "[TextureCompiler] compiled=" << built << " unchanged=" << unchanged << " failed=" << failed << '\n';
        return failed ? 1 : 0;
    }

}

int main(int argc, char** argv) {
    try {
        // --compile-tree <Assets> <Resources> [--report <file>]: with --report, images that fail
        // are written to <file> instead of failing the build (see AssetBuildReport.h).
        const bool tree = argc >= 4 && std::string(argv[1]) == "--compile-tree";
        const bool report = tree && argc == 6 && std::string(argv[4]) == "--report";
        if (tree && (argc == 4 || report)) {
            std::vector<AssetBuildReport::Problem> problems;
            int result = 0;
            try { result = CompileTree(argv[2], argv[3], problems); }
            catch (const std::exception& e) {
                if (!report) throw;
                std::cerr << "[TextureCompiler] " << e.what() << '\n';
                problems.push_back({ "Assets (all images)", e.what() });
            }
            if (!report) return result;
            if (!AssetBuildReport::Write(argv[5], problems)) { std::cerr << "[TextureCompiler] Cannot write report " << argv[5] << '\n'; return 1; }
            if (!problems.empty())
                std::cout << "[TextureCompiler] warning: " << problems.size() << " image(s) failed to compile. The build carries on; "
                             "the editor lists them and blocks Play until they are fixed.\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--compare") {
            // Every BC format on this image, with its own .texsettings - the Texture Importer's comparison.
            std::string settingsText;
            const TextureImport::Settings settings = TextureImport::ReadSettings(argv[2], settingsText);
            const TextureImport::Comparison c = TextureImport::CompareFormats(Read(argv[2]), settings);
            std::cout << "Compared at " << c.width << 'x' << c.height << " (compiled at " << c.compiledWidth << 'x' << c.compiledHeight
                      << ", usage " << TextureImport::ToString(settings.usage) << ")\n";
            for (const TextureImport::ComparedFormat& f : c.formats) {
                std::cout << "  " << (f.format == CompiledTextureFormat::RGBA8 ? "Original" : ToString(f.format)) << ": GPU "
                          << f.gpuBytes / 1024 << " KB";
                if (f.format != CompiledTextureFormat::RGBA8) std::cout << ", PSNR " << f.psnr << " dB";
                std::cout << '\n';
            }
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--inspect" && fs::path(argv[2]).extension() == CompiledSkyExtension) {
            CompiledSky sky; std::string error;
            const bool decoded = DecodeCompiledSky(Read(argv[2]), 0, sky, error);
            Check(decoded, error);
            std::cout << "Crucible sky GUID=" << AssetRegistry::GuidToString(xresource::instance_guid{ sky.assetGuid })
                      << " faces " << sky.sky.width << 'x' << sky.sky.height << ' ' << ToString(sky.sky.format)
                      << ", reflections " << sky.environment.mips.size() << " levels from " << sky.environment.width
                      << ", diffuse light (sh[0]) " << sky.sh[0][0] << ' ' << sky.sh[0][1] << ' ' << sky.sh[0][2] << '\n';
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--inspect") {
            CompiledTexture texture; std::string error;
            const bool decoded = DecodeCompiledTexture(Read(argv[2]), 0, texture, error);
            Check(decoded, error);
            std::cout << "Crucible texture v" << texture.version << " GUID=" << AssetRegistry::GuidToString(xresource::instance_guid{ texture.assetGuid })
                      << ' ' << texture.width << 'x' << texture.height << ' ' << ToString(texture.format)
                      << ((texture.flags & CompiledTextureFlag_Srgb) ? " sRGB" : " linear")
                      << ((texture.flags & CompiledTextureFlag_HasAlpha) ? " alpha" : "")
                      << ((texture.flags & CompiledTextureFlag_Billet) ? " Billet-packed" : "") << '\n';
            for (size_t i = 0; i < texture.mips.size(); ++i)
                std::cout << "  mip " << i << ": " << texture.mips[i].width << 'x' << texture.mips[i].height << ", " << texture.mips[i].size << " bytes\n";
            return 0;
        }
        std::cerr << "Usage: CrucibleTextureCompiler --compile-tree <Assets> <Resources> [--report <file>]\n"
                     "       CrucibleTextureCompiler --inspect <file.bill | file.sky>\n"
                     "       CrucibleTextureCompiler --compare <image>\n";
        return 2;
    } catch (const std::exception& e) { std::cerr << "[TextureCompiler] " << e.what() << '\n'; return 1; }
}
