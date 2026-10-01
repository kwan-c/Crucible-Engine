#pragma once
/*!
* @file			ImGuiLayer.h
* @brief		Adapts the ImGuiManager functions to Crucible::Layer so Core can drive them.
*
* Lives in the Editor project only: Engine and Game never see ImGui.
*/

#include "Systems/Layer.h"
#include "ImGui/ImGuiManager.h"
#include <Graphics/RenderSystem.h>
#include <Core.h>

namespace Crucible {

	class World;   // forward-declared: a reference doesn't need the full (heavy) ECS/World.h here

	class ImGuiLayer : public Layer {
	public:
		ImGuiLayer(Core& core) : m_context{ core.GetRenderer().GetImguiContext() }, m_core(core) {}

		void OnAttach() override { ImGuiManager::Init(m_context, m_core); }

		// Build the UI for this frame (no GPU work yet).
		void OnUpdate(float dt) override {
			ImGuiManager::BeginFrame();
			ImGuiManager::Update(dt);
			ImGuiManager::Render();
		}

		// Called by the renderer inside the open render pass.
		void OnRender() override { ImGuiManager::Draw(m_core.GetRenderer()); }

		void OnDetach() override { ImGuiManager::Exit(); }

	private:
		ImGuiVulkanContext		m_context;
		Core& m_core;
	};

}
