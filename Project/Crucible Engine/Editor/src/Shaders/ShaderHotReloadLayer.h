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
* Header-only, so it is only compiled where it is included.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "Shaders/ShaderHotReload.h"

#include "AssetPipe/AssetPaths.h"
#include "Graphics/MaterialShaderParams.h"
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
			// two entry points (SurfaceTemplate.slang; Engine/CompileShaders.cmake lists them too).
			Renderer& renderer = m_core.GetRenderer();
			for (const uint32_t count = renderer.GetMaterialShaderCount(); m_watched < count; ++m_watched) {
				const MaterialShaderHandle shader{ m_watched };
				// Its parameter list too (<Name>.params.json) - not for the built-in shader, which has none.
				ShaderHotReload::ApplyParams applyParams;
				if (shader.id != 0) {
					applyParams = [this, shader](std::string_view json, std::string& error) {
						MaterialShaderParams list;
						if (!MaterialShaderParams::Parse(json, list, error)) { return false; }
						return m_core.GetRenderer().SetMaterialShaderParams(shader, list);
					};
				}
				m_hotReload.Watch({ renderer.GetMaterialShaderName(shader), { "vertMain", "fragMain" },
					[this, shader](std::span<const uint32_t> spirv, std::string& error) {
						return m_core.GetRenderer().ReloadMaterialShader(shader, spirv, error);
					},
					std::move(applyParams) });
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
