#pragma once
/*!
* @file			ImGuiPanel_Inspector.h
* @brief		The editor's inspector for viewing and editing object properties.
*
* Sample panel: shows how an EditorPanel plugs into EditorManager. Most items are
* placeholders that just log a message until the real features exist.
*/

#include "ImGui/ImGuiManager.h"
#include "Reflection/ReflectionRegistration.h"
#include "Reflection/ReflectionExternalRegistration.h"

#include <functional>

namespace Crucible {
	class Inspector : public EditorPanel {
	public:
		// REQUIRED Overrides
		void Init() override;
		void Update() override;

		// The Hierarchy panel will call this when the user clicks something.
		template<typename T> void SetTarget(T& obj) { m_draw = [&obj] { DrawInstance(obj); }; }
		void ClearTarget() { m_draw = nullptr; }

	private:
		static bool DrawInstance(rttr::instance obj);   // true if a value changed
		std::function<void()> m_draw;
	};
}