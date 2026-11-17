/*!
* @file			ShaderHotReloadLayer.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Runs ShaderHotReload as a Core layer, and says which shaders it keeps up to date
*				and how each reaches the renderer: every mesh shader the renderer has loaded -
*				the built-in one, and each one a material names ("shader" in a .mat).
*
* Lives in the Editor project only: the Game never compiles shaders. Pushed in Editor/src/main.cpp:
*
*     core.PushLayer(std::make_unique<Crucible::ShaderHotReloadLayer>(core));
*
* Layers are updated on the frame thread before the renderer draws, which is where
* Renderer::ReloadMaterialShader must be called from.
*
* A mesh shader with a source file but no .spv yet (written while the Editor runs, not built
* since) is compiled here the first time a material uses it, so it works without a rebuild.
*
* A material can also name a SHADER GRAPH (by the graph asset's GUID - ShaderGraph/ShaderGraphAsset.h).
* Such a shader has no .slang: its source and parameter list are generated here from the
* .shadergraph file, and again whenever that file is saved.
*
* Header-only, so it is only compiled where it is included.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "Shaders/ShaderHotReload.h"
#include "ShaderGraph/ShaderGraphAsset.h"

#include "AssetPipe/AssetPaths.h"
#include "Graphics/MaterialShaderParams.h"
#include "Resources/ResourceManager.h"
#include "Systems/ExecutablePath.h"
#include "Systems/Layer.h"
#include <Graphics/RenderSystem.h>
#include <Core.h>

namespace Crucible {

	class ShaderHotReloadLayer : public Layer {
	public:
		explicit ShaderHotReloadLayer(Core& core) : m_core(core) {}

		void OnAttach() override {
			ShaderCompiler::Settings settings;
			settings.shaderDir = AssetPath("Shaders");
			// Beside the program, with the pipeline cache: per machine and per build, never in the repository.
			settings.cacheDir  = ExecutableDirectory() / "Cache" / "Shaders";
			settings.compiler  = ShaderCompiler::FindCompiler(ExecutableDirectory());
			m_hotReload.Configure(std::move(settings), ResourcePath("Shaders"));
		}

		void OnUpdate(float) override {
			// Watch each mesh shader as it appears: the built-in one (handle 0) from the first
			// frame, the others when a material first names them. Every mesh shader has the same
			// two entry points (SurfaceTemplate.slang; Engine/CompileShaders.cmake lists them too);
			// one that moves vertices has its own versions of the depth-only passes' shaders too.
			Renderer& renderer = m_core.GetRenderer();
			for (const uint32_t count = renderer.GetMaterialShaderCount(); m_watched < count; ++m_watched) {
				const MaterialShaderHandle shader{ m_watched };
				ShaderHotReload::Shader watch;
				watch.name = renderer.GetMaterialShaderName(shader);
				watch.entryPoints = { "vertMain", "fragMain" };
				watch.movedEntryPoints.assign(MaterialShaders::kMovedEntries.begin(), MaterialShaders::kMovedEntries.end());
				watch.apply = [this, shader](std::span<const uint32_t> spirv, std::string& error) {
					return m_core.GetRenderer().ReloadMaterialShader(shader, spirv, error);
				};
				// Its parameter list too - not for the built-in shader, which has none.
				if (shader.id != 0) {
					watch.applyParams = [this, shader](std::string_view json, std::string& error) {
						MaterialShaderParams list;
						if (!MaterialShaderParams::Parse(json, list, error)) { return false; }
						Renderer& target = m_core.GetRenderer();
						const MaterialShaderParams before = target.GetMaterialShaderParams(shader);
						if (!target.SetMaterialShaderParams(shader, list)) { return false; }
						// What the list has gained, the materials' own files may have values for:
						// a graph's list arrives after the scene's materials have loaded.
						m_core.GetResources().LoadNewShaderValues(shader, before);
						return true;
					};
				}
				// A name that is a shader graph's GUID: generated from that graph, not read from
				// Assets/Shaders.
				if (ShaderGraphFile graph; FindShaderGraph(m_core.GetAssets(), watch.name, graph)) {
					const std::string name = watch.name;
					watch.label = graph.label;
					watch.generatedFrom = [this, name]() {
						ShaderGraphFile now;   // looked up each time: the file may have been moved
						return FindShaderGraph(m_core.GetAssets(), name, now) ? now.file : std::filesystem::path();
					};
					watch.generate = [this, name](std::string& source, std::string& paramsJson, std::string& error) {
						return GenerateShaderGraph(m_core.GetAssets(), name, source, paramsJson, error);
					};
				}
				m_hotReload.Watch(std::move(watch));
			}
			m_hotReload.Tick();
		}

		ShaderHotReload& GetHotReload() { return m_hotReload; }

	private:
		Core&           m_core;
		ShaderHotReload m_hotReload;
		uint32_t        m_watched = 0;   // mesh shaders handed to m_hotReload so far (handles 0 .. m_watched - 1)
	};

}
