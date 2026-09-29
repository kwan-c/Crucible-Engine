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

namespace Crucible {

	class ImGuiLayer : public Layer {
	public:
		explicit ImGuiLayer(Renderer& ren) : m_context{ ren.GetImguiContext()}, m_ren{ren} {}

		void OnAttach() override { ImGuiManager::Init(m_context, m_ren); }

		// Build the UI for this frame (no GPU work yet).
		void OnUpdate(float dt) override {
			ImGuiManager::BeginFrame();
			ImGuiManager::Update(dt);
			ImGuiManager::Render();
		}

		// Called by the renderer inside the open render pass.
		void OnRender() override { ImGuiManager::Draw(m_ren); }

		void OnDetach() override { ImGuiManager::Exit(); }

	private:
		ImGuiVulkanContext		m_context;
		Renderer&				m_ren;
	};

}
