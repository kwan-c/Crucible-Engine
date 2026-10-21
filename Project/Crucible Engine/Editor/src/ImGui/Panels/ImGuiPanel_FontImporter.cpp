/*!
* @file			ImGuiPanel_FontImporter.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			21 October 2026
*
* @brief		The Font Importer window. See ImGuiPanel_FontImporter.h.
*/

#include "ImGui/Panels/ImGuiPanel_FontImporter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>

#include <imgui_impl_vulkan.h>

#include <Core.h>
#include "AssetPipe/Assetmanager.h"
#include "Diagnostics/Logger.h"
#include "Graphics/RenderSystem.h"
#include "Resources/ResourceManager.h"
#include "ImGui/EditorTheme.h"
#include "ImGui/EditorWidgets.h"
#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"

namespace {

	using namespace Crucible;

	constexpr double kStatusInterval  = 1.0;   // seconds between "is it up to date?" checks (the files may change outside the editor)
	constexpr int    kPreviewDelay    = 2;     // frames: a new texture is uploaded at the start of the next frame
	constexpr int    kRetireFrames    = 4;     // frames before a replaced preview descriptor is freed (> frames in flight)
	constexpr int    kMaxBitmapSizes  = 8;

	char const* const kOutputTypeNames[] = { "Bitmap", "SDF", "MTSDF" };
	char const* const kOutputTypeHelp[] = {
		"Pre-rendered at fixed pixel sizes. Smallest and cheapest; crisp only at the baked sizes "
		"(text scaled to other sizes looks soft). Good for small UI and pixel-art text.",
		"Signed distance field. Scales to any size from one atlas; corners come out slightly rounded.",
		"Multi-channel signed distance field. Scales to any size with sharp corners, and supports "
		"outline / glow / shadow later. Bigger atlas, slower to compile.",
	};
	constexpr int kAtlasSizes[] = { 512, 1024, 2048, 4096, 8192, 16384 };

	std::string FormatBytes(std::size_t bytes) {
		char buffer[32] = {};
		if (bytes < 1024) { std::snprintf(buffer, sizeof(buffer), "%zu B", bytes); }
		else if (bytes < 1024 * 1024) { std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0); }
		else { std::snprintf(buffer, sizeof(buffer), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0)); }
		return buffer;
	}

	std::string Describe(FontOutputType type, std::uint32_t glyphs, std::uint32_t groups, std::uint32_t width, std::uint32_t height) {
		char buffer[128] = {};
		if (type == FontOutputType::Bitmap) {
			std::snprintf(buffer, sizeof(buffer), "%s, %u glyphs in %u size(s), %ux%u atlas", ToString(type), glyphs, groups, width, height);
		} else {
			std::snprintf(buffer, sizeof(buffer), "%s, %u glyphs, %ux%u atlas", ToString(type), glyphs, width, height);
		}
		return buffer;
	}

	void HelpMarker(char const* text) {
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::BeginItemTooltip()) {
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
			ImGui::TextUnformatted(text);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	// A virtual path dropped from the Asset Browser on the last item, or "".
	std::string AcceptAssetDrop() {
		std::string path;
		if (ImGui::BeginDragDropTarget()) {
			if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload)) {
				path = static_cast<char const*>(payload->Data);
			}
			ImGui::EndDragDropTarget();
		}
		return path;
	}

}	// anonymous namespace

// ------------------------------------------------------------------------------------------------ //

Crucible::FontImporter::~FontImporter() {
	ReleasePreview();
	FreeRetiredPreviews(true);
}

void Crucible::FontImporter::Init() {
	m_panelName = "Font Importer";
	m_docked = false;   // its own floating window; dock it anywhere and imgui.ini remembers
}

void Crucible::FontImporter::Open(std::string const& fontPath) {
	m_open = true;
	m_makeFocus = true;
	if (fontPath.empty()) { return; }

	std::string source = fontPath;
	if (!FontAssets::IsSourceFontPath(fontPath)) {
		source = FontAssets::SourceFontFor(m_assets, fontPath);   // a .cfont: open the font it was compiled from
		if (source.empty()) {
			CRU_USER_WARN("[Fonts] '{}' has no .ttf/.otf next to it to import from", fontPath);
			return;
		}
	}
	SelectFont(source);
}

void Crucible::FontImporter::Update(float dt) {
	// A compile finishes (and is saved) even while the window is closed.
	if (auto report = m_job.Poll(m_assets)) {
		FinishCompile(std::move(*report));
	}
	FreeRetiredPreviews(false);

	if (!m_open) { return; }

	ImGui::SetNextWindowSize(ImVec2(520.0f, 720.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(m_panelName.c_str(), &m_open, m_flags)) {
		if (!m_fontPath.empty()) {
			m_statusAge += dt;
			if (m_statusAge >= kStatusInterval) { RefreshStatus(); }
		}

		DrawFontPicker();
		ImGui::Separator();

		if (m_fontPath.empty()) {
			ImGui::Spacing();
			ImGui::TextWrapped("Pick a .ttf / .otf above, drop one here from the Asset Browser, or double-click "
			                   "one in the Asset Browser.");
		} else {
			DrawStatus();
			ImGui::Spacing();
			DrawSettings();
			ImGui::Spacing();
			DrawButtons();
			DrawReport();
			DrawPreview();
		}
	}
	ImGui::End();
}

// ------------------------------------------------------------------------------------------------ //
//  Which font
// ------------------------------------------------------------------------------------------------ //

void Crucible::FontImporter::SelectFont(std::string const& fontPath) {
	if (fontPath == m_fontPath) { m_statusAge = 1.0e9; return; }

	if (!m_fontPath.empty() && IsDirty()) {
		CRU_USER_WARN("[Fonts] Unapplied import settings for '{}' were discarded", m_fontPath);
	}
	ReleasePreview();
	m_fontPath = fontPath;
	m_actionError.clear();
	if (m_lastReport && m_lastReport->fontPath != m_fontPath) { m_lastReport.reset(); }
	LoadSavedSettings();
	RefreshStatus();
}

void Crucible::FontImporter::LoadSavedSettings() {
	FontAssets::SavedSettings saved = FontAssets::LoadSettings(m_assets, m_fontPath);
	m_saved              = saved.settings;
	m_edit               = saved.settings;
	m_settingsFileExists = saved.fileExists;
	m_settingsWarnings   = std::move(saved.warnings);
	m_settingsError      = std::move(saved.error);

	std::string text;
	for (auto const& range : m_edit.ranges) { text += FormatCodepointRange(range) + "\n"; }
	std::snprintf(m_rangesText, sizeof(m_rangesText), "%s", text.c_str());
	m_rangesError.clear();
}

void Crucible::FontImporter::RefreshStatus() {
	m_statusAge = 0.0;
	if (m_fontPath.empty()) { return; }
	FontAssets::FontStatus const previous = m_status;
	m_status = FontAssets::GetStatus(m_assets, m_fontPath, m_saved);

	// The .cfont changed on disk (compiled here or by a teammate's commit): show the new atlas.
	bool const changed = previous.hasInfo != m_status.hasInfo
		|| (m_status.hasInfo && (previous.info.settingsHash != m_status.info.settingsHash
		                         || previous.info.sourceHash != m_status.info.sourceHash
		                         || previous.info.atlasWidth != m_status.info.atlasWidth));
	if (changed) { ReleasePreview(); }
}

bool Crucible::FontImporter::IsDirty() const {
	return SerializeFontImportSettings(m_edit) != SerializeFontImportSettings(m_saved);
}

// ------------------------------------------------------------------------------------------------ //
//  Drawing
// ------------------------------------------------------------------------------------------------ //

void Crucible::FontImporter::DrawFontPicker() {
	std::string const preview = m_fontPath.empty() ? std::string("(choose a font)") : m_fontPath;
	if (ImGui::BeginCombo("Font", preview.c_str())) {
		std::vector<std::string> fonts;
		for (AssetEntry const* entry : m_assets.GetByType(AssetType::Font)) {
			if (FontAssets::IsSourceFontPath(entry->virtualPath)) { fonts.push_back(entry->virtualPath); }
		}
		std::sort(fonts.begin(), fonts.end());
		if (fonts.empty()) { ImGui::TextDisabled("No .ttf / .otf in the Assets folder"); }
		for (std::string const& font : fonts) {
			if (ImGui::Selectable(font.c_str(), font == m_fontPath)) { SelectFont(font); }
		}
		ImGui::EndCombo();
	}
	if (std::string const dropped = AcceptAssetDrop(); !dropped.empty()) {
		AssetEntry const* entry = m_assets.Find(dropped);
		if (entry && entry->type == AssetType::Font) { Open(dropped); }
		else { CRU_USER_WARN("[Fonts] '{}' is not a font", dropped); }
	}
}

void Crucible::FontImporter::DrawStatus() {
	EditorTheme::Palette const& palette = EditorTheme::GetPalette();
	using FontAssets::FontState;

	switch (m_status.state) {
	case FontState::UpToDate:
		ImGui::TextColored(palette.success, "Up to date");
		break;
	case FontState::OutOfDate:
		ImGui::TextColored(palette.warning, "Out of date");
		ImGui::SameLine();
		ImGui::TextDisabled("- %s", m_status.detail.c_str());
		break;
	case FontState::NotCompiled:
		ImGui::TextColored(palette.warning, "Not compiled");
		ImGui::SameLine();
		ImGui::TextDisabled("- press Compile to make %s", CompiledFontPathFor(m_fontPath).c_str());
		break;
	case FontState::Unreadable:
		ImGui::TextColored(palette.error, "Compiled font unreadable");
		ImGui::TextWrapped("%s", m_status.detail.c_str());
		break;
	case FontState::NoSource:
		ImGui::TextColored(palette.error, "Font file not found: %s", m_fontPath.c_str());
		break;
	}

	if (m_status.hasInfo) {
		FontFileInfo const& info = m_status.info;
		ImGui::TextDisabled("%s  -  %s", CompiledFontPathFor(m_fontPath).c_str(),
			Describe(info.outputType, info.glyphCount, info.groupCount, info.atlasWidth, info.atlasHeight).c_str());
	}

	if (!m_settingsError.empty()) {
		ImGui::TextColored(palette.error, "Settings file ignored:");
		ImGui::TextWrapped("%s", m_settingsError.c_str());
	}
	for (std::string const& warning : m_settingsWarnings) {
		ImGui::TextColored(palette.warning, "Settings: %s", warning.c_str());
	}
	if (!m_settingsFileExists && m_settingsError.empty()) {
		ImGui::TextDisabled("No %s yet - using the defaults", FontSettingsPathFor(m_fontPath).c_str());
	}
}

void Crucible::FontImporter::DrawSettings() {
	ImGui::SeparatorText("Import Settings");
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);

	// ---- Output type ----
	int type = static_cast<int>(m_edit.outputType);
	if (ImGui::Combo("Output Type", &type, kOutputTypeNames, IM_ARRAYSIZE(kOutputTypeNames))) {
		m_edit.outputType = static_cast<FontOutputType>(type);
	}
	HelpMarker(kOutputTypeHelp[std::clamp(type, 0, 2)]);

	if (m_edit.outputType == FontOutputType::Bitmap) {
		// ---- Bitmap sizes: one row per size ----
		ImGui::TextUnformatted("Sizes (px)");
		HelpMarker("Every glyph is rendered once per size. Text is drawn with the closest one, so pick the sizes "
		           "your UI actually uses (in reference pixels).");
		int removeAt = -1;
		for (int i = 0; i < static_cast<int>(m_edit.bitmapSizes.size()); ++i) {
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
			ImGui::InputInt("##size", &m_edit.bitmapSizes[static_cast<std::size_t>(i)], 1, 4);
			m_edit.bitmapSizes[static_cast<std::size_t>(i)] = std::clamp(m_edit.bitmapSizes[static_cast<std::size_t>(i)], 4, 256);
			ImGui::SameLine();
			ImGui::BeginDisabled(m_edit.bitmapSizes.size() <= 1);
			if (ImGui::SmallButton("Remove")) { removeAt = i; }
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		if (removeAt >= 0) { m_edit.bitmapSizes.erase(m_edit.bitmapSizes.begin() + removeAt); }
		ImGui::BeginDisabled(static_cast<int>(m_edit.bitmapSizes.size()) >= kMaxBitmapSizes);
		if (ImGui::SmallButton("+ Add Size")) {
			int const last = m_edit.bitmapSizes.empty() ? 8 : m_edit.bitmapSizes.back();
			m_edit.bitmapSizes.push_back(std::min(last * 2, 256));
		}
		ImGui::EndDisabled();
	} else {
		ImGui::DragInt("Glyph Size", &m_edit.glyphSize, 0.5f, 8, 256, "%d px", ImGuiSliderFlags_AlwaysClamp);
		HelpMarker("Pixels per em in the atlas. Bigger keeps finer detail and sharper corners, but the atlas "
		           "grows with the square of it. 32-64 is typical.");
		ImGui::DragFloat("Pixel Range", &m_edit.pixelRange, 0.05f, 1.0f, 32.0f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
		HelpMarker("How far from the outline the distance field reaches, in atlas pixels. It also limits "
		           "UIText's outline / glow / shadow blur: they can reach Pixel Range / 2 atlas pixels past "
		           "the letters (the Inspector shows that in screen pixels). 8 is the default; raise it for "
		           "thick outlines or wide glows (the atlas grows a little).");
	}

	ImGui::DragInt("Padding", &m_edit.padding, 0.1f, 0, 16, "%d px", ImGuiSliderFlags_AlwaysClamp);
	HelpMarker("Empty atlas pixels around each glyph, so texture filtering never picks up its neighbour.");

	// ---- Characters ----
	ImGui::SeparatorText("Characters");
	ImGui::Checkbox("ASCII (95 printable characters)", &m_edit.includeAscii);

	ImGui::TextUnformatted("Extra ranges");
	HelpMarker("Hex Unicode codepoints, one per line: a single one (20AC = euro sign) or a range "
	           "(00C0-00FF = accented Latin letters).");
	if (ImGui::InputTextMultiline("##ranges", m_rangesText, sizeof(m_rangesText),
	                              ImVec2(ImGui::GetContentRegionAvail().x * 0.55f, ImGui::GetTextLineHeight() * 4.0f))) {
		std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
		m_rangesError.clear();
		char const* line = m_rangesText;
		int lineNumber = 1;
		while (*line) {
			char const* end = std::strchr(line, '\n');
			std::string const text(line, end ? static_cast<std::size_t>(end - line) : std::strlen(line));
			if (text.find_first_not_of(" \t\r") != std::string::npos) {
				std::pair<std::uint32_t, std::uint32_t> range;
				if (ParseCodepointRange(text, range)) { ranges.push_back(range); }
				else if (m_rangesError.empty()) { m_rangesError = "line " + std::to_string(lineNumber) + ": '" + text + "' is not hex like 20AC or 00C0-00FF"; }
			}
			if (!end) { break; }
			line = end + 1;
			++lineNumber;
		}
		if (m_rangesError.empty()) { m_edit.ranges = std::move(ranges); }
	}
	if (!m_rangesError.empty()) { ImGui::TextColored(EditorTheme::GetPalette().error, "%s", m_rangesError.c_str()); }

	// ---- Sample text files ----
	ImGui::TextUnformatted("Sample text files");
	HelpMarker("Every character used in these files is included - the easy way to get exactly the "
	           "characters your game's text needs (e.g. a translated string table). Drop text assets here.");
	int removeFile = -1;
	for (int i = 0; i < static_cast<int>(m_edit.sampleTextFiles.size()); ++i) {
		ImGui::PushID(i + 1000);
		ImGui::BulletText("%s", m_edit.sampleTextFiles[static_cast<std::size_t>(i)].c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Remove")) { removeFile = i; }
		ImGui::PopID();
	}
	if (removeFile >= 0) { m_edit.sampleTextFiles.erase(m_edit.sampleTextFiles.begin() + removeFile); }

	auto addSample = [&](std::string const& path) {
		if (std::find(m_edit.sampleTextFiles.begin(), m_edit.sampleTextFiles.end(), path) == m_edit.sampleTextFiles.end()) {
			m_edit.sampleTextFiles.push_back(path);
		}
	};
	if (ImGui::BeginCombo("##addSample", "+ Add text file...")) {
		bool any = false;
		for (AssetEntry const* entry : m_assets.GetByType(AssetType::Text)) {
			any = true;
			if (ImGui::Selectable(entry->virtualPath.c_str())) { addSample(entry->virtualPath); }
		}
		if (!any) { ImGui::TextDisabled("No text assets (.txt, .json, ...)"); }
		ImGui::EndCombo();
	}
	if (std::string const dropped = AcceptAssetDrop(); !dropped.empty()) {
		AssetEntry const* entry = m_assets.Find(dropped);
		if (entry && entry->type == AssetType::Text) { addSample(dropped); }
		else { CRU_USER_WARN("[Fonts] '{}' is not a text asset", dropped); }
	}

	// ---- Limits ----
	ImGui::SeparatorText("Limits");
	int sizeIndex = 0;
	for (int i = 0; i < IM_ARRAYSIZE(kAtlasSizes); ++i) { if (kAtlasSizes[i] <= m_edit.atlasMaxSize) { sizeIndex = i; } }
	char label[32];
	std::snprintf(label, sizeof(label), "%d x %d", kAtlasSizes[sizeIndex], kAtlasSizes[sizeIndex]);
	if (ImGui::BeginCombo("Max Atlas Size", label)) {
		for (int i = 0; i < IM_ARRAYSIZE(kAtlasSizes); ++i) {
			std::snprintf(label, sizeof(label), "%d x %d", kAtlasSizes[i], kAtlasSizes[i]);
			if (ImGui::Selectable(label, i == sizeIndex)) { m_edit.atlasMaxSize = kAtlasSizes[i]; }
		}
		ImGui::EndCombo();
	}
	HelpMarker("A compile that would need a bigger atlas fails with a message instead.");

	ImGui::PopItemWidth();
}

void Crucible::FontImporter::DrawButtons() {
	ImGui::Separator();
	bool const playing = m_core.IsInPlaySession() || m_core.HasPendingPlayRequest();
	bool const dirty   = IsDirty();
	bool const valid   = m_rangesError.empty();

	ImGui::BeginDisabled(m_job.IsRunning() || playing || !valid || m_status.state == FontAssets::FontState::NoSource);
	if (ImGui::Button("Compile", ImVec2(110.0f, 0.0f))) { StartCompile(); }
	ImGui::EndDisabled();
	if (playing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("Stop play mode to compile fonts"); }

	ImGui::SameLine();
	ImGui::BeginDisabled(!dirty || !valid);
	if (ImGui::Button("Apply")) { Apply(); }
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!dirty);
	if (ImGui::Button("Revert")) { Revert(); }
	ImGui::EndDisabled();

	if (dirty) {
		ImGui::SameLine();
		ImGui::TextColored(EditorTheme::GetPalette().warning, "Unapplied changes");
	}

	if (m_job.IsRunning()) {
		static char const kSpinner[] = { '|', '/', '-', '\\' };
		ImGui::Text("%c Compiling %s ... %.1f s", kSpinner[(ImGui::GetFrameCount() / 8) % 4], m_job.FontPath().c_str(), m_job.ElapsedSeconds());
	}
	if (!m_actionError.empty()) {
		ImGui::TextColored(EditorTheme::GetPalette().error, "%s", m_actionError.c_str());
	}
}

void Crucible::FontImporter::DrawReport() {
	if (!m_lastReport) { return; }
	FontCompileJob::Report const& r = *m_lastReport;
	EditorTheme::Palette const& palette = EditorTheme::GetPalette();

	if (!ImGui::CollapsingHeader("Last Compile", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
	if (!r.ok) {
		ImGui::TextColored(palette.error, "Failed:");
		ImGui::TextWrapped("%s", r.error.c_str());
	} else {
		ImGui::TextColored(palette.success, "Compiled in %.2f s", r.totalSeconds);
		ImGui::TextUnformatted(Describe(r.outputType, static_cast<std::uint32_t>(r.glyphCount), static_cast<std::uint32_t>(r.groupCount),
		                                r.atlasWidth, r.atlasHeight).c_str());
		ImGui::Text("%zu kerning pairs, %s on disk", r.kernCount, FormatBytes(r.fileBytes).c_str());
	}
	if (!r.missing.empty()) {
		ImGui::TextColored(palette.warning, "%zu character(s) not in the font:", r.missing.size());
		ImGui::TextWrapped("%s", DescribeCodepoints(r.missing, 16).c_str());
	}
	for (std::string const& warning : r.warnings) {
		ImGui::TextColored(palette.warning, "%s", warning.c_str());
	}
}

void Crucible::FontImporter::DrawPreview() {
	if (!ImGui::CollapsingHeader("Atlas Preview", ImGuiTreeNodeFlags_DefaultOpen)) { return; }

	if (!m_status.hasInfo) {
		ImGui::TextDisabled("Compile the font to see its atlas.");
		return;
	}
	AcquirePreview();
	if (m_previewSet == nullptr) {
		ImGui::TextDisabled("%s", m_previewTried && !m_previewTexture.IsValid()
			? "The compiled font could not be loaded (or isn't indexed yet) - see the Console."
			: "Loading...");
		return;
	}

	ImGui::TextDisabled("%s", m_previewType == FontOutputType::Mtsdf
		? "MTSDF: red/green/blue hold the multi-channel distance (that colourful look is expected)."
		: "One-channel atlas, shown as white on transparent.");

	float const available = ImGui::GetContentRegionAvail().x;
	float const scale     = std::min(1.0f, available / static_cast<float>(std::max(1u, m_previewWidth)));
	ImVec2 const size(static_cast<float>(m_previewWidth) * scale, static_cast<float>(m_previewHeight) * scale);

	// A dark backdrop so white glyphs on a transparent atlas show up.
	ImVec2 const p0 = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(24, 24, 28, 255));
	ImTextureID const texture = reinterpret_cast<ImTextureID>(m_previewSet);
	ImGui::Image(texture, size);

	// Hover: a magnified view around the pointer.
	if (ImGui::IsItemHovered() && size.x > 0.0f && size.y > 0.0f) {
		ImVec2 const mouse = ImGui::GetIO().MousePos;
		float const u = std::clamp((mouse.x - p0.x) / size.x, 0.0f, 1.0f);
		float const v = std::clamp((mouse.y - p0.y) / size.y, 0.0f, 1.0f);
		float const halfU = 32.0f / static_cast<float>(m_previewWidth);
		float const halfV = 32.0f / static_cast<float>(m_previewHeight);
		ImGui::BeginTooltip();
		ImVec2 const t0 = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(t0, ImVec2(t0.x + 192.0f, t0.y + 192.0f), IM_COL32(24, 24, 28, 255));
		ImGui::Image(texture, ImVec2(192.0f, 192.0f), ImVec2(u - halfU, v - halfV), ImVec2(u + halfU, v + halfV));
		ImGui::TextDisabled("%ux%u atlas, x %.0f  y %.0f", m_previewWidth, m_previewHeight,
		                    u * static_cast<float>(m_previewWidth), v * static_cast<float>(m_previewHeight));
		ImGui::EndTooltip();
	}
}

// ------------------------------------------------------------------------------------------------ //
//  Actions
// ------------------------------------------------------------------------------------------------ //

bool Crucible::FontImporter::Apply() {
	m_actionError.clear();
	FontImportSettings settings = m_edit;
	std::vector<std::string> const fixes = settings.Sanitize();

	std::string error;
	if (!FontAssets::SaveSettings(m_assets, m_fontPath, settings, error)) {
		m_actionError = "Apply failed: " + error;
		CRU_USER_ERROR("[Fonts] {}", m_actionError);
		return false;
	}
	for (std::string const& fix : fixes) { CRU_USER_WARN("[Fonts] {}: {}", m_fontPath, fix); }

	LoadSavedSettings();   // read back what was written, so the window shows exactly the file
	RefreshStatus();
	return true;
}

void Crucible::FontImporter::Revert() {
	LoadSavedSettings();
	m_actionError.clear();
}

void Crucible::FontImporter::StartCompile() {
	m_actionError.clear();
	if (IsDirty() && !Apply()) { return; }   // Compile always builds what is saved, so the status stays honest

	std::string error;
	if (!m_job.Start(m_assets, m_fontPath, m_saved, error)) {
		m_actionError = "Compile failed to start: " + error;
		CRU_USER_ERROR("[Fonts] {}", m_actionError);
		return;
	}
	CRU_USER_INFO("[Fonts] Compiling {} as {}...", m_fontPath, ToString(m_saved.outputType));
}

void Crucible::FontImporter::FinishCompile(FontCompileJob::Report report) {
	if (!report.ok) {
		CRU_USER_ERROR("[Fonts] {} failed to compile: {}", report.fontPath, report.error);
	} else {
		CRU_USER_INFO("[Fonts] Compiled {} -> {}: {}, {} glyphs, {}x{} atlas, {} in {:.2f} s", report.fontPath, report.compiledPath,
		              ToString(report.outputType), report.glyphCount, report.atlasWidth, report.atlasHeight,
		              FormatBytes(report.fileBytes), report.totalSeconds);
		if (!report.missing.empty()) {
			CRU_USER_WARN("[Fonts] {}: {} character(s) not in the font: {}", report.fontPath, report.missing.size(),
			              DescribeCodepoints(report.missing, 8));
		}

		// Index the new .cfont (it gets its .meta / GUID), through the Asset Browser so its view updates too.
		if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) { browser->Refresh(); }
		else { m_assets.Refresh(); }

		// Swap the new font in for everything drawing with it.
		if (ResourceManager* resources = EditorManager::GetInstance().GetResources()) {
			resources->RefreshCatalog();
			if (AssetEntry const* compiled = m_assets.Find(report.compiledPath)) {
				resources->ReloadFont(compiled->guid.m_Value);
			} else {
				CRU_USER_WARN("[Fonts] {} was written but isn't indexed yet - refresh the Asset Browser", report.compiledPath);
			}
		}
	}

	if (report.fontPath == m_fontPath) {
		ReleasePreview();
		m_lastReport = std::move(report);
		RefreshStatus();
	}
}

// ------------------------------------------------------------------------------------------------ //
//  Preview
// ------------------------------------------------------------------------------------------------ //

void Crucible::FontImporter::AcquirePreview() {
	if (m_previewSet != nullptr) { return; }

	if (!m_previewTried) {
		m_previewTried = true;
		ResourceManager* resources = EditorManager::GetInstance().GetResources();
		AssetEntry const* compiled = m_assets.Find(CompiledFontPathFor(m_fontPath));
		if (resources == nullptr || compiled == nullptr) { return; }

		FontAsset const* font = resources->GetFont(compiled->guid.m_Value);
		if (font == nullptr || !font->texture.IsValid()) { return; }
		m_previewTexture       = font->texture;
		m_previewWidth         = font->data.atlas.width;
		m_previewHeight        = font->data.atlas.height;
		m_previewType          = font->data.outputType;
		m_previewDrawableFrame = ImGui::GetFrameCount() + kPreviewDelay;
	}

	// Only once the texture is on the GPU (its upload runs at the start of the next frame).
	if (m_previewTexture.IsValid() && ImGui::GetFrameCount() >= m_previewDrawableFrame) {
		try {
			m_previewSet = ImGui_ImplVulkan_AddTexture(m_renderer.GetTextureView(m_previewTexture), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		} catch (std::exception const& e) {
			CRU_ERROR("[Fonts] Atlas preview failed: {}", e.what());
			m_previewTexture = {};
		}
	}
}

void Crucible::FontImporter::ReleasePreview() {
	if (m_previewSet != nullptr) {
		// Frames still in flight may draw it: free it a few frames from now.
		m_retired.push_back({ m_previewSet, ImGui::GetFrameCount() + kRetireFrames });
		m_previewSet = nullptr;
	}
	m_previewTexture = {};
	m_previewTried   = false;
}

void Crucible::FontImporter::FreeRetiredPreviews(bool all) {
	int const frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0;
	auto it = m_retired.begin();
	while (it != m_retired.end()) {
		if (all || frame >= it->freeAtFrame) {
			ImGui_ImplVulkan_RemoveTexture(it->set);
			it = m_retired.erase(it);
		} else {
			++it;
		}
	}
}
