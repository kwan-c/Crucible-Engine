/*!
* @file			ShaderHotReload.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Implementation - see ShaderHotReload.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "Shaders/ShaderHotReload.h"

#include "Diagnostics/Logger.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace Crucible {

	namespace {

		constexpr auto   kScanInterval = std::chrono::milliseconds(500);   // how often Assets/Shaders is checked
		constexpr auto   kSettleTime   = std::chrono::milliseconds(300);   // a change must be this old before compiling
		constexpr size_t kMaxReported  = 25;                               // Console lines per compile
		constexpr std::string_view kParamsSuffix = ".params.json";         // a mesh shader's parameter list: <Name>.params.json

		std::string ToUtf8(const fs::path& path) {
			const std::u8string text = path.generic_u8string();
			return std::string(text.begin(), text.end());
		}

		// FNV-1a over bytes; never 0 for real data (0 means "nothing").
		uint64_t HashBytes(const void* data, size_t size, uint64_t hash = 14695981039346656037ull) {
			const unsigned char* bytes = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; ++i) {
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
			return hash != 0 ? hash : 1;
		}

		// Whether a mesh shader's source says it moves vertices (Surface.slang): a line that is
		// "#define SURFACE_MOVES_VERTICES". Looked for in the shader's own text only - that is
		// where a surface says so.
		bool DefinesMovedVertices(std::string_view source) {
			constexpr std::string_view kDefine = "#define", kName = "SURFACE_MOVES_VERTICES";
			for (size_t at = source.find(kName); at != std::string_view::npos; at = source.find(kName, at + kName.size())) {
				const size_t newline = source.rfind('\n', at);
				std::string_view before = source.substr(newline == std::string_view::npos ? 0 : newline + 1);
				before = before.substr(0, at - (newline == std::string_view::npos ? 0 : newline + 1));
				while (!before.empty() && (before.front() == ' ' || before.front() == '\t')) { before.remove_prefix(1); }
				while (!before.empty() && (before.back() == ' ' || before.back() == '\t')) { before.remove_suffix(1); }
				const size_t end = at + kName.size();
				const bool wholeWord = end >= source.size() || !(std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '_');
				if (before == kDefine && wholeWord) { return true; }
			}
			return false;
		}

		uint64_t HashSpirv(std::span<const uint32_t> spirv) {
			return spirv.empty() ? 0 : HashBytes(spirv.data(), spirv.size_bytes());
		}

		// Hash of a .spv file's contents, 0 if it cannot be read.
		uint64_t HashFile(const fs::path& file) {
			std::ifstream in(file, std::ios::binary | std::ios::ate);
			if (!in) { return 0; }
			const std::streamoff size = in.tellg();
			if (size <= 0) { return 0; }
			std::vector<char> bytes(static_cast<size_t>(size));
			in.seekg(0, std::ios::beg);
			if (!in.read(bytes.data(), size)) { return 0; }
			return HashBytes(bytes.data(), bytes.size());
		}

		// Size and time of a file as one number; 0 if it is not there.
		uint64_t FileStamp(const fs::path& file) {
			std::error_code ec;
			if (file.empty() || !fs::is_regular_file(file, ec)) { return 0; }
			const uint64_t facts[2] = {
				static_cast<uint64_t>(fs::file_size(file, ec)),
				static_cast<uint64_t>(fs::last_write_time(file, ec).time_since_epoch().count())
			};
			return HashBytes(facts, sizeof(facts));
		}

		// Writes a file, unless it already holds exactly these bytes. Through a temporary file, so
		// nothing ever reads half of it. False if it could not be written.
		bool WriteIfDifferent(const fs::path& file, const void* data, size_t size) {
			if (size != 0 && HashFile(file) == HashBytes(data, size)) { return true; }
			std::error_code ec;
			fs::create_directories(file.parent_path(), ec);
			fs::path temporary = file;
			temporary += ".tmp";
			{
				std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
				out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
				if (!out) { return false; }
			}
			fs::rename(temporary, file, ec);
			return !ec;
		}

		// What messages call a shader: a generated one by its file, the others by name.
		const std::string& Label(const ShaderHotReload::Shader& shader) {
			return shader.label.empty() ? shader.name : shader.label;
		}

		ShaderHotReload* s_active = nullptr;   // ShaderHotReload::Active()

		const char* SeverityWord(ShaderCompiler::Severity severity) {
			switch (severity) {
			case ShaderCompiler::Severity::Error:   return "error";
			case ShaderCompiler::Severity::Warning: return "warning";
			default:                                return "note";
			}
		}

		// "DefaultSurface.slang(32,35): error E30027: member not found: ..." - the shape the build's
		// own shader errors have in Visual Studio's Error List.
		std::string Describe(const ShaderCompiler::Diagnostic& d) {
			std::string text;
			if (!d.file.empty()) {
				text += d.file;
				if (d.line > 0) {
					text += "(" + std::to_string(d.line);
					if (d.column > 0) { text += "," + std::to_string(d.column); }
					text += ")";
				}
				text += ": ";
			}
			text += SeverityWord(d.severity);
			if (!d.code.empty()) { text += " " + d.code; }
			text += ": " + d.message;
			return text;
		}

	}   // anonymous namespace

	ShaderHotReload::~ShaderHotReload() {
		if (s_active == this) { s_active = nullptr; }
	}

	ShaderHotReload* ShaderHotReload::Active() { return s_active; }

	const ShaderHotReload::Watched* ShaderHotReload::Find(std::string_view name) const {
		for (const Watched& watched : m_watched) {
			if (watched.shader.name == name) { return &watched; }
		}
		return nullptr;
	}

	bool ShaderHotReload::IsCompiling(std::string_view name) const {
		const Watched* watched = Find(name);
		return watched && watched->ticket != 0;
	}

	bool ShaderHotReload::HasErrors(std::string_view name) const {
		const Watched* watched = Find(name);
		return watched && watched->errors + watched->generateErrors > 0;
	}

	const std::vector<ShaderCompiler::Diagnostic>& ShaderHotReload::ErrorsOf(std::string_view name) const {
		static const std::vector<ShaderCompiler::Diagnostic> none;
		const Watched* watched = Find(name);
		return watched ? watched->diagnostics : none;
	}

	bool ShaderHotReload::SetLive(std::string_view name, std::string source, std::string paramsJson) {
		for (Watched& watched : m_watched) {
			if (watched.shader.name != name) { continue; }
			if (watched.live && watched.liveSource == source && watched.liveParams == paramsJson) { return true; }
			watched.live = true;
			watched.liveSource = std::move(source);
			watched.liveParams = std::move(paramsJson);
			m_dirty = true;   // looked at on the next Tick
			return true;
		}
		return false;
	}

	void ShaderHotReload::ClearLive(std::string_view name) {
		for (Watched& watched : m_watched) {
			if (watched.shader.name != name || !watched.live) { continue; }
			watched.live = false;
			watched.liveSource.clear();
			watched.liveParams.clear();
			// What the file says, from the start: generated and compiled again (the compile
			// normally comes straight from the cache).
			watched.generated = false;
			watched.sourceHash = 0;
			m_dirty = true;
		}
	}

	bool ShaderHotReload::IsLive(std::string_view name) const {
		const Watched* watched = Find(name);
		return watched && watched->live;
	}

	void ShaderHotReload::Configure(ShaderCompiler::Settings settings, fs::path builtDir) {
		m_builtDir = std::move(builtDir);
		m_watched.clear();
		m_firstError.clear();

		std::error_code ec;
		if (settings.compiler.empty() || !fs::is_regular_file(settings.compiler, ec)) {
			m_compiler.reset();
			m_status = Status::Disabled;
			CRU_USER_WARN("Shader hot reload is off: the shader compiler (slangc) was not found. "
			              "Shader edits need a rebuild until the Vulkan SDK is installed on this machine.");
			return;
		}

		CRU_INFO("[Editor | Shaders] Hot reload: compiling {} with {}", ToUtf8(settings.shaderDir), ToUtf8(settings.compiler));
		m_compiler = std::make_unique<ShaderCompiler>(std::move(settings));
		m_seen = FolderSignature();
		m_seenAt = Clock::now() - kSettleTime;
		m_nextScan = Clock::now() + kScanInterval;
		m_dirty = true;   // check every watched shader against its sources once, at the first Tick
		m_status = Status::UpToDate;
		s_active = this;
	}

	void ShaderHotReload::Watch(Shader shader) {
		if (!m_compiler) { return; }
		Watched watched;
		// The renderer starts with the .spv the build wrote.
		watched.runningHash = HashFile(m_builtDir / (shader.name + ".spv"));
		watched.shader = std::move(shader);
		m_watched.push_back(std::move(watched));
		m_dirty = true;
	}

	int ShaderHotReload::ErrorCount() const {
		int errors = 0;
		for (const Watched& watched : m_watched) { errors += watched.errors + watched.generateErrors; }
		return errors;
	}

	uint64_t ShaderHotReload::FolderSignature() const {
		// Name, size and time of every .slang file and every parameter list (.params.json), order
		// not mattering (the hashes are summed).
		uint64_t signature = 0;
		std::error_code ec;
		for (const fs::directory_entry& entry : fs::directory_iterator(m_compiler->GetSettings().shaderDir, ec)) {
			if (!entry.is_regular_file(ec)) { continue; }
			const std::string name = ToUtf8(entry.path().filename());
			if (entry.path().extension() != ".slang" && !name.ends_with(kParamsSuffix)) { continue; }
			const uint64_t facts[2] = {
				static_cast<uint64_t>(entry.file_size(ec)),
				static_cast<uint64_t>(entry.last_write_time(ec).time_since_epoch().count())
			};
			signature += HashBytes(facts, sizeof(facts), HashBytes(name.data(), name.size()));
		}
		// ... and the file each generated shader is made from (a shader graph), wherever it is.
		for (const Watched& watched : m_watched) {
			if (!watched.shader.generatedFrom) { continue; }
			const fs::path file = watched.shader.generatedFrom();
			const std::string name = ToUtf8(file);
			signature += HashBytes(name.data(), name.size(), FileStamp(file) + 1);
		}
		return signature;
	}

	void ShaderHotReload::Tick() {
		if (!m_compiler) {
			m_status = Status::Disabled;
			return;
		}
		const Clock::time_point now = Clock::now();

		// 1. Compiles that finished: swap them in, or report them.
		ShaderCompiler::Result result;
		while (m_compiler->Poll(result)) {
			const auto watched = std::find_if(m_watched.begin(), m_watched.end(),
				[&](const Watched& w) { return w.ticket == result.ticket; });
			if (watched == m_watched.end()) { continue; }   // replaced by a newer request
			watched->ticket = 0;
			OnCompiled(*watched, result);
		}

		// 2. Watch the sources.
		if (now >= m_nextScan) {
			m_nextScan = now + kScanInterval;
			const uint64_t signature = FolderSignature();
			if (signature != m_seen) {
				m_seen = signature;
				m_seenAt = now;   // restart the settle time on every change
				m_dirty = true;
			}
		}
		if (m_dirty && autoReload && now - m_seenAt >= kSettleTime) {
			m_dirty = false;
			for (Watched& watched : m_watched) {
				ShaderCompiler::Request request;
				request.name = watched.shader.name;
				request.entryPoints = watched.shader.entryPoints;
				if (watched.live) {
					// A live source (the unsaved graph in the Shader Graph window) stands in for
					// the file. Its parameter list is handed over when its code is swapped in.
					request.source = watched.liveSource;
				} else if (watched.shader.generate) {
					// A generated shader (a shader graph): its source and its parameter list are
					// made here, when the file it comes from has changed.
					if (!Regenerate(watched)) { continue; }   // nothing usable: the renderer keeps what it has
					request.source = watched.source;
				} else {
					CheckParams(watched);   // its parameter list first: the materials are ready when the new code arrives
				}

				// A surface that moves vertices is compiled with more entry points: its versions
				// of the depth-only passes' shaders, which the renderer then builds pipelines from.
				if (!watched.shader.movedEntryPoints.empty()) {
					std::string text = request.source;
					if (text.empty()) {
						const fs::path file = m_compiler->GetSettings().shaderDir / (watched.shader.name + ".slang");
						if (std::ifstream in(file, std::ios::binary); in) {
							text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
						}
					}
					if (DefinesMovedVertices(text)) {
						request.entryPoints.insert(request.entryPoints.end(), watched.shader.movedEntryPoints.begin(), watched.shader.movedEntryPoints.end());
					}
				}

				// Only what this shader really includes counts: saving Water.slang does not
				// recompile the mesh shader.
				const uint64_t hash = m_compiler->SourceHash(request);
				if (hash == watched.sourceHash) {
					// The code that is already compiled: of a live source, only the parameter list
					// can have changed (a default, a range) - that needs no compile.
					if (watched.live && watched.ticket == 0 && watched.errors == 0 && watched.shader.applyParams) {
						HandOverParams(watched, watched.liveParams, Label(watched.shader) + "'s parameter list");
					}
					continue;
				}
				watched.sourceHash = hash;
				watched.ticket = m_compiler->Submit(std::move(request));
			}
		}

		// Status for whoever shows it.
		const bool compiling = std::any_of(m_watched.begin(), m_watched.end(), [](const Watched& w) { return w.ticket != 0; });
		m_status = compiling ? Status::Compiling : (ErrorCount() > 0 ? Status::Errors : Status::UpToDate);
		if (m_status != Status::Errors) { m_firstError.clear(); }
	}

	void ShaderHotReload::CheckParams(Watched& watched) {
		if (!watched.shader.applyParams) { return; }

		// The list's text now ("" = no such file), against what was last handed over.
		const fs::path file = m_compiler->GetSettings().shaderDir / (watched.shader.name + std::string(kParamsSuffix));
		std::string text;
		if (std::ifstream in(file, std::ios::binary); in) {
			text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
		HandOverParams(watched, text, watched.shader.name + std::string(kParamsSuffix));
	}

	// Hands a parameter list's text ("" = the shader has none) to the shader, unless it is what was
	// handed over last. False only if the renderer would not take it.
	bool ShaderHotReload::HandOverParams(Watched& watched, const std::string& text, const std::string& what) {
		const uint64_t hash = text.empty() ? 0 : HashBytes(text.data(), text.size());
		if (hash == watched.paramsHash) { return true; }
		watched.paramsHash = hash;   // a list with a mistake in it is not handed over again until it changes

		std::string error;
		if (watched.shader.applyParams(text.empty() ? std::string_view("{}") : std::string_view(text), error)) {
			CRU_INFO("[Editor | Shaders] {} applied.", what);
			return true;
		}
		CRU_USER_ERROR("{}: {} - still using the previous parameter list.", what,
		               error.empty() ? "the renderer could not use it" : error);
		return false;
	}

	// Makes a generated shader's source again if the file it comes from has changed since the last
	// time. False when there is no source worth compiling: the file has a mistake in it.
	bool ShaderHotReload::Regenerate(Watched& watched) {
		const uint64_t stamp = watched.shader.generatedFrom ? FileStamp(watched.shader.generatedFrom()) : 0;
		if (watched.generated && stamp == watched.generatedStamp) {
			return watched.generateErrors == 0 && !watched.source.empty();
		}
		watched.generated = true;
		watched.generatedStamp = stamp;   // a file with a mistake in it is reported once, until it changes

		const std::string& label = Label(watched.shader);
		std::string source, paramsJson, error;
		if (!watched.shader.generate(source, paramsJson, error)) {
			watched.generateErrors = 1;
			const std::string text = label + ": " + (error.empty() ? "it could not be turned into a shader" : error);
			if (m_firstError.empty()) { m_firstError = text; }
			CRU_USER_ERROR("{} - still using the last version that worked.", text);
			return false;
		}
		watched.generateErrors = 0;
		watched.source = std::move(source);

		// Its parameter list first: the materials are ready when the new code arrives. Kept where
		// the build keeps a written shader's list, so the next start finds it there.
		if (watched.shader.applyParams && HandOverParams(watched, paramsJson, label + "'s parameter list")) {
			const fs::path built = m_builtDir / (watched.shader.name + std::string(kParamsSuffix));
			if (!WriteIfDifferent(built, paramsJson.data(), paramsJson.size())) {
				CRU_WARN("[Editor | Shaders] Could not write {}", ToUtf8(built));
			}
		}
		return true;
	}

	void ShaderHotReload::OnCompiled(Watched& watched, const ShaderCompiler::Result& result) {
		const std::string& name = Label(watched.shader);
		watched.diagnostics.clear();

		// The compiler's messages: errors always, warnings too when it was really run.
		size_t reported = 0;
		std::string firstError;
		for (const ShaderCompiler::Diagnostic& d : result.diagnostics) {
			if (d.severity == ShaderCompiler::Severity::Note) { continue; }
			ShaderCompiler::Diagnostic shown = d;
			if (shown.file == watched.shader.name) { shown.file = name; }   // a generated shader: its file, not its GUID
			const std::string text = Describe(shown);
			if (d.severity == ShaderCompiler::Severity::Error) {
				watched.diagnostics.push_back(d);
				if (firstError.empty()) { firstError = text; }
				if (reported++ < kMaxReported) { CRU_USER_ERROR("{}", text); }
			} else if (reported++ < kMaxReported) {
				CRU_USER_WARN("{}", text);
			}
		}

		if (!result.success) {
			watched.errors = (std::max)(result.ErrorCount(), 1);
			if (!result.failure.empty()) {
				if (firstError.empty()) { firstError = result.failure; }
				CRU_USER_ERROR("Shader {} could not be compiled: {}", name, result.failure);
				if (!result.output.empty()) { CRU_ERROR("[Editor | Shaders] slangc said:\n{}", result.output); }
			}
			if (m_firstError.empty()) { m_firstError = firstError; }
			CRU_USER_ERROR("Shader {} has errors - still using the last version that compiled.", name);
			return;
		}
		watched.errors = 0;

		// Same SPIR-V as the renderer already has (the usual case at start; also a change to a
		// comment): nothing to swap.
		// A live source's parameter list goes with its code: handed over now, not when it was
		// set, so the shader that is drawing never reads a list that was made for other code.
		if (watched.live && watched.shader.applyParams) { HandOverParams(watched, watched.liveParams, name + "'s parameter list"); }

		// A generated shader is kept where the build keeps the shaders it compiles: the next start,
		// and the Game run from the build folder, load it like any other. Not a live one - that
		// is not saved yet.
		const auto keepBuilt = [&] {
			if (!watched.shader.generate || watched.live) { return; }
			const fs::path built = m_builtDir / (watched.shader.name + ".spv");
			if (!WriteIfDifferent(built, result.spirv.data(), result.spirv.size() * sizeof(uint32_t))) {
				CRU_WARN("[Editor | Shaders] Could not write {}", ToUtf8(built));
			}
		};

		const uint64_t compiledHash = HashSpirv(result.spirv);
		if (compiledHash == watched.runningHash) {
			keepBuilt();
			CRU_INFO("[Editor | Shaders] {} is up to date.", name);
			return;
		}

		std::string error;
		if (!watched.shader.apply || !watched.shader.apply(result.spirv, error)) {
			watched.errors = 1;
			if (m_firstError.empty()) { m_firstError = error; }
			CRU_USER_ERROR("Shader {} compiled, but the renderer could not use it: {}", name, error.empty() ? "no reason given" : error);
			return;
		}
		watched.runningHash = compiledHash;
		keepBuilt();
		if (watched.live) {
			// Every edit in the Shader Graph window ends up here: the log, not the Console.
			CRU_INFO("[Editor | Shaders] {} (not saved yet) compiled in {:.1f}s and swapped in.", name, result.seconds);
		} else if (result.fromCache) {
			CRU_USER_INFO("Shader {} reloaded.", name);
		} else {
			CRU_USER_INFO("Shader {} compiled in {:.1f}s and reloaded.", name, result.seconds);
		}
	}

}
