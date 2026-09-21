/*!
* @file			ImGuiManager.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		This file is to set up the ImGui Manager
* 
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/ImGuiManager.h"
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>
#include "Core.h"
#include <iostream>

// Include all other panels...


// Anonymous namespace...
namespace {
	static void CheckVulkanResult(VkResult err)
	{
		if (err != VK_SUCCESS)
		{
			std::cerr << "ImGui Vulkan Error: "
				<< err
				<< std::endl;
		}
	}
}

void Crucible::ImGuiManager::Init(Core const& core) {
	
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	//io.Fonts->Clear(); // clear all fonts, including defaults

	// add a font. since it has been cleared, this will become the default
	// the second parameter is the pixel sizes
	//ImFont* font = io.Fonts->AddFontFromFileTTF(
	//	"../Extern/imgui/misc/fonts/DroidSans.ttf", 24.f
	//);

	//if (font == nullptr) {
	//	std::cerr << "Warning: failed to load DroidSans.ttf, falling back to default ImGui font." << std::endl;
	//	io.Fonts->AddFontDefault();
	//}

	// --------------------------------------------------
	// Platform backend
	// --------------------------------------------------
	ImGui_ImplGlfw_InitForVulkan(core.tempReturn()->GetNativeHandle(), true);

	ImGui_ImplVulkan_InitInfo initInfo = {};
	//initInfo.Instance = core.GetVkInstance();
 //   initInfo.PhysicalDevice = core.GetVkPhysicalDevice();
 //   initInfo.Device = core.GetVkDevice();

	//initInfo.QueueFamily = core.GetVkQueueFamily();

	//initInfo.Queue = core.GetVkQueue();

	// initInfo.DescriptorPool = core.GetVkDescriptorPool();   // not ready yet — leave unset (VK_NULL_HANDLE)
	//initInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;   // let ImGui create+own its own pool

 //   initInfo.MinImageCount = core.GetVkMinImageCount();

 //   initInfo.ImageCount = core.GetVkImageCount();

	//initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

 //   initInfo.CheckVkResultFn = CheckVulkanResult;

	//// Dynamic rendering
	//initInfo.UseDynamicRendering = true;

	//initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
	//	VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

	//initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount =
	//	1;

	//initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = 
	//	core.GetVkColorAttachmentFormats();

	ImGui_ImplVulkan_Init(&initInfo);

	std::cout << "ImGui Initialized!!!";
}

void Crucible::ImGuiManager::Update() {

}

void Crucible::ImGuiManager::BeginFrame() {

}

void Crucible::ImGuiManager::EndFrame() {

}

void Crucible::ImGuiManager::Exit() {

}