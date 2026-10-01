#pragma once
/*!
* @file			ImGuiPanel_Console.h
* @brief		The editor's Console: log output from the engine and editor.
*
* Fed by a logger listener (Debug::AddListener in Diagnostics/Logger.h). By default it shows the
* front-facing lines - those logged with CRU_USER_INFO / CRU_USER_WARN / CRU_USER_ERROR - plus
* every error. Ticking "Engine" also shows the internal developer logs (CRU_INFO, CRU_DEBUG...).
*
* Threading: the listener can be called from any thread that logs, so it only appends to a small
* mutex-protected pending queue. Update() (main thread) drains that queue into the display buffer,
* so drawing never holds a lock the logger might need.
*/

#include "ImGui/ImGuiManager.h"
#include "Diagnostics/Logger.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace Crucible {

	class Console : public EditorPanel {
	public:
		~Console() override;

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		void Clear();

	private:
		struct Entry {
			Debug::Level  level = Debug::Level::Info;
			bool          user = false;      // logged with CRU_USER_*
			double        seconds = 0.0;     // engine time of the latest occurrence
			std::string   source;            // "File.cpp:42", or empty
			std::string   text;
			std::uint32_t count = 1;         // identical consecutive lines are collapsed
		};

		static void OnLog(Debug::LogRecord const& record, void* self);   // any thread
		void        DrainPending();                                       // main thread
		bool        PassesFilters(Entry const& entry) const;
		static void DrawEntry(Entry const& entry);

		static constexpr std::size_t kMaxEntries = 2000;   // oldest dropped beyond this

		std::mutex         m_pendingMutex;
		std::vector<Entry> m_pending;   // written by OnLog, guarded by m_pendingMutex
		std::deque<Entry>  m_entries;   // main thread only

		Debug::ListenerId m_listener = 0;

		bool m_showInfo     = true;
		bool m_showWarnings = true;
		bool m_showErrors   = true;
		bool m_showEngine   = false;   // also show internal (non-CRU_USER_*) logs below Error
		bool m_autoScroll   = true;
		ImGuiTextFilter m_filter;
	};

}
