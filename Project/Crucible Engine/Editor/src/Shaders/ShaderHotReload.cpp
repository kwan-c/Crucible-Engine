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
		for (const Watched& watched : m_watched) { errors += watched.errors; }
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
				CheckParams(watched);   // its parameter list first: the materials are ready when the new code arrives

				ShaderCompiler::Request request;
				request.name = watched.shader.name;
				request.entryPoints = watched.shader.entryPoints;

				// Only what this shader really includes counts: saving Water.slang does not
				// recompile the mesh shader.
				const uint64_t hash = m_compiler->SourceHash(request);
				if (hash == watched.sourceHash) { continue; }
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
		const uint64_t hash = text.empty() ? 0 : HashBytes(text.data(), text.size());
		if (hash == watched.paramsHash) { return; }
		watched.paramsHash = hash;   // a list with a mistake in it is not handed over again until it changes

		std::string error;
		if (watched.shader.applyParams(text.empty() ? std::string_view("{}") : std::string_view(text), error)) {
			CRU_INFO("[Editor | Shaders] {}{} applied.", watched.shader.name, kParamsSuffix);
		} else {
			CRU_USER_ERROR("{}{}: {} - still using the previous parameter list.", watched.shader.name, kParamsSuffix,
			               error.empty() ? "the renderer could not use it" : error);
		}
	}

	void ShaderHotReload::OnCompiled(Watched& watched, const ShaderCompiler::Result& result) {
		const std::string& name = watched.shader.name;

		// The compiler's messages: errors always, warnings too when it was really run.
		size_t reported = 0;
		std::string firstError;
		for (const ShaderCompiler::Diagnostic& d : result.diagnostics) {
			if (d.severity == ShaderCompiler::Severity::Note) { continue; }
			const std::string text = Describe(d);
			if (d.severity == ShaderCompiler::Severity::Error) {
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
		const uint64_t compiledHash = HashSpirv(result.spirv);
		if (compiledHash == watched.runningHash) {
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
		if (result.fromCache) {
			CRU_USER_INFO("Shader {} reloaded.", name);
		} else {
			CRU_USER_INFO("Shader {} compiled in {:.1f}s and reloaded.", name, result.seconds);
		}
	}

}
