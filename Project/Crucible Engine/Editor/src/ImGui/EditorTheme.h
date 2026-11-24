#pragma once
/*!
* @file			EditorTheme.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			10 October 2026
*
* @brief		Editor look and feel: colour themes, font size and a user-made Custom theme.
*
* Pick a theme from View > Theme. The choice (and the Custom theme, if one was saved)
* is stored in imgui.ini next to the dock layout, under [CrucibleEditor][Appearance],
* so it comes back the next time the editor starts. It is per-machine: deleting the
* Build folder (clean.bat) resets it to Crucible Dark.
*
* Sharing themes: the Theme Editor's Export writes the live style to
* Assets/Editor/Themes/<Name>.crutones (JSON, colours and shape settings by name). Every file
* there is listed under View > Theme > Shared Themes, and double-clicking one in the Asset
* Browser applies it too; either way it is loaded into the Custom slot. The folder is in git,
* so the team gets each other's themes, but it is editor-only: Cook never copies Assets/Editor
* (Tools/Cook/Cook.cpp, ShouldCopy), so a theme can never end up in a packaged game.
* The font size is not part of a theme - it stays each person's own setting.
*
* Panels that draw their own coloured text should take the colour from GetPalette()
* instead of hard-coding it, so it stays readable on the Light theme too.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Crucible::EditorTheme {

	enum class Theme {
		CrucibleDark,   // the editor's own dark theme (default)
		Dark,           // Dear ImGui's StyleColorsDark
		Light,          // Dear ImGui's StyleColorsLight
		Classic,        // Dear ImGui's StyleColorsClassic
		Custom,         // whatever was last saved from the Theme Editor
		Count
	};

	// Status colours that follow the theme (darker on light backgrounds).
	struct Palette {
		ImVec4 success;   // play mode, "ok"
		ImVec4 warning;   // paused, warnings, unsaved markers
		ImVec4 error;     // stop, errors
	};

	// Call once, right after ImGui::CreateContext(). Registers the imgui.ini handler and
	// applies the default theme; a saved theme replaces it when ImGui loads imgui.ini.
	void Init();

	void          Apply(Theme theme);   // switch theme now and mark imgui.ini to be saved
	Theme         Current();
	char const*   Name(Theme theme);
	bool          HasCustom();          // false until "Save as Custom Theme" has been used once
	Palette const& GetPalette();

	float GetFontScale();               // global text scale, 1.0 = 100%
	void  SetFontScale(float scale);    // clamped to [0.75, 2.0]

	// Copies the live style (as edited in the Theme Editor) into the Custom slot and selects it.
	void SaveCurrentAsCustom();

	// ---- Theme files (.crutones) ----
	inline constexpr char const* kThemeExtension = ".crutones";
	inline constexpr char const* kThemeFolder    = "Editor/Themes";   // under Assets; never cooked

	// Where the theme files live: <assetRoot>/Editor/Themes. Set once by the Asset Browser.
	void SetAssetRoot(std::filesystem::path const& assetRoot);
	// Called after Export writes a file, with its path under Assets ("Editor/Themes/Warm.crutones"),
	// so the Asset Browser can pick it up and show it.
	void SetOnThemeExported(std::function<void(std::string const&)> callback);

	std::vector<std::filesystem::path> ListThemeFiles();   // the folder's .crutones files, by name
	// Writes the live style as <Name>.crutones (Name: letters, digits, spaces, '-' and '_' kept) and
	// makes it the Custom theme. false (with *error) if the name is empty or the write failed.
	bool ExportCurrent(std::string const& name, std::string* error = nullptr);
	bool ThemeFileExists(std::string const& name);
	// Loads a .crutones into the Custom slot and selects it. false (with *error) if it is not one.
	bool ImportFile(std::filesystem::path const& file, std::string* error = nullptr);
	// The theme file the Custom slot came from ("Warm"); empty if it was made in this editor.
	std::string const& CustomName();

	// Contents of the View > Theme submenu. Sets *openEditor when "Theme Editor..." is chosen.
	void DrawMenu(bool* openEditor);

	// The Theme Editor window (Dear ImGui's style editor plus save / revert). Call every frame;
	// does nothing while *open is false.
	void DrawEditorWindow(bool* open);
}
