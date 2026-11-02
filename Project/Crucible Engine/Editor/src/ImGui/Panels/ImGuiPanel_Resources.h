#pragma once
/*!
* @file			ImGuiPanel_Resources.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			30 October 2026
*
* @brief		The editor's Resources tab: what the ResourceManager holds in memory right now.
*
* The Asset Browser shows files on disk; this shows what is loaded from them (models, materials,
* textures, fonts, ...), so you can see a scene change free what the new scene doesn't use.
* "Unload unused" runs the same clean-up a scene change does (ResourceManager::UnloadUnused),
* against the open scene as it is now.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/ImGuiManager.h"
#include "Resources/ResourceManager.h"

#include <string>

namespace Crucible {

	class SceneManager;

	class ResourcesPanel : public EditorPanel {
	public:
		ResourcesPanel(ResourceManager& resources, SceneManager& scenes);

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

	private:
		void UnloadUnusedNow();

		ResourceManager& m_resources;
		SceneManager&    m_scenes;

		ImGuiTextFilter m_filter;
		int             m_kindFilter = 0;      // 0 = all, else index into the kinds seen this frame
		bool            m_showBuiltins = false;
		std::string     m_lastUnload;          // result of the last "Unload unused" press
	};

}
