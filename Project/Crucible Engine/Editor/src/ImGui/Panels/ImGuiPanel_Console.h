#pragma once
/*!
* @file			ImGuiPanel_Console.h
* @brief		The editor's Console (log output). Placeholder for now.
*
* The engine logger writes to sinks (see Engine/src/Diagnostics/LogSink.h), but those
* sinks are internal to Engine.dll. To fill this panel, the Engine needs to expose a
* way to register an extra sink (or a callback) from the Editor; this panel then keeps
* the lines in a ring buffer and draws them.
*/

#include "ImGui/ImGuiManager.h"

namespace Crucible {

	class Console : public EditorPanel {
	public:
		// REQUIRED Overrides
		void Init() override;
		void Update() override;
	};

}
