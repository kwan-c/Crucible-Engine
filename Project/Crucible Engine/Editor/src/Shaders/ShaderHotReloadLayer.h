/*!
* @file			ShaderHotReloadLayer.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Runs ShaderHotReload as a Core layer, and says which shaders it keeps up to date
*				and how each reaches the renderer.
*
* Lives in the Editor project only: the Game never compiles shaders. Pushed in Editor/src/main.cpp:
*
*     core.PushLayer(std::make_unique<Crucible::ShaderHotReloadLayer>(core));
*
* Layers are updated on the frame thread before the renderer draws, which is where
* Renderer::ReloadMeshShader must be called from.
*
* Header-only, so it is only compiled where it is included.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "Shaders/ShaderHotReload.h"

#include "AssetPipe/AssetPaths.h"
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

			// The mesh shader (DefaultSurface.slang + SurfaceTemplate.slang). Entry points as in
			// Engine/CompileShaders.cmake.
			m_hotReload.Watch({ "DefaultShader", { "vertMain", "fragMain" },
				[this](std::span<const uint32_t> spirv, std::string& error) {
					return m_core.GetRenderer().ReloadMeshShader(spirv, error);
				} });
		}

		void OnUpdate(float) override { m_hotReload.Tick(); }

		ShaderHotReload& GetHotReload() { return m_hotReload; }

	private:
		Core&           m_core;
		ShaderHotReload m_hotReload;
	};

}
