#pragma once
/*************************************************************************
* \file:         ViewportPanel.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Shared plumbing for an ImGui panel that displays a
*                Renderer::Viewport.
*
* \details
*                Both the Scene view and the Game view are "a resizable
*                panel showing a rendered image with its own camera" - this
*                class is the part they have in common: creating/resizing/
*                destroying the Renderer-side viewport and caching the
*                ImGui texture handle for it. Panels compose this (or
*                inherit from it) and are left to do only what's actually
*                different between them: which camera controller drives
*                the view, and what toolbar/gizmos sit on top of the
*                image.
*
*                Lives in the Editor project only, same as everything else
*                that touches ImGui. See the usage note at the bottom of
*                this file for how ImGuiPanel_SceneView.cpp/
*                ImGuiPanel_GameView.cpp actually consume this class.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <Graphics/RenderSystem.h>

namespace Crucible {

	/*************************************************************************
	 \brief
		Shared plumbing for an ImGui panel that displays a Renderer offscreen
		Viewport: creation, resizing, teardown, and the cached ImGui texture
		handle for the current viewport image.

	 \details
		See the file header for why this exists as shared plumbing rather
		than being duplicated in the Scene/Game view panels, and the usage
		note at the bottom of this file for the exact call pattern both
		panels follow.
	 *************************************************************************/
	class ViewportPanel {
	public:
		/*********************************************************************
		 \brief  Constructs a panel bound to the given Renderer. Creates no
				 GPU resources yet - see EnsureCreated()/Resize().
		 \param[in] renderer The Renderer to create/own the offscreen
					viewport through.
		 *********************************************************************/
		explicit ViewportPanel(Renderer& renderer) : m_renderer{ renderer } {}

		/*********************************************************************
		 \brief  Tears down the offscreen viewport and its ImGui texture, if
				 any (see Shutdown()).
		 *********************************************************************/
		~ViewportPanel() { Shutdown(); }

		/*********************************************************************
		 \brief
			Creates the offscreen viewport the first time a non-zero size is
			known.

		 \param[in] width  Panel content-region width, in pixels.
		 \param[in] height Panel content-region height, in pixels.

		 \note
			Call once the panel's content region is known to be non-zero
			(i.e. from inside the panel's Update(), not from Init() - ImGui
			doesn't know the dock layout that early). No-ops once the
			viewport already exists.
		 *********************************************************************/
		void EnsureCreated(uint32_t width, uint32_t height) {
			if (!m_handle.IsValid() && width > 0 && height > 0) {
				m_handle = m_renderer.CreateViewport(width, height);
				m_renderer.SetViewportDebugDraw(m_handle, m_debugDraw);   // apply a SetDebugDraw() made before creation
				m_renderer.SetViewportOutline(m_handle, m_outline);       // likewise SetOutline()
				RefreshTexture();
			}
		}

		/*********************************************************************
		 \brief
			Resizes the offscreen viewport to match the panel's current
			content-region size.

		 \param[in] width  Panel content-region width, in pixels.
		 \param[in] height Panel content-region height, in pixels.

		 \note
			Call every frame with the panel's current content-region size.
			No-ops if unchanged, and defers to EnsureCreated() if the
			viewport doesn't exist yet.
		 *********************************************************************/
		void Resize(uint32_t width, uint32_t height) {
			if (!m_handle.IsValid()) { EnsureCreated(width, height); return; }
			if (width == m_lastWidth && height == m_lastHeight) { return; }

			m_renderer.ResizeViewport(m_handle, width, height);
			RefreshTexture();
			m_lastWidth = width;
			m_lastHeight = height;
		}

		/*********************************************************************
		 \brief  Draws the rendered scene into the current ImGui window.
		 \param[in] size Size, in ImGui units, to draw the image at.
		 \note  No-ops (draws nothing) before the viewport's first texture
				has been registered.
		 *********************************************************************/
		void DrawImage(ImVec2 size) const {
			if (m_textureId) {
				ImGui::Image(reinterpret_cast<ImTextureID>(m_textureId), size);
			}
		}

		/*********************************************************************
		 \brief  Accesses the offscreen viewport's own independent camera.
		 \return Reference to the viewport's Camera.
		 \note  Throws if IsValid() is false - see IsValid()'s own note.
		 *********************************************************************/
		Camera& GetCamera() { return m_renderer.GetViewportCamera(m_handle); }

		/*********************************************************************
		 \brief  Shows or hides DebugDraw lines in this view (see
				 Renderer::SetViewportDebugDraw). Safe to call before the
				 viewport exists; applied when it is created.
		 \param[in] enabled true to draw debug lines here.
		 *********************************************************************/
		void SetDebugDraw(bool enabled) {
			m_debugDraw = enabled;
			if (m_handle.IsValid()) { m_renderer.SetViewportDebugDraw(m_handle, enabled); }
		}

		/*********************************************************************
		 \brief  Whether debug lines are drawn in this view.
		 \return The last value given to SetDebugDraw().
		 *********************************************************************/
		bool GetDebugDraw() const { return m_debugDraw; }

		/*********************************************************************
		 \brief  Shows or hides the selection outline in this view (see
				 Renderer::SetViewportOutline / SetOutlinedObject). Safe to
				 call before the viewport exists.
		 *********************************************************************/
		void SetOutline(bool enabled) {
			m_outline = enabled;
			if (m_handle.IsValid()) { m_renderer.SetViewportOutline(m_handle, enabled); }
		}

		/// The last value given to SetOutline().
		bool GetOutline() const { return m_outline; }

		/*********************************************************************
		 \brief  Last frame's culling numbers for this view (zeros until the
				 viewport exists).
		 \return The view's RenderCullStats.
		 *********************************************************************/
		RenderCullStats GetCullStats() const {
			return m_handle.IsValid() ? m_renderer.GetViewportCullStats(m_handle) : RenderCullStats{};
		}

		/*********************************************************************
		 \brief
			Whether the offscreen viewport has actually been created yet.

		 \return
			true once the first Resize()/EnsureCreated() call with a
			non-zero size has stood up the offscreen viewport.

		 \note
			False for a frame or two at start-up, or whenever the panel is
			fully collapsed (e.g. the panel hasn't been laid out by the
			dock system yet on its very first frame). DrawImage() already
			no-ops safely before that, but GetCamera() does NOT -
			GetViewportCamera() throws on an unset handle - so callers must
			check this before touching GetCamera().
		 *********************************************************************/
		bool IsValid() const { return m_handle.IsValid(); }

		/*********************************************************************
		 \brief  Releases the ImGui texture registration and destroys the
				 offscreen viewport, if either exists. Safe to call more
				 than once.
		 *********************************************************************/
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
		bool            m_debugDraw = false;
		bool            m_outline = false;
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
