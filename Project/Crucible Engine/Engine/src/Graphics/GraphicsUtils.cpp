#include "GraphicsUtils.h"

#include <fstream>
#include <stdexcept>

namespace Crucible::Graphics {

    std::vector<char> Utils::ReadFile(const std::string& filename) {

        std::ifstream ifs(filename, std::ios::ate | std::ios::binary);
        if (!ifs.is_open()) {
            throw std::runtime_error("[GraphicsUtils] Failed to open file: " + filename);
        }

        std::vector<char> buffer(ifs.tellg());
        ifs.seekg(0, std::ios::beg);
        ifs.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

        ifs.close();
        return buffer;
    }

}