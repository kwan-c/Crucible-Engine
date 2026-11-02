#pragma once

#include "ModelResource.h"
#include <span>

namespace Crucible {
    // 2: images are stored GPU-compressed (CompiledTextureMimeType) instead of PNG/JPEG.
    inline constexpr uint32_t CompiledModelVersion = 2;
    // Limits apply to both compiler output and runtime input, before allocation.
    inline constexpr size_t MaxCompiledModelBytes = 1024ull * 1024 * 1024;

    struct CompiledModelInfo {
        uint32_t version = CompiledModelVersion;
        uint64_t assetGuid = 0;
        uint64_t sourceHash = 0;
    };

    // Explicit little-endian, fixed-width fields; never writes C++ object layouts.
    // Throws std::runtime_error for invalid data or unsupported sizes.
    std::vector<unsigned char> EncodeCompiledModel(const ModelResource& model,
                                                  const CompiledModelInfo& info);
    // Verifies the header, checksum and all references before exposing a resource.
    // expectedGuid == 0 is for offline inspection only. Failures return no model.
    ModelImportResult DecodeCompiledModel(std::span<const unsigned char> bytes,
                                         uint64_t expectedGuid = 0,
                                         CompiledModelInfo* info = nullptr);
}
