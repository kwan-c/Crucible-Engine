#pragma once

#include <vector>

namespace {
    struct RenderData {

    };
}

namespace Crucible {

    class Renderer {
        
    public:
        void Init();
        void Update(float dt);

    private:
        std::vector<RenderData> m_renderCache;

    };
}