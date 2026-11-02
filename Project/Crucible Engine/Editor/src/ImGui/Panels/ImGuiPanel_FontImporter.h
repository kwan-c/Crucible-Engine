#pragma once
/*!
* @file			ImGuiPanel_FontImporter.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			21 October 2026
*
* @brief		The Font Importer window: a source font's import settings, whether its
*				compiled font is up to date, a Compile button, and a preview of the atlas.
*
* Opened by double-clicking a .ttf/.otf (or its .cfont) in the Asset Browser, its context
* menu's "Open in Font Importer", or View > Font Importer. A floating window with a close
* button; it remembers its place in imgui.ini like any other.
*
*	Compile   saves the settings (Apply) and compiles on a worker thread; the editor keeps
*	          running. When it finishes: the .cfont is written next to the font, the Asset
*	          Browser refreshes (so it gets a .meta / GUID), and ResourceManager::ReloadFont
*	          swaps it in for every UIText using it.
*	Apply     saves the settings without compiling (the status then says "Out of date").
*	Revert    throws away unsaved edits.
*
* Compiling is refused during Play (the asset index can't be refreshed then). A compile
* that is still running when the window is closed finishes and is saved anyway.
*/

#include "ImGui/ImGuiManager.h"
#include "Fonts/FontCompileJob.h"
#include "Graphics/TextureHandle.h"

#include <optional>
#include <string>
#include <vector>

namespace Crucible {

	class AssetManager;
	class Renderer;
	class Core;

	class FontImporter : public EditorPanel {
	public:
		FontImporter(AssetManager& assets, Renderer& renderer, Core& core)
			: m_assets(assets), m_renderer(renderer), m_core(core) {}
		~FontImporter() override;

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		/// Shows the window on a font: a .ttf/.otf, or a .cfont (opens its source font). "" = just show it.
		void Open(std::string const& fontPath);
		bool IsOpen() const { return m_open; }
		void SetOpen(bool open) { m_open = open; }

	private:
		// ---- Which font ----
		void SelectFont(std::string const& fontPath);
		void LoadSavedSettings();
		void RefreshStatus();
		bool IsDirty() const;

		// ---- Drawing ----
		void DrawFontPicker();
		void DrawStatus();
		void DrawSettings();
		void DrawButtons();
		void DrawReport();
		void DrawPreview();

		// ---- Actions ----
		bool Apply();
		void Revert();
		void StartCompile();
		void FinishCompile(FontCompileJob::Report report);

		// ---- Atlas preview (ImGui texture of the loaded font's atlas) ----
		void AcquirePreview();
		void ReleasePreview();
		void FreeRetiredPreviews(bool all);

		AssetManager& m_assets;
		Renderer&     m_renderer;
		Core&         m_core;

		bool        m_open = false;
		std::string m_fontPath;                          // the source font being edited ("" = none)

		FontImportSettings       m_saved;                // as on disk
		FontImportSettings       m_edit;                 // what the controls change
		bool                     m_settingsFileExists = false;
		std::vector<std::string> m_settingsWarnings;     // fixes made while loading the file
		std::string              m_settingsError;
		char                     m_rangesText[2048] = {};  // one range per line, mirrors m_edit.ranges
		std::string              m_rangesError;

		FontAssets::FontStatus m_status;
		double                 m_statusAge = 1.0e9;     // seconds since the last RefreshStatus

		FontCompileJob                        m_job;
		std::optional<FontCompileJob::Report> m_lastReport;   // for m_fontPath only
		std::string                           m_actionError;  // Apply / Start failures

		// Preview
		TextureHandle   m_previewTexture{};
		std::uint64_t   m_previewFontId = 0;              // pinned with ResourceManager::KeepLoaded while previewed
		VkDescriptorSet m_previewSet = nullptr;
		int             m_previewDrawableFrame = 0;     // ImGui frame from which the texture is on the GPU
		std::uint32_t   m_previewWidth = 0, m_previewHeight = 0;
		FontOutputType  m_previewType = FontOutputType::Bitmap;
		bool            m_previewTried = false;         // GetFont already asked (null is not retried every frame)
		struct Retired { VkDescriptorSet set; int freeAtFrame; };
		std::vector<Retired> m_retired;                 // freed a few frames later, once no frame in flight uses them
	};

}	// NAMESPACE CRUCIBLE
