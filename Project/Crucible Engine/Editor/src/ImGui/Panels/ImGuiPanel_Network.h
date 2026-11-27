#pragma once
/*!
* @file			ImGuiPanel_Network.h
* @author		Abhishek Akilandeswari Patchamuthu
* @par			a.patchamuthu@digipen.edu
* @date			3 November 2026
*
* @brief		The editor's Network window (Network > Network Window): this editor instance's
*				multiplayer settings, the LAN connection server, and the live numbers of a running
*				session.
*
* Like the Animator, hidden until opened and then a tab in the centre area beside the Scene and
* Game views (drag it anywhere), with three tabs:
*
*  Session            Player name; Lobby discovery (Auto / Server / LAN, the connection server's
*                     address and port: how lobby browsers find sessions and how a hosted one is
*                     listed); Quick test (start at Play) - Offline, or Host / Client with one game
*                     port, for testing a scene without a lobby; Network simulation (testing)
*                     latency and loss, which applies to every session of this editor, lobby ones
*                     included. Below them the session's status: players with their ping, the
*                     connection server's state, why the last session ended.
*  Connection Server  Starts, watches and stops CrucibleConnectionServer.exe on this PC
*                     (ConnectionServerControl): its status (uptime, lobbies), the addresses other
*                     PCs should use, "Use this server" for this editor's lobby discovery, and the
*                     server's log. Closing the editor leaves it running; the next editor finds it.
*  Debug              Per-connection ping, quality, rates and lifecycle progress, per-second rates,
*                     and the session's counters (NetStats): for chasing networking bugs.
*
* The settings start from the shared defaults in EngineConfig.json (Core loads them into the
* NetworkSession at startup) and change them for this running editor only: every change goes to
* NetworkSession::SetSettings, in memory. Nothing is written back to EngineConfig.json, so a second
* editor on the same project can choose differently. They survive Stop and Play and reset when the
* editor restarts. While a session runs (Play) they are locked; Stop unlocks them. The listen
* address (EngineConfig.json "listenAddress") is kept but no longer shown.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "ImGui/ImGuiManager.h"
#include "Network/NetworkSettings.h"

namespace Crucible
{
	class Core;
	class ConnectionServerControl;

	class NetworkPanel : public EditorPanel
	{
	public:
		enum class Tab : std::uint8_t { Session, ConnectionServer, Debug };

		explicit NetworkPanel(Core& core);
		~NetworkPanel() override;

		void Init() override;
		void Update(float dt) override;

		// ---- the window (Network menu) -------------------------------------------------------
		void Show(Tab tab = Tab::Session) { m_visible = true; m_makeFocus = true; m_selectTab = tab; m_hasSelectTab = true; }
		bool IsOpen() const noexcept { return m_visible; }
		void SetOpen(bool open) noexcept { m_visible = open; }

		// ---- the connection server on this PC (also from the Network menu) --------------------
		bool ServerRunning() const;
		bool CanStartServer() const;      // the exe exists and none is running
		void StartServer();               // with the port and lifetime the tab shows; failures go to the Console and the tab
		void StopServer();

	private:
		void LoadFromSession();   // the session's settings -> the edit fields
		void Apply();             // the edit fields -> NetworkSession::SetSettings, and validate

		void DrawSessionTab(bool editable);
		void DrawServerTab();
		void DrawDebugTab();

		bool DrawQuickTest();        // Offline / Host / Client at Play, one game port; true if changed
		bool DrawLobbyDiscovery();   // discovery mode and connection server fields; true if changed
		bool DrawSimulation();       // the testing conditions' fields; true if changed
		void DrawStatus();
		void DrawLobbyServerStatus();
		void DrawStatistics();       // live numbers of the running session
		void UpdateRates();          // the per-second rates, recomputed about once a second
		void UseLocalServer();       // this editor's lobby discovery -> the server running on this PC

		/// Counters sampled for the per-second rates, and the rates from the last full window.
		struct Rates
		{
			std::uint64_t snapshots = 0;   // host: sent; client: applied
			std::uint64_t inputs = 0;      // host: received; client: sent
			std::uint64_t messages = 0;    // received, every kind
			std::uint64_t bytes = 0;       // received, our messages only
		};

		Core&                m_core;
		NetworkSettings      m_edit;               // what the fields show; pushed to the session on every change
		std::array<char, 64> m_connectAddress{};
		std::array<char, 64> m_playerName{};
		std::array<char, 64> m_lobbyServerAddress{};
		int                  m_lobbyServerPort = 0;
		int                  m_gamePort = 0;       // host port and the port a Client joins: one game port
		std::string          m_validationError;    // empty = these settings can start a session

		bool                 m_visible = false;    // opened from the Network menu
		Tab                  m_selectTab = Tab::Session;
		bool                 m_hasSelectTab = false;

		// The connection server on this PC
		std::unique_ptr<ConnectionServerControl> m_server;
		int                  m_serverPort = 0;
		int                  m_serverLifetimeMs = 0;
		std::string          m_serverError;        // the last Start's refusal
		bool                 m_followLog = true;   // the log view sticks to its end

		std::uint32_t        m_rateGeneration = 0; // the session the samples below belong to
		double               m_rateWindowStart = -1.0;   // ImGui time of m_rateSample; < 0 = no sample yet
		Rates                m_rateSample;         // counters at the window's start
		Rates                m_ratePerSecond;      // the last full window, per second
		bool                 m_hasRates = false;   // m_ratePerSecond holds a measurement
	};
}
