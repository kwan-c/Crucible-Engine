#pragma once
/*!
* @file			ImGuiPanel_Network.h
* @author		Abhishek Akilandeswari Patchamuthu
* @par			a.patchamuthu@digipen.edu
* @date			3 November 2026
*
* @brief		The editor's Network panel: choose Offline, Host or Client for THIS editor
*				instance, with its addresses and ports, before pressing Play.
*
* The panel starts from the shared defaults in EngineConfig.json (Core loads them into the
* NetworkSession at startup) and edits them for this running editor only: every change goes to
* NetworkSession::SetSettings, in memory. Nothing is written back to EngineConfig.json, so a
* second editor opened on the same project can choose differently (one Host, one Client).
* The choice survives Stop and Play; it resets to the defaults when the editor restarts.
*
* While a session runs (Play) the controls are locked, so a running session can never be
* changed under its feet; Stop unlocks them. Play itself is unchanged: Core starts the session
* with these settings when Play begins and stops it when Play stops.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include <array>
#include <string>

#include "ImGui/ImGuiManager.h"
#include "Network/NetworkSettings.h"

namespace Crucible
{
	class Core;

	class NetworkPanel : public EditorPanel
	{
	public:
		explicit NetworkPanel(Core& core) : m_core(core) {}

		void Init() override;
		void Update(float dt) override;

		// Closed by default; opened / closed from View > Network (or its own close button).
		// Closing it keeps the settings: Play still uses them.
		void Show()             { m_visible = true; m_makeFocus = true; }
		bool IsOpen() const     { return m_visible; }
		void SetOpen(bool open) { m_visible = open; }

	private:
		void LoadFromSession();   // the session's settings -> the edit fields
		void Apply();             // the edit fields -> NetworkSession::SetSettings, and validate
		void DrawStatus();

		Core&                m_core;
		NetworkSettings      m_edit;               // what the fields show; pushed to the session on every change
		std::array<char, 64> m_listenAddress{};
		std::array<char, 64> m_connectAddress{};
		int                  m_hostPort = 0;
		int                  m_connectPort = 0;
		std::string          m_validationError;    // empty = these settings can start a session
		bool                 m_visible   = false;  // View > Network
		bool                 m_makeFocus = false;  // bring to front on the next frame (Show)
	};
}
