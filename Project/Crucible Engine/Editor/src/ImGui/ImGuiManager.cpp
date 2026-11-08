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
#include "ImGui/EditorWidgets.h"
#include "ImGui/EditorTheme.h"
#include "Diagnostics/Logger.h"
#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>
#include <imgui_internal.h>   // DockBuilder* API
#include <ImGuizmo.h>         // the Scene view's move/rotate/scale gizmo
#include <algorithm>
#include <iostream>
#include <Core.h>
#include "ECS/ECSAudioSystem.h"
#include "Input/InputSystem.h"   // the game's cursor lock, see the end of ImGuiManager::Update
#include "UI/UISystem.h"         // UI::IsMenuOpen: the game's in-game menu owns the cursor while open
#include "UI/UIEvents.h"         // QuitRequested / CustomAction from UI buttons

// Include all other panels...
#include "ImGui/Panels/ImGuiPanel_Menu.h"
#include "ImGui/Panels/ImGuiPanel_PlayPause.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"
#include "ImGui/Panels/ImGuiPanel_Hierarchy.h"
#include "ImGui/Panels/ImGuiPanel_SceneView.h"
#include "ImGui/Panels/ImGuiPanel_GameView.h"
#include "ImGui/Panels/ImGuiPanel_UIView.h"
#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "ImGui/Panels/ImGuiPanel_Console.h"
#include "ImGui/Panels/ImGuiPanel_FontImporter.h"
#include "ImGui/Panels/ImGuiPanel_PackageGame.h"
#include "ImGui/Panels/ImGuiPanel_TextureImporter.h"
#include "ImGui/Panels/ImGuiPanel_Resources.h"
#include "ImGui/Panels/ImGuiPanel_Animator.h"
#include "ImGui/Panels/ImGuiPanel_Network.h"
#include "ImGui/Panels/ImGuiPanel_BehaviorGraph.h"
#include "ImGui/Panels/ImGuiPanel_StateGraph.h"
#include "Graphics/DebugDrawSystem.h"   // the Scene view's selection outline
#include "Assets/BuildProblems.h"       // failed asset/script compiles block Play


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

		// Split order matches Unity's default layout:
		//   Right  (full height)                -> Inspector
		//   Bottom (under everything but Right) -> Asset Browser / Console
		//   Left   (above Bottom)               -> Hierarchy
		//   Top    (above Center)
		// Center is whatever remains                -> Scene / Game
		struct Split { DockArea area; ImGuiDir dir; };
		constexpr Split splits[] = {
			{ DockArea::Right,  ImGuiDir_Right },
			{ DockArea::Bottom, ImGuiDir_Down  },
			{ DockArea::Left,   ImGuiDir_Left  },
			{ DockArea::Top,    ImGuiDir_Up    },
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

	// A panel added after imgui.ini was last saved has no entry in it. With a saved layout present,
	// BuildDefaultLayout() is skipped, so ImGui would open that panel as a floating window. Dock each
	// such panel next to a panel sharing its m_dockArea instead (e.g. the UI view joins Scene / Game).
	// Panels the ini already knows are left alone, so a panel the user moved stays where they put it.
	// Must run before the panels' first Begin(), which is when ImGui reads a window's dock id.
	void DockPanelsNewToIni()
	{
		auto& panels = Crucible::EditorManager::GetInstance().m_panels;

		for (auto& entry : panels)
		{
			auto const& p = entry.second;
			if (!p->m_docked || p->m_panelName.empty()) continue;
			if (ImGui::FindWindowSettingsByID(ImHashStr(p->m_panelName.c_str())) != nullptr) continue;   // known to the ini

			for (auto& other : panels)
			{
				auto const& q = other.second;
				if (q == p || !q->m_docked || q->m_dockArea != p->m_dockArea || q->m_panelName.empty()) continue;

				ImGuiWindowSettings const* settings = ImGui::FindWindowSettingsByID(ImHashStr(q->m_panelName.c_str()));
				if (settings != nullptr && settings->DockId != 0)
				{
					ImGui::DockBuilderDockWindow(p->m_panelName.c_str(), settings->DockId);
					break;
				}
			}
		}
	}
}
// ---- dock layout (end) ----

void Crucible::ImGuiManager::Init(const ImGuiVulkanContext& context, Core& core) {
	
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// Colours and font size (View > Theme). The saved choice is restored from imgui.ini on the first frame.
	EditorTheme::Init();

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

	initInfo.DescriptorPool = context.descriptorPool;
	// Let ImGui create+own its own pool. One slot per texture ImGui draws at once (its font, the
	// viewports, Asset Browser thumbnails, importer previews...): the minimum (8) ran out.
	initInfo.DescriptorPoolSize = 64;
    initInfo.MinImageCount = context.minImageCount;
    initInfo.ImageCount = context.imageCount;
	initInfo.PipelineInfoMain.MSAASamples = context.msaaSamples;
    initInfo.CheckVkResultFn = CheckVulkanResult;

	// Dynamic rendering
	initInfo.UseDynamicRendering = true;

	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;

	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &context.colorFormat;

	// ImGui's draw calls (ImGui_ImplVulkan_RenderDrawData(), invoked from Renderer's overlay
	// callback) are recorded into the SAME open dynamic-rendering pass as the scene, which has a
	// real depth attachment bound - so ImGui's own pipeline must be told that attachment's format
	// too, or the validation layer flags every one of its draw calls (depthAttachmentFormat would
	// otherwise default to VK_FORMAT_UNDEFINED, mismatching the bound D32_SFLOAT depth image;
	// see VUID-vkCmdDrawIndexed-dynamicRenderingUnusedAttachments-08914). ImGui doesn't depth-test
	// its own draws either way (no depth/stencil state is set below), this only satisfies the
	// "formats must match" rule for a pass that has one bound.
	initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.depthAttachmentFormat = context.depthFormat;

	ImGui_ImplVulkan_Init(&initInfo);

	// Build the panel set now that the ImGui Vulkan backend (ImGui_ImplVulkan_AddTexture and
	// friends) is ready - the Scene/Game views create their offscreen viewport texture lazily,
	// on their first Update() with a non-zero size, but they still need a live Renderer&
	// reference to do it.
	EditorManager::GetInstance().Init(core);

	CRU_INFO("ImGui Successfully Initialized");
}

void Crucible::ImGuiManager::Update(float dt) {
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
		else
			DockPanelsNewToIni();   // the saved layout stays; only panels it has never seen get docked
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

		currentPanel.second->Update(dt);
	}

	// Whether the cursor was locked at the end of last frame's editor update, i.e. before this
	// frame's systems ran (see the release-key rule below).
	static bool s_cursorLockedLastFrame = false;

	// The game's mouse look, with Unity's Game-view cursor rules. Scripts lock the cursor
	// (Input.SetMouseLocked); nothing locks it on Play. Getting it back is the editor's job: the
	// release key (Shift+F1 by default, Project Settings > Editor) and Pause put on the Editor
	// release hold, which no script can undo; a left click on the Game view takes that hold off,
	// so it only restores a lock the editor took away. Stop drops the request and every hold.
	// Escape is the game's (UIMenuSystem's pause menu), exactly as in the built game. After the
	// panel loop on purpose, so the Game view's hover state is this frame's.
	if (Core* core = EditorManager::GetInstance().GetCore()) {
		ImGuiIO& io = ImGui::GetIO();
		if (core->GetPlayState() == PlayState::Playing) {
			auto gameView = EditorManager::GetInstance().GetPanel<GameView>(ImguiPanels::GAMESCENE);
			// Only if the cursor was already locked before this frame's systems ran: should the key
			// be set to one the game also answers (Escape for its menu), a press that re-locked the
			// cursor this same frame is the game's - the editor must not undo it.
			if (Input::IsMouseLocked() && s_cursorLockedLastFrame && IsChordPressed(EditorManager::GetInstance().GetReleaseCursorKey()))
				Input::SetMouseReleased(Input::MouseRelease::Editor, true);
			// ...but not when the click is on the game's UI (that click is for a button), nor while the
			// game wants a free cursor (its menu is open, or a title-screen canvas is shown).
			else if (Input::IsMouseReleased(Input::MouseRelease::Editor) && gameView && gameView->IsViewportHovered()
			         && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !Input::IsPointerOverUI() && !UI::IsCursorNeeded())
				Input::SetMouseReleased(Input::MouseRelease::Editor, false);
		}
		else if (core->GetPlayState() == PlayState::Paused) {
			if (Input::IsMouseLocked())
				Input::SetMouseReleased(Input::MouseRelease::Editor, true);   // the editor needs its pointer; a click after Resume returns it
		}
		else {
			Input::ResetMouseLock();   // Stopped: free, and nothing left waiting to lock it
		}

		// A locked cursor is hidden but still moves, and ImGui still follows it - so a click
		// meant for the game (Fire) would also press whatever panel button it is invisibly over.
		// NoMouse makes the editor ignore the mouse until the cursor is free again.
		if (Input::IsMouseLocked()) io.ConfigFlags |=  ImGuiConfigFlags_NoMouse;
		else                        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

		s_cursorLockedLastFrame = Input::IsMouseLocked();
	}
}

void Crucible::ImGuiManager::BeginFrame() {

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// Once per frame, after NewFrame and before any panel draws a gizmo (the Scene view).
	ImGuizmo::BeginFrame();
}

void Crucible::ImGuiManager::Render() {
	EditorWidgets::EndFrameMouseLock();   // apply/release the infinite-drag cursor lock for this frame
	ImGui::Render();
}

// Must be called while the frame's render pass is open (from Layer::OnRender).
void Crucible::ImGuiManager::Draw(Renderer& ren) {
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ren.GetCurrentCommandBuff());

	// NOTE: multi-viewport support (ImGuiConfigFlags_ViewportsEnable) needs
	// ImGui::UpdatePlatformWindows() / RenderPlatformWindowsDefault() AFTER the main render
	// pass has ended. Viewports are not enabled in Init() today, so that is omitted here.
}

void Crucible::ImGuiManager::ResetLayout() {
	g_forceReset = true;
}

void Crucible::ImGuiManager::Exit() {
	// Drop every panel (Scene/Game views included) while the Renderer and the ImGui Vulkan
	// backend are still alive: EditorManager's s_Instance is a static-storage-duration object,
	// so left to its own devices it would only be destroyed at static-teardown time - well after Core has already
	// destroyed the Renderer and the window - and ViewportPanel::~ViewportPanel() needs both
	// still alive to release its offscreen image / descriptor set / ImGui texture handle.
	EditorManager::GetInstance().Shutdown();

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

Crucible::EditorManager& Crucible::EditorManager::GetInstance()
{
	if (s_Instance == nullptr)
	{
		s_Instance = std::make_unique<EditorManager>();
	}
	return *s_Instance;
}

void Crucible::EditorManager::Init(Core& core)
{
	// Clears itself whenever a scene is created or opened (SceneManager::OnSceneChanged).
	m_history.Init(core.GetWorld(), core.GetScenes());

	// Add in Editor Panels here...
	//
	// Panels in the same dock area become tabs, ordered by their ImguiPanels value.
	m_panels[ImguiPanels::EDITORSCENE] = std::make_shared<SceneView>(core.GetRenderer(), core.GetResources(), core.GetScenes(), core.GetWorld());
	m_panels[ImguiPanels::UISCENE] = std::make_shared<UIView>(core.GetRenderer(), core.GetWorld());
	m_panels[ImguiPanels::GAMESCENE] = std::make_shared<GameView>(core.GetRenderer(), core.GetConfig().project);
	m_panels[ImguiPanels::MENU] = std::make_shared<Menu>(core.GetScenes(), core.GetAssets());
	m_panels[ImguiPanels::HIERARCHY] = std::make_shared<Hierarchy>(core.GetWorld(), core.GetResources(), core.GetScenes());
	m_panels[ImguiPanels::INSPECTOR] = std::make_shared<Inspector>(core.GetWorld(), core.GetRenderer(), core.GetScenes(), core.GetAssets());
	// AudioSources stay silent while editing and sound in play mode; Core switches the audio
	// system's edit/pause modes on Play, Pause, Resume and Stop. Asset previews always work.
	ECSAudioSystem* audio = core.Systems().FindSystem<ECSAudioSystem>();   // null if audio is not registered
	m_panels[ImguiPanels::ASSETBROWSER] = std::make_shared<AssetBrowser>(core.GetAssets(), core.GetScenes(), audio);   // audio may be null: previews just do nothing
	m_panels[ImguiPanels::RESOURCES] = std::make_shared<ResourcesPanel>(core.GetResources(), core.GetScenes());   // what is loaded in memory (next to the Asset Browser)
	m_panels[ImguiPanels::CONSOLE] = std::make_shared<Console>();
	m_panels[ImguiPanels::PLAYPAUSE] = std::make_shared<PlayPause>(core);   // queues Play/Pause/Resume/Stop on Core
	m_panels[ImguiPanels::FONTIMPORTER] = std::make_shared<FontImporter>(core.GetAssets(), core.GetRenderer(), core);   // .ttf -> .cfont (Editor/src/Fonts)
	m_panels[ImguiPanels::TEXTUREIMPORTER] = std::make_shared<TextureImporter>(core.GetAssets(), core.GetRenderer(), core);   // image -> .bill / .sky (Tools/Common/TextureImport)
	m_panels[ImguiPanels::ANIMATOR] = std::make_shared<AnimatorPanel>(core);
	m_panels[ImguiPanels::NETWORK] = std::make_shared<NetworkPanel>(core);   // this instance's Offline/Host/Client settings, used at the next Play
	m_panels[ImguiPanels::BEHAVIORGRAPH] = std::make_shared<BehaviorGraphPanel>(core);   // floating: edit .behaviorgraph files (AI)
	m_panels[ImguiPanels::STATEGRAPH] = std::make_shared<StateGraphPanel>(core);         // floating: edit .stategraph files (AI)
	m_panels[ImguiPanels::PACKAGEGAME] = std::make_shared<PackageGamePanel>(core);       // floating: File > Package Game... (zip for players)

	for (auto& panel : m_panels)
		panel.second->Init();

	// Assets the last build could not compile (it carries on instead of failing, so the editor
	// opens), and scripts with errors: listed in the Console, and Play is refused until fixed.
#ifdef CRUCIBLE_BUILD_DIR
	BuildProblems::LoadBuildReports(std::filesystem::path(CRUCIBLE_BUILD_DIR));
#endif
	core.GetPlayMode().SetPlayGuard([] { return BuildProblems::PlayBlockReason(); });

	// The mouse-release key. A bad value in the file falls back to the default (and says so).
	{
		std::string const& text = core.GetConfig().editor.releaseCursorKey;
		KeyChord chord;
		if (ParseKeyChord(text, chord)) {
			m_releaseCursorKey = chord;
		}
		else {
			ParseKeyChord(EditorSettings{}.releaseCursorKey, m_releaseCursorKey);
			CRU_USER_WARN("[Editor] releaseCursorKey '{}' in EngineConfig.json is not a key (e.g. \"Shift+F1\") - using {}",
				text, KeyChordToString(m_releaseCursorKey));
		}
	}

	// Selection and undo history across Play / Stop (see OnPlayStateChanged).
	m_core = &core;
	m_playStateChanged = core.Events().engine.Subscribe<EngineEvents::PlayStateChanged>(
		[this](const EngineEvents::PlayStateChanged& change) { OnPlayStateChanged(change); });

	// UI buttons. Quitting the editor from a game menu would lose work, so a "Quit" button stops
	// Play mode instead - Unity's Application.Quit does nothing in the editor either.
	m_uiQuit = core.Events().engine.Subscribe<UIEvents::QuitRequested>([this](const UIEvents::QuitRequested& click) {
		CRU_USER_INFO("[UI] Quit button (entity {}) pressed: stopping Play mode. In the built game this closes the window.", click.button.index);
		if (m_core && m_core->IsInPlaySession()) { m_core->RequestStop(); }
	});
	// Custom actions are the game's to run (GameApp::RegisterAction, later scripts); here they are
	// only logged, so a menu can be tested in the editor.
	m_uiCustom = core.Events().engine.Subscribe<UIEvents::CustomAction>([](const UIEvents::CustomAction& click) {
		CRU_USER_INFO("[UI] Button (entity {}) action '{}' - run by the game (GameApp::RegisterAction), not in the editor",
		              click.button.index, click.actionId);
	});
}

void Crucible::EditorManager::Shutdown()
{
	m_playStateChanged.Reset();
	m_uiQuit.Reset();
	m_uiCustom.Reset();
	m_core = nullptr;
	m_history.Clear();
	m_panels.clear();
}

void Crucible::EditorManager::OnPlayStateChanged(const EngineEvents::PlayStateChanged& change)
{
	if (m_core == nullptr) { return; }

	auto inspector = GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	World& world = m_core->GetWorld();
	SceneManager& scenes = m_core->GetScenes();

	bool const started = change.from == PlayState::Edit && change.to != PlayState::Edit;
	bool const stopped = change.from != PlayState::Edit && change.to == PlayState::Edit;

	if (started) {
		// Finish an edit in progress while it still belongs to edit mode, then remember the
		// selection by guid - the Play snapshot has just given every live entity one.
		if (inspector) { inspector->CommitPendingEdit(); }
		m_selectionBeforePlay.clear();
		if (inspector) {
			// Active entity last, so it is active again after Stop (SetSelection's default).
			std::vector<Entity> selection = inspector->GetSelection();
			Entity const active = inspector->GetSelectedEntity();
			std::stable_partition(selection.begin(), selection.end(), [&](Entity e) { return !(e == active); });
			for (Entity const e : selection) {
				std::uint64_t const guid = scenes.Current().IdOf(e).m_Value;
				if (guid != 0) { m_selectionBeforePlay.push_back(guid); }
			}
		}
		m_history.BeginPlaySession();
		return;
	}

	if (!stopped) { return; }   // Pause / Resume: same world, same handles

	// A play-mode edit still open goes into the play-mode history, which is dropped next.
	if (inspector) { inspector->CommitPendingEdit(); }
	m_history.EndPlaySession(change.sceneRestored);

	if (inspector) {
		// Every handle from before is stale after a restore: find the pre-Play selection
		// again by guid. Without a restore the world was kept, so keep a selection that
		// is still alive.
		std::vector<Entity> restored;
		if (change.sceneRestored) {
			for (std::uint64_t const guid : m_selectionBeforePlay) {
				Entity const e = scenes.Current().EntityOf(xresource::instance_guid{ guid });
				if (e.IsValid() && world.IsAlive(e)) { restored.push_back(e); }
			}
		}
		else {
			restored = inspector->GetSelection();   // GetSelection only returns live entities
			Entity const active = inspector->GetSelectedEntity();
			std::stable_partition(restored.begin(), restored.end(), [&](Entity e) { return !(e == active); });
		}

		if (!restored.empty()) {
			inspector->SetSelection(restored);   // the last one (the pre-Play active entity) is active
		}
		else if (inspector->GetSelectedEntity().IsValid()) {
			inspector->ClearTarget();   // a stale or play-mode-only selection; an asset preview is left alone
		}
	}

	// The Scene view's outline reads this every frame; don't let it hold a stale handle meanwhile.
	DebugDrawSystem::SetSelectedEntity(inspector ? inspector->GetSelectedEntity() : kNullEntity);
	m_selectionBeforePlay.clear();
}

void Crucible::EditorManager::Undo()
{
	if (auto inspector = GetPanel<Inspector>(ImguiPanels::INSPECTOR)) { inspector->CommitPendingEdit(); }
	if (!m_history.CanUndo()) { return; }
	Entity const active = m_history.Undo();
	SelectAfterHistoryStep(active, m_history.LastSelection());
}

void Crucible::EditorManager::Redo()
{
	if (auto inspector = GetPanel<Inspector>(ImguiPanels::INSPECTOR)) { inspector->CommitPendingEdit(); }
	if (!m_history.CanRedo()) { return; }
	Entity const active = m_history.Redo();
	SelectAfterHistoryStep(active, m_history.LastSelection());
}

void Crucible::EditorManager::SelectAfterHistoryStep(Entity entity, std::vector<Entity> const& all)
{
	auto inspector = GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (!inspector) { return; }

	World& world = m_history.GetWorld();
	std::vector<Entity> alive;
	for (Entity const e : all) {
		if (e.IsValid() && world.IsAlive(e)) { alive.push_back(e); }
	}

	if (alive.size() > 1 || (alive.size() == 1 && !world.IsAlive(entity))) {
		inspector->SetSelection(alive, entity);   // a multi-selection step: select all of it again
	}
	else if (entity.IsValid() && world.IsAlive(entity)) {
		inspector->SetSelectedEntity(entity);
	}
	else if (inspector->GetSelectedEntity().IsValid() && !world.IsAlive(inspector->GetSelectedEntity())) {
		// Undoing a Create destroyed the entity that was selected.
		inspector->ClearTarget();
	}
}

float Crucible::EditorPanel::PushFontSize(float const fontScale)
{
	// ImGui 1.92+: ImFont::Scale is gone; sizes are set per PushFont(). Scale relative to the current base size.
	float const oldSize = ImGui::GetStyle().FontSizeBase;
	ImGui::PushFont(nullptr, oldSize * fontScale);   // nullptr = keep current font
	return oldSize;
}

void Crucible::EditorPanel::PopFontSize(float const prevScale)
{
	(void)prevScale;   // PopFont() restores the previous font and size by itself
	ImGui::PopFont();
}
