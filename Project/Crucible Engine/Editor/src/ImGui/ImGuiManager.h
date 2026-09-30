#pragma once
/*!
* @file			ImGuiManager.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		This file is to set up the ImGui Manager
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include <imgui.h>

#include <string>
#include <map>
#include <memory>

#include "Graphics/Vulkan/ImGuiContext.h"
#include "Graphics/RenderSystem.h"

enum class ImguiPanels
{
	EDITORSCENE,
	GAMESCENE,
	MENU,
	HIERARCHY,
	INSPECTOR,
	ASSETBROWSER,
	PLAYPAUSE,
	CONSOLE,
	ANIMATOR
};

namespace Crucible {

	class Core;
	class World;   // forward-declared: EditorManager::Init/ImGuiManager::Init only take a reference
	class SceneManager;
	class ResourceManager;   // Resources/ResourceManager.h - include it where you call into it

	class EditorPanel {
	public:
		virtual ~EditorPanel() = default;

		virtual void Init() = 0;
		// dt: seconds since last frame. Panels that don't need it just ignore the parameter
		// (e.g. `void Update(float) override { ... }`); the Scene/Game viewport panels need it
		// to drive the editor fly-camera at a frame-rate-independent speed.
		virtual void Update(float dt) = 0;

		std::string m_panelName;
		ImGuiWindowFlags m_flags{};
		ImVec2 m_windowPadding{ 0, 0 };

		bool m_makeFocus = false;
		bool m_rightClick = false;

		// ---- Initial docking (read once, when the default layout is built) ----
		// Set these in the panel's Init(). Example:
		//		m_docked    = true;
		//		m_dockArea  = DockArea::Right;
		//		m_dockRatio = 0.25f;
		enum class DockArea { Center, Left, Right, Top, Bottom };

		bool     m_docked = false;              // true: start docked in m_dockArea, false: start as a floating window
		DockArea m_dockArea = DockArea::Center; // where to dock (ignored when m_docked is false)
		float    m_dockRatio = 0.25f;           // fraction of the space still free when this area is split off (order: Right, Bottom, Left, Top); the first panel to use an area decides


		/*!
		 * @brief	The scale size of a heading 1.
		 */
		static constexpr float m_Heading1Scale{ 1.0f };

		/*!
		 * @brief	The scale size of a heading 2.
		 */
		static constexpr float m_Heading2Scale{ 0.85f };

		/*!
		 * @brief	The scale size of a normal scale.
		 */
		static constexpr float m_NormalScale{ 0.8f };


		float PushFontSize(float const fontScale);

		/*!
		 * @brief	Restores the old font size.
		 *
		 * @param	prevScale
		 *			The old font size to restore to.
		 */
		void PopFontSize(float const prevScale);
	};

	class EditorManager {
	public:
		static EditorManager& GetInstance();

		// Builds the panel set. Call once, from ImGuiManager::Init(), after the ImGui Vulkan
		// backend is ready - the Scene/Game views need a live Renderer& to construct their
		// ViewportPanel (see ImGuiPanel_SceneView.h / ImGuiPanel_GameView.h), and Hierarchy/
		// Inspector need a live World& to list/select entities and edit their components.
		void Init(Core& core);

		// Drops every panel (and, with them, any GPU-owning members like ViewportPanel) while
		// the Renderer and the ImGui Vulkan backend are still alive. MUST be called from
		// ImGuiManager::Exit() before ImGui_ImplVulkan_Shutdown(): EditorManager itself is a
		// static-storage-duration object (see s_Instance), so without this its panels would otherwise
		// only be destroyed at static-teardown time - after Core has already destroyed the
		// Renderer and the window, which would leave ViewportPanel::Shutdown() reaching into
		// dead objects.
		void Shutdown();

		// The engine's ResourceManager (asset id <-> Renderer handle, mesh/material pickers).
		// Set once from main.cpp (core.GetResources()) before the ImGui layer is pushed; panels
		// fetch it here instead of threading one more reference through every constructor.
		void             SetResources(ResourceManager* resources) { m_resources = resources; }
		ResourceManager* GetResources() const { return m_resources; }

		std::map<ImguiPanels, std::shared_ptr<EditorPanel>> m_panels;

		// This function assumes the caller gives the correct type.
		// make sure to input in correct type of Panel like:
		//	GetPanel<HierarchyPanel>(PanelTypes::HIERARCHY);
		template<typename T>
		std::shared_ptr<T> GetPanel(ImguiPanels _panelType)
		{
			return std::static_pointer_cast<T>(m_panels[_panelType]);
		}


	private:
		static std::unique_ptr<EditorManager> s_Instance;
		ResourceManager* m_resources = nullptr;
	};

	namespace ImGuiManager {
		void Init(const ImGuiVulkanContext& context, Core& core);
		void Update(float dt);
		void BeginFrame();
		void Render();                    // finalize the UI (call once per frame, from Layer::OnUpdate)
		void Draw(Renderer& ren);		  // record draw commands (call from Layer::OnRender, render pass open)
		void Exit();
		void ResetLayout();               // rebuild the default dock layout on the next frame (View > Reset Layout)
	}
}
