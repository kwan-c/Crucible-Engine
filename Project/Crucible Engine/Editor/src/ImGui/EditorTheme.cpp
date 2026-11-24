/*!
* @file			EditorTheme.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			10 October 2026
*
* @brief		Editor colour themes and font size, defined in EditorTheme.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/EditorTheme.h"

#include "Json/JsonWrapper.h"
#include "Diagnostics/Logger.h"

#include <imgui_internal.h>   // ImGuiSettingsHandler, AddSettingsHandler, MarkIniSettingsDirty
#include "misc/cpp/imgui_stdlib.h"   // ImGui::InputText(std::string*)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <cstring>
#include <string>
#include <system_error>

namespace {

	using Crucible::EditorTheme::Theme;
	using Crucible::EditorTheme::Palette;

	constexpr int   kThemeCount  = static_cast<int>(Theme::Count);
	constexpr float kMinFontScale = 0.75f;
	constexpr float kMaxFontScale = 2.0f;

	Theme      g_theme     = Theme::CrucibleDark;
	float      g_fontScale = 1.0f;
	bool       g_hasCustom = false;
	ImGuiStyle g_custom;              // the Custom theme; only meaningful once g_hasCustom is true
	int        g_loadedTheme = -1;    // theme read from imgui.ini, applied in IniApplyAll
	std::string g_customName;         // the .crutones the Custom theme came from; empty = made here

	std::filesystem::path                   g_themeFolder;   // <Assets>/Editor/Themes (SetAssetRoot)
	std::function<void(std::string const&)> g_onExported;    // SetOnThemeExported
	std::string                             g_exportName;    // the Theme Editor's name field

	// Status colours. The light set is darker so it still reads on a pale background.
	Palette const kDarkPalette {
		ImVec4(0.30f, 0.78f, 0.42f, 1.0f),   // success
		ImVec4(0.96f, 0.72f, 0.22f, 1.0f),   // warning
		ImVec4(0.92f, 0.36f, 0.32f, 1.0f) }; // error
	Palette const kLightPalette {
		ImVec4(0.12f, 0.55f, 0.25f, 1.0f),
		ImVec4(0.72f, 0.45f, 0.00f, 1.0f),
		ImVec4(0.80f, 0.18f, 0.15f, 1.0f) };
	Palette g_palette = kDarkPalette;

	// Stable names written to imgui.ini (the menu shows Name() instead).
	char const* IniKey(Theme theme) {
		switch (theme) {
		case Theme::CrucibleDark: return "CrucibleDark";
		case Theme::Dark:         return "Dark";
		case Theme::Light:        return "Light";
		case Theme::Classic:      return "Classic";
		case Theme::Custom:       return "Custom";
		default:                  return "CrucibleDark";
		}
	}

	// ---- Shape settings (rounding, borders, padding) ----
	// These change between themes and are saved with the Custom theme. Everything else in
	// ImGuiStyle is left alone.
	struct FloatField { char const* name; float  ImGuiStyle::* member; };
	struct Vec2Field  { char const* name; ImVec2 ImGuiStyle::* member; };

	FloatField const kFloatFields[] = {
		{ "WindowRounding",    &ImGuiStyle::WindowRounding },
		{ "ChildRounding",     &ImGuiStyle::ChildRounding },
		{ "PopupRounding",     &ImGuiStyle::PopupRounding },
		{ "FrameRounding",     &ImGuiStyle::FrameRounding },
		{ "ScrollbarRounding", &ImGuiStyle::ScrollbarRounding },
		{ "GrabRounding",      &ImGuiStyle::GrabRounding },
		{ "TabRounding",       &ImGuiStyle::TabRounding },
		{ "WindowBorderSize",  &ImGuiStyle::WindowBorderSize },
		{ "ChildBorderSize",   &ImGuiStyle::ChildBorderSize },
		{ "PopupBorderSize",   &ImGuiStyle::PopupBorderSize },
		{ "FrameBorderSize",   &ImGuiStyle::FrameBorderSize },
		{ "TabBorderSize",     &ImGuiStyle::TabBorderSize },
	};
	Vec2Field const kVec2Fields[] = {
		{ "WindowPadding",     &ImGuiStyle::WindowPadding },
		{ "FramePadding",      &ImGuiStyle::FramePadding },
		{ "ItemSpacing",       &ImGuiStyle::ItemSpacing },
		{ "ItemInnerSpacing",  &ImGuiStyle::ItemInnerSpacing },
		{ "CellPadding",       &ImGuiStyle::CellPadding },
	};

	void CopyShape(ImGuiStyle& dst, ImGuiStyle const& src) {
		for (FloatField const& f : kFloatFields) { dst.*f.member = src.*f.member; }
		for (Vec2Field const& f : kVec2Fields)   { dst.*f.member = src.*f.member; }
	}

	void CopyColours(ImGuiStyle& dst, ImGuiStyle const& src) {
		std::copy(std::begin(src.Colors), std::end(src.Colors), std::begin(dst.Colors));
	}

	ImVec4 WithAlpha(ImVec4 c, float a) { c.w = a; return c; }

	// Charcoal greys with an ember-orange accent (a crucible is where metal is melted).
	void StyleCrucibleDark(ImGuiStyle& s) {
		ImGui::StyleColorsDark(&s);   // fills any colour not set below

		s.WindowRounding = 4.0f;  s.ChildRounding = 4.0f;  s.PopupRounding = 4.0f;
		s.FrameRounding = 3.0f;   s.GrabRounding = 3.0f;   s.TabRounding = 3.0f;
		s.ScrollbarRounding = 6.0f;
		s.WindowBorderSize = 1.0f; s.ChildBorderSize = 1.0f; s.PopupBorderSize = 1.0f;
		s.FrameBorderSize = 0.0f;  s.TabBorderSize = 0.0f;
		s.WindowPadding = ImVec2(8.0f, 8.0f);
		s.FramePadding = ImVec2(6.0f, 4.0f);
		s.ItemSpacing = ImVec2(8.0f, 5.0f);
		s.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
		s.CellPadding = ImVec2(5.0f, 3.0f);

		ImVec4 const ember(0.93f, 0.51f, 0.20f, 1.0f);
		ImVec4 const emberDim(0.62f, 0.33f, 0.13f, 1.0f);
		ImVec4 const border(0.23f, 0.23f, 0.25f, 1.0f);
		ImVec4* c = s.Colors;

		c[ImGuiCol_Text]                      = ImVec4(0.90f, 0.90f, 0.91f, 1.0f);
		c[ImGuiCol_TextDisabled]              = ImVec4(0.50f, 0.50f, 0.53f, 1.0f);
		c[ImGuiCol_WindowBg]                  = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
		c[ImGuiCol_ChildBg]                   = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
		c[ImGuiCol_PopupBg]                   = ImVec4(0.10f, 0.10f, 0.11f, 0.98f);
		c[ImGuiCol_Border]                    = border;
		c[ImGuiCol_BorderShadow]              = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
		c[ImGuiCol_FrameBg]                   = ImVec4(0.19f, 0.19f, 0.21f, 1.0f);
		c[ImGuiCol_FrameBgHovered]            = ImVec4(0.24f, 0.24f, 0.27f, 1.0f);
		c[ImGuiCol_FrameBgActive]             = ImVec4(0.28f, 0.28f, 0.31f, 1.0f);
		c[ImGuiCol_TitleBg]                   = ImVec4(0.08f, 0.08f, 0.09f, 1.0f);
		c[ImGuiCol_TitleBgActive]             = ImVec4(0.10f, 0.10f, 0.11f, 1.0f);
		c[ImGuiCol_TitleBgCollapsed]          = ImVec4(0.08f, 0.08f, 0.09f, 0.75f);
		c[ImGuiCol_MenuBarBg]                 = ImVec4(0.09f, 0.09f, 0.10f, 1.0f);
		c[ImGuiCol_ScrollbarBg]               = ImVec4(0.10f, 0.10f, 0.11f, 0.60f);
		c[ImGuiCol_ScrollbarGrab]             = ImVec4(0.28f, 0.28f, 0.30f, 1.0f);
		c[ImGuiCol_ScrollbarGrabHovered]      = ImVec4(0.35f, 0.35f, 0.38f, 1.0f);
		c[ImGuiCol_ScrollbarGrabActive]       = WithAlpha(ember, 0.80f);
		c[ImGuiCol_CheckMark]                 = ember;
		c[ImGuiCol_SliderGrab]                = WithAlpha(ember, 0.80f);
		c[ImGuiCol_SliderGrabActive]          = ember;
		c[ImGuiCol_Button]                    = ImVec4(0.22f, 0.22f, 0.24f, 1.0f);
		c[ImGuiCol_ButtonHovered]             = ImVec4(0.30f, 0.30f, 0.33f, 1.0f);
		c[ImGuiCol_ButtonActive]              = emberDim;
		c[ImGuiCol_Header]                    = WithAlpha(ember, 0.28f);
		c[ImGuiCol_HeaderHovered]             = WithAlpha(ember, 0.40f);
		c[ImGuiCol_HeaderActive]              = WithAlpha(ember, 0.55f);
		c[ImGuiCol_Separator]                 = border;
		c[ImGuiCol_SeparatorHovered]          = WithAlpha(ember, 0.60f);
		c[ImGuiCol_SeparatorActive]           = ember;
		c[ImGuiCol_ResizeGrip]                = WithAlpha(ember, 0.15f);
		c[ImGuiCol_ResizeGripHovered]         = WithAlpha(ember, 0.50f);
		c[ImGuiCol_ResizeGripActive]          = WithAlpha(ember, 0.80f);
		c[ImGuiCol_InputTextCursor]           = c[ImGuiCol_Text];
		c[ImGuiCol_Tab]                       = ImVec4(0.10f, 0.10f, 0.11f, 1.0f);
		c[ImGuiCol_TabHovered]                = ImVec4(0.24f, 0.24f, 0.27f, 1.0f);
		c[ImGuiCol_TabSelected]               = ImVec4(0.17f, 0.17f, 0.19f, 1.0f);
		c[ImGuiCol_TabSelectedOverline]       = ember;
		c[ImGuiCol_TabDimmed]                 = ImVec4(0.09f, 0.09f, 0.10f, 1.0f);
		c[ImGuiCol_TabDimmedSelected]         = ImVec4(0.14f, 0.14f, 0.16f, 1.0f);
		c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.40f, 0.40f, 0.42f, 1.0f);
		c[ImGuiCol_DockingPreview]            = WithAlpha(ember, 0.55f);
		c[ImGuiCol_DockingEmptyBg]            = ImVec4(0.07f, 0.07f, 0.08f, 1.0f);
		c[ImGuiCol_PlotHistogram]             = ember;
		c[ImGuiCol_PlotHistogramHovered]      = ImVec4(1.00f, 0.62f, 0.30f, 1.0f);
		c[ImGuiCol_TableHeaderBg]             = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);
		c[ImGuiCol_TableBorderStrong]         = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);
		c[ImGuiCol_TableBorderLight]          = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
		c[ImGuiCol_TableRowBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
		c[ImGuiCol_TableRowBgAlt]             = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
		c[ImGuiCol_TextLink]                  = ImVec4(1.00f, 0.64f, 0.35f, 1.0f);
		c[ImGuiCol_TextSelectedBg]            = WithAlpha(ember, 0.35f);
		c[ImGuiCol_DragDropTarget]            = ember;
		c[ImGuiCol_NavCursor]                 = ember;
		c[ImGuiCol_ModalWindowDimBg]          = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);
	}

	bool HasLightBackground(ImGuiStyle const& s) {
		ImVec4 const bg = s.Colors[ImGuiCol_WindowBg];
		return 0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z > 0.5f;
	}

	// Applies a theme without asking ImGui to rewrite imgui.ini (used while loading it).
	void ApplyNoSave(Theme theme) {
		if (theme == Theme::Custom && !g_hasCustom) { theme = Theme::CrucibleDark; }

		ImGuiStyle& s = ImGui::GetStyle();
		ImGuiStyle const stock;   // Dear ImGui's default sizes

		switch (theme) {
		case Theme::Dark:    CopyShape(s, stock); ImGui::StyleColorsDark(&s);    break;
		case Theme::Light:   CopyShape(s, stock); ImGui::StyleColorsLight(&s);   break;
		case Theme::Classic: CopyShape(s, stock); ImGui::StyleColorsClassic(&s); break;
		case Theme::Custom:  CopyShape(s, g_custom); CopyColours(s, g_custom);   break;
		case Theme::CrucibleDark:
		default:             StyleCrucibleDark(s); theme = Theme::CrucibleDark;  break;
		}

		g_theme = theme;
		g_palette = HasLightBackground(s) ? kLightPalette : kDarkPalette;
		s.FontScaleMain = g_fontScale;
	}

	// ---- .crutones files (Assets/Editor/Themes) ----
	// JSON with everything by name, so a theme still loads after Dear ImGui adds, renames or
	// reorders colours (unknown names are skipped, missing ones keep Crucible Dark's value):
	// { "type": "CrucibleEditorTheme", "version": 1, "name": "Warm",
	//   "colors": { "WindowBg": [0.12, 0.12, 0.13, 1.0], ... },
	//   "style":  { "WindowRounding": [4.0], "FramePadding": [6.0, 4.0], ... } }
	constexpr char const* kThemeFileType    = "CrucibleEditorTheme";
	constexpr int         kThemeFileVersion = 1;

	// A path as UTF-8 text, for names shown in menus (narrow .string() can throw on Windows
	// for characters outside the system code page).
	std::string Utf8(std::filesystem::path const& path) {
		std::u8string const text = path.u8string();
		return std::string(text.begin(), text.end());
	}

	// Letters, digits, spaces, '-' and '_' only (a valid file name everywhere), trimmed.
	std::string SanitizeThemeName(std::string const& name) {
		std::string out;
		for (char const ch : name) {
			unsigned char const c = static_cast<unsigned char>(ch);
			if (c < 128 && (std::isalnum(c) || ch == ' ' || ch == '-' || ch == '_')) { out += ch; }
		}
		std::size_t const first = out.find_first_not_of(' ');
		if (first == std::string::npos) { return {}; }
		out = out.substr(first, out.find_last_not_of(' ') - first + 1);
		if (out.size() > 64) { out.resize(64); }
		return out;
	}

	std::filesystem::path ThemePath(std::string const& sanitizedName) {
		return g_themeFolder / (sanitizedName + Crucible::EditorTheme::kThemeExtension);
	}

	// Rounded so the file reads 0.12 rather than 0.11999999731779099.
	double Rounded(float value) { return std::round(static_cast<double>(value) * 1000.0) / 1000.0; }

	bool WriteThemeFile(std::filesystem::path const& file, std::string const& name, ImGuiStyle const& s, std::string& error) {
		Crucible::JsonWrapper doc;
		doc.CreateEmpty();
		doc.Set("type", std::string(kThemeFileType));
		doc.Set("version", kThemeFileVersion);
		doc.Set("name", name);
		for (int i = 0; i < ImGuiCol_COUNT; ++i) {
			ImVec4 const& c = s.Colors[i];
			doc.SetList(std::string("colors/") + ImGui::GetStyleColorName(i),
			            std::vector<double>{ Rounded(c.x), Rounded(c.y), Rounded(c.z), Rounded(c.w) });
		}
		for (FloatField const& f : kFloatFields) {
			doc.SetList(std::string("style/") + f.name, std::vector<double>{ Rounded(s.*f.member) });
		}
		for (Vec2Field const& f : kVec2Fields) {
			ImVec2 const v = s.*f.member;
			doc.SetList(std::string("style/") + f.name, std::vector<double>{ Rounded(v.x), Rounded(v.y) });
		}

		std::error_code ec;
		std::filesystem::create_directories(file.parent_path(), ec);
		if (!doc.SaveToFile(file)) { error = "could not write " + Utf8(file); return false; }
		return true;
	}

	bool ReadThemeFile(std::filesystem::path const& file, ImGuiStyle& out, std::string& error) {
		Crucible::JsonWrapper doc;
		if (!doc.ReadFromFile(file)) { error = "it is not a readable JSON file"; return false; }
		if (doc.Get<std::string>("type") != kThemeFileType || !doc.Has("colors")) {
			error = "it is not an editor theme (no \"type\": \"CrucibleEditorTheme\")";
			return false;
		}
		if (doc.Get<int>("version", kThemeFileVersion) > kThemeFileVersion) {
			CRU_USER_WARN("Theme '{}' was made by a newer editor; loading what this one understands", Utf8(file.stem()));
		}

		StyleCrucibleDark(out);   // the base for anything the file leaves out
		for (int i = 0; i < ImGuiCol_COUNT; ++i) {
			std::vector<float> const c = doc.GetList<float>(std::string("colors/") + ImGui::GetStyleColorName(i));
			if (c.size() == 4) { out.Colors[i] = ImVec4(c[0], c[1], c[2], c[3]); }
		}
		for (FloatField const& f : kFloatFields) {
			std::vector<float> const v = doc.GetList<float>(std::string("style/") + f.name);
			if (v.size() >= 1) { out.*f.member = v[0]; }
		}
		for (Vec2Field const& f : kVec2Fields) {
			std::vector<float> const v = doc.GetList<float>(std::string("style/") + f.name);
			if (v.size() >= 2) { out.*f.member = ImVec2(v[0], v[1]); }
		}
		return true;
	}

	// The Theme Editor's export, once the name is known to be new or the user agreed to replace it.
	void DoExport() {
		std::string error;
		if (!Crucible::EditorTheme::ExportCurrent(g_exportName, &error)) {
			CRU_USER_ERROR("Could not export the theme: {}", error);
		}
	}

	// Lists the shared themes as menu items / selectables; picking one imports it.
	void DrawThemeFileList(bool asMenuItems) {
		std::vector<std::filesystem::path> const files = Crucible::EditorTheme::ListThemeFiles();
		if (files.empty()) {
			ImGui::TextDisabled("None yet - name one in the Theme Editor and press Export");
			return;
		}
		for (std::filesystem::path const& file : files) {
			std::string const name = Utf8(file.stem());
			bool const current = g_theme == Theme::Custom && g_customName == name;
			bool const picked = asMenuItems ? ImGui::MenuItem(name.c_str(), nullptr, current)
			                                : ImGui::Selectable(name.c_str(), current);
			if (picked) {
				std::string error;
				if (!Crucible::EditorTheme::ImportFile(file, &error)) {
					CRU_USER_ERROR("Could not load theme '{}': {}", name, error);
				}
			}
		}
	}

	// ---- imgui.ini ----
	// [CrucibleEditor][Appearance]
	// Theme=CrucibleDark
	// FontScale=1.00
	// CustomCol=WindowBg,0.120,0.120,0.130,1.000   (one per colour, only once a Custom theme exists)
	// CustomVar=FramePadding,6.00,4.00             (shape settings of the Custom theme)
	// CustomName=Warm                              (the .crutones it came from, if any)

	// Reads up to `count` comma-separated floats starting at `text`. Returns how many were read.
	int ReadFloats(char const* text, float* out, int count) {
		int read = 0;
		while (read < count && text != nullptr && *text != '\0') {
			char* end = nullptr;
			float const value = std::strtof(text, &end);
			if (end == text) { break; }
			out[read++] = value;
			text = (*end == ',') ? end + 1 : nullptr;
		}
		return read;
	}

	void* IniReadOpen(ImGuiContext*, ImGuiSettingsHandler*, char const* name) {
		static int s_entry = 0;
		return std::strcmp(name, "Appearance") == 0 ? &s_entry : nullptr;
	}

	void IniReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, char const* line) {
		char const* eq = std::strchr(line, '=');
		if (eq == nullptr) { return; }
		std::string const key(line, eq);
		char const* value = eq + 1;

		if (key == "Theme") {
			for (int i = 0; i < kThemeCount; ++i) {
				if (std::strcmp(value, IniKey(static_cast<Theme>(i))) == 0) { g_loadedTheme = i; }
			}
		}
		else if (key == "CustomName") {
			g_customName = SanitizeThemeName(value);
		}
		else if (key == "FontScale") {
			float scale = 1.0f;
			if (ReadFloats(value, &scale, 1) == 1) { g_fontScale = std::clamp(scale, kMinFontScale, kMaxFontScale); }
		}
		else if (key == "CustomCol" || key == "CustomVar") {
			char const* comma = std::strchr(value, ',');
			if (comma == nullptr) { return; }
			std::string const name(value, comma);
			float v[4] = {};
			int const n = ReadFloats(comma + 1, v, 4);

			if (key == "CustomCol" && n == 4) {
				for (int i = 0; i < ImGuiCol_COUNT; ++i) {
					if (name == ImGui::GetStyleColorName(i)) {
						g_custom.Colors[i] = ImVec4(v[0], v[1], v[2], v[3]);
						g_hasCustom = true;
					}
				}
			}
			else if (key == "CustomVar") {
				for (FloatField const& f : kFloatFields) { if (name == f.name && n >= 1) { g_custom.*f.member = v[0]; } }
				for (Vec2Field const& f : kVec2Fields)   { if (name == f.name && n >= 2) { g_custom.*f.member = ImVec2(v[0], v[1]); } }
			}
		}
	}

	void IniApplyAll(ImGuiContext*, ImGuiSettingsHandler*) {
		ApplyNoSave(g_loadedTheme >= 0 ? static_cast<Theme>(g_loadedTheme) : g_theme);
		g_loadedTheme = -1;
	}

	void IniWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
		out->appendf("[%s][Appearance]\n", handler->TypeName);
		out->appendf("Theme=%s\n", IniKey(g_theme));
		out->appendf("FontScale=%.2f\n", static_cast<double>(g_fontScale));
		if (g_hasCustom) {
			if (!g_customName.empty()) { out->appendf("CustomName=%s\n", g_customName.c_str()); }
			for (int i = 0; i < ImGuiCol_COUNT; ++i) {
				ImVec4 const& c = g_custom.Colors[i];
				out->appendf("CustomCol=%s,%.3f,%.3f,%.3f,%.3f\n", ImGui::GetStyleColorName(i),
					static_cast<double>(c.x), static_cast<double>(c.y), static_cast<double>(c.z), static_cast<double>(c.w));
			}
			for (FloatField const& f : kFloatFields) {
				out->appendf("CustomVar=%s,%.2f\n", f.name, static_cast<double>(g_custom.*f.member));
			}
			for (Vec2Field const& f : kVec2Fields) {
				ImVec2 const v = g_custom.*f.member;
				out->appendf("CustomVar=%s,%.2f,%.2f\n", f.name, static_cast<double>(v.x), static_cast<double>(v.y));
			}
		}
		out->append("\n");
	}
}

namespace Crucible::EditorTheme {

	void Init() {
		ImGuiSettingsHandler handler;
		handler.TypeName   = "CrucibleEditor";
		handler.TypeHash   = ImHashStr("CrucibleEditor");
		handler.ReadOpenFn = IniReadOpen;
		handler.ReadLineFn = IniReadLine;
		handler.ApplyAllFn = IniApplyAll;
		handler.WriteAllFn = IniWriteAll;
		ImGui::AddSettingsHandler(&handler);

		ApplyNoSave(Theme::CrucibleDark);   // until imgui.ini is read on the first frame
	}

	void Apply(Theme theme) {
		ApplyNoSave(theme);
		ImGui::MarkIniSettingsDirty();
	}

	Theme Current()                 { return g_theme; }
	bool HasCustom()                { return g_hasCustom; }
	Palette const& GetPalette()     { return g_palette; }
	float GetFontScale()            { return g_fontScale; }

	char const* Name(Theme theme) {
		switch (theme) {
		case Theme::CrucibleDark: return "Crucible Dark";
		case Theme::Dark:         return "ImGui Dark";
		case Theme::Light:        return "Light";
		case Theme::Classic:      return "Classic";
		case Theme::Custom:       return "Custom";
		default:                  return "?";
		}
	}

	void SetFontScale(float scale) {
		g_fontScale = std::clamp(scale, kMinFontScale, kMaxFontScale);
		ImGui::GetStyle().FontScaleMain = g_fontScale;
		ImGui::MarkIniSettingsDirty();
	}

	void SaveCurrentAsCustom() {
		ImGuiStyle const& s = ImGui::GetStyle();
		g_custom = s;
		g_hasCustom = true;
		g_customName.clear();   // edited here: no longer the theme file it may have come from
		g_fontScale = std::clamp(s.FontScaleMain, kMinFontScale, kMaxFontScale);   // the style editor can change it too
		g_theme = Theme::Custom;
		g_palette = HasLightBackground(s) ? kLightPalette : kDarkPalette;
		ImGui::MarkIniSettingsDirty();
	}

	void SetAssetRoot(std::filesystem::path const& assetRoot) {
		g_themeFolder = assetRoot / "Editor" / "Themes";
	}

	void SetOnThemeExported(std::function<void(std::string const&)> callback) {
		g_onExported = std::move(callback);
	}

	std::string const& CustomName() { return g_customName; }

	std::vector<std::filesystem::path> ListThemeFiles() {
		std::vector<std::filesystem::path> files;
		std::error_code ec;
		if (g_themeFolder.empty() || !std::filesystem::is_directory(g_themeFolder, ec)) { return files; }
		for (std::filesystem::directory_iterator it(g_themeFolder, ec), end; !ec && it != end; it.increment(ec)) {
			if (!it->is_regular_file(ec)) { continue; }
			std::string ext = Utf8(it->path().extension());
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (ext == kThemeExtension) { files.push_back(it->path()); }
		}
		std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) { return Utf8(a.stem()) < Utf8(b.stem()); });
		return files;
	}

	bool ThemeFileExists(std::string const& name) {
		std::string const clean = SanitizeThemeName(name);
		std::error_code ec;
		return !clean.empty() && !g_themeFolder.empty() && std::filesystem::exists(ThemePath(clean), ec);
	}

	bool ExportCurrent(std::string const& name, std::string* error) {
		std::string const clean = SanitizeThemeName(name);
		std::string message;
		if (g_themeFolder.empty())  { message = "the Assets folder is not known yet"; }
		else if (clean.empty())     { message = "give the theme a name (letters, digits, spaces, '-' or '_')"; }
		else {
			ImGuiStyle const& live = ImGui::GetStyle();
			if (WriteThemeFile(ThemePath(clean), clean, live, message)) {
				// What was exported is now this editor's Custom theme too, under that name.
				SaveCurrentAsCustom();
				g_customName = clean;
				std::string const virtualPath = std::string(kThemeFolder) + "/" + clean + kThemeExtension;
				CRU_USER_INFO("Exported editor theme to Assets/{}", virtualPath);
				if (g_onExported) { g_onExported(virtualPath); }
				return true;
			}
		}
		if (error != nullptr) { *error = message; }
		return false;
	}

	bool ImportFile(std::filesystem::path const& file, std::string* error) {
		ImGuiStyle loaded;
		std::string message;
		if (!ReadThemeFile(file, loaded, message)) {
			if (error != nullptr) { *error = message; }
			return false;
		}
		g_custom = loaded;
		g_hasCustom = true;
		g_customName = SanitizeThemeName(Utf8(file.stem()));
		Apply(Theme::Custom);   // also marks imgui.ini, so it is still selected next time
		CRU_USER_INFO("Editor theme '{}' applied", g_customName);
		return true;
	}

	void DrawMenu(bool* openEditor) {
		for (int i = 0; i < kThemeCount; ++i) {
			Theme const theme = static_cast<Theme>(i);
			bool const enabled = theme != Theme::Custom || g_hasCustom;
			std::string label = Name(theme);
			if (theme == Theme::Custom && !g_customName.empty()) { label += " (" + g_customName + ")"; }
			if (ImGui::MenuItem(label.c_str(), nullptr, g_theme == theme, enabled)) { Apply(theme); }
			if (theme == Theme::Custom && !enabled) {
				ImGui::SetItemTooltip("Make one in Theme Editor... and press Save as Custom Theme");
			}
		}

		if (ImGui::BeginMenu("Shared Themes")) {
			DrawThemeFileList(true);
			ImGui::EndMenu();
		}
		ImGui::SetItemTooltip("The .crutones files in Assets/Editor/Themes - shared through git,\n"
		                      "never packaged into the game. Picking one makes it your Custom theme.");

		ImGui::Separator();
		if (ImGui::BeginMenu("Font Size")) {
			float const sizes[] = { 0.80f, 0.90f, 1.00f, 1.10f, 1.25f, 1.50f };
			for (float const size : sizes) {
				char label[16];
				std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(size * 100.0f + 0.5f));
				bool const selected = std::abs(g_fontScale - size) < 0.005f;
				if (ImGui::MenuItem(label, nullptr, selected)) { SetFontScale(size); }
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Theme Editor...") && openEditor != nullptr) { *openEditor = true; }
	}

	void DrawEditorWindow(bool* open) {
		if (open == nullptr || !*open) { return; }

		ImGui::SetNextWindowSize(ImVec2(460.0f, 620.0f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Theme Editor", open)) {
			ImGui::TextWrapped("Edits preview live. Press Save as Custom Theme to keep them - "
			                   "otherwise they are lost when you switch theme or restart the editor.");
			ImGui::Spacing();

			if (ImGui::Button("Save as Custom Theme")) { SaveCurrentAsCustom(); }
			ImGui::SameLine();
			std::string const revert = std::string("Revert to ") + Name(g_theme);
			if (ImGui::Button(revert.c_str())) { Apply(g_theme); }

			// Share: write the live style to Assets/Editor/Themes/<Name>.crutones, or load one.
			ImGui::Spacing();
			if (ImGui::IsWindowAppearing() && g_exportName.empty()) { g_exportName = g_customName; }
			ImGui::SetNextItemWidth(200.0f);
			ImGui::InputTextWithHint("##ThemeName", "Theme name", &g_exportName);
			ImGui::SameLine();
			bool const canExport = !SanitizeThemeName(g_exportName).empty() && !g_themeFolder.empty();
			ImGui::BeginDisabled(!canExport);
			if (ImGui::Button("Export")) {
				if (ThemeFileExists(g_exportName)) { ImGui::OpenPopup("Replace theme?"); }
				else                               { DoExport(); }
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Saves these settings to Assets/Editor/Themes/<name>.crutones.\n"
			                      "Commit it to share it; it is never packaged into the game.");
			ImGui::SameLine();
			if (ImGui::Button("Import...")) { ImGui::OpenPopup("##ImportTheme"); }

			if (ImGui::BeginPopup("##ImportTheme")) {
				DrawThemeFileList(false);
				ImGui::EndPopup();
			}
			if (ImGui::BeginPopupModal("Replace theme?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("Assets/%s/%s%s already exists. Replace it?", kThemeFolder,
				            SanitizeThemeName(g_exportName).c_str(), kThemeExtension);
				if (ImGui::Button("Replace")) { DoExport(); ImGui::CloseCurrentPopup(); }
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
				ImGui::EndPopup();
			}

			ImGui::Separator();
			ImGui::ShowStyleEditor();   // Dear ImGui's full colour / size editor
		}
		ImGui::End();
	}
}
