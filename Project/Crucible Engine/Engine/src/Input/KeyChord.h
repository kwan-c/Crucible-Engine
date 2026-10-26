#pragma once
/*!
* @file			KeyChord.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			26 October 2026
*
* @brief		A key plus the modifiers that must be held with it ("Shift+F1", "Ctrl+Alt+P"),
*				for shortcuts that live in a config file and can be rebound.
*
* Text form: modifiers first, then the key, joined by '+'. Modifiers are Ctrl, Shift and Alt
* (case-insensitive; "Control" also works). The key is a KeyCodeToString name ("F1", "P",
* "Escape", "Num5"...). Spaces around the '+' are ignored.
*
* A chord matches only with exactly its modifiers held - either side counts - so "F1" is not
* pressed while Shift is held, and "Shift+F1" is not pressed by F1 alone. The key may not be a
* modifier or a mouse button.
*/

#include <string>
#include <string_view>

#include "EngineExport.h"
#include "Input/KeyCode.h"

namespace Crucible {

	struct KeyChord {
		KeyCode key   = KeyCode::Unknown;
		bool    ctrl  = false;
		bool    shift = false;
		bool    alt   = false;

		bool IsValid() const { return key != KeyCode::Unknown; }
		bool operator==(const KeyChord& other) const {
			return key == other.key && ctrl == other.ctrl && shift == other.shift && alt == other.alt;
		}
		bool operator!=(const KeyChord& other) const { return !(*this == other); }
	};

	/// Can `key` be the key of a chord? (Not Unknown/Count, a modifier or a mouse button.)
	ENGINE_API bool IsChordKey(KeyCode key);

	/// The KeyCode named `name` (as KeyCodeToString spells it, case-insensitive), or Unknown.
	ENGINE_API KeyCode KeyCodeFromString(std::string_view name);

	/*!
	 \brief  Reads "Shift+F1" and the like.
	 \return false (and `out` untouched) if the text is empty, names an unknown key or modifier,
	         has no key, has two keys, or the key can't be a chord key (see IsChordKey).
	 */
	ENGINE_API bool ParseKeyChord(std::string_view text, KeyChord& out);

	/// "Ctrl+Shift+Alt+Key" (only the modifiers that are set). "" for an invalid chord.
	ENGINE_API std::string KeyChordToString(const KeyChord& chord);

	/// Did the chord's key go down this frame with exactly its modifiers held? (Input::IsPressed.)
	ENGINE_API bool IsChordPressed(const KeyChord& chord);

}	// NAMESPACE CRUCIBLE
