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
	MENU,
	HIERARCHY,
	INSPECTOR,
	ASSETBROWSER,
	PLAYPAUSE,
	GAMESCENE,
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
		static std::unique_ptr<EditorPanel> s_Instance;
	};

	namespace ImGuiManager {
		void Init(Core const& core);
		void Update();
		void BeginFrame();
		void EndFrame();
		void Exit();
	}
}