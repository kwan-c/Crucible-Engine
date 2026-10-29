/*!
* @file			KeyChord.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			26 October 2026
*
* @brief		Key chords: parse, print, test. See KeyChord.h.
*/

#include "Input/KeyChord.h"

#include <cctype>
#include <cstdint>

#include "Input/InputSystem.h"

namespace Crucible {

	namespace {
		bool EqualsNoCase(std::string_view a, std::string_view b) {
			if (a.size() != b.size()) { return false; }
			for (std::size_t i = 0; i < a.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) { return false; }
			}
			return true;
		}

		std::string_view Trim(std::string_view text) {
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) { text.remove_prefix(1); }
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))  { text.remove_suffix(1); }
			return text;
		}
	}

	bool IsChordKey(KeyCode key) {
		switch (key) {
			case KeyCode::Unknown:
			case KeyCode::Count:
			case KeyCode::LeftShift:   case KeyCode::RightShift:
			case KeyCode::LeftControl: case KeyCode::RightControl:
			case KeyCode::LeftAlt:     case KeyCode::RightAlt:
			case KeyCode::LeftSuper:   case KeyCode::RightSuper:
			case KeyCode::MouseLeft:   case KeyCode::MouseRight: case KeyCode::MouseMiddle:
			case KeyCode::MouseButton4: case KeyCode::MouseButton5: case KeyCode::MouseButton6:
			case KeyCode::MouseButton7: case KeyCode::MouseButton8:
				return false;
			default:
				return static_cast<std::uint16_t>(key) < static_cast<std::uint16_t>(KeyCode::Count);
		}
	}

	KeyCode KeyCodeFromString(std::string_view name) {
		name = Trim(name);
		if (name.empty()) { return KeyCode::Unknown; }
		for (std::uint16_t i = 1; i < static_cast<std::uint16_t>(KeyCode::Count); ++i) {
			const KeyCode key = static_cast<KeyCode>(i);
			if (EqualsNoCase(name, KeyCodeToString(key))) { return key; }
		}
		return KeyCode::Unknown;
	}

	bool ParseKeyChord(std::string_view text, KeyChord& out) {
		KeyChord chord;
		bool any = false;
		while (true) {
			const std::size_t plus = text.find('+');
			const std::string_view part = Trim(text.substr(0, plus));
			if (part.empty()) { return false; }   // "", "Shift+", "++"
			any = true;

			if      (EqualsNoCase(part, "Ctrl") || EqualsNoCase(part, "Control")) { if (chord.ctrl)  { return false; } chord.ctrl  = true; }
			else if (EqualsNoCase(part, "Shift"))                                 { if (chord.shift) { return false; } chord.shift = true; }
			else if (EqualsNoCase(part, "Alt"))                                   { if (chord.alt)   { return false; } chord.alt   = true; }
			else {
				if (chord.key != KeyCode::Unknown) { return false; }   // two keys
				chord.key = KeyCodeFromString(part);
				if (!IsChordKey(chord.key)) { return false; }
			}

			if (plus == std::string_view::npos) { break; }
			text.remove_prefix(plus + 1);
		}
		if (!any || !chord.IsValid()) { return false; }   // modifiers only
		out = chord;
		return true;
	}

	std::string KeyChordToString(const KeyChord& chord) {
		if (!IsChordKey(chord.key)) { return {}; }
		std::string text;
		if (chord.ctrl)  { text += "Ctrl+"; }
		if (chord.shift) { text += "Shift+"; }
		if (chord.alt)   { text += "Alt+"; }
		text += KeyCodeToString(chord.key);
		return text;
	}

	bool IsChordPressed(const KeyChord& chord) {
		if (!IsChordKey(chord.key) || !Input::IsPressed(chord.key)) { return false; }
		const bool ctrl  = Input::IsDown(KeyCode::LeftControl) || Input::IsDown(KeyCode::RightControl);
		const bool shift = Input::IsDown(KeyCode::LeftShift)   || Input::IsDown(KeyCode::RightShift);
		const bool alt   = Input::IsDown(KeyCode::LeftAlt)     || Input::IsDown(KeyCode::RightAlt);
		return ctrl == chord.ctrl && shift == chord.shift && alt == chord.alt;
	}

}	// NAMESPACE CRUCIBLE
