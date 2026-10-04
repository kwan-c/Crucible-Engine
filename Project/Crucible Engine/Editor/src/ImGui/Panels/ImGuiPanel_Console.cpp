/*!
* @file			ImGuiPanel_Console.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's Console: log output from the engine and editor.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_Console.h"

#include <cstdio>
#include <format>
#include <utility>

namespace {

	using Crucible::Debug::Level;

	ImVec4 LevelColour(Level level) {
		switch (level) {
			case Level::Trace:
			case Level::Debug:   return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
			case Level::Info:    return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
			case Level::Warning: return ImVec4(1.00f, 0.80f, 0.30f, 1.0f);
			default:             return ImVec4(1.00f, 0.42f, 0.42f, 1.0f);   // Error, Fatal
		}
	}

	char const* LevelTag(Level level) {
		switch (level) {
			case Level::Trace:   return "[TRACE]";
			case Level::Debug:   return "[DEBUG]";
			case Level::Info:    return "[INFO] ";
			case Level::Warning: return "[WARN] ";
			case Level::Error:   return "[ERROR]";
			case Level::Fatal:   return "[FATAL]";
			default:             return "[?]    ";
		}
	}

}

Crucible::Console::~Console() {
	// Must happen before this object is gone: the logger would otherwise call OnLog on freed memory.
	if (m_listener != 0) {
		Debug::RemoveListener(m_listener);
		m_listener = 0;
	}
}

void Crucible::Console::Init() {
	m_panelName = "Console";

	// Bottom area, tabbed behind the Asset Browser.
	m_docked = true;
	m_dockArea = DockArea::Bottom;

	m_listener = Debug::AddListener(&Console::OnLog, this);
	if (m_listener == 0) {
		CRU_ERROR("[Console] Could not register a log listener - the Console will stay empty");
	}
}

void Crucible::Console::Clear() {
	m_entries.clear();
	std::scoped_lock lock{ m_pendingMutex };
	m_pending.clear();
}

void Crucible::Console::OnLog(Debug::LogRecord const& record, void* self) {
	// Called under the logger's lock, possibly from a worker thread: copy and get out.
	auto* console = static_cast<Console*>(self);

	Entry entry;
	entry.level   = record.level;
	entry.user    = record.userFacing;
	entry.seconds = record.seconds;
	entry.text    = std::string(record.message);
	if (!record.file.empty()) {
		entry.source = std::format("{}:{}", record.file, record.line);
	}

	std::scoped_lock lock{ console->m_pendingMutex };
	console->m_pending.push_back(std::move(entry));
}

void Crucible::Console::DrainPending() {
	std::vector<Entry> incoming;
	{
		std::scoped_lock lock{ m_pendingMutex };
		incoming.swap(m_pending);
	}

	for (Entry& entry : incoming) {
		// The same line again straight after itself (e.g. a warning raised every frame) bumps a
		// counter on the previous entry instead of flooding the buffer.
		if (!m_entries.empty()) {
			Entry& last = m_entries.back();
			if (last.level == entry.level && last.user == entry.user &&
				last.source == entry.source && last.text == entry.text) {
				++last.count;
				last.seconds = entry.seconds;
				continue;
			}
		}

		m_entries.push_back(std::move(entry));
		if (m_entries.size() > kMaxEntries) {
			m_entries.pop_front();
		}
	}
}

bool Crucible::Console::PassesFilters(Entry const& entry) const {
	// Internal developer logs stay hidden unless "Engine" is ticked - except errors, which
	// are always worth seeing whoever logged them.
	if (!entry.user && !m_showEngine && entry.level < Level::Error) return false;

	if (entry.level >= Level::Error)         { if (!m_showErrors)   return false; }
	else if (entry.level == Level::Warning)  { if (!m_showWarnings) return false; }
	else                                     { if (!m_showInfo)     return false; }

	if (m_filter.IsActive() &&
		!m_filter.PassFilter(entry.text.c_str()) &&
		!m_filter.PassFilter(entry.source.c_str())) {
		return false;
	}
	return true;
}

void Crucible::Console::DrawEntry(Entry const& entry) {
	ImGui::BeginGroup();   // so hover/click below covers the whole line

	ImGui::TextDisabled("%8.2fs", entry.seconds);
	ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Text, LevelColour(entry.level));
	ImGui::TextUnformatted(LevelTag(entry.level));
	ImGui::SameLine();
	ImGui::TextUnformatted(entry.text.c_str());
	ImGui::PopStyleColor();

	if (entry.count > 1) {
		ImGui::SameLine();
		ImGui::TextDisabled("x%u", static_cast<unsigned>(entry.count));
	}

	ImGui::EndGroup();

	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s%s\nRight-click to copy",
			entry.source.empty() ? "(no source)" : entry.source.c_str(),
			entry.user ? "" : "  [engine]");
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
		ImGui::SetClipboardText(entry.text.c_str());
	}
}

void Crucible::Console::Update(float) {
	DrainPending();   // even when the window is collapsed, so the buffer stays current

	if (ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {

		// ---- Toolbar ----
		std::size_t infos = 0, warnings = 0, errors = 0;
		for (Entry const& entry : m_entries) {
			if (!entry.user && !m_showEngine && entry.level < Level::Error) continue;
			if (entry.level >= Level::Error)        ++errors;
			else if (entry.level == Level::Warning) ++warnings;
			else                                    ++infos;
		}

		if (ImGui::Button("Clear")) Clear();
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_autoScroll);
		ImGui::SameLine();
		ImGui::TextDisabled("|");

		// "###id" keeps each checkbox's ID fixed while its count changes.
		char label[48];
		std::snprintf(label, sizeof(label), "Info (%zu)###info", infos);
		ImGui::SameLine(); ImGui::Checkbox(label, &m_showInfo);
		std::snprintf(label, sizeof(label), "Warnings (%zu)###warnings", warnings);
		ImGui::SameLine(); ImGui::Checkbox(label, &m_showWarnings);
		std::snprintf(label, sizeof(label), "Errors (%zu)###errors", errors);
		ImGui::SameLine(); ImGui::Checkbox(label, &m_showErrors);

		ImGui::SameLine();
		ImGui::Checkbox("Engine", &m_showEngine);
		ImGui::SetItemTooltip("Also show internal engine logs (CRU_INFO, CRU_DEBUG, ...).\n"
		                      "Off: only CRU_USER_* messages and errors.");

		ImGui::SameLine();
		m_filter.Draw("Filter", 180.0f);

		ImGui::Separator();

		// ---- Log ----
		if (ImGui::BeginChild("##ConsoleLog", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
			std::vector<Entry const*> visible;
			visible.reserve(m_entries.size());
			for (Entry const& entry : m_entries) {
				if (PassesFilters(entry)) visible.push_back(&entry);
			}

			// Only the rows on screen are actually drawn.
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(visible.size()));
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
					ImGui::PushID(i);
					DrawEntry(*visible[static_cast<std::size_t>(i)]);
					ImGui::PopID();
				}
			}

			// Follow new lines only while already at the bottom, so scrolling up to read stays put.
			if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
				ImGui::SetScrollHereY(1.0f);
			}
		}
		ImGui::EndChild();
	}
	ImGui::End();
}
