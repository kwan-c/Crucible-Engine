#pragma once
/*!
* @file			ImGuiPanel_Hierarchy.h
* @brief		The editor's Hierarchy (list of entities in the open scene). Placeholder for now.
*
* Reserves the left column of the default layout. The real version lists the World's
* entities and calls Inspector::SetTarget() when one is clicked.
*/

#include "ImGui/ImGuiManager.h"

namespace Crucible {

	class Hierarchy : public EditorPanel {
	public:
		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;
	};

}
