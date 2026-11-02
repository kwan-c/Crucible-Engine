#pragma once
/*!
* @file			ImGuiPanel_TextureImporter.h
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			31 October 2026
*
* @brief		The Texture Importer window: an image's import settings (its .texsettings),
*				whether its compiled .bill / .sky is up to date, a Compile button, and previews
*				that update as the settings change.
*
* Opened by double-clicking an image in the Asset Browser, or View > Texture Importer. A
* floating window, like the Font Importer.
*
*	Result    the image exactly as the current settings compile it (format, max size,
*	          quality), with a slider through its mip levels.
*	Formats   the image in every BC format, decoded back - what the GPU would show - next to
*	          the original, with each one's GPU memory and an error score (PSNR: higher is
*	          closer). Hover to move the zoomed spot; "Use" picks that format.
*	          Both previews refresh by themselves shortly after a setting changes; they run on
*	          a copy at most 512 pixels wide (Tools/Common/TextureImport::CompareFormats).
*	Compile   saves the settings (Apply) and compiles on a worker thread, the same way the build
*	          does (Tools/Common/TextureImport): writes the .bill / .sky and swaps the new texture
*	          in for every material using it.
*	Apply     saves the settings without compiling; Revert throws away unsaved edits.
*
* All the previews share two ImGui textures (one image of the formats side by side, one of the
* mip levels side by side): ImGui has only a few texture slots for the whole editor.
*/

#include "ImGui/ImGuiManager.h"
#include "Graphics/TextureHandle.h"
#include "TextureImport.h"   // Tools/Common

#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace Crucible {

	class AssetManager;
	class Renderer;
	class Core;

	class TextureImporter : public EditorPanel {
	public:
		TextureImporter(AssetManager& assets, Renderer& renderer, Core& core)
			: m_assets(assets), m_renderer(renderer), m_core(core) {}
		~TextureImporter() override;

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		/// Shows the window on an image (a virtual path in Assets). "" = just show it.
		void Open(std::string const& imagePath);
		bool IsOpen() const { return m_open; }
		void SetOpen(bool open) { m_open = open; }

	private:
		/// One format in the comparison.
		struct Candidate {
			CompiledTextureFormat format = CompiledTextureFormat::RGBA8;   // RGBA8 = the original
			double                psnr = 0.0;      // dB against the original; 0 for the original
			std::uint64_t         gpuBytes = 0;    // GPU memory of the real compiled texture, mips included
			char const*           note = "";       // what the format keeps
		};
		/// One mip level's place in the mip strip.
		struct MipRect { std::uint32_t x = 0, width = 0, height = 0; };
		/// A texture ImGui draws from (one descriptor set).
		struct Preview {
			TextureHandle texture{};
			void*         set = nullptr;   // ImGui texture (VkDescriptorSet), from the frame the texture is on the GPU
			int           readyFrame = 0;  // ImGui frame from which the texture is on the GPU
			std::uint32_t width = 0, height = 0;
		};
		struct Comparison {
			std::string             imagePath;
			std::uint32_t           width = 0, height = 0;                 // preview size (each format)
			std::uint32_t           compiledWidth = 0, compiledHeight = 0; // after maxSize
			std::vector<Candidate>  candidates;                            // [0] = the original
			std::vector<unsigned char> formatPixels;                       // the formats side by side (until uploaded)
			CompiledTextureFormat   resultFormat = CompiledTextureFormat::BC7;
			std::vector<MipRect>    mips;                                  // the result's levels in the mip strip
			std::vector<unsigned char> mipPixels;                          // the levels side by side (until uploaded)
			std::uint32_t           mipStripWidth = 0, mipStripHeight = 0;
			std::string             error;
		};
		struct CompileResult {
			bool          ok = false;
			std::string   imagePath, compiledPath, summary, error;
			std::uint64_t guid = 0;
			bool          skybox = false;
			double        seconds = 0.0;
		};

		// ---- Which image ----
		void SelectImage(std::string const& imagePath);
		void LoadSavedSettings();
		void RefreshStatus();
		bool IsDirty() const { return !(m_edit == m_saved); }
		std::string CompiledPathFor(std::string const& imagePath) const;

		// ---- Drawing ----
		void DrawImagePicker();
		void DrawStatus();
		void DrawSettings();
		void DrawButtons();
		void DrawResult();
		void DrawFormats();

		// ---- Actions ----
		bool Apply();
		void StartCompile();
		void FinishCompile(CompileResult result);
		void UpdatePreviews();   // starts a comparison once the settings have stopped changing
		void StartComparison();
		void FinishComparison(Comparison comparison);
		void ShowPendingWhenReady();   // swaps the pending comparison in once its textures can be drawn
		void ReleaseComparison();
		bool Acquire(Preview& preview);   // its ImGui texture, once on the GPU
		void Retire(Preview& preview);
		void FreeRetired(bool all);

		AssetManager& m_assets;
		Renderer&     m_renderer;
		Core&         m_core;

		bool        m_open = false;
		std::string m_imagePath;     // the source image being edited ("" = none)
		bool        m_skybox = false;

		TextureImport::Settings m_saved;   // as on disk
		TextureImport::Settings m_edit;    // what the controls change
		std::string             m_settingsError;
		std::string             m_actionError;

		// Status of the compiled file
		std::string m_statusText;
		bool        m_upToDate = false;

		std::future<CompileResult> m_compileJob;
		std::string                m_lastCompile;   // summary line of the last compile of m_imagePath

		// Previews
		std::future<Comparison>  m_compareJob;
		Comparison               m_comparison;
		TextureImport::Settings  m_comparedSettings;     // what the shown (or running) comparison used
		bool                     m_hasCompared = false;
		TextureImport::Settings  m_lastSeenEdit;
		double                   m_editChangedAt = 0.0;  // ImGui time of the last settings change
		Preview                  m_formatsPreview;
		Preview                  m_mipsPreview;
		// A finished comparison waiting for its textures to reach the GPU. The one on screen stays
		// until then, so the window never shrinks for a few frames (which made ImGui scroll it back up).
		Comparison               m_pending;
		Preview                  m_pendingFormats;
		Preview                  m_pendingMips;
		bool                     m_hasPending = false;
		int                      m_mipLevel = 0;
		float                    m_zoom = 1.0f;
		float                    m_focusU = 0.5f, m_focusV = 0.5f;

		struct Retired { void* set; TextureHandle texture; int freeAtFrame; };
		std::vector<Retired> m_retired;   // freed a few frames later, once no frame in flight uses them
	};

}	// NAMESPACE CRUCIBLE
