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
* Testing aids (Networking Phase 0):
*  - "Network simulation" adds latency and packet loss to this whole editor instance while its
*    session runs (GNS's fake lag and loss; see NetworkSimulation). Set it on one instance only.
*  - "Statistics" shows the running session live: each connection's ping, quality, rates and
*    lifecycle progress (from GNS), message and snapshot rates per second, and the session's own
*    counters (NetStats). Nothing here changes the session.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include <array>
#include <cstdint>
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

	private:
		void LoadFromSession();   // the session's settings -> the edit fields
		void Apply();             // the edit fields -> NetworkSession::SetSettings, and validate
		void DrawStatus();
		bool DrawSimulation();    // the testing conditions' fields; true if one changed
		void DrawStatistics();    // live numbers of the running session
		void UpdateRates();       // the per-second rates, recomputed about once a second

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
		std::array<char, 64> m_listenAddress{};
		std::array<char, 64> m_connectAddress{};
		std::array<char, 64> m_playerName{};
		int                  m_hostPort = 0;
		int                  m_connectPort = 0;
		std::string          m_validationError;    // empty = these settings can start a session

		std::uint32_t        m_rateGeneration = 0; // the session the samples below belong to
		double               m_rateWindowStart = -1.0;   // ImGui time of m_rateSample; < 0 = no sample yet
		Rates                m_rateSample;         // counters at the window's start
		Rates                m_ratePerSecond;      // the last full window, per second
		bool                 m_hasRates = false;   // m_ratePerSecond holds a measurement
	};
}
