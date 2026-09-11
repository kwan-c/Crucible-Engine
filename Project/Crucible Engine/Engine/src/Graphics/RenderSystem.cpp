#include "RenderSystem.h"

#if defined(INTELLISENSE) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <GLFW/glfw3.h>


// Sandbox testing.
namespace {

    float vertices[] = {
    -0.5f, -0.5f, 0.0f,
     0.5f, -0.5f, 0.0f,
     0.0f,  0.5f, 0.0f
    };

    void InitVulkan() {

    }


}

namespace Crucible {

    void Renderer::Init() {}

    void Renderer::Update(float dt) {

        // QUERY & CULL

        // POPULATE RENDER QUEUE

        // SORT & BATCH

        // EXECUTE GRAPHICS CALLS
    }

}