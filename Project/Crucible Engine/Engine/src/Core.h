#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <memory>
#include <vector>

#include "EngineExport.h"	// DLL export
#include "Systems/Layer.h"
#include "Systems/WindowManager.h"
#include "Graphics/RenderSystem.h"

namespace Crucible
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL member in an exported class; only used inside Engine.dll
#endif
    class ENGINE_API Core
    {
    public:
        Core();
        ~Core();

        // Core owns raw pointers and unique_ptr layers: it must never be copied.
        // (Also required because MSVC instantiates copy operations for dllexport classes.)
        Core(const Core&) = delete;
        Core& operator=(const Core&) = delete;

        // Core main functions
        bool Init();
        void Run();

        // Attaches a layer (calls OnAttach) and takes ownership. Layers are updated in the
        // order pushed, and detached in reverse order when Core shuts down.
        void PushLayer(std::unique_ptr<Layer> layer);

        WindowManager const* tempReturn() const {
            return m_window;
        };

        // Vulkan handle accessors (forwarded to Renderer).
        // Return VK_NULL_HANDLE when renderer is not available.
        VkInstance           GetVkInstance() const {
            return (m_renderer != nullptr) ? m_renderer->GetVkInstance() : VK_NULL_HANDLE;
        }
        VkPhysicalDevice     GetVkPhysicalDevice() const {
            return (m_renderer != nullptr) ? m_renderer->GetVkPhysicalDevice() : VK_NULL_HANDLE;
        }
        VkDevice             GetVkDevice() const {
            return (m_renderer != nullptr) ? m_renderer->GetVkDevice() : VK_NULL_HANDLE;
        }
        VkQueue              GetVkQueue() const {
            return (m_renderer != nullptr) ? m_renderer->GetVkQueue() : VK_NULL_HANDLE;
        }
        VkDescriptorPool     GetVkDescriptorPool() const {
            return (m_renderer != nullptr) ? m_renderer->GetVkDescriptorPool() : VK_NULL_HANDLE;
        }
        VkCommandPool        GetVkCommandPool() const {
            return (m_renderer != nullptr) ? m_renderer->GetVkCommandPool() : VK_NULL_HANDLE;
        }
        VkCommandBuffer      GetCurrentCommandBuffer() const {
            return (m_renderer != nullptr) ? m_renderer->GetCurrentCommandBuffer() : VK_NULL_HANDLE;
        }
        const VkFormat*      GetVkColorAttachmentFormats() const {
            return (m_renderer != nullptr) ? m_renderer->GetVkColorAttachmentFormats() : nullptr;
        }
        uint32_t             GetVkQueueFamily() const {
            return m_renderer->GetVkQueueFamily();
        }
        uint32_t             GetVkMinImageCount() const {
            return m_renderer->GetVkMinImageCount();
        }
        uint32_t             GetVkImageCount() const {
            return m_renderer->GetVkImageCount();
        }


    private:
        void CleanUp();

        WindowManager* m_window;
        Renderer* m_renderer;
        std::vector<std::unique_ptr<Layer>> m_layers;

        // Core systems
        //std::vector<BaseSystem*> m_systems;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}