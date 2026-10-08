/*************************************************************************
  \file       VulkanShader.h
  \brief
    Loading a compiled shader (.spv) from disk into a shader module. Shared
    by the render passes (Graphics/Passes/), which create their own pipelines.
*************************************************************************/
#pragma once

#include "VulkanContext.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Crucible {

    /*********************************************************************
     \brief  Reads a .spv file and makes a shader module from it.
     \param[in] context   The VulkanContext to create it through.
     \param[in] filename  Path to the .spv, relative to the working directory.
     \return The shader module. Only needed while pipelines are being built.
     \throws std::runtime_error if the file cannot be opened.
     *********************************************************************/
    [[nodiscard]] inline vk::raii::ShaderModule LoadShaderModule(VulkanContext& context, const std::string& filename) {

        std::ifstream ifs(filename, std::ios::ate | std::ios::binary);
        if (!ifs.is_open()) {
            throw std::runtime_error("[Renderer | Shader] Failed to open file: " + filename);
        }
        std::vector<char> code(static_cast<size_t>(ifs.tellg()));
        ifs.seekg(0, std::ios::beg);
        ifs.read(code.data(), static_cast<std::streamsize>(code.size()));

        vk::ShaderModuleCreateInfo createInfo{
            .codeSize = code.size() * sizeof(char),
            .pCode    = reinterpret_cast<const uint32_t*>(code.data())
        };
        return { context.GetDevice(), createInfo };
    }

}   // NAMESPACE CRUCIBLE
