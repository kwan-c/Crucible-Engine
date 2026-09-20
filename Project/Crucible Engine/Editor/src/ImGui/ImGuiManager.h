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

	class EditorPanel {
	public:
		virtual void Init() = 0;
		virtual void Update() = 0;

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
		float    m_dockRatio = 0.25f;           // fraction of the space still free when this area is split off (order: Left, Right, Top, Bottom); the first panel to use an area decides


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
	};

	namespace ImGuiManager {
		void Init(Core const& core);
		void Update();
		void BeginFrame();
		void Render();                    // finalize the UI (call once per frame, from Layer::OnUpdate)
		void Draw(Core const& core);      // record draw commands (call from Layer::OnRender, render pass open)
		void Exit();
		void ResetLayout();               // rebuild the default dock layout on the next frame (View > Reset Layout)
	}
}