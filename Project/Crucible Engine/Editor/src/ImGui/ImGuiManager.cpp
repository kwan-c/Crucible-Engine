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
#include "Diagnostics/Logger.h"
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>
#include <imgui_internal.h>   // DockBuilder* API
#include <iostream>

// Include all other panels...
#include "ImGui/Panels/ImGuiPanel_Menu.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"


std::unique_ptr<Crucible::EditorManager> Crucible::EditorManager::s_Instance;

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

// ---- dock layout (begin) ----
namespace {

	using DockArea = Crucible::EditorPanel::DockArea;

	bool g_layoutPending = true;   // check once at startup whether a default layout is needed
	bool g_forceReset = false;     // set by ImGuiManager::ResetLayout()

	// True when imgui.ini already holds a layout the user made (split nodes, or windows docked in the root).
	// An untouched dockspace (single empty node) is NOT treated as a user layout.
	bool HasUserLayout(ImGuiID dockspace)
	{
		ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
		if (node == nullptr) return false;
		return !node->IsLeafNode() || node->Windows.Size > 0;
	}

	// Splits the dockspace according to the panels' m_docked / m_dockArea / m_dockRatio settings,
	// then docks each panel's window into its area.
	void BuildDefaultLayout(ImGuiID dockspace, ImVec2 size)
	{
		auto& panels = Crucible::EditorManager::GetInstance().m_panels;

		ImGui::DockBuilderRemoveNode(dockspace);
		ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace, size);

		// Split order: Left, Right (full height), then Top, Bottom (inside what is left).
		// Center is whatever remains.
		struct Split { DockArea area; ImGuiDir dir; };
		constexpr Split splits[] = {
			{ DockArea::Left,   ImGuiDir_Left  },
			{ DockArea::Right,  ImGuiDir_Right },
			{ DockArea::Top,    ImGuiDir_Up    },
			{ DockArea::Bottom, ImGuiDir_Down  },
		};

		ImGuiID areaId[5] = {};   // indexed by static_cast<int>(DockArea)
		ImGuiID remaining = dockspace;

		for (Split const& s : splits)
		{
			bool requested = false;
			float ratio = 0.25f;
			for (auto& entry : panels)
			{
				auto const& p = entry.second;
				if (p->m_docked && p->m_dockArea == s.area) { requested = true; ratio = p->m_dockRatio; break; }
			}
			if (!requested) continue;

			ImGuiID atDir = 0, opposite = 0;
			ImGui::DockBuilderSplitNode(remaining, s.dir, ratio, &atDir, &opposite);
			areaId[static_cast<int>(s.area)] = atDir;
			remaining = opposite;
		}
		areaId[static_cast<int>(DockArea::Center)] = remaining;

		for (auto& entry : panels)
		{
			auto const& p = entry.second;
			if (p->m_docked && !p->m_panelName.empty())
				ImGui::DockBuilderDockWindow(p->m_panelName.c_str(), areaId[static_cast<int>(p->m_dockArea)]);
		}

		ImGui::DockBuilderFinish(dockspace);
	}
}
// ---- dock layout (end) ----

void Crucible::ImGuiManager::Init(const ImGuiVulkanContext& context) {
	
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

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
	ImGui_ImplGlfw_InitForVulkan(context.window, true);

	ImGui_ImplVulkan_InitInfo initInfo = {};
	initInfo.Instance = context.instance;
    initInfo.PhysicalDevice = context.physicalDevice;
    initInfo.Device = context.device;
	initInfo.QueueFamily = context.graphicsQueueFamily;
	initInfo.Queue = context.graphicsQueue;

	//initInfo.DescriptorPool = context.descriptorPool;   // not ready yet � leave unset (VK_NULL_HANDLE)
	initInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;   // let ImGui create+own its own pool
    initInfo.MinImageCount = context.minImageCount;
    initInfo.ImageCount = context.imageCount;
	initInfo.PipelineInfoMain.MSAASamples = context.msaaSamples;
    initInfo.CheckVkResultFn = CheckVulkanResult;

	// Dynamic rendering
	initInfo.UseDynamicRendering = true;

	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;

	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &context.colorFormat;

	ImGui_ImplVulkan_Init(&initInfo);

	CRU_INFO("ImGui Successfully Initialized");
}

void Crucible::ImGuiManager::Update() {
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace", 0, window_flags);
	ImGui::PopStyleVar(3);

	ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");

	// Build the default layout on the first frame (unless imgui.ini holds a layout the user made),
	// or whenever View > Reset Layout asked for it. Must happen before DockSpace() and the panels' Begin().
	if (g_layoutPending || g_forceReset)
	{
		bool const force = g_forceReset;
		g_layoutPending = false;
		g_forceReset = false;
		if (force || !HasUserLayout(dockspace_id))
			BuildDefaultLayout(dockspace_id, viewport->WorkSize);
	}

	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

	ImGui::End();


	for (auto& currentPanel : EditorManager::GetInstance().m_panels)
	{
		if (currentPanel.second->m_makeFocus)
		{
			ImGui::SetNextWindowFocus();
			currentPanel.second->m_makeFocus = false;
		}
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			currentPanel.second->m_rightClick = true;
		}
		else
		{
			currentPanel.second->m_rightClick = false;
		}

		currentPanel.second->Update();
	}
}

void Crucible::ImGuiManager::BeginFrame() {

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	//ImGuizmo begin frame
	//ImGuizmo::BeginFrame();
}

void Crucible::ImGuiManager::Render() {
	ImGui::Render();
}

// Must be called while the frame's render pass is open (from Layer::OnRender).
void Crucible::ImGuiManager::Draw(const Renderer& ren) {
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *ren.GetCurrentCommandBuff());

	// NOTE: multi-viewport support (ImGuiConfigFlags_ViewportsEnable) needs
	// ImGui::UpdatePlatformWindows() / RenderPlatformWindowsDefault() AFTER the main render
	// pass has ended. Viewports are not enabled in Init() today, so that is omitted here.
}

void Crucible::ImGuiManager::ResetLayout() {
	g_forceReset = true;
}

void Crucible::ImGuiManager::Exit() {
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

Crucible::EditorManager& Crucible::EditorManager::GetInstance()
{
	// TODO: insert return statement here
	if (s_Instance == nullptr)
	{
		s_Instance = std::make_unique<EditorManager>();

		// Add in Editor Panels here...
		// 
		//s_Instance->m_panels[ImguiPanels::EDITORSCENE] = std::make_shared<EditorScene>();
		s_Instance->m_panels[ImguiPanels::MENU] = std::make_shared<Menu>();
		//s_Instance->m_panels[ImguiPanels::HIERARCHY] = std::make_shared<Hierarchy>();
		s_Instance->m_panels[ImguiPanels::INSPECTOR] = std::make_shared<Inspector>();
		//s_Instance->m_panels[ImguiPanels::ASSETBROWSER] = std::make_shared<AssetBrowser>();
		//s_Instance->m_panels[ImguiPanels::PLAYPAUSE] = std::make_shared<PlayPause>();
		//s_Instance->m_panels[ImguiPanels::GAMESCENE] = std::make_shared<GameScene>();

		for (auto& panel : s_Instance->m_panels)
			panel.second->Init();
	}
	return *s_Instance;
}

float Crucible::EditorPanel::PushFontSize(float const fontScale)
{
	ImFont* const imfont = ImGui::GetFont();
	float const old_size = imfont->Scale;
	imfont->Scale = fontScale;
	ImGui::PushFont(imfont);
	return old_size;
}

void Crucible::EditorPanel::PopFontSize(float const prevScale)
{
	ImFont* const imfont = ImGui::GetFont();
	imfont->Scale = prevScale;
	ImGui::PopFont();
}
