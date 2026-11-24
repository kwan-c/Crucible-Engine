/*!
* @file			ShaderCompiler.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Compiles Slang shaders to SPIR-V from inside the Editor.
*
* Runs slangc (the Vulkan SDK's compiler, the one the build uses) as a hidden child process with
* exactly the build's flags (Engine/CompileShaders.cmake), so what the Editor compiles is what a
* rebuild would produce. Two kinds of request:
*   - a shader file in Assets/Shaders, by name ("DefaultShader" -> DefaultShader.slang);
*   - source text made in memory (a shader graph's generated code), which may #include the
*     files in Assets/Shaders.
*
* Submit() queues a request for a worker thread, so the frame never waits for the compiler;
* Poll() each frame collects finished results. Compile() does the same work on the calling
* thread, for tools and tests.
*
* Results are kept on disk, named by a hash of everything that decides the output: the source,
* every file it #includes (found by reading the #include lines), the entry points, the flags and
* the compiler itself. The same shader is therefore compiled once, not at every start, and a
* shader is only recompiled when a file it really includes changes.
*
* A failed compile reports the compiler's messages (file, line, column, text). It produces no
* SPIR-V, so the caller keeps whatever shader it was already using.
*
* The Game never uses this: it only loads .spv files. Editor only (see ShaderHotReload).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Crucible {

	class ShaderCompiler {
	public:
		struct Settings {
			std::filesystem::path shaderDir;   // the .slang sources and what they #include (Assets/Shaders)
			std::filesystem::path cacheDir;    // compiled results are kept here (created when first needed)
			std::filesystem::path compiler;    // slangc; see FindCompiler(). Empty = every compile fails, saying so
		};

		struct Request {
			std::string name;                       // names the result, its cache file and messages: "DefaultShader".
			                                        // Letters, digits, '_', '-' and '.' only.
			std::string source;                     // Slang source text. Empty = compile <shaderDir>/<name>.slang
			std::vector<std::string> entryPoints;   // e.g. "vertMain", "fragMain"
		};

		enum class Severity { Note, Warning, Error };

		/// One compiler message.
		struct Diagnostic {
			Severity    severity = Severity::Error;
			std::string file;      // as the compiler printed it (relative to shaderDir for its files);
			                       // the request's name for source text
			int         line = 0, column = 0;
			std::string code;      // e.g. "E30027"
			std::string message;
		};

		struct Result {
			uint64_t                ticket = 0;      // what Submit() returned (0 from Compile())
			std::string             name;            // Request::name
			bool                    success = false;
			bool                    fromCache = false;   // nothing was compiled: the result was already on disk
			std::vector<uint32_t>   spirv;           // the compiled module (empty unless success)
			std::vector<Diagnostic> diagnostics;     // errors, warnings and notes, in the compiler's order
			std::string             failure;         // why it could not run at all (not a shader error)
			std::string             output;          // everything the compiler printed, for the log
			uint64_t                hash = 0;        // SourceHash() of what was compiled
			double                  seconds = 0.0;

			int ErrorCount() const;
		};

		explicit ShaderCompiler(Settings settings);
		~ShaderCompiler();   // waits for a compile that is running; queued ones are dropped
		ShaderCompiler(const ShaderCompiler&) = delete;
		ShaderCompiler& operator=(const ShaderCompiler&) = delete;

		const Settings& GetSettings() const { return m_settings; }

		/*********************************************************************
		 \brief  Queues a compile for the worker thread and returns at once.
		         A request with the same name that is still waiting (not yet
		         started) is replaced: only the newest text is worth compiling,
		         and the replaced ticket never reports.
		 \return The ticket Result::ticket will carry.
		 *********************************************************************/
		uint64_t Submit(Request request);

		/*********************************************************************
		 \brief  Call every frame. Returns true and fills `out` once for each
		         finished request, oldest first; false when there is none.
		 *********************************************************************/
		bool Poll(Result& out);

		/// True while a request is waiting or being compiled.
		bool IsBusy() const;

		/*********************************************************************
		 \brief  Compiles on the calling thread and returns when done (from the
		         cache when it can). Safe to call from any thread.
		 *********************************************************************/
		Result Compile(const Request& request) const;

		/*********************************************************************
		 \brief  A number that changes when anything that decides the request's
		         SPIR-V changes: its source, a file it #includes (directly or
		         through another include), its entry points, the flags or the
		         compiler. 0 if the source cannot be read.
		 *********************************************************************/
		uint64_t SourceHash(const Request& request) const;

		/*********************************************************************
		 \brief  Looks for slangc: beside the program (`besideProgram`, e.g. a
		         copy shipped with the Editor), then in the Vulkan SDK the
		         VULKAN_SDK environment variable names - where the build finds
		         it too.
		 \return Its path, or an empty path if there is none.
		 *********************************************************************/
		static std::filesystem::path FindCompiler(const std::filesystem::path& besideProgram);

		/// The compiler's output as messages. Understands both of slangc's formats.
		static std::vector<Diagnostic> ParseDiagnostics(std::string_view output);

		/// The names in a source's `#include "name"` lines, in order.
		static std::vector<std::string> IncludedNames(std::string_view source);

	private:
		struct Pending {
			uint64_t ticket = 0;
			Request  request;
		};

		void WorkerMain();

		Settings m_settings;

		mutable std::mutex      m_mutex;      // guards everything below
		std::condition_variable m_wake;       // worker: a new request, or stop
		std::deque<Pending>     m_queue;      // waiting
		std::deque<Result>      m_finished;   // waiting for Poll()
		std::thread             m_worker;     // started by the first Submit()
		uint64_t                m_nextTicket = 1;
		bool                    m_compiling = false;
		bool                    m_stop = false;
	};

}
