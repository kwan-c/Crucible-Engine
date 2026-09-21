#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <memory>

#include "EngineExport.h"	// DLL export
#include "Systems/Layer.h"
#include "Systems/WindowManager.h"
#include "Graphics/RenderSystem.h"
#include "ImGui/ImGuiManager.h"

namespace Crucible
{
    class ENGINE_API Core
    {
    public:
        Core();
        ~Core();

        // Core main functions
        bool Init();
        void Run();

        WindowManager const* tempReturn() const {
            return m_window;
        };

        // Vulkan handle accessors (forwarded to Renderer).
        // Return VK_NULL_HANDLE when renderer is not available.
        //VkInstance           GetVkInstance() const {
        //    return (m_renderer != nullptr) ? m_renderer->GetVkInstance() : VK_NULL_HANDLE;
        //}
        //VkPhysicalDevice     GetVkPhysicalDevice() const {
        //    return (m_renderer != nullptr) ? m_renderer->GetVkPhysicalDevice() : VK_NULL_HANDLE;
        //}
        //VkDevice             GetVkDevice() const {
        //    return (m_renderer != nullptr) ? m_renderer->GetVkDevice() : VK_NULL_HANDLE;
        //}
        //VkQueue              GetVkQueue() const {
        //    return (m_renderer != nullptr) ? m_renderer->GetVkQueue() : VK_NULL_HANDLE;
        //}
        VkDescriptorPool     GetVkDescriptorPool() const {
            return (m_renderer != nullptr) ? m_renderer->GetVkDescriptorPool() : VK_NULL_HANDLE;
        }
        VkCommandPool        GetVkCommandPool() const {
            return (m_renderer != nullptr) ? m_renderer->GetVkCommandPool() : VK_NULL_HANDLE;
        }
        const VkFormat*      GetVkColorAttachmentFormats() const {
            return (m_renderer != nullptr) ? m_renderer->GetVkColorAttachmentFormats() : nullptr;
        }
        //uint32_t             GetVkQueueFamily() const {
        //    return m_renderer->GetVkQueueFamily();
        //}
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

        // Core systems
        //std::vector<BaseSystem*> m_systems;
    };
}