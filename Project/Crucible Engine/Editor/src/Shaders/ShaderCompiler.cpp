/*!
* @file			ShaderCompiler.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Implementation - see ShaderCompiler.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "Shaders/ShaderCompiler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <map>
#include <regex>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace Crucible {

	namespace {

		// slangc's arguments apart from the source, the entry points and the output.
		// KEEP IN STEP with CRUCIBLE_SLANGC_FLAGS in Engine/CompileShaders.cmake: the Editor must
		// compile a shader exactly as the build does.
		constexpr std::array<std::string_view, 7> kCompilerFlags{
			"-target", "spirv",
			"-profile", "spirv_1_4",
			"-emit-spirv-directly",
			"-fvk-use-entrypoint-name",
			"-matrix-layout-column-major",
		};

		constexpr uint32_t kSpirvMagic     = 0x07230203u;
		constexpr size_t   kSpirvMinWords  = 5;   // the header
		constexpr size_t   kKeptPerShader  = 4;   // cached results kept for one shader name (newest first)
		constexpr auto     kStaleTempAfter = std::chrono::hours(1);   // leftovers of a compile that never finished

		// ---- Text and paths ----

		std::string ToUtf8(const fs::path& path) {
			const std::u8string text = path.generic_u8string();
			return std::string(text.begin(), text.end());
		}

		fs::path FromUtf8(std::string_view text) {
			return fs::path(std::u8string(text.begin(), text.end()));
		}

		std::string_view TrimLeft(std::string_view text) {
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) { text.remove_prefix(1); }
			return text;
		}

		bool StartsWith(std::string_view text, std::string_view prefix) {
			return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
		}

		bool ReadText(const fs::path& file, std::string& out) {
			std::ifstream in(file, std::ios::binary | std::ios::ate);
			if (!in) { return false; }
			const std::streamoff size = in.tellg();
			if (size < 0) { return false; }
			out.resize(static_cast<size_t>(size));
			in.seekg(0, std::ios::beg);
			return size == 0 || static_cast<bool>(in.read(out.data(), size));
		}

		// A whole SPIR-V module, or false: missing, not 32-bit words, or not starting with the magic number.
		bool ReadSpirv(const fs::path& file, std::vector<uint32_t>& out) {
			std::ifstream in(file, std::ios::binary | std::ios::ate);
			if (!in) { return false; }
			const std::streamoff size = in.tellg();
			if (size < static_cast<std::streamoff>(kSpirvMinWords * sizeof(uint32_t)) || size % static_cast<std::streamoff>(sizeof(uint32_t)) != 0) {
				return false;
			}
			std::vector<uint32_t> words(static_cast<size_t>(size) / sizeof(uint32_t));
			in.seekg(0, std::ios::beg);
			if (!in.read(reinterpret_cast<char*>(words.data()), size) || words.front() != kSpirvMagic) { return false; }
			out = std::move(words);
			return true;
		}

		// Request::name becomes part of file names: keep it to characters that are safe in one.
		bool IsValidName(std::string_view name) {
			if (name.empty() || name.front() == '.' || name.find("..") != std::string_view::npos) { return false; }
			return std::all_of(name.begin(), name.end(), [](char c) {
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
			});
		}

		std::string HashText(uint64_t hash) {
			char text[17];
			std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
			return text;
		}

		// ---- Hashing (FNV-1a, 64 bits) ----

		class Hasher {
		public:
			void Add(std::string_view text) {
				Add(static_cast<uint64_t>(text.size()));   // the length too, so "ab","c" differs from "a","bc"
				for (const char c : text) { Byte(static_cast<unsigned char>(c)); }
			}
			void Add(uint64_t value) {
				for (int i = 0; i < 8; ++i) { Byte(static_cast<unsigned char>(value >> (i * 8))); }
			}
			uint64_t Value() const { return m_hash != 0 ? m_hash : 1; }   // never 0: 0 means "could not hash"

		private:
			void Byte(unsigned char b) {
				m_hash ^= b;
				m_hash *= 1099511628211ull;
			}
			uint64_t m_hash = 14695981039346656037ull;
		};

		// ---- The request's source and what it includes ----

		bool LoadSource(const ShaderCompiler::Settings& settings, const ShaderCompiler::Request& request, std::string& text) {
			if (!request.source.empty()) {
				text = request.source;
				return true;
			}
			return ReadText(settings.shaderDir / FromUtf8(request.name + ".slang"), text);
		}

		// Every file `text` includes, and what those include: name (relative to shaderDir) -> contents.
		// A name is looked up beside the including file first, then in shaderDir, as slangc does. A file
		// that cannot be found is left out: the compiler reports it.
		void CollectIncludes(std::string_view text, const fs::path& fromDir, const fs::path& shaderDir, std::map<std::string, std::string>& found) {
			for (const std::string& name : ShaderCompiler::IncludedNames(text)) {
				fs::path file;
				std::error_code ec;
				for (const fs::path& dir : { fromDir, shaderDir }) {
					const fs::path candidate = (dir / FromUtf8(name)).lexically_normal();
					if (fs::is_regular_file(candidate, ec)) { file = candidate; break; }
				}
				if (file.empty()) { continue; }

				const fs::path relative = file.lexically_relative(shaderDir.lexically_normal());
				const bool inside = !relative.empty() && *relative.begin() != "..";
				const std::string key = ToUtf8(inside ? relative : file);
				if (found.contains(key)) { continue; }   // already read (and guards against include cycles)

				std::string contents;
				if (!ReadText(file, contents)) { continue; }
				const auto added = found.emplace(key, std::move(contents)).first;
				CollectIncludes(added->second, file.parent_path(), shaderDir, found);
			}
		}

		uint64_t HashRequest(const ShaderCompiler::Settings& settings, const ShaderCompiler::Request& request, const std::string& text) {
			Hasher hasher;
			hasher.Add("crucible-shader-1");   // bump to throw every cached result away

			// The compiler itself: a new SDK may compile the same source differently.
			std::error_code ec;
			hasher.Add(ToUtf8(settings.compiler));
			hasher.Add(static_cast<uint64_t>(fs::file_size(settings.compiler, ec)));
			hasher.Add(static_cast<uint64_t>(fs::last_write_time(settings.compiler, ec).time_since_epoch().count()));

			for (const std::string_view flag : kCompilerFlags) { hasher.Add(flag); }
			hasher.Add(static_cast<uint64_t>(request.entryPoints.size()));
			for (const std::string& entry : request.entryPoints) { hasher.Add(entry); }

			hasher.Add(text);
			std::map<std::string, std::string> includes;   // sorted by name, so the order of discovery does not matter
			CollectIncludes(text, settings.shaderDir, settings.shaderDir, includes);
			for (const auto& [name, contents] : includes) {
				hasher.Add(name);
				hasher.Add(contents);
			}
			return hasher.Value();
		}

		// ---- The cache folder ----

		// Keeps the newest kKeptPerShader results of `name` (<name>-<16 hex digits>.spv, with the
		// generated .slang beside it if there is one) and removes the rest, plus what failed or
		// interrupted compiles left behind long ago: temporary outputs, and generated sources
		// with no result.
		void PruneCache(const fs::path& cacheDir, const std::string& name) {
			std::error_code ec;
			std::vector<std::pair<fs::file_time_type, fs::path>> results;
			std::vector<fs::path> stale;
			const auto now = fs::file_time_type::clock::now();

			// Listed first, removed afterwards: nothing is deleted while the folder is being walked.
			for (const fs::directory_entry& entry : fs::directory_iterator(cacheDir, ec)) {
				if (!entry.is_regular_file(ec)) { continue; }
				const std::string file = ToUtf8(entry.path().filename());
				const fs::file_time_type written = entry.last_write_time(ec);
				const bool old = now - written > kStaleTempAfter;

				if (file.size() > 8 && file.compare(file.size() - 8, 8, ".tmp.spv") == 0) {
					if (old) { stale.push_back(entry.path()); }
					continue;
				}

				// <name>-<16 hex>.<extension> exactly: "Default" must not match "DefaultShader-....spv".
				const std::string extension = ToUtf8(entry.path().extension());
				const size_t expected = name.size() + 1 + 16 + extension.size();
				if ((extension != ".spv" && extension != ".slang") || file.size() != expected ||
				    !StartsWith(file, name) || file[name.size()] != '-') {
					continue;
				}
				const std::string_view digits = std::string_view(file).substr(name.size() + 1, 16);
				if (!std::all_of(digits.begin(), digits.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) { continue; }

				if (extension == ".spv") {
					results.emplace_back(written, entry.path());
				} else if (old) {
					fs::path result = entry.path();
					result.replace_extension(".spv");
					if (!fs::exists(result, ec)) { stale.push_back(entry.path()); }
				}
			}

			if (results.size() > kKeptPerShader) {
				std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
				for (size_t i = kKeptPerShader; i < results.size(); ++i) {
					stale.push_back(results[i].second);
					fs::path generated = results[i].second;
					generated.replace_extension(".slang");
					stale.push_back(generated);
				}
			}
			for (const fs::path& file : stale) { fs::remove(file, ec); }
		}

		// ---- Running the compiler ----

		struct ProcessResult {
			bool        started = false;
			int         exitCode = -1;
			std::string output;        // stdout and stderr together
			std::string launchError;   // why it did not start
		};

#ifdef _WIN32
		// One argument of a Windows command line, quoted the way the C runtime of the program being
		// started un-quotes it: backslashes are literal except before a quote, where they double.
		void AppendArgument(std::wstring& commandLine, std::wstring_view argument) {
			if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
				commandLine.append(argument);
				return;
			}
			commandLine.push_back(L'"');
			for (size_t i = 0; i < argument.size(); ++i) {
				size_t backslashes = 0;
				while (i < argument.size() && argument[i] == L'\\') { ++i; ++backslashes; }
				if (i == argument.size()) {
					commandLine.append(backslashes * 2, L'\\');   // before the closing quote
					break;
				}
				if (argument[i] == L'"') {
					commandLine.append(backslashes * 2 + 1, L'\\');
				} else {
					commandLine.append(backslashes, L'\\');
				}
				commandLine.push_back(argument[i]);
			}
			commandLine.push_back(L'"');
		}

		// Runs `program` hidden, in `workingDir`, and waits for it. Its stdout and stderr go into one pipe.
		ProcessResult RunProcess(const fs::path& program, const std::vector<std::string>& arguments, const fs::path& workingDir) {
			ProcessResult result;

			SECURITY_ATTRIBUTES security{};
			security.nLength = sizeof(security);
			security.bInheritHandle = TRUE;
			HANDLE readPipe = nullptr, writePipe = nullptr;
			if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
				result.launchError = "could not create a pipe for the compiler's output";
				return result;
			}
			SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);   // the child gets the write end only

			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			startup.dwFlags = STARTF_USESTDHANDLES;
			startup.hStdOutput = writePipe;
			startup.hStdError = writePipe;

			std::wstring commandLine;
			AppendArgument(commandLine, program.native());
			for (const std::string& argument : arguments) {
				commandLine.push_back(L' ');
				AppendArgument(commandLine, FromUtf8(argument).native());
			}

			PROCESS_INFORMATION process{};
			const BOOL launched = CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
			                                     nullptr, workingDir.empty() ? nullptr : workingDir.c_str(), &startup, &process);
			const DWORD launchCode = launched ? 0 : GetLastError();
			CloseHandle(writePipe);   // only the child writes now: ReadFile ends when it exits
			if (!launched) {
				CloseHandle(readPipe);
				result.launchError = "could not start " + ToUtf8(program) + " (Windows error " + std::to_string(launchCode) + ")";
				return result;
			}
			result.started = true;
			CloseHandle(process.hThread);

			char buffer[4096];
			DWORD read = 0;
			while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) { result.output.append(buffer, read); }
			CloseHandle(readPipe);

			WaitForSingleObject(process.hProcess, INFINITE);
			DWORD exitCode = 1;
			GetExitCodeProcess(process.hProcess, &exitCode);
			CloseHandle(process.hProcess);
			result.exitCode = static_cast<int>(exitCode);
			return result;
		}

		std::wstring Environment(const wchar_t* name) {
			wchar_t* value = nullptr;
			size_t length = 0;
			if (_wdupenv_s(&value, &length, name) != 0 || value == nullptr) { return {}; }
			std::wstring text(value);
			std::free(value);
			return text;
		}
#else
		std::string ShellQuoted(const std::string& text) {
			std::string quoted = "'";
			for (const char c : text) {
				if (c == '\'') { quoted += "'\\''"; } else { quoted += c; }
			}
			return quoted + "'";
		}

		ProcessResult RunProcess(const fs::path& program, const std::vector<std::string>& arguments, const fs::path& workingDir) {
			ProcessResult result;
			std::string command;
			if (!workingDir.empty()) { command += "cd " + ShellQuoted(workingDir.string()) + " && "; }
			command += ShellQuoted(program.string());
			for (const std::string& argument : arguments) { command += " " + ShellQuoted(argument); }
			command += " 2>&1";

			FILE* pipe = popen(command.c_str(), "r");
			if (!pipe) {
				result.launchError = "could not start " + ToUtf8(program);
				return result;
			}
			result.started = true;
			char buffer[4096];
			size_t read = 0;
			while ((read = fread(buffer, 1, sizeof(buffer), pipe)) > 0) { result.output.append(buffer, read); }
			const int status = pclose(pipe);
			result.exitCode = (status >= 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
			return result;
		}

		std::string Environment(const char* name) {
			const char* value = std::getenv(name);
			return value ? std::string(value) : std::string();
		}
#endif

		// ---- Compiler messages ----

		ShaderCompiler::Severity SeverityOf(const std::string& word) {
			if (word == "warning") { return ShaderCompiler::Severity::Warning; }
			if (word == "note")    { return ShaderCompiler::Severity::Note; }
			return ShaderCompiler::Severity::Error;   // error, fatal error, internal error
		}

		int ToInt(std::string_view digits) {
			int value = 0;
			for (const char c : digits) {
				if (c < '0' || c > '9' || value > 100000000) { return 0; }
				value = value * 10 + (c - '0');
			}
			return value;
		}

		// "file:line:column" (or "file:line"), read from the right so "C:\folder\file.slang:3:7" works.
		void ParseLocation(std::string_view text, ShaderCompiler::Diagnostic& out) {
			const auto lastNumber = [](std::string_view& from, int& number) {
				const size_t colon = from.rfind(':');
				if (colon == std::string_view::npos || colon + 1 == from.size()) { return false; }
				const std::string_view digits = from.substr(colon + 1);
				if (!std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) { return false; }
				number = ToInt(digits);
				from = from.substr(0, colon);
				return true;
			};
			int last = 0, beforeLast = 0;
			if (!lastNumber(text, last)) {
				out.file = std::string(text);
				return;
			}
			if (lastNumber(text, beforeLast)) {
				out.line = beforeLast;
				out.column = last;
			} else {
				out.line = last;
			}
			out.file = std::string(text);
		}

	}   // anonymous namespace

	// ------------------------------------------------------------------------------------------- //
	//  Pieces that stand alone
	// ------------------------------------------------------------------------------------------- //

	int ShaderCompiler::Result::ErrorCount() const {
		return static_cast<int>(std::count_if(diagnostics.begin(), diagnostics.end(),
			[](const Diagnostic& d) { return d.severity == Severity::Error; }));
	}

	std::vector<std::string> ShaderCompiler::IncludedNames(std::string_view source) {
		std::vector<std::string> names;
		size_t start = 0;
		while (start < source.size()) {
			size_t end = source.find('\n', start);
			if (end == std::string_view::npos) { end = source.size(); }
			std::string_view line = TrimLeft(source.substr(start, end - start));
			start = end + 1;

			if (line.empty() || line.front() != '#') { continue; }
			line = TrimLeft(line.substr(1));
			if (!StartsWith(line, "include")) { continue; }
			line = TrimLeft(line.substr(7));
			if (line.empty() || line.front() != '"') { continue; }
			const size_t close = line.find('"', 1);
			if (close == std::string_view::npos || close == 1) { continue; }
			names.emplace_back(line.substr(1, close - 1));
		}
		return names;
	}

	std::vector<ShaderCompiler::Diagnostic> ShaderCompiler::ParseDiagnostics(std::string_view output) {
		// slangc prints a message in one of two shapes. The current one is a block:
		//
		//     error[E30027]: member not found
		//       --> DefaultSurface.slang:32:35
		//        |
		//     32 | o.roughness = mrSample.g * pc.roughnessFactr;
		//        |                               ^^^^^^^^^^^^^^ 'roughnessFactr' is not a member of 'PushConstants'.
		//     ---'
		//
		// and older versions print one line:
		//
		//     DefaultSurface.slang(32): error 30027: 'roughnessFactr' is not a member of 'PushConstants'.
		static const std::regex blockHead(R"(^(fatal error|internal error|error|warning|note)(?:\[([A-Za-z0-9_]+)\])?: (.*)$)");
		static const std::regex oneLine(R"(^(.*?)\((\d+)(?:,(\d+))?\): (fatal error|internal error|error|warning|note)(?: ([A-Za-z]*\d+))?: (.*)$)");

		std::vector<Diagnostic> diagnostics;
		bool inBlock = false;      // the lines that follow belong to diagnostics.back()
		bool hasDetail = false;    // ... and its message already has the text beside the ^^^ marker

		size_t start = 0;
		while (start < output.size()) {
			size_t end = output.find('\n', start);
			if (end == std::string_view::npos) { end = output.size(); }
			std::string_view lineView = output.substr(start, end - start);
			start = end + 1;
			if (!lineView.empty() && lineView.back() == '\r') { lineView.remove_suffix(1); }
			const std::string line(lineView);
			const std::string_view trimmed = TrimLeft(lineView);

			std::smatch match;
			if (std::regex_match(line, match, oneLine)) {
				Diagnostic d;
				d.file = match[1].str();
				d.line = ToInt(match[2].str());
				d.column = match[3].matched ? ToInt(match[3].str()) : 0;
				d.severity = SeverityOf(match[4].str());
				d.code = match[5].matched ? match[5].str() : std::string();
				d.message = match[6].str();
				diagnostics.push_back(std::move(d));
				inBlock = false;
				continue;
			}
			if (std::regex_match(line, match, blockHead)) {
				Diagnostic d;
				d.severity = SeverityOf(match[1].str());
				d.code = match[2].matched ? match[2].str() : std::string();
				d.message = match[3].str();
				diagnostics.push_back(std::move(d));
				inBlock = true;
				hasDetail = false;
				continue;
			}
			if (!inBlock) { continue; }

			Diagnostic& current = diagnostics.back();
			if (StartsWith(trimmed, "--> ")) {
				if (current.file.empty()) { ParseLocation(trimmed.substr(4), current); }
			} else if (StartsWith(trimmed, "|")) {
				// The marker line: carets under the source, then what is wrong with it.
				std::string_view marker = TrimLeft(trimmed.substr(1));
				if (!hasDetail && !marker.empty() && marker.front() == '^') {
					while (!marker.empty() && marker.front() == '^') { marker.remove_prefix(1); }
					marker = TrimLeft(marker);
					if (!marker.empty()) {
						// Title, then the detail - unless the detail already starts with the title
						// ("undefined identifier" / "undefined identifier 'x'.").
						if (StartsWith(marker, current.message)) {
							current.message = std::string(marker);
						} else {
							current.message += ": ";
							current.message += marker;
						}
						hasDetail = true;
					}
				}
			} else if (StartsWith(trimmed, "-") && !trimmed.empty() && trimmed.back() == '\'') {
				inBlock = false;   // the block's closing line
			}
		}
		return diagnostics;
	}

	fs::path ShaderCompiler::FindCompiler(const fs::path& besideProgram) {
#ifdef _WIN32
		const fs::path programName = L"slangc.exe";
		const fs::path sdk = Environment(L"VULKAN_SDK");
#else
		const fs::path programName = "slangc";
		const fs::path sdk = Environment("VULKAN_SDK");
#endif
		std::error_code ec;

#ifdef CRUCIBLE_SLANGC
		// The compiler the build itself uses, if Editor/CMakeLists.txt passes it on.
		if (const fs::path fromBuild = FromUtf8(CRUCIBLE_SLANGC); fs::is_regular_file(fromBuild, ec)) { return fromBuild; }
#endif
		if (!besideProgram.empty() && fs::is_regular_file(besideProgram / programName, ec)) { return besideProgram / programName; }
		if (!sdk.empty()) {
			for (const char* folder : { "Bin", "bin" }) {
				if (fs::is_regular_file(sdk / folder / programName, ec)) { return sdk / folder / programName; }
			}
		}
		return {};
	}

	// ------------------------------------------------------------------------------------------- //
	//  Compiling
	// ------------------------------------------------------------------------------------------- //

	uint64_t ShaderCompiler::SourceHash(const Request& request) const {
		std::string text;
		if (!LoadSource(m_settings, request, text)) { return 0; }
		return HashRequest(m_settings, request, text);
	}

	ShaderCompiler::Result ShaderCompiler::Compile(const Request& request) const {
		using Clock = std::chrono::steady_clock;
		const Clock::time_point began = Clock::now();

		Result result;
		result.name = request.name;
		const auto done = [&]() -> Result&& {
			result.seconds = std::chrono::duration<double>(Clock::now() - began).count();
			return std::move(result);
		};
		const auto fail = [&](std::string why) -> Result&& {
			result.success = false;
			result.spirv.clear();
			result.failure = std::move(why);
			return done();
		};

		std::error_code ec;
		if (!IsValidName(request.name)) {
			return fail("\"" + request.name + "\" is not a usable shader name (letters, digits, '_', '-' and '.' only)");
		}
		if (request.entryPoints.empty()) { return fail("no entry points were given"); }
		if (m_settings.compiler.empty() || !fs::is_regular_file(m_settings.compiler, ec)) {
			return fail("the shader compiler (slangc) was not found - install the Vulkan SDK the build uses, or put slangc beside the Editor");
		}

		const bool fromText = !request.source.empty();
		std::string text;
		if (!LoadSource(m_settings, request, text)) {
			return fail("could not read " + ToUtf8(m_settings.shaderDir / FromUtf8(request.name + ".slang")));
		}
		result.hash = HashRequest(m_settings, request, text);

		// Already compiled, in this run or an earlier one?
		const std::string stem = request.name + "-" + HashText(result.hash);
		const fs::path cached = m_settings.cacheDir / FromUtf8(stem + ".spv");
		if (ReadSpirv(cached, result.spirv)) {
			fs::last_write_time(cached, fs::file_time_type::clock::now(), ec);   // recently used: PruneCache keeps it
			result.success = true;
			result.fromCache = true;
			return done();
		}

		fs::create_directories(m_settings.cacheDir, ec);
		if (ec) { return fail("could not create " + ToUtf8(m_settings.cacheDir) + ": " + ec.message()); }

		// Source text is compiled from a file of its own in the cache folder (kept beside the
		// result, which is handy when a generated shader needs reading). A shader file is
		// compiled where it is, by name, exactly as the build does.
		const fs::path generated = m_settings.cacheDir / FromUtf8(stem + ".slang");
		if (fromText) {
			std::ofstream out(generated, std::ios::binary | std::ios::trunc);
			out.write(text.data(), static_cast<std::streamsize>(text.size()));
			if (!out) { return fail("could not write " + ToUtf8(generated)); }
		}

		// The compiler writes beside the final name; the result is renamed into place only once it
		// is whole, so a half-written file is never taken for a cached result.
		static std::atomic<uint32_t> serial{ 0 };
		const std::string unique = HashText(static_cast<uint64_t>(Clock::now().time_since_epoch().count())) + "-" + std::to_string(serial.fetch_add(1));
		const fs::path temporary = m_settings.cacheDir / FromUtf8(stem + "." + unique + ".tmp.spv");

		std::vector<std::string> arguments;
		arguments.push_back(fromText ? ToUtf8(generated) : request.name + ".slang");
		for (const std::string_view flag : kCompilerFlags) { arguments.emplace_back(flag); }
		if (fromText) {
			arguments.emplace_back("-I");
			arguments.push_back(ToUtf8(m_settings.shaderDir));
		}
		for (const std::string& entry : request.entryPoints) {
			arguments.emplace_back("-entry");
			arguments.push_back(entry);
		}
		arguments.emplace_back("-o");
		arguments.push_back(ToUtf8(temporary));

		ProcessResult process = RunProcess(m_settings.compiler, arguments, m_settings.shaderDir);
		result.output = std::move(process.output);
		result.diagnostics = ParseDiagnostics(result.output);
		if (fromText) {
			// Messages about the generated file are about the request: name it, not the cache file.
			const std::string generatedName = stem + ".slang";
			for (Diagnostic& d : result.diagnostics) {
				if (ToUtf8(FromUtf8(d.file).filename()) == generatedName) { d.file = request.name; }
			}
		}

		if (!process.started) { return fail(process.launchError); }
		if (process.exitCode != 0 || !ReadSpirv(temporary, result.spirv)) {
			fs::remove(temporary, ec);
			if (result.ErrorCount() > 0) {
				result.success = false;   // the diagnostics say why
				return done();
			}
			return fail(process.exitCode != 0 ? "slangc failed (exit code " + std::to_string(process.exitCode) + ")"
			                                  : "slangc reported success but wrote no SPIR-V");
		}

		// Into the cache. If the rename fails (another Editor got there first, say) the SPIR-V in
		// memory is still good.
		fs::rename(temporary, cached, ec);
		if (ec) { fs::remove(temporary, ec); }
		PruneCache(m_settings.cacheDir, request.name);

		result.success = true;
		return done();
	}

	// ------------------------------------------------------------------------------------------- //
	//  The worker thread
	// ------------------------------------------------------------------------------------------- //

	ShaderCompiler::ShaderCompiler(Settings settings) : m_settings(std::move(settings)) {
		// The compiler runs in shaderDir, so a path relative to the Editor's own working directory
		// would mean somewhere else to it: make them all absolute once, here.
		std::error_code ec;
		for (fs::path* path : { &m_settings.shaderDir, &m_settings.cacheDir, &m_settings.compiler }) {
			if (path->empty()) { continue; }
			const fs::path absolute = fs::absolute(*path, ec);
			if (!ec) { *path = absolute.lexically_normal(); }
		}
	}

	ShaderCompiler::~ShaderCompiler() {
		{
			const std::lock_guard lock(m_mutex);
			m_stop = true;
			m_queue.clear();
		}
		m_wake.notify_all();
		if (m_worker.joinable()) { m_worker.join(); }   // a compile takes a second or two; never leave the thread running
	}

	uint64_t ShaderCompiler::Submit(Request request) {
		uint64_t ticket = 0;
		{
			const std::lock_guard lock(m_mutex);
			ticket = m_nextTicket++;
			const auto waiting = std::find_if(m_queue.begin(), m_queue.end(),
				[&](const Pending& pending) { return pending.request.name == request.name; });
			if (waiting != m_queue.end()) {
				waiting->ticket = ticket;   // newest wins; it keeps its place in the queue
				waiting->request = std::move(request);
			} else {
				m_queue.push_back({ ticket, std::move(request) });
			}
			if (!m_worker.joinable()) { m_worker = std::thread([this] { WorkerMain(); }); }
		}
		m_wake.notify_one();
		return ticket;
	}

	bool ShaderCompiler::Poll(Result& out) {
		const std::lock_guard lock(m_mutex);
		if (m_finished.empty()) { return false; }
		out = std::move(m_finished.front());
		m_finished.pop_front();
		return true;
	}

	bool ShaderCompiler::IsBusy() const {
		const std::lock_guard lock(m_mutex);
		return m_compiling || !m_queue.empty();
	}

	void ShaderCompiler::WorkerMain() {
		std::unique_lock lock(m_mutex);
		for (;;) {
			m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
			if (m_stop) { return; }

			Pending pending = std::move(m_queue.front());
			m_queue.pop_front();
			m_compiling = true;
			lock.unlock();

			Result result;
			try {
				result = Compile(pending.request);
			} catch (const std::exception& e) {
				result = Result{};
				result.name = pending.request.name;
				result.failure = e.what();
			}
			result.ticket = pending.ticket;

			lock.lock();
			m_compiling = false;
			m_finished.push_back(std::move(result));
		}
	}

}
