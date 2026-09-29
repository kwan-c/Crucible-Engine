#pragma once
/*!
* @file			ViewportPanel.h
* @brief		Shared plumbing for an ImGui panel that displays a Renderer::Viewport.
*
* Both the Scene view and the Game view are "a resizable panel showing a rendered image with
* its own camera" - this class is the part they have in common: creating/resizing/destroying the
* Renderer-side viewport and caching the ImGui texture handle for it. Panels compose this (or
* inherit from it) and are left to do only what's actually different between them: which camera
* controller drives the view, and what toolbar/gizmos sit on top of the image.
*
* Lives in the Editor project only, same as everything else that touches ImGui.
*/

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <Graphics/RenderSystem.h>

namespace Crucible {

	class ViewportPanel {
	public:
		explicit ViewportPanel(Renderer& renderer) : m_renderer{ renderer } {}

		~ViewportPanel() { Shutdown(); }

		// Call once the panel's content region is known to be non-zero (i.e. from inside the
		// panel's Update(), not from Init() - ImGui doesn't know the dock layout that early).
		void EnsureCreated(uint32_t width, uint32_t height) {
			if (!m_handle.IsValid() && width > 0 && height > 0) {
				m_handle = m_renderer.CreateViewport(width, height);
				RefreshTexture();
			}
		}

		// Call every frame with the panel's current content-region size. No-ops if unchanged.
		void Resize(uint32_t width, uint32_t height) {
			if (!m_handle.IsValid()) { EnsureCreated(width, height); return; }
			if (width == m_lastWidth && height == m_lastHeight) { return; }

			m_renderer.ResizeViewport(m_handle, width, height);
			RefreshTexture();
			m_lastWidth = width;
			m_lastHeight = height;
		}

		// Draws the rendered scene into the current ImGui window, filling `size`.
		void DrawImage(ImVec2 size) const {
			if (m_textureId) {
				ImGui::Image(reinterpret_cast<ImTextureID>(m_textureId), size);
			}
		}

		Camera& GetCamera() { return m_renderer.GetViewportCamera(m_handle); }

		// False until the first Resize()/EnsureCreated() call with a non-zero size has actually
		// stood up the offscreen viewport (e.g. the panel is collapsed, or hasn't been laid out
		// by the dock system yet on its very first frame). DrawImage() already no-ops safely
		// before that, but GetCamera() does NOT - GetViewportCamera() throws on an unset handle -
		// so callers must check this before touching GetCamera().
		bool IsValid() const { return m_handle.IsValid(); }

		void Shutdown() {
			if (m_textureId) {
				ImGui_ImplVulkan_RemoveTexture(m_textureId);
				m_textureId = nullptr;
			}
			if (m_handle.IsValid()) {
				m_renderer.DestroyViewport(m_handle);
				m_handle = {};
			}
		}

	private:
		// Re-registers the (possibly just-recreated) color image with ImGui's Vulkan backend.
		// The old descriptor set is invalid the moment ResizeViewport() swaps the image, so it
		// must be dropped and re-added rather than reused.
		void RefreshTexture() {
			if (m_textureId) {
				ImGui_ImplVulkan_RemoveTexture(m_textureId);
			}
			m_textureId = ImGui_ImplVulkan_AddTexture(
				m_renderer.GetViewportSampler(),
				m_renderer.GetViewportColorView(m_handle),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		Renderer&       m_renderer;
		ViewportHandle  m_handle{};
		VkDescriptorSet m_textureId = nullptr;   // ImGui_ImplVulkan's ImTextureID is this cast to void*
		uint32_t        m_lastWidth = 0, m_lastHeight = 0;
	};

}   // NAMESPACE CRUCIBLE

/*
    ---- Real usage: ImGui/Panels/ImGuiPanel_SceneView.cpp and ImGuiPanel_GameView.cpp ----

    Both panels compose a ViewportPanel member (constructed with the Renderer&, threaded down
    from ImGuiLayer -> ImGuiManager::Init() -> EditorManager::Init() -> the panel's own
    constructor) and, each Update(dt):

        1. Resize() to the panel's current content-region size (SceneView: the whole image area;
           GameView: the letterboxed rect, since that's the resolution the game camera should
           actually render at).
        2. Check IsValid() before touching GetCamera() - false for a frame or two at start-up, or
           whenever the panel is fully collapsed.
        3. Drive the camera:
             - SceneView owns a FlyCameraController and feeds it CameraInput built from raw ImGui
               mouse/key state while RMB is held over the viewport (own, independent camera).
             - GameView instead copies Renderer::GetCamera() (the main/gameplay camera) into
               m_viewport.GetCamera() every frame - see the comment above that line in
               ImGuiPanel_GameView.cpp for why a copy is needed rather than a shared reference.
        4. DrawImage() into the window, on top of an EditorWidgets::DrawViewportPlaceholder()
           fallback so there's something sane on screen before the first real frame lands.
*/
