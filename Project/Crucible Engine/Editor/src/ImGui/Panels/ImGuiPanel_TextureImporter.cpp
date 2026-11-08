/*!
* @file			ImGuiPanel_TextureImporter.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			31 October 2026
*
* @brief		The Texture Importer window. See ImGuiPanel_TextureImporter.h.
*/

#include "ImGui/Panels/ImGuiPanel_TextureImporter.h"
#include "Assets/BuildProblems.h"   // a failed image blocks Play until it compiles

#include "Resources/CompiledSky.h"
#include "Resources/CompiledTexture.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>

#include <imgui_impl_vulkan.h>

#include <Core.h>
#include "AssetPipe/Assetmanager.h"
#include "Diagnostics/Logger.h"
#include "Graphics/RenderSystem.h"
#include "Resources/ResourceManager.h"
#include "ImGui/EditorWidgets.h"
#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"

namespace {

	using namespace Crucible;
	namespace fs = std::filesystem;

	constexpr int           kPreviewDelay   = 2;      // frames: a new texture is uploaded at the start of the next frame
	constexpr int           kRetireFrames   = 4;      // frames before a replaced preview is freed (> frames in flight)
	constexpr std::uint32_t kPreviewMax     = 512;    // the previews run on the image shrunk to this (fast; enough to judge)
	constexpr double        kSettleSeconds  = 0.35;   // a setting must stay put this long before the previews refresh

	ImVec4 const kGood(0.45f, 0.85f, 0.45f, 1.0f);
	ImVec4 const kWarn(0.95f, 0.75f, 0.30f, 1.0f);
	ImVec4 const kBad(1.0f, 0.4f, 0.4f, 1.0f);

	std::vector<unsigned char> ReadFile(fs::path const& path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) { throw std::runtime_error("cannot open " + path.string()); }
		std::vector<unsigned char> bytes(static_cast<std::size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return bytes;
	}

	std::string FormatBytes(std::uint64_t bytes) {
		char buffer[32] = {};
		if (bytes < 1024) { std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes)); }
		else if (bytes < 1024 * 1024) { std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0); }
		else { std::snprintf(buffer, sizeof(buffer), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0)); }
		return buffer;
	}

	bool IsImage(AssetEntry const& entry) {
		if (entry.type != AssetType::Texture || entry.compiled) { return false; }
		std::string const& e = entry.extension;
		return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp" || e == ".hdr";
	}

	char const* FormatName(CompiledTextureFormat format) {
		return format == CompiledTextureFormat::RGBA8 ? "Original" : ToString(format);
	}

	char const* FormatNote(CompiledTextureFormat format) {
		switch (format) {
			case CompiledTextureFormat::BC1: return "RGB + on/off alpha, 4 bits per pixel. Smallest; smooth gradients get blocky.";
			case CompiledTextureFormat::BC3: return "RGBA, 8 bits per pixel. Smooth alpha; colour like BC1.";
			case CompiledTextureFormat::BC4: return "One channel (red), 4 bits per pixel. Masks, roughness, height - shown grey.";
			case CompiledTextureFormat::BC5: return "Two channels (red, green), 8 bits per pixel. Normal maps - blue is rebuilt.";
			case CompiledTextureFormat::BC7: return "RGBA, 8 bits per pixel. The best quality; the default for colour.";
			default:                         return "Uncompressed RGBA8, 32 bits per pixel - what the GPU would hold without BC.";
		}
	}

	char const* UsageNote(TextureImport::Usage usage) {
		switch (usage) {
			case TextureImport::Usage::Color:  return "Colour art you see (albedo, emissive, UI). Mipmaps are averaged in linear light so they don't darken.";
			case TextureImport::Usage::Linear: return "Numbers stored as colour (metallic-roughness, occlusion). Mipmaps average the values as they are.";
			case TextureImport::Usage::Mask:   return "Only the image's red channel matters (masks, roughness, height). BC4 keeps just that, at half the size.";
			case TextureImport::Usage::Normal: return "A normal map. BC5 keeps x and y (the shader rebuilds z); mipmaps average the directions so bumps survive.";
		}
		return "";
	}

	char const* Rating(double psnr) {
		if (psnr >= 45.0) { return "Excellent"; }
		if (psnr >= 38.0) { return "Very good"; }
		if (psnr >= 32.0) { return "Good"; }
		if (psnr >= 26.0) { return "Visible artifacts"; }
		return "Poor";
	}

	void HelpMarker(char const* text) {
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::BeginItemTooltip()) {
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
			ImGui::TextUnformatted(text);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	// A short explanation under a control, wrapped to the window.
	void Explain(char const* text) {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", text);
		ImGui::PopTextWrapPos();
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

	template<class T>
	bool IsReady(std::future<T> const& job) {
		return job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}

}	// anonymous namespace

// ------------------------------------------------------------------------------------------------ //

Crucible::TextureImporter::~TextureImporter() {
	if (m_compileJob.valid()) { m_compileJob.wait(); }
	if (m_compareJob.valid()) { m_compareJob.wait(); }
	ReleaseComparison();
	FreeRetired(true);
}

void Crucible::TextureImporter::Init() {
	m_panelName = "Texture Importer";
	m_docked = false;   // its own floating window; dock it anywhere and imgui.ini remembers
}

void Crucible::TextureImporter::Open(std::string const& imagePath) {
	m_open = true;
	m_makeFocus = true;
	if (!imagePath.empty()) { SelectImage(imagePath); }
}

void Crucible::TextureImporter::Update(float) {
	// Jobs finish (and a compile is saved) even while the window is closed.
	if (IsReady(m_compileJob)) { FinishCompile(m_compileJob.get()); }
	if (IsReady(m_compareJob)) { FinishComparison(m_compareJob.get()); }
	FreeRetired(false);

	if (!m_open) { return; }

	ImGui::SetNextWindowSize(ImVec2(760.0f, 820.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(m_panelName.c_str(), &m_open, m_flags)) {
		DrawImagePicker();
		ImGui::Separator();

		if (m_imagePath.empty()) {
			ImGui::Spacing();
			ImGui::TextWrapped("Pick an image above, drop one here from the Asset Browser, or double-click one in the Asset Browser.");
		} else {
			UpdatePreviews();
			ShowPendingWhenReady();
			DrawStatus();
			ImGui::Spacing();
			DrawSettings();
			ImGui::Spacing();
			DrawButtons();
			ImGui::Spacing();
			DrawResult();
			DrawFormats();
		}
	}
	ImGui::End();
}

// ------------------------------------------------------------------------------------------------ //
//  Which image
// ------------------------------------------------------------------------------------------------ //

void Crucible::TextureImporter::SelectImage(std::string const& imagePath) {
	AssetEntry const* entry = m_assets.Find(imagePath);
	if (entry == nullptr || !IsImage(*entry)) {
		CRU_USER_WARN("[Textures] '{}' is not an image in Assets", imagePath);
		return;
	}
	if (imagePath == m_imagePath) { RefreshStatus(); return; }
	if (!m_imagePath.empty() && IsDirty()) {
		CRU_USER_WARN("[Textures] Unapplied import settings for '{}' were discarded", m_imagePath);
	}

	ReleaseComparison();
	m_imagePath   = imagePath;
	m_skybox      = IsSkyboxSource(imagePath);
	m_hasCompared = false;   // the previews refresh for the new image
	m_lastCompile.clear();
	m_actionError.clear();
	m_mipLevel = 0;
	m_zoom = 1.0f;
	m_focusU = m_focusV = 0.5f;
	LoadSavedSettings();
	m_lastSeenEdit = m_edit;
	RefreshStatus();
}

void Crucible::TextureImporter::LoadSavedSettings() {
	m_settingsError.clear();
	m_saved = {};
	if (AssetEntry const* entry = m_assets.Find(m_imagePath)) {
		try {
			std::string text;
			m_saved = TextureImport::ReadSettings(entry->absolutePath, text);
		} catch (std::exception const& e) {
			m_settingsError = e.what();
		}
	}
	m_edit = m_saved;
}

std::string Crucible::TextureImporter::CompiledPathFor(std::string const& imagePath) const {
	char const* extension = IsSkyboxSource(imagePath) ? CompiledSkyExtension : CompiledTextureExtension;
	if (AssetEntry const* source = m_assets.Find(imagePath)) {
		// Keep the path the build already uses for this GUID (it survives a source rename).
		if (AssetEntry const* compiled = m_assets.FindByGuid(source->guid); compiled && compiled->compiled && compiled->extension == extension) {
			return compiled->virtualPath;
		}
	}
	fs::path path(imagePath);
	path.replace_extension(extension);
	return path.generic_string();
}

void Crucible::TextureImporter::RefreshStatus() {
	m_upToDate = false;
	m_statusText.clear();
	AssetEntry const* entry = m_assets.Find(m_imagePath);
	if (entry == nullptr) { m_statusText = "The image is gone from Assets."; return; }

	std::string const compiledPath = CompiledPathFor(m_imagePath);
	fs::path const target = fs::path(m_assets.GetResourceRoot()) / compiledPath;
	std::error_code ec;
	if (!fs::exists(target, ec)) { m_statusText = "Not compiled yet (" + compiledPath + ")."; return; }

	try {
		std::string settingsText;
		TextureImport::ReadSettings(entry->absolutePath, settingsText);
		const std::uint64_t hash = TextureImport::SourceHash(ReadFile(entry->absolutePath), settingsText);
		m_upToDate = TextureImport::IsUpToDate(target, entry->guid.m_Value, hash, m_skybox);

		const auto bytes = ReadFile(target);
		std::string detail, error;
		if (m_skybox) {
			CompiledSky sky;
			if (DecodeCompiledSky(bytes, entry->guid.m_Value, sky, error)) {
				detail = std::to_string(sky.sky.width) + " per face " + ToString(sky.sky.format) + ", " + std::to_string(sky.environment.mips.size()) + " reflection levels";
			}
		} else {
			CompiledTexture texture;
			if (DecodeCompiledTexture(bytes, entry->guid.m_Value, texture, error)) {
				detail = std::to_string(texture.width) + "x" + std::to_string(texture.height) + " " + ToString(texture.format) + ", " +
				         std::to_string(texture.mips.size()) + " mips, GPU " + FormatBytes(texture.data.size());
			}
		}
		if (!error.empty()) { detail = "unreadable: " + error; }
		m_statusText = (m_upToDate ? "Up to date: " : "Out of date: ") + compiledPath + " (" + detail + ", file " + FormatBytes(bytes.size()) + ")";
	} catch (std::exception const& e) {
		m_statusText = std::string("Could not check: ") + e.what();
	}
}

// ------------------------------------------------------------------------------------------------ //
//  Drawing
// ------------------------------------------------------------------------------------------------ //

void Crucible::TextureImporter::DrawImagePicker() {
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::BeginCombo("##image", m_imagePath.empty() ? "(pick an image)" : m_imagePath.c_str())) {
		for (AssetEntry const& entry : m_assets.GetAll()) {
			if (!IsImage(entry)) { continue; }
			if (ImGui::Selectable(entry.virtualPath.c_str(), entry.virtualPath == m_imagePath)) { SelectImage(entry.virtualPath); }
		}
		ImGui::EndCombo();
	}
	if (std::string const dropped = AcceptAssetDrop(); !dropped.empty()) { SelectImage(dropped); }
}

void Crucible::TextureImporter::DrawStatus() {
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextColored(m_upToDate ? kGood : kWarn, "%s", m_statusText.c_str());
	ImGui::PopTextWrapPos();
	if (m_compileJob.valid()) { ImGui::TextDisabled("Compiling..."); }
	else if (!m_lastCompile.empty()) { ImGui::TextDisabled("Last compile: %s", m_lastCompile.c_str()); }
}

void Crucible::TextureImporter::DrawSettings() {
	if (!m_settingsError.empty()) {
		ImGui::TextColored(kBad, "Its .texsettings is broken: %s (Apply writes a fresh one)", m_settingsError.c_str());
	}

	if (m_skybox) {
		Explain("Skybox (its name has \"_skybox\", or it is a .hdr): compiled to a .sky - six cube faces and the sky's lighting, "
		        "baked offline, all BC6H (HDR). There is no format to choose.");
		int faceSize = static_cast<int>(m_edit.faceSize);
		constexpr int kFaces[] = { 128, 256, 512, 1024, 2048 };
		if (ImGui::BeginCombo("Face size", std::to_string(faceSize).c_str())) {
			for (int size : kFaces) {
				if (ImGui::Selectable(std::to_string(size).c_str(), size == faceSize)) { m_edit.faceSize = static_cast<std::uint32_t>(size); }
			}
			ImGui::EndCombo();
		}
		char gpu[96];
		std::snprintf(gpu, sizeof(gpu), "Pixels per side of each face: sharper when bigger. GPU memory about %s.",
		              FormatBytes(std::uint64_t{ m_edit.faceSize } * m_edit.faceSize * 6 + 128 * 128 * 6 * 4 / 3).c_str());
		Explain(gpu);
		ImGui::Checkbox("Pack with Billet", &m_edit.billet);
		Explain("Lossless packing so the file takes less disk space; unpacked at load. GPU memory is the same either way.");
		return;
	}

	// Usage
	int usage = static_cast<int>(m_edit.usage);
	char const* const usages[] = { "Color", "Linear", "Mask", "Normal" };
	if (ImGui::Combo("Usage", &usage, usages, IM_ARRAYSIZE(usages))) { m_edit.usage = static_cast<TextureImport::Usage>(usage); }
	Explain(UsageNote(m_edit.usage));

	// Format
	TextureImport::Settings autoSettings = m_edit;
	autoSettings.format = TextureImport::Format::Auto;
	std::string const autoLabel = std::string("Auto (") + ToString(TextureImport::ResolveFormat(autoSettings)) + ")";
	int format = static_cast<int>(m_edit.format);
	char const* const formats[] = { autoLabel.c_str(), "BC1", "BC3", "BC4", "BC5", "BC7" };
	if (ImGui::Combo("Format", &format, formats, IM_ARRAYSIZE(formats))) { m_edit.format = static_cast<TextureImport::Format>(format); }
	const CompiledTextureFormat chosen = TextureImport::ResolveFormat(m_edit);
	Explain(FormatNote(chosen));
	if (m_edit.usage == TextureImport::Usage::Normal && chosen != CompiledTextureFormat::BC5 && chosen != CompiledTextureFormat::BC7) {
		ImGui::TextColored(kWarn, "A normal map needs at least red and green: use BC5 (or BC7).");
	}
	if ((chosen == CompiledTextureFormat::BC4 || chosen == CompiledTextureFormat::BC5) && m_edit.usage == TextureImport::Usage::Color) {
		ImGui::TextColored(kWarn, "%s drops colour channels - fine for data, not for colour art.", ToString(chosen));
	}

	// Max size
	constexpr std::uint32_t kSizes[] = { 128, 256, 512, 1024, 2048, 4096, 8192, 16384 };
	auto sizeLabel = [](std::uint32_t size) { return size >= 16384 ? std::string("No limit") : std::to_string(size); };
	if (ImGui::BeginCombo("Max size", sizeLabel(m_edit.maxSize).c_str())) {
		for (std::uint32_t size : kSizes) {
			if (ImGui::Selectable(sizeLabel(size).c_str(), size == m_edit.maxSize)) { m_edit.maxSize = size; }
		}
		ImGui::EndCombo();
	}
	if (m_hasCompared && m_comparison.compiledWidth > 0 && m_comparedSettings.maxSize == m_edit.maxSize) {
		char size[160];
		std::snprintf(size, sizeof(size), "Compiles at %ux%u. A bigger image is halved until both sides fit; each halving uses a quarter of the memory.",
		              m_comparison.compiledWidth, m_comparison.compiledHeight);
		Explain(size);
	} else {
		Explain("A bigger image is halved until both sides fit; each halving uses a quarter of the memory.");
	}

	// Mipmaps, quality, packing
	ImGui::Checkbox("Mipmaps", &m_edit.mipmaps);
	Explain(m_edit.mipmaps
		? "Smaller copies for when the texture is far away or small on screen (a third more memory). Slide through them under Result."
		: "Off: only the full-size image. Far away it shimmers and is slower to sample - only for UI drawn at its own size.");
	ImGui::SliderFloat("Quality", &m_edit.quality, 0.0f, 1.0f, "%.2f");
	Explain("How hard the compressor searches for the best blocks. Higher is a little closer to the original (see the dB numbers) and much slower to compile.");
	ImGui::Checkbox("Pack with Billet", &m_edit.billet);
	Explain("Lossless packing so the file takes less disk space; unpacked at load. GPU memory is the same either way.");
}

void Crucible::TextureImporter::DrawButtons() {
	bool const playing = m_core.IsInPlaySession() || m_core.HasPendingPlayRequest();

	ImGui::BeginDisabled(!IsDirty());
	if (ImGui::Button("Apply")) { Apply(); }
	ImGui::SameLine();
	if (ImGui::Button("Revert")) { m_edit = m_saved; m_actionError.clear(); }
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(m_compileJob.valid() || playing);
	if (ImGui::Button("Compile")) { StartCompile(); }
	ImGui::EndDisabled();
	if (playing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("Stop play mode to compile textures"); }
	if (IsDirty()) { ImGui::SameLine(); ImGui::TextColored(kWarn, "Not applied yet - the previews already show it."); }

	if (!m_actionError.empty()) { ImGui::TextColored(kBad, "%s", m_actionError.c_str()); }
}

void Crucible::TextureImporter::DrawResult() {
	if (m_skybox) { return; }
	ImGui::SeparatorText("Result with these settings");
	if (!m_comparison.error.empty()) { ImGui::TextColored(kBad, "Preview failed: %s", m_comparison.error.c_str()); return; }
	if (m_comparison.mips.empty() || !Acquire(m_mipsPreview)) {
		ImGui::TextDisabled("%s", m_compareJob.valid() ? "Updating the previews..." : "Preparing...");
		return;
	}

	int const levels = static_cast<int>(m_comparison.mips.size());
	m_mipLevel = std::clamp(m_mipLevel, 0, levels - 1);
	MipRect const& level = m_comparison.mips[static_cast<std::size_t>(m_mipLevel)];

	ImGui::Text("%s, %ux%u, %d mip level%s", ToString(m_comparison.resultFormat), m_comparison.compiledWidth, m_comparison.compiledHeight,
	            levels, levels == 1 ? "" : "s");
	if (m_compareJob.valid()) { ImGui::SameLine(); ImGui::TextDisabled("(updating...)"); }
	if (levels > 1) {
		ImGui::SetNextItemWidth(260.0f);
		ImGui::SliderInt("Mip level", &m_mipLevel, 0, levels - 1);
		const std::uint32_t shownW = std::max(1u, m_comparison.compiledWidth >> m_mipLevel), shownH = std::max(1u, m_comparison.compiledHeight >> m_mipLevel);
		char note[220] = "Level 0: the full image, used up close.";
		if (m_mipLevel > 0) {
			std::snprintf(note, sizeof(note), "Level %d: %ux%u - what the GPU uses when the texture covers about that many pixels on screen. "
			              "Stretched here so you can see how much detail is left.", m_mipLevel, shownW, shownH);
		}
		Explain(note);
	} else {
		Explain("Mipmaps are off: this one image is used at every distance.");
	}

	// The level, stretched to the same size as level 0.
	float const side = std::min(ImGui::GetContentRegionAvail().x, 360.0f);
	float const aspect = static_cast<float>(m_comparison.height) / static_cast<float>(std::max(1u, m_comparison.width));
	ImVec2 const size(side, side * aspect);
	ImVec2 const p0 = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(24, 24, 28, 255));
	float const sw = static_cast<float>(m_mipsPreview.width), sh = static_cast<float>(m_mipsPreview.height);
	ImGui::Image(reinterpret_cast<ImTextureID>(m_mipsPreview.set), size,
	             ImVec2(static_cast<float>(level.x) / sw, 0.0f),
	             ImVec2(static_cast<float>(level.x + level.width) / sw, static_cast<float>(level.height) / sh));
}

void Crucible::TextureImporter::DrawFormats() {
	if (m_skybox || m_comparison.candidates.empty() || !Acquire(m_formatsPreview)) { return; }

	ImGui::SeparatorText("Every format on this image");
	Explain("Each one compressed and decoded back - exactly what the GPU would show. Hover an image to move the zoomed spot; "
	        "\"Use\" picks that format.");
	ImGui::SetNextItemWidth(200.0f);
	ImGui::SliderFloat("Zoom", &m_zoom, 1.0f, 16.0f, "%.0fx", ImGuiSliderFlags_Logarithmic);

	// The same spot of every format: a window of 1/zoom around the shared focus.
	float const half = 0.5f / m_zoom;
	float const u0 = std::clamp(m_focusU - half, 0.0f, 1.0f - 2.0f * half), v0 = std::clamp(m_focusV - half, 0.0f, 1.0f - 2.0f * half);
	float const count = static_cast<float>(m_comparison.candidates.size());

	CompiledTextureFormat const chosen = TextureImport::ResolveFormat(m_edit);
	int const columns = std::max(1, std::min(3, static_cast<int>(ImGui::GetContentRegionAvail().x / 220.0f)));
	if (ImGui::BeginTable("formats", columns, ImGuiTableFlags_SizingStretchSame)) {
		for (std::size_t i = 0; i < m_comparison.candidates.size(); ++i) {
			Candidate const& candidate = m_comparison.candidates[i];
			ImGui::TableNextColumn();
			ImGui::PushID(static_cast<int>(i));
			bool const original = candidate.format == CompiledTextureFormat::RGBA8;
			if (!original && candidate.format == chosen) { ImGui::TextColored(kGood, "%s  (chosen)", FormatName(candidate.format)); }
			else { ImGui::TextUnformatted(FormatName(candidate.format)); }

			// This format's cell of the shared texture (the formats sit side by side in it).
			float const side = ImGui::GetContentRegionAvail().x;
			float const aspect = static_cast<float>(m_comparison.height) / static_cast<float>(std::max(1u, m_comparison.width));
			ImVec2 const size(side, side * aspect);
			ImVec2 const uvMin((static_cast<float>(i) + u0) / count, v0), uvMax((static_cast<float>(i) + u0 + 2.0f * half) / count, v0 + 2.0f * half);
			ImVec2 const p0 = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(24, 24, 28, 255));
			ImGui::Image(reinterpret_cast<ImTextureID>(m_formatsPreview.set), size, uvMin, uvMax);
			if (ImGui::IsItemHovered() && size.x > 0.0f && size.y > 0.0f) {
				ImVec2 const mouse = ImGui::GetIO().MousePos;
				m_focusU = u0 + (mouse.x - p0.x) / size.x * 2.0f * half;
				m_focusV = v0 + (mouse.y - p0.y) / size.y * 2.0f * half;
			}

			ImGui::Text("GPU %s", FormatBytes(candidate.gpuBytes).c_str());
			if (original) { ImGui::TextDisabled("the reference"); }
			else { ImGui::Text("%.1f dB - %s", candidate.psnr, Rating(candidate.psnr)); }
			Explain(candidate.note);
			if (!original) {
				if (ImGui::SmallButton("Use")) {
					switch (candidate.format) {
						case CompiledTextureFormat::BC1: m_edit.format = TextureImport::Format::BC1; break;
						case CompiledTextureFormat::BC3: m_edit.format = TextureImport::Format::BC3; break;
						case CompiledTextureFormat::BC4: m_edit.format = TextureImport::Format::BC4; break;
						case CompiledTextureFormat::BC5: m_edit.format = TextureImport::Format::BC5; break;
						default:                         m_edit.format = TextureImport::Format::BC7; break;
					}
				}
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	Explain("dB (PSNR): how close to the original - 45+ excellent, 38+ very good, 32+ good, below that artifacts show. "
	        "Scored on the channels each format keeps (BC4: red; BC5: red and green).");
}

// ------------------------------------------------------------------------------------------------ //
//  Actions
// ------------------------------------------------------------------------------------------------ //

bool Crucible::TextureImporter::Apply() {
	m_actionError.clear();
	AssetEntry const* entry = m_assets.Find(m_imagePath);
	if (entry == nullptr) { m_actionError = "The image is gone from Assets."; return false; }

	std::ofstream file(TextureImport::SettingsPathFor(entry->absolutePath), std::ios::binary | std::ios::trunc);
	std::string const text = TextureImport::WriteSettingsText(m_edit);
	if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
		m_actionError = "Apply failed: could not write the .texsettings file";
		CRU_USER_ERROR("[Textures] {}", m_actionError);
		return false;
	}
	file.close();
	LoadSavedSettings();   // read back what was written, so the window shows exactly the file
	m_lastSeenEdit = m_edit;
	RefreshStatus();
	return true;
}

void Crucible::TextureImporter::StartCompile() {
	m_actionError.clear();
	if (IsDirty() && !Apply()) { return; }   // Compile always builds what is saved, so the status stays honest

	AssetEntry const* entry = m_assets.Find(m_imagePath);
	if (entry == nullptr) { m_actionError = "The image is gone from Assets."; return; }
	if (m_assets.GetResourceRoot().empty()) { m_actionError = "There is no Resources folder to compile into."; return; }

	std::string const compiledPath = CompiledPathFor(m_imagePath);
	fs::path const target = fs::path(m_assets.GetResourceRoot()) / compiledPath;
	fs::path const source = entry->absolutePath;
	std::uint64_t const guid = entry->guid.m_Value;
	bool const skybox = m_skybox;
	std::string const imagePath = m_imagePath;

	CRU_USER_INFO("[Textures] Compiling {}...", m_imagePath);
	m_compileJob = std::async(std::launch::async, [=]() {
		CompileResult result;
		result.imagePath = imagePath;
		result.compiledPath = compiledPath;
		result.guid = guid;
		result.skybox = skybox;
		auto const start = std::chrono::steady_clock::now();
		try {
			std::string settingsText;
			TextureImport::Settings const settings = TextureImport::ReadSettings(source, settingsText);
			std::vector<unsigned char> const bytes = ReadFile(source);
			TextureImport::Compiled const compiled = TextureImport::CompileImage(bytes, settings, guid, TextureImport::SourceHash(bytes, settingsText), skybox);
			TextureImport::WriteCompiledFile(target, compiled.bytes, guid, compiledPath);
			result.summary = compiled.summary;
			result.ok = true;
		} catch (std::exception const& e) {
			result.error = e.what();
		}
		result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		return result;
	});
}

void Crucible::TextureImporter::FinishCompile(CompileResult result) {
	if (!result.ok) {
		CRU_USER_ERROR("[Textures] {} failed to compile: {}", result.imagePath, result.error);
		if (result.imagePath == m_imagePath) { m_actionError = "Compile failed: " + result.error; }
		BuildProblems::SetAssetFailed(result.imagePath, result.error);   // blocks Play until it compiles
		return;
	}
	BuildProblems::ClearAsset(result.imagePath);
	CRU_USER_INFO("[Textures] Compiled {} -> {}: {} in {:.2f} s", result.imagePath, result.compiledPath, result.summary, result.seconds);

	// Only one compiled file may claim the GUID: drop the other kind (a .bill left from before the
	// image was renamed "_skybox", or the reverse).
	for (AssetEntry const& entry : m_assets.GetAll()) {
		if (!entry.compiled || entry.guid.m_Value != result.guid || entry.virtualPath == result.compiledPath) { continue; }
		if (entry.extension != CompiledTextureExtension && entry.extension != CompiledSkyExtension) { continue; }
		std::error_code ec;
		fs::remove(entry.absolutePath, ec);
		fs::remove(AssetRegistry::MetaPathFor(entry.absolutePath), ec);
	}

	// Index the new file, through the Asset Browser so its view updates too. The refresh also
	// hot-reloads it (ResourceManager::ReloadChanged): materials switch to the new texture, a
	// skybox is loaded again on the next frame.
	if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) {
		browser->Refresh();
	} else {
		m_assets.Refresh();
		if (ResourceManager* resources = EditorManager::GetInstance().GetResources()) {
			resources->RefreshCatalog();
			resources->ReloadChanged();
		}
	}

	if (result.imagePath == m_imagePath) {
		m_lastCompile = result.summary;
		RefreshStatus();
	}
}

void Crucible::TextureImporter::UpdatePreviews() {
	if (m_skybox) { return; }
	double const now = ImGui::GetTime();
	if (!(m_edit == m_lastSeenEdit)) {   // a setting just changed: wait until it stays put
		m_lastSeenEdit = m_edit;
		m_editChangedAt = now;
	}
	bool const stale = !m_hasCompared || !(m_comparedSettings == m_edit);
	if (stale && !m_compareJob.valid() && now - m_editChangedAt >= kSettleSeconds) { StartComparison(); }
}

void Crucible::TextureImporter::StartComparison() {
	AssetEntry const* entry = m_assets.Find(m_imagePath);
	if (entry == nullptr || m_skybox) { return; }

	fs::path const source = entry->absolutePath;
	std::string const imagePath = m_imagePath;
	TextureImport::Settings const settings = m_edit;   // what is in the window, applied or not
	m_comparedSettings = settings;
	m_hasCompared = true;
	m_compareJob = std::async(std::launch::async, [=]() {
		Comparison comparison;
		comparison.imagePath = imagePath;
		try {
			// The same comparison as CrucibleTextureCompiler --compare (Tools/Common/TextureImport).
			TextureImport::Comparison compared = TextureImport::CompareFormats(ReadFile(source), settings, kPreviewMax);
			comparison.width          = compared.width;
			comparison.height         = compared.height;
			comparison.compiledWidth  = compared.compiledWidth;
			comparison.compiledHeight = compared.compiledHeight;
			comparison.resultFormat   = compared.resultFormat;

			// Every format side by side in one image: one texture, so one ImGui slot.
			std::uint32_t const w = compared.width, h = compared.height, count = static_cast<std::uint32_t>(compared.formats.size());
			comparison.formatPixels.assign(std::size_t{ w } * count * h * 4, 0);
			for (std::uint32_t i = 0; i < count; ++i) {
				TextureImport::ComparedFormat const& format = compared.formats[i];
				for (std::uint32_t y = 0; y < h; ++y) {
					std::copy_n(&format.rgba[std::size_t{ y } * w * 4], std::size_t{ w } * 4,
					            &comparison.formatPixels[(std::size_t{ y } * w * count + std::size_t{ i } * w) * 4]);
				}
				comparison.candidates.push_back({ format.format, format.psnr, format.gpuBytes, FormatNote(format.format) });
			}

			// The result's mip levels side by side in one image too.
			for (TextureImport::PreviewLevel const& level : compared.resultLevels) {
				comparison.mips.push_back({ comparison.mipStripWidth, level.width, level.height });
				comparison.mipStripWidth += level.width;
				comparison.mipStripHeight = std::max(comparison.mipStripHeight, level.height);
			}
			comparison.mipPixels.assign(std::size_t{ comparison.mipStripWidth } * comparison.mipStripHeight * 4, 0);
			for (std::size_t l = 0; l < compared.resultLevels.size(); ++l) {
				TextureImport::PreviewLevel const& level = compared.resultLevels[l];
				for (std::uint32_t y = 0; y < level.height; ++y) {
					std::copy_n(&level.rgba[std::size_t{ y } * level.width * 4], std::size_t{ level.width } * 4,
					            &comparison.mipPixels[(std::size_t{ y } * comparison.mipStripWidth + comparison.mips[l].x) * 4]);
				}
			}
		} catch (std::exception const& e) {
			comparison = Comparison{};
			comparison.imagePath = imagePath;
			comparison.error = e.what();
		}
		return comparison;
	});
}

void Crucible::TextureImporter::FinishComparison(Comparison comparison) {
	if (comparison.imagePath != m_imagePath) { return; }   // the image was switched meanwhile

	// Uploaded as "pending": the comparison on screen stays until these textures can be drawn
	// (ShowPendingWhenReady), so the window keeps its height and its scroll position.
	Retire(m_pendingFormats);
	Retire(m_pendingMips);
	m_pending = std::move(comparison);
	m_hasPending = true;
	const auto upload = [&](Preview& preview, std::vector<unsigned char>& pixels, std::uint32_t width, std::uint32_t height) {
		if (pixels.empty() || width == 0 || height == 0) { return; }
		try {
			preview.texture    = m_renderer.CreateTextureFromPixels(pixels.data(), width, height, TexturePixelFormat::RGBA8, false);
			preview.width      = width;
			preview.height     = height;
			preview.readyFrame = ImGui::GetFrameCount() + kPreviewDelay;
		} catch (std::exception const& e) {
			CRU_ERROR("[Textures] Preview failed: {}", e.what());
		}
		pixels.clear();
		pixels.shrink_to_fit();
	};
	upload(m_pendingFormats, m_pending.formatPixels, m_pending.width * static_cast<std::uint32_t>(m_pending.candidates.size()), m_pending.height);
	upload(m_pendingMips, m_pending.mipPixels, m_pending.mipStripWidth, m_pending.mipStripHeight);
}

void Crucible::TextureImporter::ShowPendingWhenReady() {
	if (!m_hasPending) { return; }
	// A failed comparison has no textures: show its error at once.
	bool const ready = !m_pending.error.empty() || (Acquire(m_pendingFormats) && Acquire(m_pendingMips));
	if (!ready) { return; }
	Retire(m_formatsPreview);
	Retire(m_mipsPreview);
	m_comparison     = std::move(m_pending);
	m_formatsPreview = m_pendingFormats;
	m_mipsPreview    = m_pendingMips;
	m_pending        = {};
	m_pendingFormats = {};
	m_pendingMips    = {};
	m_hasPending     = false;
}

bool Crucible::TextureImporter::Acquire(Preview& preview) {
	if (preview.set != nullptr) { return true; }
	if (!preview.texture.IsValid() || ImGui::GetFrameCount() < preview.readyFrame) { return false; }
	try {
		preview.set = ImGui_ImplVulkan_AddTexture(m_renderer.GetTextureView(preview.texture), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	} catch (std::exception const& e) {
		CRU_ERROR("[Textures] Preview failed: {}", e.what());
		preview.texture = {};
	}
	return preview.set != nullptr;
}

void Crucible::TextureImporter::Retire(Preview& preview) {
	if (preview.set != nullptr || preview.texture.IsValid()) {
		// Frames still in flight may draw it: free it a few frames from now.
		int const frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0;
		m_retired.push_back({ preview.set, preview.texture, frame + kRetireFrames });
	}
	preview = {};
}

void Crucible::TextureImporter::ReleaseComparison() {
	Retire(m_formatsPreview);
	Retire(m_mipsPreview);
	Retire(m_pendingFormats);
	Retire(m_pendingMips);
	m_comparison = {};
	m_pending    = {};
	m_hasPending = false;
}

void Crucible::TextureImporter::FreeRetired(bool all) {
	int const frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0;
	auto it = m_retired.begin();
	while (it != m_retired.end()) {
		if (all || frame >= it->freeAtFrame) {
			if (it->set != nullptr) { ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(it->set)); }
			if (it->texture.IsValid()) { m_renderer.DestroyTexture(it->texture); }
			it = m_retired.erase(it);
		} else {
			++it;
		}
	}
}
