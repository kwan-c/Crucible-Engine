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
#include "Resource/AssetManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>

#ifdef _WIN32
    // WIN32_LEAN_AND_MEAN trims rarely-used headers; NOMINMAX stops Windows.h
    // defining min/max as macros, which would break std::min and std::max.
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace {

    //! How many parent folders to check before giving up on finding "Assets".
    constexpr int MAX_SEARCH_DEPTH = 10;

    //! The folder name the upward search looks for.
    constexpr const char* ASSET_ROOT_FOLDER_NAME = "Assets";

    //! Environment variable that overrides the search, for packaged builds.
    constexpr const char* ASSET_ROOT_ENV_VAR = "CRUCIBLE_ASSET_ROOT";

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

    /*************************************************************************
     \brief
        Convert an AssetType to a printable name.

     \param[in]
        type The asset type to convert.
     \return
        A null-terminated string such as "Texture". Never null.

     \note
        Listing every enumerator explicitly, with no default case, means the
        compiler warns if a new AssetType is added and not handled here.
     *************************************************************************/
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
        case AssetType::Unknown:  return "Unknown";
        case AssetType::Count:    break;
        }
        return "Unknown";
    }

    /*************************************************************************
     \brief
        Convert a string to lowercase.

     \param[in]
        str The string to convert.
     \return
        A lowercased copy.

     \note
        Each character is cast to unsigned char before std::tolower is
        called. Passing a negative char to tolower is undefined behaviour,
        which can happen with accented characters in a filename.
     *************************************************************************/
    std::string AssetManager::ToLower(const std::string& str)
    {
        std::string out = str;
        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    /*************************************************************************
     \brief
        Convert backslashes to forward slashes, changing nothing else.

     \param[in]
        path The path to convert.
     \return
        The same path using forward slashes.

     \note
        Safe for absolute paths. Unlike NormalizePath it preserves a leading
        slash, so "/home/nel/Assets" and "//server/share" survive intact.
     *************************************************************************/
    std::string AssetManager::NormalizeSeparators(const std::string& path)
    {
        std::string out = path;
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    }

    /*************************************************************************
     \brief
        Convert a path into the canonical virtual-path form.

     \param[in]
        path The path to convert.
     \return
        Forward slashes, with any leading "./" and "/" removed.

     \details
        Every lookup key passes through here, so "Textures/a.png",
        "Textures\\a.png" and "./Textures/a.png" all resolve to the same
        asset.

     \note
        For virtual paths only. Virtual paths are relative to the asset root
        by definition, so removing a leading slash is correct for them - and
        actively wrong for an absolute path, which would be turned into a
        relative one that opens nothing. Use NormalizeSeparators for those.
     *************************************************************************/
    std::string AssetManager::NormalizePath(const std::string& path)
    {
        std::string out = NormalizeSeparators(path);

        while (out.size() >= 2 && out[0] == '.' && out[1] == '/')
            out.erase(0, 2);
        while (!out.empty() && out.front() == '/')
            out.erase(0, 1);

        return out;
    }

    /*************************************************************************
     \brief
        Work out which AssetType a file extension belongs to.

     \param[in]
        lowercaseExtension Extension including the dot, already lowercased.
     \return
        The matching type, or AssetType::Unknown if unrecognised.

     \note
        This is the only place the engine learns about a file format.
        Supporting a new one is a single line here - no other file changes.
     *************************************************************************/
    AssetType AssetManager::ClassifyExtension(const std::string& lowercaseExtension)
    {
        const std::string& e = lowercaseExtension;

        if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" ||
            e == ".bmp" || e == ".dds" || e == ".hdr")
            return AssetType::Texture;

        if (e == ".gltf" || e == ".glb" || e == ".fbx" || e == ".obj" || e == ".dae")
            return AssetType::Model;

        // .spv is compiled SPIR-V, which a Vulkan backend would use in place
        // of GLSL source. Both are classified as shaders.
        if (e == ".vert" || e == ".frag" || e == ".geom" || e == ".comp" ||
            e == ".glsl" || e == ".spv")
            return AssetType::Shader;

        if (e == ".wav" || e == ".mp3" || e == ".ogg" || e == ".flac" ||
            e == ".bank" || e == ".fsb")
            return AssetType::Audio;

        if (e == ".scene")  return AssetType::Scene;
        if (e == ".prefab") return AssetType::Prefab;
        if (e == ".mat")    return AssetType::Material;

        if (e == ".ttf" || e == ".otf")
            return AssetType::Font;

        if (e == ".lua" || e == ".cs")
            return AssetType::Script;

        if (e == ".json" || e == ".txt" || e == ".xml" ||
            e == ".yaml" || e == ".yml" || e == ".ini" || e == ".cfg")
            return AssetType::Text;

        return AssetType::Unknown;
    }

    /*************************************************************************
     \brief
        The folder the running executable lives in.

     \return
        Absolute path to that folder, or the working directory as a fallback.

     \details
        On Windows this asks the OS for the executable's own path. Elsewhere,
        and if that call fails, it falls back to the working directory.

     \note
        The executable's folder is preferred over the working directory
        because a debugger or a shortcut can set the working directory to
        anywhere, whereas the executable's location is fixed.
     *************************************************************************/
    fs::path AssetManager::GetExecutableDirectory()
    {
#ifdef _WIN32
        wchar_t buffer[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
            return fs::path(buffer).parent_path();
#endif
        std::error_code ec;
        const fs::path cwd = fs::current_path(ec);
        return ec ? fs::path{} : cwd;
    }

    /*************************************************************************
     \brief
        Work out where the Assets folder is.

     \param[in]
        explicitRoot A caller-supplied path, or an empty string.
     \return
        Absolute path to the asset root, or an empty path if none was found.

     \details
        Three strategies, in priority order:

          1. The explicit path, if one was given and is a real folder.
          2. The CRUCIBLE_ASSET_ROOT environment variable.
          3. An upward walk for a folder named "Assets", starting from the
             executable's folder and then from the working directory.

        The upward walk is what makes the same binary work everywhere.
        Visual Studio runs the game from build/Game/Debug, several levels
        below the project root, and that depth changes with the CMake
        generator or when the game is packaged for release. Walking up until
        an Assets folder appears handles the build tree, an install folder
        and a zipped release with no configuration at all.

     \note
        _dupenv_s is used instead of std::getenv on Windows because MSVC
        deprecates getenv and would raise C4996 at the project's /W4 level.
     *************************************************************************/
    fs::path AssetManager::FindAssetRoot(const std::string& explicitRoot)
    {
        std::error_code ec;

        // 1. An explicitly supplied path wins over everything else.
        if (!explicitRoot.empty())
        {
            const fs::path candidate{ explicitRoot };
            if (fs::is_directory(candidate, ec))
                return fs::weakly_canonical(candidate, ec);

            std::cout << "[Assets] Explicit root '" << explicitRoot
                      << "' is not a folder - falling back to search.\n";
        }

        // 2. Environment variable override.
#ifdef _WIN32
        {
            char*  envValue = nullptr;
            size_t envSize  = 0;
            if (_dupenv_s(&envValue, &envSize, ASSET_ROOT_ENV_VAR) == 0 && envValue != nullptr)
            {
                const fs::path candidate{ envValue };
                std::free(envValue);          // _dupenv_s allocates; we must free it
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

        // 3. Walk upward from each starting point looking for "Assets".
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

                // Stop at the filesystem root, where parent_path() stops changing.
                const fs::path parent = dir.parent_path();
                if (parent.empty() || parent == dir)
                    break;
                dir = parent;
            }
        }

        return {};
    }

    /*************************************************************************
     \brief
        Locate the asset root and build the index.

     \param[in]
        explicitRoot Optional path to use as the asset root.
     \return
        true if an asset root was found and scanned, false otherwise.

     \note
        A false return is recoverable. The engine can carry on and report the
        problem, which is more useful than refusing to start.
     *************************************************************************/
    bool AssetManager::Init(const std::string& explicitRoot)
    {
        m_assetRoot = FindAssetRoot(explicitRoot);

        if (m_assetRoot.empty())
        {
            std::cout << "[Assets] Could not find an '" << ASSET_ROOT_FOLDER_NAME
                      << "' folder. Searched upward from the executable and the "
                      << "working directory.\n";
            m_assetRootStr.clear();
            m_initialized = false;
            return false;
        }

        // NormalizeSeparators, not NormalizePath - this path is absolute.
        m_assetRootStr = NormalizeSeparators(m_assetRoot.string());
        m_initialized  = true;

        std::cout << "[Assets] Root: " << m_assetRootStr << "\n";

        return Refresh();
    }

    /*************************************************************************
     \brief
        Release the index and everything held in the cache.

     \note
        Assets still referenced elsewhere survive, because they are handed
        out as shared pointers and this only drops the cache's own claim.
     *************************************************************************/
    void AssetManager::Shutdown()
    {
        UnloadAll();
        m_index.clear();
        m_lookup.clear();
        m_assetRoot.clear();
        m_assetRootStr.clear();
        m_initialized = false;
    }

    /*************************************************************************
     \brief
        Scan the asset root and rebuild the index.

     \return
        true if the scan completed, false if there is no asset root.

     \details
        Walks the whole tree recursively and records one AssetEntry per file:
        its virtual path, absolute path, extension, type, size and timestamp.
        No file is opened, so this stays fast even on a large project.

     \note
        Filesystem errors are collected in an error_code and skipped rather
        than thrown. A single locked or unreadable file logs a line and the
        scan continues, instead of losing the entire index.
     *************************************************************************/
    bool AssetManager::Refresh()
    {
        m_index.clear();
        m_lookup.clear();

        if (!m_initialized || m_assetRoot.empty())
            return false;

        std::error_code ec;
        fs::recursive_directory_iterator it{
            m_assetRoot, fs::directory_options::skip_permission_denied, ec };

        if (ec)
        {
            std::cout << "[Assets] Could not scan the asset root: " << ec.message() << "\n";
            return false;
        }

        const fs::recursive_directory_iterator end{};
        for (; it != end; it.increment(ec))
        {
            if (ec)
            {
                std::cout << "[Assets] Scan error: " << ec.message() << "\n";
                ec.clear();
                continue;
            }

            const fs::path& p = it->path();

            if (IsHiddenOrIgnored(p))
            {
                // For a hidden folder, also stop the iterator descending into
                // it - otherwise .git alone would add thousands of entries.
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

            AssetEntry entry;
            entry.absolutePath = NormalizeSeparators(p.string());

            // The virtual path is the file's location relative to the root.
            // If that cannot be worked out, fall back to the bare filename so
            // the asset still appears rather than vanishing from the index.
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

        // Sort alphabetically so console output and any future browser list
        // appear in a stable, predictable order.
        std::sort(m_index.begin(), m_index.end(),
            [](const AssetEntry& a, const AssetEntry& b) {
                return a.virtualPath < b.virtualPath;
            });

        // The lookup table is built AFTER the sort, and this ordering matters.
        // Sorting moves every element, so indices recorded during the scan
        // would afterwards point at the wrong entries - a silent bug that
        // would make Find() return the wrong asset.
        m_lookup.reserve(m_index.size());
        for (std::size_t i = 0; i < m_index.size(); ++i)
            m_lookup.emplace(m_index[i].virtualPath, i);

        std::cout << "[Assets] Indexed " << m_index.size() << " file(s).\n";
        return true;
    }

    /*************************************************************************
     \brief
        Look up one asset in the index.

     \param[in]
        virtualPath Path relative to the asset root.
     \return
        Pointer to the entry, or nullptr if no such asset is indexed.

     \note
        The pointer does not survive the next Refresh() or Shutdown(), since
        both rebuild the underlying vector.
     *************************************************************************/
    const AssetEntry* AssetManager::Find(const std::string& virtualPath) const
    {
        const auto it = m_lookup.find(NormalizePath(virtualPath));
        return (it == m_lookup.end()) ? nullptr : &m_index[it->second];
    }

    /*************************************************************************
     \brief
        Whether an asset is present in the index.

     \param[in]
        virtualPath Path relative to the asset root.
     \return
        true if the file was found during the scan.
     *************************************************************************/
    bool AssetManager::Exists(const std::string& virtualPath) const
    {
        return Find(virtualPath) != nullptr;
    }

    /*************************************************************************
     \brief
        Every indexed asset of one type.

     \param[in]
        type The category to filter by.
     \return
        Pointers to the matching entries, in index order.

     \note
        Pointers into m_index, so they follow the same lifetime rule as
        Find(): they do not survive a Refresh().
     *************************************************************************/
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

    /*************************************************************************
     \brief
        How many indexed assets have a given type.

     \param[in]
        type The category to count.
     \return
        Number of matching assets.
     *************************************************************************/
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

    /*************************************************************************
     \brief
        Convert a virtual path into a real filesystem path.

     \param[in]
        virtualPath Path relative to the asset root.
     \return
        An absolute path suitable for opening.

     \note
        An unindexed asset still gets a plausible path back rather than an
        error, so a file created since the last Refresh() can still be
        opened by the caller if it chooses to.
     *************************************************************************/
    fs::path AssetManager::Resolve(const std::string& virtualPath) const
    {
        if (const AssetEntry* entry = Find(virtualPath))
            return fs::path{ entry->absolutePath };

        return m_assetRoot / fs::path{ NormalizePath(virtualPath) };
    }

    /*************************************************************************
     \brief
        Read a file's raw bytes from disk.

     \param[in]
        virtualPath Path relative to the asset root.
     \param[out]
        outBytes Receives the file contents. Cleared on failure.
     \return
        true if the file was read, false if it could not be opened.

     \details
        Opens the file positioned at the end (std::ios::ate) so its size can
        be read from the stream position, then rewinds and reads the whole
        thing in one go.

     \note
        Bypasses the cache. Use Load() for anything requested repeatedly.
     *************************************************************************/
    bool AssetManager::ReadBytes(const std::string& virtualPath,
                                 std::vector<unsigned char>& outBytes) const
    {
        outBytes.clear();

        const fs::path full = Resolve(virtualPath);

        std::ifstream file{ full, std::ios::binary | std::ios::ate };
        if (!file.is_open())
        {
            std::cout << "[Assets] Could not open '" << virtualPath << "'.\n";
            return false;
        }

        const std::streamsize size = file.tellg();
        if (size < 0)
            return false;

        file.seekg(0, std::ios::beg);
        outBytes.resize(static_cast<std::size_t>(size));

        // A zero-byte file is legitimate, so only treat a failed read as an
        // error when there was actually something to read.
        if (size > 0 && !file.read(reinterpret_cast<char*>(outBytes.data()), size))
        {
            std::cout << "[Assets] Could not read '" << virtualPath << "'.\n";
            outBytes.clear();
            return false;
        }

        return true;
    }

    /*************************************************************************
     \brief
        Read a file's contents as text.

     \param[in]
        virtualPath Path relative to the asset root.
     \param[out]
        outText Receives the file contents. Cleared on failure.
     \return
        true if the file was read, false otherwise.

     \note
        Reads in binary mode, so line endings are preserved exactly as they
        appear on disk rather than being translated.
     *************************************************************************/
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

    /*************************************************************************
     \brief
        Write text to a file inside the asset root.

     \param[in]
        virtualPath Destination path relative to the asset root.
     \param[in]
        text The contents to write.
     \return
        true if the file was written successfully.

     \details
        Creates any missing parent folders, then overwrites the destination.

     \note
        The index is not updated automatically. Call Refresh() afterwards if
        the new file needs to appear in the asset list.
     *************************************************************************/
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
            std::cout << "[Assets] Could not open '" << virtualPath << "' for writing.\n";
            return false;
        }

        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file.good();
    }

    /*************************************************************************
     \brief
        Get an asset's contents, reading from disk only the first time.

     \param[in]
        virtualPath Path relative to the asset root.
     \return
        Shared pointer to the asset's data, or nullptr if it could not be
        read.

     \details
        The cache is checked first. On a hit the stored object is returned
        with no disk access at all - which is why the "[Assets] Loaded"
        message appears only once per asset, and its absence on a second call
        is the proof that caching is working.

     \note
        A missing file returns nullptr rather than throwing. A missing asset
        is a content problem, and taking the whole game down over one absent
        texture would make it far harder to find.
     *************************************************************************/
    std::shared_ptr<const AssetData> AssetManager::Load(const std::string& virtualPath)
    {
        const std::string key = NormalizePath(virtualPath);

        const auto cached = m_cache.find(key);
        if (cached != m_cache.end())
            return cached->second;

        std::vector<unsigned char> bytes;
        if (!ReadBytes(key, bytes))
            return nullptr;

        auto data = std::make_shared<AssetData>();
        data->virtualPath = key;
        data->bytes       = std::move(bytes);

        // Carry the type across from the index, so callers can tell what they
        // were given without a second lookup.
        if (const AssetEntry* entry = Find(key))
            data->type = entry->type;

        m_cache[key] = data;

        std::cout << "[Assets] Loaded " << key
                  << " (" << FormatSize(data->Size()) << ")\n";

        return data;
    }

    /*************************************************************************
     \brief
        Whether an asset is currently held in the cache.

     \param[in]
        virtualPath Path relative to the asset root.
     \return
        true if the asset has been loaded and not since unloaded.
     *************************************************************************/
    bool AssetManager::IsLoaded(const std::string& virtualPath) const
    {
        return m_cache.find(NormalizePath(virtualPath)) != m_cache.end();
    }

    /*************************************************************************
     \brief
        Drop one asset from the cache.

     \param[in]
        virtualPath Path relative to the asset root.
     \return
        true if the asset was cached and has been removed.

     \note
        Erasing only releases the cache's reference. Any system still holding
        this asset keeps a valid copy until it releases it too, which is the
        whole reason Load() returns a shared pointer rather than a reference.
     *************************************************************************/
    bool AssetManager::Unload(const std::string& virtualPath)
    {
        const std::string key = NormalizePath(virtualPath);

        if (m_cache.erase(key) == 0)
            return false;

        std::cout << "[Assets] Unloaded " << key << "\n";
        return true;
    }

    /*************************************************************************
     \brief
        Re-read an asset from disk, discarding the cached copy.

     \param[in]
        virtualPath Path relative to the asset root.
     \return
        true if the asset was read again successfully.

     \details
        Used to pick up a file edited while the engine is running - tweaking
        a shader or a config without restarting.
     *************************************************************************/
    bool AssetManager::Reload(const std::string& virtualPath)
    {
        const std::string key = NormalizePath(virtualPath);
        Unload(key);
        return Load(key) != nullptr;
    }

    /*************************************************************************
     \brief
        Empty the cache, keeping the index intact.

     \note
        The engine still knows what assets exist afterwards; it simply holds
        none of them in memory.
     *************************************************************************/
    void AssetManager::UnloadAll()
    {
        m_cache.clear();
    }

    /*************************************************************************
     \brief
        Print every indexed asset to stdout, grouped by type.

     \details
        The fastest way to confirm the engine found what you expect. The
        paths printed are exactly the strings Load() accepts, so they can be
        copied straight out of the console.
     *************************************************************************/
    void AssetManager::PrintIndex() const
    {
        std::cout << "\n===================== Asset Index =====================\n";

        if (!m_initialized)
        {
            std::cout << "  Not initialized - no asset root.\n";
            std::cout << "=======================================================\n\n";
            return;
        }

        std::cout << "  Root : " << m_assetRootStr << "\n";
        std::cout << "  Files: " << m_index.size() << "\n";

        // Walk the AssetType list rather than the index, so assets appear
        // grouped by category. Types with nothing in them are skipped.
        for (int i = 0; i < static_cast<int>(AssetType::Count); ++i)
        {
            const AssetType type = static_cast<AssetType>(i);

            const std::vector<const AssetEntry*> ofType = GetByType(type);
            if (ofType.empty())
                continue;

            std::cout << "\n  " << ToString(type) << " (" << ofType.size() << ")\n";
            for (const AssetEntry* entry : ofType)
            {
                std::cout << "      " << std::setw(44) << std::left << entry->virtualPath
                          << FormatSize(entry->sizeBytes) << "\n";
            }
        }

        std::cout << "=======================================================\n\n";
    }

    /*************************************************************************
     \brief
        Print the contents of the cache to stdout.

     \details
        Shows which assets are in memory and how much they occupy. Calling it
        before loading anything reports that the index is full while nothing
        has been read, which makes the difference between discovering an
        asset and loading one visible at a glance.
     *************************************************************************/
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
