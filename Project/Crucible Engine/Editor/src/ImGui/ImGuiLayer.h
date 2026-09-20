#pragma once
/*!
* @file			ImGuiLayer.h
* @brief		Adapts the ImGuiManager functions to Crucible::Layer so Core can drive them.
*
* Lives in the Editor project only: Engine and Game never see ImGui.
*/

#include "Systems/Layer.h"
#include "ImGui/ImGuiManager.h"

namespace Crucible {

	class ImGuiLayer : public Layer {
	public:
		explicit ImGuiLayer(Core& core) : m_core{ core } {}

		void OnAttach() override { ImGuiManager::Init(m_core); }

		// Build the UI for this frame (no GPU work yet).
		void OnUpdate(float) override {
			ImGuiManager::BeginFrame();
			ImGuiManager::Update();
			ImGuiManager::Render();
		}

		// Called by the renderer inside the open render pass.
		void OnRender() override { ImGuiManager::Draw(m_core); }

		void OnDetach() override { ImGuiManager::Exit(); }

	private:
		Core& m_core;
	};

}
