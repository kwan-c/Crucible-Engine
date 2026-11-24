/*************************************************************************
  \file       VulkanShader.h
  \brief
    Loading a compiled shader (.spv) from disk into a shader module. Shared
    by the render passes (Graphics/Passes/), which create their own pipelines.

    Shaders are always read from <Resources>/Shaders/ (see AssetPipe/AssetPaths.h),
    wherever the program was started from - so callers pass just the file
    name, e.g. LoadShaderModule(context, "DefaultShader.spv").
*************************************************************************/
#pragma once

#include "VulkanContext.h"
#include "AssetPipe/AssetPaths.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Crucible {

    /*********************************************************************
     \brief  Makes a shader module from SPIR-V that is already in memory (a
             shader the Editor has just compiled - see ShaderHotReload).
     \param[in] context  The VulkanContext to create it through.
     \param[in] spirv    The whole module, as 32-bit words.
     \return The shader module. Only needed while pipelines are being built.
     \throws std::runtime_error if it does not start like a SPIR-V module.
     *********************************************************************/
    [[nodiscard]] inline vk::raii::ShaderModule CreateShaderModule(VulkanContext& context, std::span<const uint32_t> spirv) {

        // Header (5 words) starting with SPIR-V's magic number: anything else would be handed
        // to the driver as it is, and the driver does not have to check.
        if (spirv.size() < 5 || spirv.front() != 0x07230203u) {
            throw std::runtime_error("[Renderer | Shader] That is not a SPIR-V module (" + std::to_string(spirv.size()) + " words)");
        }
        vk::ShaderModuleCreateInfo createInfo{
            .codeSize = spirv.size_bytes(),
            .pCode = spirv.data()
        };
        return { context.GetDevice(), createInfo };
    }

    /*********************************************************************
     \brief  Reads <Resources>/Shaders/<shaderName>: a compiled shader's
             SPIR-V, as 32-bit words. For a shader whose code is kept after
             its pipelines are built, to build more later (MaterialShaders).
     \param[in] shaderName  The compiled shader's file name only, e.g. "Post.spv" -
                            not a path. CompileShaders.cmake writes them into
                            Resources/Shaders.
     \return The whole file.
     \throws std::runtime_error if the name is a path, or the file is missing,
             unreadable, empty or not whole 32-bit SPIR-V words.
     *********************************************************************/
    [[nodiscard]] inline std::vector<uint32_t> LoadShaderWords(std::string_view shaderName) {

        // A name, never a path: keeps working-directory-relative loads ("../../../Assets/...")
        // from creeping back in, and keeps every shader inside Resources/Shaders.
        if (shaderName.empty() ||
            shaderName.find_first_of("/\\") != std::string_view::npos ||
            shaderName.find("..") != std::string_view::npos) {
            throw std::runtime_error("[Renderer | Shader] A shader is loaded by its file name in Resources/Shaders "
                                     "(e.g. \"Post.spv\"), not by a path: \"" + std::string(shaderName) + "\"");
        }

        const std::filesystem::path path = ResourcePath("Shaders") / shaderName;

        // std::filesystem::path opens with the wide-character API on Windows, so folders with
        // non-English names (common under OneDrive) work too.
        std::ifstream ifs(path, std::ios::ate | std::ios::binary);
        if (!ifs.is_open()) {
            throw std::runtime_error("[Renderer | Shader] Failed to open " + PathToUtf8(path) +
                                     " - is the Resources folder there, next to Assets? (Assets is searched "
                                     "for upward from the executable; set CRUCIBLE_ASSET_ROOT to point at it directly)");
        }

        // SPIR-V is a stream of 32-bit words: anything else is a truncated or wrong file, and
        // Vulkan would reject it far less readably than this.
        const std::streamoff byteCount = ifs.tellg();
        if (byteCount <= 0 || byteCount % static_cast<std::streamoff>(sizeof(uint32_t)) != 0) {
            throw std::runtime_error("[Renderer | Shader] " + PathToUtf8(path) + " is empty or not valid SPIR-V (" +
                                     std::to_string(byteCount) + " bytes) - try rebuilding to recompile the shaders");
        }

        // uint32_t storage, so the words are correctly aligned for pCode.
        std::vector<uint32_t> code(static_cast<size_t>(byteCount) / sizeof(uint32_t));
        ifs.seekg(0, std::ios::beg);
        if (!ifs.read(reinterpret_cast<char*>(code.data()), byteCount)) {
            throw std::runtime_error("[Renderer | Shader] Failed to read " + PathToUtf8(path));
        }
        return code;
    }

    /*********************************************************************
     \brief  Reads <Resources>/Shaders/<shaderName> and makes a shader module from it.
     \param[in] context     The VulkanContext to create it through.
     \param[in] shaderName  The compiled shader's file name only, e.g. "Post.spv" -
                            not a path (see LoadShaderWords).
     \return The shader module. Only needed while pipelines are being built.
     \throws std::runtime_error if the name is a path, or the file is missing,
             unreadable, empty or not whole 32-bit SPIR-V words.
     *********************************************************************/
    [[nodiscard]] inline vk::raii::ShaderModule LoadShaderModule(VulkanContext& context, std::string_view shaderName) {
        return CreateShaderModule(context, LoadShaderWords(shaderName));
    }

}   // NAMESPACE CRUCIBLE
