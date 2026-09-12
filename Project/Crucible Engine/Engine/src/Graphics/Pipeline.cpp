#include "Pipeline.h"

// STL
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace Crucible {

    Pipeline::Pipeline(const std::string& vertFilepath, const std::string& fragFilepath, const PipelineConfigInfo& configInfo) {
        CreateGraphicsPipeline(vertFilepath, fragFilepath, configInfo);
    }

    std::vector<char> Pipeline::ReadFile(const std::string& filepath) {

        // ate: seek to end immediately, binary: read as binary.
        std::ifstream ifs{filepath, std::ios::ate | std::ios::binary};
        if (!ifs.is_open()) {
            throw std::runtime_error("[Graphics|Pipeline] Failed to open file: " + filepath);
        }

        // because of 'ate' flag, gets last position -> fileSize;
        size_t fileSize = static_cast<size_t>(ifs.tellg());
        std::vector<char> buffer(fileSize);

        ifs.seekg(0);
        ifs.read(buffer.data(), fileSize);


        ifs.close();
        return buffer;
    }

    void Pipeline::CreateGraphicsPipeline(const std::string& vertFilepath, const std::string& fragFilepath, const PipelineConfigInfo& configInfo) {

        auto vertCode = ReadFile(vertFilepath);
        auto fragCode = ReadFile(fragFilepath);

        std::cout << "[Graphics|Pipeline] Vertex shader code size: " << vertCode.size() << '\n';
        std::cout << "[Graphics|Pipeline] Fragment shader code size: " << fragCode.size() << '\n';

    }

}
