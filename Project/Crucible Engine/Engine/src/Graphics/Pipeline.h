#pragma once

// STL
#include <string>
#include <vector>

namespace Crucible {

    struct PipelineConfigInfo {};

    class Pipeline {

    public:
        Pipeline(const std::string& vertFilepath, const std::string& fragFilepath, const PipelineConfigInfo& configInfo);

    private:
        static std::vector<char> ReadFile(const std::string& filepath);

        void CreateGraphicsPipeline(const std::string& vertFilepath, const std::string& fragFilepath, const PipelineConfigInfo& configInfo);

    };

}