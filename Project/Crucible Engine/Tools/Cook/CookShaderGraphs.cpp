/*!
* @file			CookShaderGraphs.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			17 November 2026
*
* @brief		Implementation - see CookShaderGraphs.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "CookShaderGraphs.h"

#include "ShaderGraph/ShaderGraph.h"            // Editor/src: no engine, only rapidjson
#include "ShaderGraph/ShaderGraphGenerator.h"
#include "ShaderGraph/ShaderGraphSubgraphs.h"
#include "Shaders/ShaderCompiler.h"

#include "AssetPipe/AssetManager.h"
#include "AssetPipe/AssetRegistry.h"
#include "Systems/ExecutablePath.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <regex>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace Crucible::Cook {

	namespace {

		constexpr std::string_view kGraphExtension  = ".shadergraph";
		constexpr std::string_view kSpirvSuffix     = ".spv";
		constexpr std::string_view kParamsSuffix    = ".params.json";
		constexpr std::size_t      kGuidDigits      = 16;   // AssetRegistry::GuidToString

		// The entry points of every mesh shader, and the ones a shader that moves vertices has as
		// well. Must stay the same as MaterialShaders::kVertexEntry, kFragmentEntry and
		// kMovedEntries (Engine/src/Graphics/MaterialShaders.h), in the same order, so this
		// compiles exactly what the Editor's hot reload does - and finds it in the same cache.
		constexpr std::array<const char*, 2>  kMeshEntries{ "vertMain", "fragMain" };
		constexpr std::array<const char*, 11> kMovedEntries{
			"shadowVert", "shadowCutoutVert", "shadowCutoutFrag",
			"depthNormalVert", "depthNormalFrag", "depthNormalCutoutVert", "depthNormalCutoutFrag",
			"maskVert", "maskFrag", "maskCutoutVert", "maskCutoutFrag" };

		std::string Utf8(fs::path const& path) {
			std::u8string const text = path.generic_u8string();
			return std::string(reinterpret_cast<char const*>(text.data()), text.size());
		}

		bool IsHidden(fs::path const& relative) {
			for (fs::path const& part : relative) {
				std::string const name = part.string();
				if (!name.empty() && name.front() == '.') return true;
			}
			return false;
		}

		std::string Upper(std::string text) {
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return text;
		}

		bool IsGuidName(std::string_view name) {
			return name.size() == kGuidDigits && std::all_of(name.begin(), name.end(), [](char c) {
				return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
			});
		}

		bool ReadFile(fs::path const& file, std::string& out) {
			std::ifstream in(file, std::ios::binary);
			if (!in) return false;
			out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			return true;
		}

		// Writes `bytes` to `file` unless it already holds exactly them. 1 = written, 0 = already
		// so, -1 = failed (reported).
		int WriteIfDifferent(fs::path const& file, std::string_view bytes, Report& report) {
			if (std::string existing; ReadFile(file, existing) && existing == bytes) return 0;
			std::error_code ec;
			fs::create_directories(file.parent_path(), ec);
			{
				std::ofstream out(file, std::ios::binary | std::ios::trunc);
				if (out) out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
				if (out) return 1;
			}
			report.errors.push_back("Could not write " + Utf8(file));
			return -1;
		}

		// Removes a file and the .meta beside it (the Editor's asset index makes one for files in
		// Resources too). True if the file was there.
		bool RemoveWithMeta(fs::path const& file, Report& report) {
			std::error_code ec;
			bool const existed = fs::exists(file, ec);
			for (fs::path const& each : { file, AssetRegistry::MetaPathFor(file) }) {
				if (!fs::exists(each, ec)) continue;
				fs::remove(each, ec);
				if (ec) report.errors.push_back("Could not remove " + Utf8(each) + ": " + ec.message());
			}
			return existed;
		}

		// The first compiler message that is an error, as one line.
		std::string FirstError(ShaderCompiler::Result const& result) {
			for (ShaderCompiler::Diagnostic const& d : result.diagnostics) {
				if (d.severity != ShaderCompiler::Severity::Error) continue;
				std::string line = d.file;
				if (d.line > 0) line += "(" + std::to_string(d.line) + ")";
				return line + ": " + d.message;
			}
			return result.failure.empty() ? "slangc reported errors" : result.failure;
		}

		struct GraphFile {
			fs::path    file;
			std::string virtualPath;   // "ShaderGraphs/Ember.shadergraph"
			std::string guid;          // "7C3E91B4D2A65F08", what a material's "shader" says
		};

	}   // anonymous namespace

	void CookShaderGraphs(fs::path const& assets, fs::path const& resources, ShaderOptions const& options, Report& report) {

		fs::path const outDir = resources / "Shaders";
		std::error_code ec;

		// Every graph with a GUID. One without a .meta has no name a material could use yet.
		std::vector<GraphFile> graphs;
		ShaderGraph::ClearSubgraphs();   // filled from Assets below: a deleted sub-graph must not linger (in the Editor too)
		for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end;
			!ec && it != end; it.increment(ec))
		{
			fs::path const relative = it->path().lexically_relative(assets);
			if (IsHidden(relative)) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
			if (!it->is_regular_file(ec)) continue;
			const std::string extension = AssetManager::ToLower(it->path().extension().string());

			// A sub-graph: into the library the generator finds them in, by GUID (ShaderGraphSubgraphs.h).
			if (extension == ShaderGraph::kSubgraphExtension) {
				AssetMeta meta;
				if (!AssetRegistry::ReadMeta(AssetRegistry::MetaPathFor(it->path()), meta) || meta.guid.m_Value == 0) {
					report.warnings.push_back(relative.generic_string() + " has no .meta (GUID) yet - open the editor once so it gets one; graphs cannot use it");
					continue;
				}
				std::string text;
				ReadFile(it->path(), text);
				ShaderGraph::RegisterSubgraph(AssetRegistry::GuidToString(meta.guid), it->path().filename().string(), text);
				continue;
			}
			if (extension != kGraphExtension) continue;

			GraphFile graph{ it->path(), relative.generic_string(), {} };
			AssetMeta meta;
			if (!AssetRegistry::ReadMeta(AssetRegistry::MetaPathFor(it->path()), meta) || meta.guid.m_Value == 0) {
				report.warnings.push_back(graph.virtualPath + " has no .meta (GUID) yet - open the editor once so it gets one; not compiled");
				continue;
			}
			graph.guid = AssetRegistry::GuidToString(meta.guid);
			graphs.push_back(std::move(graph));
		}
		if (ec) { report.errors.push_back("Could not read all of Assets: " + ec.message()); return; }

		ShaderCompiler::Settings settings;
		settings.shaderDir = assets / "Shaders";   // Surface.slang, SurfaceTemplate.slang, GraphNodes.slang ...
		settings.cacheDir  = options.cacheDir.empty() ? ExecutableDirectory() / "Cache" / "Shaders" : options.cacheDir;
		settings.compiler  = options.compiler.empty() ? ShaderCompiler::FindCompiler(ExecutableDirectory()) : options.compiler;
		bool const canCompile = !settings.compiler.empty() && fs::is_regular_file(settings.compiler, ec);
		ShaderCompiler const compiler(settings);

		std::unordered_set<std::string> known;   // the GUIDs of the graphs in Assets (upper case)
		for (GraphFile const& graph : graphs) {
			known.insert(Upper(graph.guid));
			fs::path const spvFile    = outDir / (graph.guid + std::string(kSpirvSuffix));
			fs::path const paramsFile = outDir / (graph.guid + std::string(kParamsSuffix));

			if (!canCompile) {
				// Keep what is there: it is all the game can have.
				if (fs::is_regular_file(spvFile, ec)) {
					report.warnings.push_back(graph.virtualPath + " was not compiled: slangc was not found (install the Vulkan SDK the build uses); "
					                          "Shaders/" + graph.guid + ".spv is kept as it is and may be out of date");
				} else {
					report.missing.push_back("Shaders/" + graph.guid + ".spv, the shader of " + graph.virtualPath +
					                         " (slangc was not found: install the Vulkan SDK the build uses)");
				}
				continue;
			}

			// Anything that stops the shader: its old outputs go, and its materials show the
			// error material in the game - the same as the Editor shows for a graph that never worked.
			auto const fail = [&](std::string const& why) {
				bool const hadOutput = RemoveWithMeta(spvFile, report);
				RemoveWithMeta(paramsFile, report);
				if (hadOutput) ++report.removed;
				report.warnings.push_back(graph.virtualPath + " has no shader: " + why +
				                          " - materials using it draw with the error material");
			};

			std::string text, error;
			ShaderGraph::Graph loaded;
			if (!ReadFile(graph.file, text)) { fail("the file could not be read"); continue; }
			if (!ShaderGraph::Load(text, loaded, error)) { fail(error); continue; }

			ShaderGraph::Generated generated = ShaderGraph::Generate(loaded);
			std::string firstStop;
			std::size_t stops = 0;
			for (ShaderGraph::Problem const& problem : generated.problems) {
				std::string const line = ShaderGraph::Describe(loaded, problem);
				if (problem.stops) {
					if (stops++ == 0) firstStop = line;
				} else {
					report.warnings.push_back(graph.virtualPath + ": " + line + " (the Output does not use this node)");
				}
			}
			if (!generated.ok) {
				fail(stops <= 1 ? firstStop : firstStop + " (and " + std::to_string(stops - 1) + " more problem(s); open it in the Shader Graph window)");
				continue;
			}

			ShaderCompiler::Request request;
			request.name   = graph.guid;
			request.source = generated.source;
			request.entryPoints.assign(kMeshEntries.begin(), kMeshEntries.end());
			if (generated.movesVertices) request.entryPoints.insert(request.entryPoints.end(), kMovedEntries.begin(), kMovedEntries.end());

			ShaderCompiler::Result const result = compiler.Compile(request);
			if (!result.success) { fail("it does not compile: " + FirstError(result)); continue; }

			std::string_view const spirv(reinterpret_cast<char const*>(result.spirv.data()), result.spirv.size() * sizeof(uint32_t));
			int const wroteCode   = WriteIfDifferent(spvFile, spirv, report);
			int const wroteParams = WriteIfDifferent(paramsFile, generated.paramsJson, report);
			if (wroteCode < 0 || wroteParams < 0) continue;
			(wroteCode > 0 || wroteParams > 0) ? ++report.compiled : ++report.unchanged;
		}

		// Outputs whose graph is gone: <GUID>.spv / <GUID>.params.json with no graph of that GUID.
		// Never a hand-written shader's (one with a .slang in Assets/Shaders), whatever its name.
		if (!canCompile) return;   // nothing could be checked: leave everything
		std::vector<fs::path> stale;
		for (fs::directory_iterator it(outDir, ec), end; !ec && it != end; it.increment(ec)) {
			if (!it->is_regular_file(ec)) continue;
			std::string const name = it->path().filename().string();
			std::string stem;
			if (name.size() > kParamsSuffix.size() && name.ends_with(kParamsSuffix)) stem = name.substr(0, name.size() - kParamsSuffix.size());
			else if (name.size() > kSpirvSuffix.size() && name.ends_with(kSpirvSuffix)) stem = name.substr(0, name.size() - kSpirvSuffix.size());
			if (!IsGuidName(stem) || known.contains(Upper(stem)) || fs::exists(settings.shaderDir / (stem + ".slang"), ec)) continue;
			stale.push_back(it->path());
		}
		for (fs::path const& file : stale) {
			if (RemoveWithMeta(file, report) && file.extension() == kSpirvSuffix) ++report.removed;
		}

	}   // CookShaderGraphs()

	void CheckMaterialShaders(fs::path const& assets, fs::path const& resources, Report& report) {

		static std::regex const field(R"re("shader"\s*:\s*"([^"]*)")re");
		std::error_code ec;
		for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end;
			!ec && it != end; it.increment(ec))
		{
			fs::path const relative = it->path().lexically_relative(assets);
			if (IsHidden(relative)) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
			if (!it->is_regular_file(ec) || AssetRegistry::IsMetaFile(it->path())) continue;
			if (AssetManager::ClassifyExtension(AssetManager::ToLower(it->path().extension().string())) != AssetType::Material) continue;

			std::string text;
			if (!ReadFile(it->path(), text)) continue;   // Cook's copy step reports a file it cannot read
			std::smatch match;
			if (!std::regex_search(text, match, field)) continue;
			std::string const shader = match[1].str();
			if (shader.empty() || fs::is_regular_file(resources / "Shaders" / (shader + std::string(kSpirvSuffix)), ec)) continue;

			std::string const why = IsGuidName(shader)
				? "its shader graph has a problem, does not compile or was deleted - see the warnings above"
				: "add Assets/Shaders/" + shader + ".slang to Engine/CompileShaders.cmake and rebuild";
			report.missing.push_back("Shaders/" + shader + ".spv, the shader " + relative.generic_string() +
			                         " uses - it would draw with the error material (" + why + ")");
		}

	}   // CheckMaterialShaders()

}
