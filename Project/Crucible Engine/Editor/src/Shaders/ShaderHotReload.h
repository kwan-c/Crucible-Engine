/*!
* @file			ShaderHotReload.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Editor shader hot reload: save a .slang file and the running Editor
*				recompiles the shaders that include it and swaps them in.
*
* Every frame (Tick):
*   1. Collects finished compiles (ShaderCompiler). A good one is handed to the renderer through
*      the shader's `apply` function; a failed one reports the compiler's errors to the Console,
*      with file and line, and the renderer keeps the shader it had.
*   2. Checks Assets/Shaders twice a second. When a .slang file was added, removed or saved and
*      then left alone for a moment (so a half-saved file is not compiled), every watched shader
*      whose own source or #includes changed is compiled again. A shader that does not include
*      the file is left alone.
*
* A mesh shader's parameter list (<Name>.params.json beside the .slang, see the engine's
* Graphics/MaterialShaderParams.h) is watched the same way: when it is saved, its text goes to the
* shader's `applyParams` function. No compile is needed for that; the shader's source usually
* changes with it and is recompiled as above.
*
* A shader can also be GENERATED instead of written - a shader graph. It has no .slang in
* Assets/Shaders: its `generate` function makes the source and the parameter list from the file
* it comes from (`generatedFrom`, watched like a .slang file). What comes out is compiled and
* swapped in the same way, and is also kept in Resources/Shaders (<name>.spv, <name>.params.json),
* where the build keeps the shaders it compiles - so the next start, and the Game run from the
* build folder, load it like any other shader.
*
* A watched shader can also be given a LIVE source (SetLive): text to compile instead of what its
* file says, until ClearLive. That is how the Shader Graph window shows a graph that has not been
* saved yet, on the scene's materials and on its own preview. A live result is never written to
* Resources/Shaders - only what is saved is kept.
*
* At start, each watched shader is compiled once (normally straight from the compiler's cache)
* and compared with the .spv the build produced, which is what the renderer loaded. Identical:
* nothing happens. Different - a shader was edited while the Editor was closed and the project
* not rebuilt since - the fresh one is swapped in.
*
* Without slangc (no Vulkan SDK on this machine) hot reload is off and says so once; the Editor
* runs on the built .spv files as before.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "Shaders/ShaderCompiler.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Crucible {

	class ShaderHotReload {
	public:
		/// Gives the renderer newly compiled SPIR-V. Returns false, and why, if it could not use it.
		using Apply = std::function<bool(std::span<const uint32_t> spirv, std::string& error)>;
		/// Gives the renderer a shader's parameter list: the text of its .params.json ("{}" when the
		/// file has gone). Returns false, and why, if it is not a usable list.
		using ApplyParams = std::function<bool(std::string_view json, std::string& error)>;
		/// For a generated shader (a shader graph): makes its Slang source and its parameter list (the
		/// text of a .params.json). Returns false, and why in a few words, if it cannot - the details
		/// it reports itself.
		using Generate = std::function<bool(std::string& source, std::string& paramsJson, std::string& error)>;

		struct Shader {
			std::string              name;          // "DefaultShader": Assets/Shaders/DefaultShader.slang, built to DefaultShader.spv
			std::vector<std::string> entryPoints;   // as in Engine/CompileShaders.cmake
			Apply                    apply;         // called on the thread that calls Tick()
			ApplyParams              applyParams;   // optional: for a shader that can have a <name>.params.json

			// A generated shader has these as well, and no <name>.slang:
			std::string                            label;           // what messages call it: "Ember.shadergraph". Empty = name
			std::function<std::filesystem::path()> generatedFrom;   // the file it is made from, wherever that is now
			Generate                               generate;
		};

		enum class Status {
			Disabled,    // not configured, or no compiler on this machine
			UpToDate,    // the renderer's shaders match the sources
			Compiling,
			Errors       // a shader's last compile failed; the renderer still has the one before
		};

		ShaderHotReload() = default;
		~ShaderHotReload();
		ShaderHotReload(const ShaderHotReload&) = delete;
		ShaderHotReload& operator=(const ShaderHotReload&) = delete;

		/// The Editor's hot reload - the one Configure() was last called on - or null. For a panel
		/// that wants to show what a shader it edits is doing (the Shader Graph window).
		static ShaderHotReload* Active();

		/*********************************************************************
		 \brief  Sets the folders and the compiler to use (once).
		 \param  settings  See ShaderCompiler::Settings.
		 \param  builtDir  Where the build puts the .spv files the renderer
		                   loads at start (Resources/Shaders).
		 *********************************************************************/
		void Configure(ShaderCompiler::Settings settings, std::filesystem::path builtDir);
		bool IsConfigured() const { return m_compiler != nullptr; }

		/// Adds a shader to keep up to date. After Configure().
		void Watch(Shader shader);

		/// Call once per frame, on the frame thread, outside command recording.
		void Tick();

		bool autoReload = true;   // compile automatically when a .slang file changes

		Status GetStatus() const { return m_status; }
		int ErrorCount() const;                                   // errors in the shaders that failed
		const std::string& FirstError() const { return m_firstError; }

		// One watched shader, by Shader::name. All false / empty for a shader that is not watched.
		bool IsCompiling(std::string_view name) const;
		bool HasErrors(std::string_view name) const;    // its last generate or compile failed
		/// The compiler's errors from its last compile. A generated shader's own lines carry
		/// Diagnostic::file == its name.
		const std::vector<ShaderCompiler::Diagnostic>& ErrorsOf(std::string_view name) const;

		/*********************************************************************
		 \brief  Compiles `source` for a watched shader in place of its file's
		         source, and hands over `paramsJson` as its parameter list
		         when that code is swapped in. Stays until ClearLive().
		 \return false if no shader of that name is watched (yet).
		 *********************************************************************/
		bool SetLive(std::string_view name, std::string source, std::string paramsJson);
		/// Back to what the shader's file says (it is read and compiled again).
		void ClearLive(std::string_view name);
		bool IsLive(std::string_view name) const;

	private:
		using Clock = std::chrono::steady_clock;

		struct Watched {
			Shader      shader;
			uint64_t    sourceHash = 0;    // ShaderCompiler::SourceHash of what was last sent to compile
			uint64_t    ticket = 0;        // the compile that is running (0 = none)
			uint64_t    runningHash = 0;   // hash of the SPIR-V the renderer has now
			int         errors = 0;        // from the last compile (0 = it was fine)
			uint64_t    paramsHash = 0;    // hash of the .params.json text last handed over (0 = none)
			std::vector<ShaderCompiler::Diagnostic> diagnostics;   // the errors of the last compile

			// A live source (SetLive):
			bool        live = false;
			std::string liveSource;
			std::string liveParams;            // its parameter list, handed over with its code

			// A generated shader:
			std::string source;                // what `generate` last made (kept: recompiled when an include changes)
			uint64_t    generatedStamp = 0;    // FileStamp of generatedFrom when it was made
			bool        generated = false;     // `generate` has been tried at least once
			int         generateErrors = 0;    // 1 = the last try failed
		};

		void OnCompiled(Watched& watched, const ShaderCompiler::Result& result);
		void CheckParams(Watched& watched);
		bool HandOverParams(Watched& watched, const std::string& text, const std::string& what);
		bool Regenerate(Watched& watched);
		uint64_t FolderSignature() const;
		const Watched* Find(std::string_view name) const;

		std::unique_ptr<ShaderCompiler> m_compiler;
		std::filesystem::path           m_builtDir;
		std::vector<Watched>            m_watched;

		uint64_t          m_seen = 0;       // FolderSignature() found by the last scan
		Clock::time_point m_seenAt{}, m_nextScan{};
		bool              m_dirty = false;  // the folder changed: watched shaders need checking
		Status            m_status = Status::Disabled;
		std::string       m_firstError;
	};

}
