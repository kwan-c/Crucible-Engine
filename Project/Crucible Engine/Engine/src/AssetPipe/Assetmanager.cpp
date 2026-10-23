/*************************************************************************
  \file       AssetManager.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       12 September 2026
  \version    1.0
  \brief
    Asset management implementation.
  \details
    Implements asset discovery, path resolution, file reading and caching
    for the Crucible engine.

    Discovery walks the Assets folder once at startup and records what it
    finds. Reading and caching happen per asset, on request. Every
    filesystem call uses the std::error_code overloads rather than the
    throwing ones, so a single unreadable file logs a message and is
    skipped instead of aborting a scan or bringing down the engine.
  \dependencies
    AssetManager.h, <algorithm>, <array>, <cctype>, <cstdlib>, <fstream>,
    <iomanip>, <iostream>, <Windows.h> (Windows only)
  \see
    AssetManager.h, AssetManagerTest.cpp
  \history
    - 1.0 (12 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "AssetManager.h"
#include "AssetRegistry.h"
#include "AssetPaths.h"
#include "Systems/ExecutablePath.h"
 
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <xresource_guid.h>


namespace fs = std::filesystem;
 
namespace {
 
    // How many parent folders to walk before giving up on finding "Assets".
    constexpr int MAX_SEARCH_DEPTH = 4;
 
    // The folder name the upward search looks for.
    constexpr const char* ASSET_ROOT_FOLDER_NAME = "Assets";

    // Environment variable that overrides the search, for packaged builds.
    constexpr const char* ASSET_ROOT_ENV_VAR     = "CRUCIBLE_ASSET_ROOT";
 
    /*************************************************************************
     \brief
        Whether a path should be left out of the index.

     \param[in]
        p The path to test.
     \return
        true if the file or folder should be skipped.

     \details
        Skips anything whose name begins with a dot. That covers .git and
        .vs, which would otherwise contribute thousands of irrelevant
        entries to the index.
     *************************************************************************/
    bool IsHiddenOrIgnored(const fs::path& p)
    {
        const std::string name = p.filename().string();
        return name.empty() || name.front() == '.';
    }
 
    /*************************************************************************
     \brief
        Format a byte count for display.

     \param[in]
        bytes The size to format.
     \return
        A short human-readable string such as "67.4 KB".
     *************************************************************************/
    std::string FormatSize(std::uintmax_t bytes)
    {
        char buffer[32] = {};
        if (bytes < 1024ull)
            std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
        else if (bytes < 1024ull * 1024ull)
            std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
        else
            std::snprintf(buffer, sizeof(buffer), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return std::string(buffer);
    }
}
 
namespace Crucible {
 
    // =======================================================================
    //  Helpers
    // =======================================================================
 
    const char* ToString(AssetType type)
    {
        switch (type)
        {
        case AssetType::Texture:  return "Texture";
        case AssetType::Model:    return "Model";
        case AssetType::Shader:   return "Shader";
        case AssetType::Audio:    return "Audio";
        case AssetType::Scene:    return "Scene";
        case AssetType::Prefab:   return "Prefab";
        case AssetType::Font:     return "Font";
        case AssetType::Material: return "Material";
        case AssetType::Script:   return "Script";
        case AssetType::Text:     return "Text";
        case AssetType::AnimatorController: return "AnimatorController";
        case AssetType::Unknown:  return "Unknown";
        case AssetType::Count:    break;
        }
        return "Unknown";
    }
 
    std::string AssetManager::ToLower(const std::string& str)
    {
        std::string out = str;
        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }
 
    std::string AssetManager::NormalizeSeparators(const std::string& path)
    {
        std::string out = path;
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    }
 
    std::string AssetManager::NormalizePath(const std::string& path)
    {
        std::string out = NormalizeSeparators(path);
 
        // Strip a leading "./" and any leading slash, so every virtual path is
        // unambiguously relative to the asset root.
        //
        // NOTE: absolute paths must use NormalizeSeparators instead - stripping
        // the leading slash off "/home/nel/Assets" or off a network share
        // "//server/share" quietly turns it into a relative path that opens
        // nothing.
        while (out.size() >= 2 && out[0] == '.' && out[1] == '/')
            out.erase(0, 2);
        while (!out.empty() && out.front() == '/')
            out.erase(0, 1);
 
        return out;
    }
 
    AssetType AssetManager::ClassifyExtension(const std::string& lowercaseExtension)
    {
        const std::string& e = lowercaseExtension;
 
        // Add a new asset type by adding one line here. That is the only place
        // the engine needs to learn about it.
        if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" ||
            e == ".bmp" || e == ".dds" || e == ".hdr")
            return AssetType::Texture;
 
        if (e == ".cmodel" || e == ".gltf" || e == ".glb" || e == ".fbx" || e == ".obj" || e == ".dae")
            return AssetType::Model;
 
        if (e == ".vert" || e == ".frag" || e == ".geom" || e == ".comp" ||
            e == ".glsl" || e == ".spv")
            return AssetType::Shader;
 
        if (e == ".wav" || e == ".mp3" || e == ".ogg" || e == ".flac" ||
            e == ".bank" || e == ".fsb")
            return AssetType::Audio;
 
        if (e == ".scene")  
            return AssetType::Scene;

        if (e == ".cprefab") 
            return AssetType::Prefab;

        if (e == ".mat")    
            return AssetType::Material;

        if (e == ".animcontroller")
            return AssetType::AnimatorController;
 
        if (e == ".ttf" || e == ".otf" || e == ".cfont")   // .cfont = compiled by the editor (Text/FontData.h)
            return AssetType::Font;
 
        if (e == ".cs")
            return AssetType::Script;
 
        if (e == ".json" || e == ".txt" || e == ".xml" ||
            e == ".yaml" || e == ".yml" || e == ".ini" || e == ".cfg")
            return AssetType::Text;
 
        return AssetType::Unknown;
    }
 
    // =======================================================================
    //  Finding the asset root
    // =======================================================================
 
    fs::path AssetManager::GetExecutableDirectory()
    {
        // Shared with the logger (crucible.log goes next to the .exe) - see Systems/ExecutablePath.h.
        return ExecutableDirectory();
    }
 
    fs::path AssetManager::FindAssetRoot(const std::string& explicitRoot)
    {
        std::error_code ec;
 
        // 1. An explicit path wins.
        if (!explicitRoot.empty())
        {
            const fs::path candidate{ explicitRoot };
            if (fs::is_directory(candidate, ec))
                return fs::weakly_canonical(candidate, ec);
 
            std::cout << "[Assetmanager] Explicit root '" << explicitRoot
                      << "' is not a folder - falling back to search.\n";
        }
 
        // 2. Environment variable, useful for a packaged build.
#ifdef _WIN32
        {
            char*  envValue = nullptr;
            size_t envSize  = 0;
            if (_dupenv_s(&envValue, &envSize, ASSET_ROOT_ENV_VAR) == 0 && envValue != nullptr)
            {
                const fs::path candidate{ envValue };
                std::free(envValue);
                if (fs::is_directory(candidate, ec))
                    return fs::weakly_canonical(candidate, ec);
            }
        }
#else
        if (const char* envValue = std::getenv(ASSET_ROOT_ENV_VAR))
        {
            const fs::path candidate{ envValue };
            if (fs::is_directory(candidate, ec))
                return fs::weakly_canonical(candidate, ec);
        }
#endif
 
        // 3. Walk upward looking for a folder called "Assets".
        const std::array<fs::path, 2> startPoints = {
            GetExecutableDirectory(),
            fs::current_path(ec)
        };
 
        for (const fs::path& start : startPoints)
        {
            if (start.empty())
                continue;
 
            fs::path dir = start;
            for (int depth = 0; depth < MAX_SEARCH_DEPTH; ++depth)
            {
                const fs::path candidate = dir / ASSET_ROOT_FOLDER_NAME;
                if (fs::is_directory(candidate, ec))
                    return fs::weakly_canonical(candidate, ec);
 
                const fs::path parent = dir.parent_path();
                if (parent.empty() || parent == dir)
                    break;
                dir = parent;
            }
        }
 
        return {};
    }
 
    // =======================================================================
    //  Lifetime
    // =======================================================================
 
    bool AssetManager::Init(const std::string& explicitRoot)
    {
        // No explicit root: use the engine-wide one (AssetPaths.h), so the index, the shaders and
        // the config file can never disagree about which Assets folder they are using.
        m_assetRoot = explicitRoot.empty() ? AssetRoot() : FindAssetRoot(explicitRoot);
 
        if (m_assetRoot.empty())
        {
            std::cout << "[Assetmanager] Could not find an '" << ASSET_ROOT_FOLDER_NAME
                      << "' folder. Searched upward from the executable and the "
                      << "working directory.\n";
            m_assetRootStr.clear();
            m_initialized = false;
            return false;
        }
 
        m_assetRootStr = NormalizeSeparators(m_assetRoot.string());
        m_initialized  = true;
 
        std::cout << "[Assetmanager] Root: " << m_assetRootStr << "\n";
 
        return Refresh();
    }
 
    void AssetManager::Shutdown()
    {
        UnloadAll();
        m_index.clear();
        m_lookup.clear();
        m_registry.Clear();
        m_assetRoot.clear();
        m_assetRootStr.clear();
        m_initialized = false;
    }
 
    // =======================================================================
    //  INDEX
    // =======================================================================
 
    bool AssetManager::Refresh()
    {
        m_index.clear();
        m_lookup.clear();
        m_registry.Clear();
 
        if (!m_initialized || m_assetRoot.empty())
            return false;
 
        std::error_code ec;
        fs::recursive_directory_iterator it{
            m_assetRoot, fs::directory_options::skip_permission_denied, ec };
 
        if (ec)
        {
            std::cout << "[Assetmanager] Could not scan the asset root: " << ec.message() << "\n";
            return false;
        }
 
        const fs::recursive_directory_iterator end{};
        for (; it != end; it.increment(ec))
        {
            // One unreadable file must not abort the whole scan.
            if (ec)
            {
                std::cout << "[Assetmanager] Scan error: " << ec.message() << "\n";
                ec.clear();
                continue;
            }
 
            const fs::path& p = it->path();
 
            if (IsHiddenOrIgnored(p))
            {
                if (it->is_directory(ec))
                    it.disable_recursion_pending();
                ec.clear();
                continue;
            }
 
            if (!it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }
 
            // A .meta describes an asset - it is not one itself, so it never
            // belongs in the index. AssetRegistry collects them separately.
            if (AssetRegistry::IsMetaFile(p))
            {
                continue;
            }

            AssetEntry entry;
            entry.absolutePath = NormalizeSeparators(p.string());
 
            const fs::path relative = fs::relative(p, m_assetRoot, ec);
            entry.virtualPath = ec ? p.filename().string()
                                   : NormalizePath(relative.string());
            ec.clear();
 
            entry.extension = ToLower(p.extension().string());
            entry.type      = ClassifyExtension(entry.extension);
 
            entry.sizeBytes = fs::file_size(p, ec);
            if (ec) { entry.sizeBytes = 0; ec.clear(); }
 
            const fs::file_time_type writeTime = fs::last_write_time(p, ec);
            entry.lastWriteTime = ec ? 0
                : static_cast<std::int64_t>(writeTime.time_since_epoch().count());
            ec.clear();
 
            m_index.push_back(std::move(entry));
        }
 
        // Give every asset its stable guid before anything is sorted: adopt
        // if from a .meta carry it across a rename, or mint a new one.
        m_registry.Reconcile(m_index, m_assetRoot);

        // Alphabetical, so console output and any future browser list are stable.
        std::sort(m_index.begin(), m_index.end(),
            [](const AssetEntry& a, const AssetEntry& b) {
                return a.virtualPath < b.virtualPath;
            });
 
        // The sort moved everything, so the lookup table is built afterwards.
        // Building it during the scan would leave every index pointing at the
        // wrong entry - a nasty, silent bug.
        m_lookup.reserve(m_index.size());
        for (std::size_t i = 0; i < m_index.size(); ++i)
            m_lookup.emplace(m_index[i].virtualPath, i);
 
        std::cout << "[Assetmanager] Indexed " << m_index.size() << " file(s).\n";
        return true;
    }
 
    const AssetEntry* AssetManager::Find(const std::string& virtualPath) const
    {
        const auto it = m_lookup.find(NormalizePath(virtualPath));
        return (it == m_lookup.end()) ? nullptr : &m_index[it->second];
    }
 
    bool AssetManager::Exists(const std::string& virtualPath) const
    {
        return Find(virtualPath) != nullptr;
    }
 
    std::vector<const AssetEntry*> AssetManager::GetByType(AssetType type) const
    {
        std::vector<const AssetEntry*> out;
        for (const AssetEntry& entry : m_index)
        {
            if (entry.type == type)
                out.push_back(&entry);
        }
        return out;
    }
 
    std::size_t AssetManager::CountByType(AssetType type) const
    {
        std::size_t count = 0;
        for (const AssetEntry& entry : m_index)
        {
            if (entry.type == type)
                ++count;
        }
        return count;
    }
 
    fs::path AssetManager::Resolve(const std::string& virtualPath) const
    {
        if (const AssetEntry* entry = Find(virtualPath))
            return fs::path{ entry->absolutePath };
 
        // Not indexed - perhaps created since the last Refresh(). Still hand back
        // a usable path so the caller can decide what to do.
        return m_assetRoot / fs::path{ NormalizePath(virtualPath) };
    }
 
    // =======================================================================
    //  IDENTITY
    // =======================================================================

    const AssetEntry* AssetManager::FindByGuid(xresource::instance_guid id) const
    {
        // The registry knows which path carries the guid; the index knows
        // everything else about that path.
        if (const std::string* path = m_registry.GetPath(id))
        {
            return Find(*path);
        }
        return nullptr;
    }

    xresource::instance_guid AssetManager::GetGuid(const std::string& virtualPath) const
    {
        if (const AssetEntry* entry = Find(virtualPath))
        {
            return entry->guid;
        }
        return xresource::instance_guid{};
    }

    // =======================================================================
    //  READ
    // =======================================================================
 
    bool AssetManager::ReadBytes(const std::string& virtualPath,
                                 std::vector<unsigned char>& outBytes) const
    {
        outBytes.clear();
 
        const fs::path full = Resolve(virtualPath);
 
        std::ifstream file{ full, std::ios::binary | std::ios::ate };
        if (!file.is_open())
        {
            std::cout << "[Assetmanager] Could not open '" << virtualPath << "'.\n";
            return false;
        }
 
        const std::streamsize size = file.tellg();
        if (size < 0)
            return false;
 
        file.seekg(0, std::ios::beg);
        outBytes.resize(static_cast<std::size_t>(size));
 
        if (size > 0 && !file.read(reinterpret_cast<char*>(outBytes.data()), size))
        {
            std::cout << "[Assetmanager] Could not read '" << virtualPath << "'.\n";
            outBytes.clear();
            return false;
        }
 
        return true;
    }
 
    bool AssetManager::ReadText(const std::string& virtualPath, std::string& outText) const
    {
        std::vector<unsigned char> bytes;
        if (!ReadBytes(virtualPath, bytes))
        {
            outText.clear();
            return false;
        }
 
        outText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    }
 
    bool AssetManager::WriteText(const std::string& virtualPath, const std::string& text)
    {
        if (!m_initialized)
            return false;
 
        const fs::path full = m_assetRoot / fs::path{ NormalizePath(virtualPath) };
 
        std::error_code ec;
        fs::create_directories(full.parent_path(), ec);
 
        std::ofstream file{ full, std::ios::binary | std::ios::trunc };
        if (!file.is_open())
        {
            std::cout << "[Assetmanager] Could not open '" << virtualPath << "' for writing.\n";
            return false;
        }
 
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file.good();
    }
 
    // =======================================================================
    //  CACHE
    // =======================================================================
 
    std::shared_ptr<const AssetData> AssetManager::Load(const std::string& virtualPath)
    {
        const std::string key = NormalizePath(virtualPath);
 
        // Already loaded - hand back the same copy, no disk access.
        const auto cached = m_cache.find(key);
        if (cached != m_cache.end())
            return cached->second;
 
        std::vector<unsigned char> bytes;
        if (!ReadBytes(key, bytes))
            return nullptr;              // missing asset -> null, never an exception
 
        auto data = std::make_shared<AssetData>();
        data->virtualPath = key;
        data->bytes       = std::move(bytes);
 
        if (const AssetEntry* entry = Find(key))
            data->type = entry->type;
 
        m_cache[key] = data;
 
        std::cout << "[Assetmanager] Loaded " << key
                  << " (" << FormatSize(data->Size()) << ")\n";
 
        return data;
    }
 
    bool AssetManager::IsLoaded(const std::string& virtualPath) const
    {
        return m_cache.find(NormalizePath(virtualPath)) != m_cache.end();
    }
 
    bool AssetManager::Unload(const std::string& virtualPath)
    {
        const std::string key = NormalizePath(virtualPath);
 
        // Erasing only drops the cache's own reference. Anyone still holding the
        // shared_ptr keeps a valid copy until they are finished with it.
        if (m_cache.erase(key) == 0)
            return false;
 
        std::cout << "[Assetmanager] Unloaded " << key << "\n";
        return true;
    }
 
    bool AssetManager::Reload(const std::string& virtualPath)
    {
        const std::string key = NormalizePath(virtualPath);
        Unload(key);
        return Load(key) != nullptr;
    }
 
    void AssetManager::UnloadAll()
    {
        m_cache.clear();
    }
 
    // =======================================================================
    //  DEBUG
    // =======================================================================
 
    void AssetManager::PrintIndex() const
    {
        std::cout << "\n===================== Asset Index =====================\n";
 
        if (!m_initialized)
        {
            std::cout << "  Not initialized - no asset root.\n";
            std::cout << "=======================================================\n\n";
            return;
        }
 
        //std::cout << "  Root : " << m_assetRootStr << "\n";
        std::cout << "  Files: " << m_index.size() << "\n";
 
        for (int i = 0; i < static_cast<int>(AssetType::Count); ++i)
        {
            const AssetType type = static_cast<AssetType>(i);
 
            const std::vector<const AssetEntry*> ofType = GetByType(type);
            if (ofType.empty())
                continue;
 
            std::cout << "\n" << ToString(type) << " (" << ofType.size() << ")\n";
            
            for (const AssetEntry* entry : ofType)
            {
                std::cout << "  " << AssetRegistry::GuidToString(entry->guid)
                          << "  " << std::setw(60) << std::left << entry->virtualPath
                          << FormatSize(entry->sizeBytes) << "\n";
            }
        }
 
        std::cout << "=======================================================\n\n";
    }
 
    void AssetManager::PrintLoaded() const
    {
        std::cout << "\n==================== Loaded Assets ====================\n";
 
        if (m_cache.empty())
        {
            std::cout << "  Nothing loaded yet. The index knows about "
                      << m_index.size() << " file(s); none have been read.\n";
        }
        else
        {
            std::size_t totalBytes = 0;
            for (const auto& [path, data] : m_cache)
            {
                std::cout << "      " << std::setw(44) << std::left << path
                          << std::setw(10) << std::left << ToString(data->type)
                          << FormatSize(data->Size()) << "\n";
                totalBytes += data->Size();
            }
            std::cout << "\n  " << m_cache.size() << " of " << m_index.size()
                      << " indexed file(s) loaded, " << FormatSize(totalBytes)
                      << " in memory.\n";
        }
 
        std::cout << "=======================================================\n\n";
    }
 
}
