/*************************************************************************
  \file       AssetManager.h
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       12 September 2026
  \version    1.0
  \brief
    Declares the asset management system for the Crucible engine.
  \details
    This file declares the AssetManager, which is responsible for three
    things: discovering every file inside the project's Assets folder,
    reading those files from disk on request, and caching what has been
    read so that asking twice only costs one disk access.

    It also declares the supporting types: AssetType (what kind of asset a
    file is), AssetEntry (one discovered file) and AssetData (the contents
    of one file once it has been read).

    The system deliberately stops at raw bytes. Decoding an image into
    pixels, parsing a mesh, or handing a buffer to FMOD are separate steps
    owned by the renderer and the audio system respectively. Keeping those
    out means this file compiles without any graphics or audio dependency
    and survives the engine changing rendering backend.
  \dependencies
    EngineExport.h, <cstdint>, <filesystem>, <memory>, <string>,
    <unordered_map>, <vector>
  \see
    AssetManager.cpp
  \history
    - 1.0 (12 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once
 
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <xresource_guid.h>

#include "EngineExport.h"
#include "AssetRegistry.h"

namespace Crucible {
 
    /*************************************************************************
     \brief
        The category an asset belongs to, derived from its file extension.

     \details
        Used to group assets in the editor and to decide which system should
        be given the file. This describes only what kind of thing the file
        is; it says nothing about whether the file has been read yet.

     \note
        Count is a sentinel marking the end of the list. It is never a valid
        asset type, and exists so loops can iterate every real type.
     *************************************************************************/
    enum class AssetType : std::uint8_t
    {
        Unknown = 0,
        Texture,
        Model,
        Shader,
        Audio,
        Scene,
        Prefab,
        Font,
        Material,
        Script,
        Text,
 
        Count
    };
 
    /*************************************************************************
     \brief
        Convert an AssetType to a printable name.

     \param[in]
        type The asset type to convert.
     \return
        A null-terminated string such as "Texture". Never null.

     \note
        Returns "Unknown" for any unrecognised value, so this is safe to
        call on a type read from a file or cast from an integer.
     *************************************************************************/
    ENGINE_API const char* ToString(AssetType type);
 
    /*************************************************************************
     \brief
        One file discovered inside the Assets folder.

     \details
        This is an INDEX entry. It records that a file exists and describes
        it, but holds none of its contents - an AssetEntry for a 200 MB
        video costs the same few hundred bytes as one for a text file.

        virtualPath is the engine-facing name for the asset. It is always
        relative to the asset root and always uses forward slashes, for
        example "Textures/player.png". Code outside AssetManager should use
        virtual paths exclusively and never handle an absolute path, so that
        moving, zipping or cloning the project cannot invalidate an asset
        reference.

     \note
        lastWriteTime is recorded but not yet used. It is stored so that a
        future hot-reload feature can detect a file changing on disk without
        needing a second scan.
     *************************************************************************/
    struct ENGINE_API AssetEntry
    {
        std::string    virtualPath;                         // Path relative to the asset root, forward slashes
        std::string    absolutePath;                        // Full path on this machine
        std::string    extension;                           // Lowercase, includes the dot
        AssetType      type          = AssetType::Unknown;
        std::uintmax_t sizeBytes     = 0;                   // Size on disk, recorded during the scan
        std::int64_t   lastWriteTime = 0;                   //!< Filesystem timestamp, for future hot reload

        // Stable identity, loaded from(or written to) the asset's .meta file.
        // Unlike virtualPath this survives the file being renamed or moved, so
        // it is what scenes, prefabs and materials should reference.
        xresource::instance_guid guid;

        // FNV-1a hash of the file's contents. Used to recognise an asset whose
        // filename changed: same bytes, same asset, keep the same guid.
        std::uint64_t contentHash = 0;
    };
 
    /*************************************************************************
     \brief
        The contents of one asset, once it has actually been read from disk.

     \details
        This is a CACHE entry, and it holds raw bytes only. Turning those
        bytes into something usable - decoding a PNG into pixels, compiling
        shader source, handing a buffer to FMOD - is the job of whichever
        system asked for the asset.

        Keeping AssetManager at the byte level is what allows it to compile
        with no graphics or audio dependency, and is why a change of
        rendering backend does not affect this file.

     \note
        Instances are handed out as std::shared_ptr, so a caller keeps a
        valid copy even if the cache later drops the asset.
     *************************************************************************/
    struct ENGINE_API AssetData
    {
        std::string                virtualPath;
        AssetType                  type = AssetType::Unknown;
        std::vector<unsigned char> bytes;
 
        /*********************************************************************
         \brief  Whether this asset holds any data.
         \return true when at least one byte was read.
         *********************************************************************/
        bool        IsValid() const { return !bytes.empty(); }

        /*********************************************************************
         \brief  The size of the asset in memory.
         \return Number of bytes held.
         *********************************************************************/
        std::size_t Size()    const { return bytes.size(); }
 
        /*********************************************************************
         \brief
            Interpret the bytes as text.

         \return
            A copy of the contents as a std::string.

         \details
            Convenience for the text-based assets: shaders, configs, scripts
            and JSON. Calling this on a binary asset such as a PNG produces a
            string of unprintable characters rather than an error, so only use
            it where the asset is known to be text.
         *********************************************************************/
        std::string AsText() const
        {
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
    };
 
    /*************************************************************************
     \brief
        Discovers, reads and caches the project's assets.

     \details
        The class has three responsibilities, and the distinction between
        the first and the third is the most important idea in it:

          INDEX - find the Assets folder, walk it, and record every file
                  along with its type and size. Runs once at startup and
                  covers everything. Cheap, because no file is opened.

          READ  - open a file and pull its bytes off disk, on request.

          CACHE - keep what has been read, so a second request for the same
                  asset costs nothing.

        The index covers every file; the cache holds only what something has
        actually asked for. An asset browser is therefore built from the
        index alone and never needs a single asset loaded - which is what
        stops a project with gigabytes of textures from taking minutes to
        start.

     \par Usage
     \code
        Crucible::AssetManager assets;
        assets.Init();                                 // find and index
        assets.PrintIndex();                           // see what was found

        auto data = assets.Load("Textures/player.png"); // read and cache
        if (data)
            std::cout << data->Size() << " bytes\n";
     \endcode

     \note
        Non-copyable. The manager owns the cache that every handed-out
        asset pointer refers to, so there must only ever be one of it.
     *************************************************************************/
    class ENGINE_API AssetManager
    {
    public:
        AssetManager() = default;
        ~AssetManager() = default;
 
        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;
 
        /*********************************************************************
         \brief
            Locate the asset root and build the index.

         \param[in]
            explicitRoot Optional path to use as the asset root. When empty,
            the root is found automatically.
         \return
            true if an asset root was found and scanned, false otherwise.

         \details
            When no explicit root is given, three strategies are tried in
            order: the CRUCIBLE_ASSET_ROOT environment variable, then a
            search upward from the executable's folder for a directory named
            "Assets", then the same search from the working directory.

            Searching upward matters because Visual Studio runs the game from
            build/Game/Debug, several levels below the project root, and that
            depth changes with the generator or when the game is packaged. A
            hardcoded relative path such as "../../../Assets/" breaks in all
            of those cases; the upward search works from the build tree, an
            install folder and a zipped release without configuration.

         \note
            Returning false is not fatal. The engine can continue and report
            the problem rather than failing to start.
         *********************************************************************/
        bool Init(const std::string& explicitRoot = {});

        /*********************************************************************
         \brief
            Release the index and everything held in the cache.

         \details
            Assets still referenced elsewhere stay alive until those holders
            release them, because assets are handed out as shared pointers.
         *********************************************************************/
        void Shutdown();
 
        /*********************************************************************
         \brief  Whether Init() successfully found an asset root.
         \return true when the manager is usable.
         *********************************************************************/
        bool IsInitialized() const { return m_initialized; }
 
        // ===================================================================
        //  INDEX - what exists. None of this touches the disk.
        // ===================================================================
 
        /*********************************************************************
         \brief
            Scan the asset root again and rebuild the index.

         \return
            true if the scan completed, false if there is no asset root.

         \details
            Picks up files added, removed or renamed since the last scan.
            Cheap enough to call from a key press or an editor button.

         \note
            Does not disturb the cache. An asset already loaded stays loaded,
            even if its file has since been deleted from disk.
         *********************************************************************/
        bool Refresh();
 
        /*********************************************************************
         \brief  The resolved asset root.
         \return Absolute path to the Assets folder, using forward slashes.
         *********************************************************************/
        const std::string& GetAssetRoot() const { return m_assetRootStr; }
 
        /*********************************************************************
         \brief
            Look up one asset in the index.

         \param[in]
            virtualPath Path relative to the asset root, for example
            "Textures/player.png". Backslashes are accepted and normalised.
         \return
            Pointer to the entry, or nullptr if no such asset is indexed.

         \note
            The pointer is invalidated by the next call to Refresh() or
            Shutdown(), so do not store it across those calls.
         *********************************************************************/
        const AssetEntry* Find(const std::string& virtualPath) const;

        /*********************************************************************
         \brief  Whether an asset is present in the index.
         \param[in] virtualPath Path relative to the asset root.
         \return true if the file was found during the scan.
         *********************************************************************/
        bool Exists(const std::string& virtualPath) const;

        /*********************************************************************
         \brief
            Every indexed asset, sorted by virtual path.

         \return
            Const reference to the full index.

         \details
            This is what an asset browser iterates to draw its rows. It costs
            nothing, because the index holds descriptions rather than
            contents.
         *********************************************************************/
        const std::vector<AssetEntry>& GetAll() const { return m_index; }

        /*********************************************************************
         \brief
            Every indexed asset of one type.

         \param[in]
            type The category to filter by.
         \return
            Pointers to the matching entries, in index order. Empty if none.

         \details
            Gives an asset browser one tab per category without any filtering
            logic of its own.

         \note
            The returned pointers follow the same lifetime rule as Find():
            they do not survive a Refresh().
         *********************************************************************/
        std::vector<const AssetEntry*> GetByType(AssetType type) const;

        /*********************************************************************
         \brief  How many indexed assets have a given type.
         \param[in] type The category to count.
         \return Number of matching assets.
         *********************************************************************/
        std::size_t CountByType(AssetType type) const;

        /*********************************************************************
         \brief  How many assets were found in total.
         \return Size of the index.
         *********************************************************************/
        std::size_t GetAssetCount() const { return m_index.size(); }
 
        // ===================================================================
        //  IDENTITY - stable guids, independent of filename.
        // ===================================================================

        /*********************************************************************
         \brief
            Look up an asset by its stable guid.

         \param[in]
            id The asset's guid, as stored in its .meta file.
         \return
            Pointer to the entry, or nullptr if no asset carries that guid.

         \details
            This is the lookup a scene or prefab file uses. Because the guid
            does not change when a file is renamed or moved, a reference saved
            weeks ago still resolves.

            Delegates to AssetRegistry for the guid -> path step, then looks
            that path up in the index.
         *********************************************************************/
        const AssetEntry* FindByGuid(xresource::instance_guid id) const;

        /*********************************************************************
         \brief  The guid of an asset, found by path.
         \param[in] virtualPath Path relative to the asset root.
         \return The asset's guid, or an empty guid if it is not indexed.
         *********************************************************************/
        xresource::instance_guid GetGuid(const std::string& virtualPath) const;

        /*********************************************************************
         \brief  The registry that owns guids and .meta files.
         \return Const reference, for guid -> path lookups.
         \note
            Guid text formatting lives there too: AssetRegistry::GuidToString.
         *********************************************************************/
        const AssetRegistry& GetRegistry() const { return m_registry; }

        /*********************************************************************
         \brief
            Convert a virtual path into a real filesystem path.

         \param[in]
            virtualPath Path relative to the asset root.
         \return
            An absolute path suitable for opening.

         \details
            Needed by third-party libraries that insist on opening files
            themselves rather than accepting a memory buffer - FMOD being
            the obvious one in this project.

         \note
            Returns a plausible path even for an asset that is not indexed,
            so the caller can decide how to handle a file created since the
            last Refresh().
         *********************************************************************/

        std::filesystem::path Resolve(const std::string& virtualPath) const;
 
        // ===================================================================
        //  READ - straight from disk every time, with no caching.
        // ===================================================================
 
        /*********************************************************************
         \brief
            Read a file's raw bytes from disk.

         \param[in]
            virtualPath Path relative to the asset root.
         \param[out]
            outBytes Receives the file contents. Cleared on failure.
         \return
            true if the file was read, false if it could not be opened.

         \note
            Bypasses the cache entirely, reading from disk on every call.
            Use Load() for anything requested more than once.
         *********************************************************************/
        bool ReadBytes(const std::string& virtualPath, std::vector<unsigned char>& outBytes) const;

        /*********************************************************************
         \brief
            Read a file's contents as text.

         \param[in]
            virtualPath Path relative to the asset root.
         \param[out]
            outText Receives the file contents. Cleared on failure.
         \return
            true if the file was read, false otherwise.
         *********************************************************************/
        bool ReadText(const std::string& virtualPath, std::string& outText) const;
 
        /*********************************************************************
         \brief
            Write text to a file inside the asset root.

         \param[in]
            virtualPath Destination path relative to the asset root.
         \param[in]
            text The contents to write.
         \return
            true if the file was written successfully.

         \details
            Creates any missing parent folders. Overwrites an existing file.

         \note
            Present for the serialization work to come - saving a scene,
            writing a .meta file - rather than for loading assets.
         *********************************************************************/
        bool WriteText(const std::string& virtualPath, const std::string& text);
 
        // ===================================================================
        //  CACHE - read once, keep the result.
        // ===================================================================
 
        /*********************************************************************
         \brief
            Get an asset's contents, reading from disk only the first time.

         \param[in]
            virtualPath Path relative to the asset root.
         \return
            Shared pointer to the asset's data, or nullptr if the file could
            not be read.

         \details
            On the first call the file is read and stored. Every later call
            returns the same object without touching the disk.

            The return type is deliberate. Handing out a reference into the
            cache would leave callers holding a dangling reference the moment
            Unload(), Reload() or UnloadAll() ran. A shared pointer means
            dropping an asset from the cache only releases the cache's own
            claim on it; anyone still using it keeps a valid copy until they
            are finished.

         \note
            A missing file returns nullptr and does not throw. A missing
            asset is a content problem, not a reason to bring the game down,
            so callers should check for null and substitute a placeholder.
         *********************************************************************/
        std::shared_ptr<const AssetData> Load(const std::string& virtualPath);
 
        /*********************************************************************
         \brief  Whether an asset is currently held in the cache.
         \param[in] virtualPath Path relative to the asset root.
         \return true if the asset has been loaded and not since unloaded.
         *********************************************************************/
        bool IsLoaded(const std::string& virtualPath) const;

        /*********************************************************************
         \brief  How many assets are currently cached.
         \return Number of assets held in memory.
         *********************************************************************/
        std::size_t GetLoadedCount() const { return m_cache.size(); }
 
        /*********************************************************************
         \brief
            Drop one asset from the cache.

         \param[in]
            virtualPath Path relative to the asset root.
         \return
            true if the asset was cached and has been removed.

         \details
            Frees the memory but leaves the file on disk and the entry in the
            index. The next Load() will read it again.

         \note
            Callers already holding this asset keep a valid copy.
         *********************************************************************/
        bool Unload(const std::string& virtualPath);

        /*********************************************************************
         \brief
            Re-read an asset from disk, discarding the cached copy.

         \param[in]
            virtualPath Path relative to the asset root.
         \return
            true if the asset was read again successfully.

         \details
            Used to pick up a file edited while the engine is running.
         *********************************************************************/
        bool Reload(const std::string& virtualPath);

        /*********************************************************************
         \brief  Empty the cache, keeping the index intact.
         *********************************************************************/
        void UnloadAll();
 
        // ===================================================================
        //  DEBUG - console output, standing in for the editor UI.
        // ===================================================================
 
        /*********************************************************************
         \brief
            Print every indexed asset to stdout, grouped by type.

         \details
            The quickest way to confirm the engine found what you expect.
            The paths it prints are exactly the strings Load() accepts.
         *********************************************************************/
        void PrintIndex() const;

        /*********************************************************************
         \brief
            Print the contents of the cache to stdout.

         \details
            Shows which assets are currently in memory and how much they
            occupy. Run it before loading anything and it reports that the
            index is full while nothing has been read - which is the
            difference between discovering and loading, made visible.
         *********************************************************************/
        void PrintLoaded() const;
 
        // ===================================================================
        //  HELPERS
        // ===================================================================
 
        /*********************************************************************
         \brief
            Work out which AssetType a file extension belongs to.

         \param[in]
            lowercaseExtension Extension including the dot, already
            lowercased, for example ".png".
         \return
            The matching type, or AssetType::Unknown if unrecognised.

         \note
            This is the single place the engine learns about a new kind of
            asset. Supporting one more file format is one more line here.
         *********************************************************************/
        static AssetType ClassifyExtension(const std::string& lowercaseExtension);
 
        /*********************************************************************
         \brief
            Convert backslashes to forward slashes, changing nothing else.

         \param[in]
            path The path to convert.
         \return
            The same path using forward slashes.

         \note
            Safe for ABSOLUTE paths: it preserves a leading "/" on Linux and
            a leading "//" on a Windows network share. Use this, not
            NormalizePath, whenever the path is absolute.
         *********************************************************************/
        static std::string NormalizeSeparators(const std::string& path);
 
        /*********************************************************************
         \brief
            Convert a path into the canonical virtual-path form.

         \param[in]
            path The path to convert.
         \return
            Forward slashes, with any leading "./" and "/" removed.

         \details
            Guarantees that two spellings of the same asset - "Textures/a.png"
            and "Textures\\a.png" - produce the same cache and index key.

         \note
            For VIRTUAL paths only. Virtual paths are always relative to the
            asset root, so stripping a leading slash is correct for them and
            wrong for anything absolute: it would turn "/home/nel/Assets"
            into a relative path that opens nothing.
         *********************************************************************/
        static std::string NormalizePath(const std::string& path);
 
        /*********************************************************************
         \brief  Convert a string to lowercase.
         \param[in] str The string to convert.
         \return A lowercased copy.
         *********************************************************************/
        static std::string ToLower(const std::string& str);
 
    private:
        /*********************************************************************
         \brief
            Work out where the Assets folder is.

         \param[in]
            explicitRoot A caller-supplied path, or an empty string.
         \return
            Absolute path to the asset root, or an empty path if none found.

         \details
            Tries, in order: the explicit path, the CRUCIBLE_ASSET_ROOT
            environment variable, then an upward walk from the executable's
            folder and from the working directory.
         *********************************************************************/
        static std::filesystem::path FindAssetRoot(const std::string& explicitRoot);

        /*********************************************************************
         \brief
            The folder the running executable lives in.

         \return
            Absolute path to that folder, or the working directory as a
            fallback.

         \note
            The executable's own folder is used rather than the working
            directory because a debugger can set the working directory to
            anywhere, while the executable's location is fixed.
         *********************************************************************/
        static std::filesystem::path GetExecutableDirectory();
 
        std::filesystem::path m_assetRoot;      // Resolved Assets folder
        std::string           m_assetRootStr;   // Same path, forward slashes, for display
 
        // The index: every file that exists.
        std::vector<AssetEntry>                      m_index;
        std::unordered_map<std::string, std::size_t> m_lookup;  // virtualPath -> index

        // Owns every asset's permanent guid and the .meta files that store
        // them. Reconciled at the end of each Refresh().
        AssetRegistry m_registry;
 
        // The cache: only what has actually been read.
        std::unordered_map<std::string, std::shared_ptr<const AssetData>> m_cache;
 
        bool m_initialized = false;
    };
 
}