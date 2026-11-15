/*************************************************************************
  \file       PipelineCache.cpp
  \brief
    See PipelineCache.h.
*************************************************************************/
#include "Graphics/Vulkan/PipelineCache.h"

#include "AssetPipe/AssetPaths.h"   // PathToUtf8
#include "Diagnostics/Logger.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <system_error>
#include <vector>

namespace Crucible {

    namespace {

        // More than any real cache; a larger file is something else and is not read.
        constexpr std::uintmax_t kMaxCacheBytes = 512ull * 1024ull * 1024ull;

        // What every pipeline cache starts with (VkPipelineCacheHeaderVersionOne), read field by
        // field so no struct packing is assumed.
        constexpr size_t kHeaderBytes = 16 + VK_UUID_SIZE;

        std::vector<char> ReadWholeFile(const std::filesystem::path& file) {
            std::error_code ec;
            const std::uintmax_t size = std::filesystem::file_size(file, ec);
            if (ec || size == 0 || size > kMaxCacheBytes) { return {}; }

            std::ifstream in(file, std::ios::binary);
            std::vector<char> bytes(static_cast<size_t>(size));
            if (!in || !in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) { return {}; }
            return bytes;
        }

        // True if `bytes` is a cache written by this GPU with this driver version.
        bool MatchesDevice(const std::vector<char>& bytes, const vk::PhysicalDeviceProperties& gpu) {
            if (bytes.size() < kHeaderBytes) { return false; }

            uint32_t header[4];   // headerSize, headerVersion, vendorID, deviceID
            std::memcpy(header, bytes.data(), sizeof(header));
            return header[0] >= kHeaderBytes && header[0] <= bytes.size()
                && header[1] == static_cast<uint32_t>(vk::PipelineCacheHeaderVersion::eOne)
                && header[2] == gpu.vendorID
                && header[3] == gpu.deviceID
                && std::memcmp(bytes.data() + sizeof(header), gpu.pipelineCacheUUID.data(), VK_UUID_SIZE) == 0;
        }

    }   // anonymous namespace

    void PipelineCache::Load(const vk::raii::PhysicalDevice& gpu, const vk::raii::Device& device, const std::filesystem::path& file) {

        m_file      = file;
        m_savedSize = 0;

        std::vector<char> bytes;
        if (!m_file.empty()) {
            bytes = ReadWholeFile(m_file);
            if (!bytes.empty() && !MatchesDevice(bytes, gpu.getProperties())) {
                CRU_INFO("[Renderer | PipelineCache] {} was written by another GPU or driver version - starting empty.", PathToUtf8(m_file));
                bytes.clear();
            }
        }

        if (!bytes.empty()) {
            try {
                const vk::PipelineCacheCreateInfo info{ .initialDataSize = bytes.size(), .pInitialData = bytes.data() };
                m_cache     = vk::raii::PipelineCache(device, info);
                m_savedSize = bytes.size();
                CRU_INFO("[Renderer | PipelineCache] Loaded {} KB from {}.", bytes.size() / 1024, PathToUtf8(m_file));
                return;
            } catch (const std::exception& e) {
                CRU_WARN("[Renderer | PipelineCache] The driver refused {} ({}) - starting empty.", PathToUtf8(m_file), e.what());
            }
        }

        m_cache = vk::raii::PipelineCache(device, vk::PipelineCacheCreateInfo{});

    }   // PipelineCache::Load()

    void PipelineCache::Save() {

        if (m_file.empty() || !*m_cache) { return; }

        try {
            const std::vector<uint8_t> data = m_cache.getData();
            if (data.size() < kHeaderBytes || data.size() == m_savedSize) { return; }   // empty, or nothing new

            std::error_code ec;
            std::filesystem::create_directories(m_file.parent_path(), ec);

            // Written beside it, then renamed over it: a crash or a second Editor never leaves
            // half a cache where the next start would read it.
            std::filesystem::path partial = m_file;
            partial += ".partial";
            {
                std::ofstream out(partial, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
                if (!out) {
                    CRU_WARN("[Renderer | PipelineCache] Could not write {}.", PathToUtf8(partial));
                    return;
                }
            }
            std::filesystem::rename(partial, m_file, ec);
            if (ec) {
                CRU_WARN("[Renderer | PipelineCache] Could not replace {}: {}", PathToUtf8(m_file), ec.message());
                std::filesystem::remove(partial, ec);
                return;
            }

            m_savedSize = data.size();
            CRU_INFO("[Renderer | PipelineCache] Saved {} KB to {}.", data.size() / 1024, PathToUtf8(m_file));
        } catch (const std::exception& e) {
            CRU_WARN("[Renderer | PipelineCache] Could not save the pipeline cache: {}", e.what());
        }

    }   // PipelineCache::Save()

}   // NAMESPACE CRUCIBLE
