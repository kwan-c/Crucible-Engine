/*!
* @file			ImGuiPanel_SceneView.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's Scene view: the free-camera view of the level you are editing.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_SceneView.h"
#include "Scene/SceneManager.h"
#include "ImGui/EditorWidgets.h"
#include "ImGui/EditorTheme.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Graphics/DebugDraw.h"
#include "Graphics/DebugDrawSystem.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"
#include "Undo/EntityCommands.h"
#include "Resources/ResourceManager.h"
#include "Spatial/Bounds.h"

#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include <algorithm>

void Crucible::SceneView::Init() {
	m_panelName = "Scene";

	// Centre of the layout, tabbed with the Game view. Scene starts as the visible tab.
	m_docked = true;
	m_dockArea = DockArea::Center;
	m_makeFocus = true;

	m_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	// Debug lines (colliders, velocities, grid...) show in the Scene view only: the Game view's
	// viewport keeps the Renderer's default (off), and the Game build has no viewports at all.
	m_viewport.SetDebugDraw(true);
	m_viewport.SetOutline(true);
}

void Crucible::SceneView::Update(float dt) {
	// Cursor on the gizmo (or dragging it), going by last frame's Manipulate. ImGuizmo only
	// takes a click when NO ImGui item is hovered (ImGuizmo.cpp, CanActivate:
	// !ImGui::IsAnyItemHovered()), and the invisible viewport button below always is - so while
	// the gizmo is hot that button is left out (see below). NoMove keeps a floating Scene window
	// from being dragged around by the same click once nothing else claims it.
	bool const gizmoHot = m_gizmoDrawn && ImGuizmo::IsOver();

	// Zero padding so the scene image fills the window edge to edge.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	bool const visible = ImGui::Begin(m_panelName.c_str(), nullptr, m_flags | (gizmoHot ? ImGuiWindowFlags_NoMove : 0));
	ImGui::PopStyleVar();

	if (!visible) {
		// Tab hidden behind another tab, or window collapsed: nothing to render this frame.
		m_hovered = m_focused = false;
		m_flying = false;
		m_gizmoDrawn = false;
		EndGizmoDrag();
		ImGui::End();
		return;
	}

	DrawToolbar();

	ImVec2 const avail = ImGui::GetContentRegionAvail();
	ImVec2 const min = ImGui::GetCursorScreenPos();
	ImVec2 const max(min.x + avail.x, min.y + avail.y);
	m_viewportSize = avail;

	uint32_t const width = static_cast<uint32_t>(std::max(avail.x, 0.0f));
	uint32_t const height = static_cast<uint32_t>(std::max(avail.y, 0.0f));
	m_viewport.Resize(width, height);

	// Placeholder first (also covers the gap on the very first frame or two, before the
	// Renderer has actually created the offscreen image), then the real image on top of it.
	EditorWidgets::DrawViewportPlaceholder(min, max, "Scene View", "Waiting for the renderer...");
	ImGui::SetCursorScreenPos(min);
	m_viewport.DrawImage(avail);

	// One invisible item covering the image area, so hover / click / drop queries work on it.
	// Not while the gizmo is hot: a hovered button stops ImGuizmo from ever starting a drag
	// (see gizmoHot above). A Dummy (no id, so never "hovered") keeps the layout, and still
	// gives the drag-drop target below an item to attach to.
	ImGui::SetCursorScreenPos(min);
	ImVec2 const viewportItemSize(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f));
	if (gizmoHot) {
		ImGui::Dummy(viewportItemSize);
		m_hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max);
	} else {
		ImGui::InvisibleButton("##SceneViewport", viewportItemSize,
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		m_hovered = ImGui::IsItemHovered();
	}
	m_focused = ImGui::IsWindowFocused();

	// Fly-camera latch: starts the moment RMB goes down while the cursor is over the viewport,
	// and keeps going - even once the drag carries the cursor outside the panel's rect, which
	// happens constantly with a big mouse-look swing - until RMB is released. Deliberately not
	// ImGui::IsItemActive(): that goes true for whichever registered button pressed the item
	// first, so it can't tell a right-drag apart from a left-drag on its own.
	bool const rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	if (!rightMouseDown) {
		m_flying = false;
	} else if (m_hovered && !m_flying) {
		m_flying = true;
	}

	// Accept assets dragged in from the Asset Browser.
	// Editor-only overlays: selection mirror for DebugDrawSystem, then icons + click picking.
	SyncSelection();
	m_iconHits.clear();
	if (m_showIcons) { DrawIcons(min, avail); }
	if (m_showStats) { DrawStatsOverlay(min); }
	DrawGizmo(min, avail);   // before picking, so a click on the gizmo is known to be the gizmo's
	HandleClickPicking(min, avail);
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { m_gizmoClaimedClick = false; }

	if (ImGui::BeginDragDropTarget()) {
		if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload)) {
			char const* virtualPath = static_cast<char const*>(payload->Data);
			CRU_INFO("[Scene] Asset dropped into scene: {} (spawning not implemented yet)", virtualPath);
		}
		ImGui::EndDragDropTarget();
	}

	UpdateEditorCamera(dt);

	ImGui::End();
}

void Crucible::SceneView::UpdateEditorCamera(float dt) {
	if (!m_viewport.IsValid()) { return; }   // not stood up yet (panel was 0-sized this frame)

	// Frame Selected glide. Grabbing the camera (RMB) cancels it, so the two never fight.
	if (m_focusT >= 0.0f) {
		if (m_flying) {
			m_focusT = -1.0f;
		} else {
			constexpr float kFocusDuration = 0.25f;   // seconds
			m_focusT = std::min(m_focusT + dt / kFocusDuration, 1.0f);
			float const s = m_focusT * m_focusT * (3.0f - 2.0f * m_focusT);   // smoothstep: ease in and out
			m_viewport.GetCamera().position = glm::mix(m_focusFrom, m_focusTo, s);
			if (m_focusT >= 1.0f) { m_focusT = -1.0f; }
		}
	}

	CameraInput input{};
	input.active = m_flying;

	if (m_flying) {
		// While flying, hide the OS cursor and read raw mouse delta / wheel / WASDQE + Shift
		// straight from ImGui - the engine's own Input system only tracks keys today (see
		// Engine/src/Input/InputSystem.h) and isn't attached/updated by Core yet, and this is
		// editor-only UI input anyway, so there's no reason to route it through the gameplay
		// input system.
		//ImGui::SetMouseCursor(ImGuiMouseCursor_None);
		EditorWidgets::RequestMouseLock();

		ImGuiIO const& io = ImGui::GetIO();
		input.look = { io.MouseDelta.x, io.MouseDelta.y };
		input.zoom = io.MouseWheel;
		input.boost = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);

		glm::vec3 move{ 0.0f };
		if (ImGui::IsKeyDown(ImGuiKey_W)) move.z += 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_S)) move.z -= 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_D)) move.x += 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_A)) move.x -= 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_E)) move.y += 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1.0f;
		input.move = move;
	}

	m_flyController.Update(m_viewport.GetCamera(), input, dt);
}

void Crucible::SceneView::FrameSelected() {
	if (!m_viewport.IsValid()) { return; }   // GetCamera() is not safe before this

	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (!inspector) { return; }

	// World-space box around everything selected. A mesh contributes its real bounds; anything
	// without one (lights, cameras, empties) a small box around its position.
	constexpr float kPointHalfSize = 0.5f;
	AABB box;
	bool any = false;
	auto add = [&](AABB const& b) { box = any ? AABB::Union(box, b) : b; any = true; };

	for (Entity const entity : inspector->GetSelection()) {
		if (!m_world.IsAlive(entity)) { continue; }
		WorldTransform const* wt = m_world.Read<WorldTransform>(entity);
		if (!wt || !wt->valid) { continue; }   // created this frame: not placed yet

		bool usedMesh = false;
		if (MeshRenderer const* mr = m_world.Read<MeshRenderer>(entity)) {
			// Entities loaded from a scene carry only meshAsset (the handle is resolved per frame
			// by RenderExtractSystem), so resolve it the same way here.
			MeshHandle const mesh = (mr->meshAsset != 0) ? m_resources.GetMesh(mr->meshAsset) : mr->mesh;
			glm::vec3 mn, mx;
			if (mesh.IsValid() && m_renderer.GetMeshBounds(mesh, mn, mx)) {
				add(AABB::Transform(AABB(mn, mx), wt->matrix));
				usedMesh = true;
			}
		}
		if (!usedMesh) {
			glm::vec3 const p(wt->matrix[3]);
			add(AABB(p - glm::vec3(kPointHalfSize), p + glm::vec3(kPointHalfSize)));
		}
	}
	if (!any) { return; }

	// Back off along the current view direction until a sphere around the box fits both the
	// vertical and the horizontal field of view, plus a little margin.
	Camera& camera = m_viewport.GetCamera();
	float const radius = std::max(glm::length(box.Extents()), 0.25f);
	float const halfV = glm::radians(camera.fovY) * 0.5f;
	float const halfH = std::atan(std::tan(halfV) * camera.aspect);
	float const half = std::min(halfV, halfH);
	float const distance = std::max(radius / std::sin(half) * 1.1f, camera.nearZ + radius);

	m_focusFrom = camera.position;
	m_focusTo = box.Center() - camera.Forward() * distance;
	m_focusT = 0.0f;
}

void Crucible::SceneView::DrawToolbar() {
	EditorWidgets::BeginToolbarRow();

	using EditorWidgets::ToolbarToggle;
	if (ToolbarToggle("Move", m_gizmoMode == GizmoMode::Translate))  m_gizmoMode = GizmoMode::Translate;
	ImGui::SameLine();
	if (ToolbarToggle("Rotate", m_gizmoMode == GizmoMode::Rotate))   m_gizmoMode = GizmoMode::Rotate;
	ImGui::SameLine();
	if (ToolbarToggle("Scale", m_gizmoMode == GizmoMode::Scale))     m_gizmoMode = GizmoMode::Scale;
	ImGui::SameLine();
	// Axes for Move and Rotate: the world's, or the object's own. Scale always uses its own.
	if (ToolbarToggle(m_gizmoSpace == GizmoSpace::Local ? "Local" : "World", m_gizmoSpace == GizmoSpace::Local))
		m_gizmoSpace = (m_gizmoSpace == GizmoSpace::Local) ? GizmoSpace::World : GizmoSpace::Local;
	if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Gizmo axes: World or Local. Hold Ctrl while dragging to snap."); }

	// Unity-style W / E / R shortcuts while the Scene view is focused (and no text box is active).
	// Suppressed while flying: W/E are also the fly-camera's forward/up-move keys, and R would be
	// a very unwelcome surprise mid-flight.
	if (!m_flying && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_gizmoMode = GizmoMode::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_gizmoMode = GizmoMode::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_gizmoMode = GizmoMode::Scale;
		if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FrameSelected();
	}

	ImGui::SameLine();
	if (ToolbarToggle("Gizmos", m_viewport.GetDebugDraw() || m_showIcons)) { ImGui::OpenPopup("##SceneGizmos"); }
	if (ImGui::BeginPopup("##SceneGizmos")) {
		DrawGizmoMenu();
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (ToolbarToggle("Rendering", false)) { ImGui::OpenPopup("##SceneRendering"); }
	if (ImGui::BeginPopup("##SceneRendering")) {
		DrawRenderingMenu();
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	ImGui::TextDisabled("|  %d x %d", static_cast<int>(m_viewportSize.x), static_cast<int>(m_viewportSize.y));
	if (m_viewport.IsValid()) {
		// Last frame's culling for this view: drawn / registered (+ immediate demo items).
		const RenderCullStats stats = m_viewport.GetCullStats();
		ImGui::SameLine();
		ImGui::TextDisabled("|  Drawn %u / %u", stats.proxiesVisible + stats.immediateVisible, stats.proxies + stats.immediate);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Entities: %u of %u in view (%u moving)\nImmediate items: %u of %u\n"
				"Draw calls: %u (instanced)\nShadow casters: %u in %u draws\n"
				"Particles: %u in %u draws\nPoint/spot lights in view: %u",
				stats.proxiesVisible, stats.proxies, stats.movingProxies, stats.immediateVisible, stats.immediate,
				stats.drawCalls, stats.shadowCasters, stats.shadowDrawCalls,
				stats.particles, stats.particleDrawCalls, stats.clusteredLights);
		}
	}

	ImGui::SameLine();

	bool isChanged = m_scenes.IsDirty();
	ImGui::TextDisabled("| ");
	ImGui::SameLine();

	if (isChanged) {
		ImGui::TextColored(EditorTheme::GetPalette().warning, "Current Scene: %s *", m_scenes.CurrentName().c_str());
	}
	else {
		ImGui::TextDisabled("Current Scene: %s", m_scenes.CurrentName().c_str());
	}

	if (m_flying) {
		ImGui::SameLine();
		ImGui::TextDisabled("|  Flying (RMB + WASD/QE, Shift to boost)");
	}

	EditorWidgets::EndToolbarRow();
}

// ------------------------------------------------------------------------------------------------- //
//  EDITOR-ONLY DEBUG VISUALS
// ------------------------------------------------------------------------------------------------- //

namespace {

	ImU32 ToImColor(const glm::vec3& c, float alpha = 1.0f) {
		auto byte = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
		return IM_COL32(byte(c.r), byte(c.g), byte(c.b), byte(alpha));
	}

	// World point -> screen pixel inside the viewport image. The Renderer draws with a flipped
	// (negative-height) Vulkan viewport, so NDC +Y is the TOP of the image.
	bool ProjectToScreen(const glm::mat4& viewProj, const glm::vec3& p, ImVec2 viewMin, ImVec2 viewSize, ImVec2& out, float& depth) {
		const glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
		if (clip.w <= 1e-4f) { return false; }                       // behind the camera
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		if (ndc.z < 0.0f || ndc.z > 1.0f) { return false; }          // outside near/far
		out = ImVec2(viewMin.x + (ndc.x * 0.5f + 0.5f) * viewSize.x,
		             viewMin.y + (0.5f - ndc.y * 0.5f) * viewSize.y);
		depth = clip.w;
		return out.x >= viewMin.x - 16.0f && out.x <= viewMin.x + viewSize.x + 16.0f
		    && out.y >= viewMin.y - 16.0f && out.y <= viewMin.y + viewSize.y + 16.0f;
	}

	// Glyphs, ~20 px, drawn with plain ImDrawList shapes (no icon textures to ship).
	void GlyphSun(ImDrawList* dl, ImVec2 p, ImU32 col) {
		dl->AddCircleFilled(p, 4.5f, col);
		for (int i = 0; i < 8; ++i) {
			const float a = 0.785398f * static_cast<float>(i);
			const float cx = std::cos(a), sy = std::sin(a);
			dl->AddLine(ImVec2(p.x + cx * 7.0f, p.y + sy * 7.0f), ImVec2(p.x + cx * 10.5f, p.y + sy * 10.5f), col, 1.5f);
		}
	}
	void GlyphBulb(ImDrawList* dl, ImVec2 p, ImU32 col) {
		dl->AddCircleFilled(ImVec2(p.x, p.y - 2.0f), 6.0f, col);
		dl->AddRectFilled(ImVec2(p.x - 3.0f, p.y + 4.0f), ImVec2(p.x + 3.0f, p.y + 8.5f), IM_COL32(190, 190, 190, 255), 1.0f);
	}
	void GlyphSpot(ImDrawList* dl, ImVec2 p, ImU32 col) {
		dl->AddRectFilled(ImVec2(p.x - 3.0f, p.y - 9.0f), ImVec2(p.x + 3.0f, p.y - 4.0f), IM_COL32(190, 190, 190, 255), 1.0f);
		dl->AddTriangleFilled(ImVec2(p.x, p.y - 5.0f), ImVec2(p.x - 7.5f, p.y + 8.0f), ImVec2(p.x + 7.5f, p.y + 8.0f), col);
	}
	void GlyphGlobe(ImDrawList* dl, ImVec2 p, ImU32 col) {
		dl->AddCircle(p, 8.0f, col, 0, 1.5f);
		dl->AddEllipse(p, ImVec2(3.5f, 8.0f), col, 0.0f, 0, 1.2f);
		dl->AddLine(ImVec2(p.x - 8.0f, p.y), ImVec2(p.x + 8.0f, p.y), col, 1.2f);
	}
	void GlyphCamera(ImDrawList* dl, ImVec2 p, ImU32 col) {
		dl->AddRect(ImVec2(p.x - 8.0f, p.y - 4.5f), ImVec2(p.x + 3.0f, p.y + 4.5f), col, 1.5f, 0, 1.5f);          // body
		dl->AddTriangle(ImVec2(p.x + 3.0f, p.y), ImVec2(p.x + 8.0f, p.y - 4.0f), ImVec2(p.x + 8.0f, p.y + 4.0f), col, 1.5f);   // lens hood
	}
	void GlyphDiamond(ImDrawList* dl, ImVec2 p, ImU32 col) {
		dl->AddQuad(ImVec2(p.x, p.y - 6.0f), ImVec2(p.x + 6.0f, p.y), ImVec2(p.x, p.y + 6.0f), ImVec2(p.x - 6.0f, p.y), col, 1.5f);
	}

	constexpr float kIconRadius = 12.0f;   // backdrop disc + click radius, in pixels

}   // anonymous namespace

void Crucible::SceneView::SyncSelection() {
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	const Entity active = inspector ? inspector->GetSelectedEntity() : kNullEntity;
	DebugDrawSystem::SetSelectedEntity(active);

	// Every selected entity gets the outline. Picks up selections made elsewhere (Hierarchy,
	// Inspector) too; the renderer ignores the call when the list hasn't changed.
	std::vector<std::uint64_t> outlined;
	if (inspector) {
		for (Entity const e : inspector->GetSelection()) { outlined.push_back(EntityToUserData(e)); }
	}
	m_renderer.SetOutlinedObjects(outlined);
}

void Crucible::SceneView::DrawIcons(ImVec2 viewMin, ImVec2 viewSize) {

	if (!m_viewport.IsValid() || viewSize.x <= 0.0f || viewSize.y <= 0.0f) { return; }

	Camera& camera = m_viewport.GetCamera();
	camera.SetAspect(viewSize.x, viewSize.y);   // match the image this frame (the Renderer does the same)
	const glm::mat4 viewProj = camera.GetProjection() * camera.GetView();
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	const Entity active = DebugDrawSystem::GetSelectedEntity();

	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(viewMin, ImVec2(viewMin.x + viewSize.x, viewMin.y + viewSize.y), true);

	// Backdrop + selection ring + glyph, and remember where it went for picking.
	auto place = [&](Entity entity, const WorldTransform& wt, auto&& glyph) {
		if (!wt.valid) { return; }
		ImVec2 screen; float depth;
		if (!ProjectToScreen(viewProj, glm::vec3(wt.matrix[3]), viewMin, viewSize, screen, depth)) { return; }
		dl->AddCircleFilled(screen, kIconRadius, IM_COL32(18, 18, 22, 170));
		// Selection ring: solid on the active entity, fainter on the rest of a multi-selection.
		if (entity == active) { dl->AddCircle(screen, kIconRadius + 1.5f, IM_COL32(255, 140, 26, 255), 0, 2.0f); }
		else if (inspector && inspector->IsSelected(entity)) { dl->AddCircle(screen, kIconRadius + 1.5f, IM_COL32(255, 140, 26, 150), 0, 2.0f); }
		glyph(screen);
		m_iconHits.push_back({ entity, screen, depth });
	};

	m_world.CreateQuery<const WorldTransform, const Light>().ForEach(
		[&](Entity e, const WorldTransform& wt, const Light& light) {
			const ImU32 col = light.enabled ? ToImColor(light.colour) : IM_COL32(128, 128, 128, 255);
			place(e, wt, [&](ImVec2 p) {
				switch (light.type) {
					case LightType::Directional: GlyphSun(dl, p, col);  break;
					case LightType::Point:       GlyphBulb(dl, p, col); break;
					case LightType::Spot:        GlyphSpot(dl, p, col); break;
				}
			});
		});

	m_world.CreateQuery<const WorldTransform, const Skybox>().ForEach(
		[&](Entity e, const WorldTransform& wt, const Skybox&) {
			place(e, wt, [&](ImVec2 p) { GlyphGlobe(dl, p, IM_COL32(120, 190, 255, 255)); });
		});

	m_world.CreateQuery<const WorldTransform, const CameraComponent>().ForEach(
		[&](Entity e, const WorldTransform& wt, const CameraComponent& cam) {
			const ImU32 col = cam.enabled ? IM_COL32(235, 235, 235, 255) : IM_COL32(128, 128, 128, 255);
			place(e, wt, [&](ImVec2 p) { GlyphCamera(dl, p, col); });
		});

	// Anything else with nothing to click on in the scene (empties, collider-only, characters...).
	m_world.BuildQuery<const WorldTransform>().Exclude<MeshRenderer, Light, Skybox, CameraComponent>().Build().ForEach(
		[&](Entity e, const WorldTransform& wt) {
			place(e, wt, [&](ImVec2 p) { GlyphDiamond(dl, p, IM_COL32(200, 200, 200, 255)); });
		});

	dl->PopClipRect();
}

void Crucible::SceneView::Select(Entity entity) {
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) {
		ImGuiIO const& io = ImGui::GetIO();
		// Ctrl+click toggles, Shift+click adds; with either held, a click on empty space keeps
		// the selection instead of clearing it. A plain click selects just what was hit.
		if (io.KeyCtrl)       { if (entity.IsValid()) { inspector->ToggleSelected(entity); } }
		else if (io.KeyShift) { if (entity.IsValid()) { inspector->AddToSelection(entity); } }
		else                  { inspector->SetSelectedEntity(entity); }
	}
	SyncSelection();
}

void Crucible::SceneView::HandleClickPicking(ImVec2 viewMin, ImVec2 viewSize) {

	if (!m_hovered || m_flying || !m_viewport.IsValid()) { return; }
	if (m_gizmoClaimedClick || ImGuizmo::IsUsing()) { return; }   // that press grabbed the gizmo
	if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) { return; }

	// A click, not the end of a drag (camera orbit, future gizmo drags).
	const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
	if (drag.x * drag.x + drag.y * drag.y > 16.0f) { return; }

	const ImVec2 mouse = ImGui::GetMousePos();
	const IconHit* best = nullptr;
	float bestDist = kIconRadius * kIconRadius;
	for (const IconHit& hit : m_iconHits) {   // (empty when icons are hidden - falls through to the ray)
		const float dx = hit.pos.x - mouse.x, dy = hit.pos.y - mouse.y;
		const float d = dx * dx + dy * dy;
		// Nearest on screen; overlapping icons -> the one nearer the camera.
		if (d < bestDist || (best && d == bestDist && hit.depth < best->depth)) { best = &hit; bestDist = d; }
	}
	if (best) { Select(best->entity); return; }

	// No icon: cast a ray from the cursor through the scene. The image is drawn with a flipped
	// (negative-height) viewport, so screen top = NDC +1.
	if (viewSize.x <= 0.0f || viewSize.y <= 0.0f) { return; }
	Camera& camera = m_viewport.GetCamera();
	const float ndcX = (mouse.x - viewMin.x) / viewSize.x * 2.0f - 1.0f;
	const float ndcY = 1.0f - (mouse.y - viewMin.y) / viewSize.y * 2.0f;
	const glm::mat4 invViewProj = glm::inverse(camera.GetProjection() * camera.GetView());
	glm::vec4 nearPoint = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);   // Vulkan depth: near = 0
	glm::vec4 farPoint  = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
	nearPoint /= nearPoint.w;
	farPoint  /= farPoint.w;

	const glm::vec3 origin(nearPoint);
	const glm::vec3 toFar = glm::vec3(farPoint) - origin;
	RenderRaycastHit hit;
	if (m_renderer.Raycast(origin, toFar, glm::length(toFar), hit)) {
		const Entity entity = UserDataToEntity(hit.userData);
		if (m_world.IsAlive(entity)) { Select(entity); return; }
	}
	Select(kNullEntity);   // clicked empty space
}

void Crucible::SceneView::DrawGizmoMenu() {

	bool lines = m_viewport.GetDebugDraw();
	if (ImGui::Checkbox("Debug lines", &lines)) { m_viewport.SetDebugDraw(lines); }

	ImGui::BeginDisabled(!lines);
	ImGui::Indent();
	struct Entry { char const* label; DebugCategory category; };
	const Entry entries[] = {
		{ "Colliders",      DebugCategory::Colliders  },
		{ "Velocities",     DebugCategory::Velocities },
		{ "Light gizmos",   DebugCategory::Lights     },
		{ "Grid",           DebugCategory::Grid       },
		//{ "Selection",      DebugCategory::Selection  },
		{ "Gameplay lines", DebugCategory::Gameplay   },
		{ "Culling bounds", DebugCategory::Bounds     },
	};
	for (const Entry& e : entries) {
		bool on = DebugDraw::IsCategoryEnabled(e.category);
		if (ImGui::Checkbox(e.label, &on)) { DebugDraw::SetCategoryEnabled(e.category, on); }
	}
	bool throughWalls = DebugDrawSystem::GetCollidersThroughWalls();
	if (ImGui::Checkbox("Colliders through walls", &throughWalls)) { DebugDrawSystem::SetCollidersThroughWalls(throughWalls); }
	if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Draw colliders even when another object is in front of them."); }
	float scale = DebugDrawSystem::GetVelocityScale();
	ImGui::SetNextItemWidth(140.0f);
	if (ImGui::SliderFloat("Velocity scale", &scale, 0.01f, 2.0f, "%.2f m per m/s")) { DebugDrawSystem::SetVelocityScale(scale); }
	ImGui::Unindent();
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::Checkbox("Icons (lights, cameras, sky, empties)", &m_showIcons);
	bool outline = m_viewport.GetOutline();
	if (ImGui::Checkbox("Selection outline", &outline)) { m_viewport.SetOutline(outline); }
}

// Renderer-wide settings: unlike the Gizmos menu these change the picture itself, in the Game
// view as well as here. They are not saved - each start of the editor begins from the defaults
// in RenderTypes.h (PostSettings, ShadowSettings).
void Crucible::SceneView::DrawRenderingMenu() {
	// The glow around very bright things.
	ImGui::SeparatorText("Bloom");
	PostSettings& post = m_renderer.GetPostSettings();
	ImGui::Checkbox("Enabled##bloom", &post.bloom);
	ImGui::BeginDisabled(!post.bloom);
	ImGui::SetNextItemWidth(140.0f);
	ImGui::SliderFloat("Bloom intensity", &post.bloomIntensity, 0.0f, 2.0f, "%.2f");
	ImGui::SetNextItemWidth(140.0f);
	ImGui::SliderFloat("Bloom threshold", &post.bloomThreshold, 0.0f, 4.0f, "%.2f");
	ImGui::EndDisabled();

	ImGui::SeparatorText("Anti-aliasing");
	ImGui::Checkbox("FXAA", &post.fxaa);
	// MSAA: the scene is drawn with several samples per pixel (the Renderer falls back to what the GPU supports).
	int msaaChoice = post.msaaSamples >= 4 ? 2 : post.msaaSamples >= 2 ? 1 : 0;
	ImGui::SetNextItemWidth(140.0f);
	if (ImGui::Combo("MSAA", &msaaChoice, "Off\0" "2x\0" "4x\0")) { post.msaaSamples = 1u << msaaChoice; }

	// Soft darkening of ambient light in creases, corners and under objects.
	ImGui::SeparatorText("Ambient occlusion");
	ImGui::Checkbox("Enabled##ao", &post.ao);
	ImGui::BeginDisabled(!post.ao);
	ImGui::SetNextItemWidth(140.0f);
	ImGui::SliderFloat("Radius", &post.aoRadius, 0.1f, 2.0f, "%.2f m");
	ImGui::SetNextItemWidth(140.0f);
	ImGui::SliderFloat("Strength", &post.aoIntensity, 0.0f, 3.0f, "%.2f");
	ImGui::EndDisabled();

	ImGui::SeparatorText("Sun shadows");
	ShadowSettings& shadows = m_renderer.GetShadowSettings();
	ImGui::Checkbox("Enabled##shadows", &shadows.enabled);
	ImGui::BeginDisabled(!shadows.enabled);
	ImGui::SetNextItemWidth(140.0f);
	ImGui::SliderFloat("Distance", &shadows.distance, 10.0f, 200.0f, "%.0f m");
	ImGui::SetNextItemWidth(140.0f);
	ImGui::SliderFloat("Normal bias", &shadows.normalBias, 0.0f, 4.0f, "%.2f texels");
	ImGui::Checkbox("Show cascades (debug tint)", &shadows.debugCascades);
	ImGui::EndDisabled();

	// Measuring: where the frame's time goes, and a built-in crowd to load the renderer with.
	ImGui::SeparatorText("Performance");
	ImGui::Checkbox("Show timings in the view", &m_showStats);
	StressSettings& stress = m_renderer.GetStressSettings();
	int stressCount = static_cast<int>(stress.count);
	ImGui::SetNextItemWidth(140.0f);
	if (ImGui::InputInt("Load test objects", &stressCount, 1000, 5000)) {
		stress.count = static_cast<uint32_t>(std::clamp(stressCount, 0, 100000));
	}
	ImGui::BeginDisabled(stress.count == 0);
	ImGui::Checkbox("Objects move", &stress.moving);
	ImGui::SetNextItemWidth(140.0f);
	ImGui::SliderFloat("Spacing", &stress.spacing, 1.0f, 10.0f, "%.1f m");
	ImGui::EndDisabled();

	// Screen-space UI: a built-in check of the UI pass, drawn as the game's UI (Game view only).
	ImGui::SeparatorText("Interface");
	bool uiTest = m_renderer.GetUITestOverlay();
	if (ImGui::Checkbox("UI test overlay (Game view)", &uiTest)) { m_renderer.SetUITestOverlay(uiTest); }
}

void Crucible::SceneView::DrawStatsOverlay(ImVec2 viewMin) {
	const RenderFrameStats& stats = m_renderer.GetFrameStats();
	const RenderCullStats   cull  = m_viewport.GetCullStats();

	// All values are averages over roughly the last ten frames, in milliseconds.
	char text[768];
	int  length = std::snprintf(text, sizeof(text),
		"Frame  %.2f ms  (%.0f fps)\n"
		"CPU    wait %.2f   prepare %.2f   upload %.2f   record %.2f   submit %.2f\n",
		stats.frameMs, stats.frameMs > 0.0f ? 1000.0f / stats.frameMs : 0.0f,
		stats.cpuWaitMs, stats.cpuPrepareMs, stats.cpuUploadMs, stats.cpuRecordMs, stats.cpuSubmitMs);
	if (stats.gpuValid && length > 0 && length < static_cast<int>(sizeof(text))) {
		const auto gpu = [&](GpuSection section) { return stats.gpuMs[static_cast<size_t>(section)]; };
		length += std::snprintf(text + length, sizeof(text) - length,
			"GPU    total %.2f\n"
			"       shadows %.2f   occlusion %.2f   scene %.2f   bloom %.2f\n"
			"       final %.2f   outline %.2f   uploads %.2f   editor UI %.2f\n",
			stats.gpuTotalMs,
			gpu(GpuSection::Shadows), gpu(GpuSection::AmbientOcclusion), gpu(GpuSection::Scene), gpu(GpuSection::Bloom),
			gpu(GpuSection::Final), gpu(GpuSection::Outline), gpu(GpuSection::Uploads), gpu(GpuSection::Interface));
	}
	if (length > 0 && length < static_cast<int>(sizeof(text))) {
		std::snprintf(text + length, sizeof(text) - length,
			"View   %u of %u objects (%u travelling)   %u draws   shadows: %u objects in %u draws",
			cull.proxiesVisible + cull.immediateVisible, cull.proxies + cull.immediate, cull.movingProxies,
			cull.drawCalls, cull.shadowCasters, cull.shadowDrawCalls);
	}

	ImDrawList* const drawList = ImGui::GetWindowDrawList();
	const ImVec2 pos(viewMin.x + 10.0f, viewMin.y + 10.0f);
	const ImVec2 size = ImGui::CalcTextSize(text);
	drawList->AddRectFilled(ImVec2(pos.x - 6.0f, pos.y - 4.0f), ImVec2(pos.x + size.x + 6.0f, pos.y + size.y + 4.0f), IM_COL32(0, 0, 0, 170), 4.0f);
	drawList->AddText(pos, IM_COL32(235, 235, 235, 255), text);
}

// ------------------------------------------------------------------ gizmo

namespace Crucible {
namespace {

	// World matrix of `entity`'s parent, composed from the Transforms themselves (see DrawGizmo
	// for why not WorldTransform). Identity for a root.
	glm::mat4 ParentWorldOf(World& world, Entity entity) {
		glm::mat4 parentWorld(1.0f);
		for (Entity p = world.GetParent(entity); p.IsValid(); p = world.GetParent(p)) {
			if (Transform const* t = world.Read<Transform>(p)) {
				parentWorld = Detail::ComposeTRS(t->positions, t->rotation, t->scale) * parentWorld;
			}
		}
		return parentWorld;
	}

	// The selected entities a gizmo drag should move: the ones with a Transform whose parent /
	// ancestor is NOT also selected (a selected child already moves with its selected parent;
	// moving it too would move it twice).
	std::vector<Entity> GizmoMovers(World& world, std::vector<Entity> const& selection) {
		auto const selected = [&selection](Entity e) { return std::find(selection.begin(), selection.end(), e) != selection.end(); };
		std::vector<Entity> movers;
		for (Entity const e : selection) {
			if (!world.IsAlive(e) || !world.HasComponent<Transform>(e)) { continue; }
			bool covered = false;
			for (Entity p = world.GetParent(e); p.IsValid(); p = world.GetParent(p)) {
				if (selected(p)) { covered = true; break; }
			}
			if (!covered) { movers.push_back(e); }
		}
		return movers;
	}

}   // anonymous namespace
}   // namespace Crucible

void Crucible::SceneView::DrawGizmo(ImVec2 viewMin, ImVec2 viewSize) {
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	Entity const entity = inspector ? inspector->GetSelectedEntity() : kNullEntity;

	// Nothing to manipulate. Also finishes a drag whose entity went away mid-drag.
	if (!m_viewport.IsValid() || viewSize.x <= 0.0f || viewSize.y <= 0.0f || m_flying
		|| !entity.IsValid() || !m_world.IsAlive(entity) || !m_world.HasComponent<Transform>(entity)) {
		m_gizmoDrawn = false;
		EndGizmoDrag();
		return;
	}
	if (m_gizmoWasUsing && !(entity == m_gizmoEntity)) { EndGizmoDrag(); }   // selection changed mid-drag

	Camera& camera = m_viewport.GetCamera();
	glm::mat4 const view = camera.GetView();
	glm::mat4 const projection = camera.GetProjection();   // GLM, Y up: what ImGuizmo expects (see Camera.h)

	// Transform is LOCAL (relative to the parent); the gizmo works in world space. The parent
	// chain is composed from the Transforms themselves, the same way HierarchyTransformSystem
	// does (Detail::ComposeTRS, parent * local), rather than read from WorldTransform: that is
	// only refreshed once a frame, so mid-drag it would lag a frame behind the mouse.
	glm::mat4 const parentWorld = ParentWorldOf(m_world, entity);
	Transform const local = *m_world.Read<Transform>(entity);   // a copy: the "before" of this frame's change
	glm::mat4 const oldWorld = parentWorld * Detail::ComposeTRS(local.positions, local.rotation, local.scale);
	glm::mat4 world = oldWorld;

	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
	float snapStep = 0.5f;          // Ctrl-snap: half a unit...
	char const* verb = "Move";
	if (m_gizmoMode == GizmoMode::Rotate) { operation = ImGuizmo::ROTATE; snapStep = 15.0f; verb = "Rotate"; }   // ...15 degrees...
	if (m_gizmoMode == GizmoMode::Scale)  { operation = ImGuizmo::SCALE;  snapStep = 0.1f;  verb = "Scale"; }    // ...0.1
	ImGuizmo::MODE const mode = (m_gizmoMode == GizmoMode::Scale || m_gizmoSpace == GizmoSpace::Local) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
	float const snap[3] = { snapStep, snapStep, snapStep };
	bool const snapping = ImGui::GetIO().KeyCtrl;   // Unity: hold Ctrl to snap

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
	ImGuizmo::SetRect(viewMin.x, viewMin.y, viewSize.x, viewSize.y);
	ImGuizmo::SetGizmoSizeClipSpace(0.25f);

	bool const changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
		operation, mode, glm::value_ptr(world), nullptr, snapping ? snap : nullptr);
	bool const usingGizmo = ImGuizmo::IsUsing();
	m_gizmoDrawn = true;

	// A press on the gizmo is a drag - never also a pick of whatever is behind it.
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (usingGizmo || ImGuizmo::IsOver())) {
		m_gizmoClaimedClick = true;
	}

	if (usingGizmo && !m_gizmoWasUsing) {
		// The drag starts: close any Inspector edit first so the two stay separate undo steps,
		// then remember every entity it will move as it is now. The whole drag becomes one step.
		if (inspector) { inspector->CommitPendingEdit(); }
		m_gizmoEntity = entity;
		m_gizmoBefore.clear();
		std::vector<Entity> movers = GizmoMovers(m_world, inspector ? inspector->GetSelection() : std::vector<Entity>{});
		if (movers.empty()) { movers.push_back(entity); }
		for (Entity const e : movers) { m_gizmoBefore.emplace_back(e, EntityState::Capture(m_world, e)); }
		m_gizmoVerb = verb;
	}

	if (changed) {
		// The active entity's own part, exactly as before multi-selection: only the part being
		// dragged is written back, so a move never nudges the rotation or scale through float
		// round-off. Skipped when its parent is also selected - the parent carries it along.
		bool const activeMoves = m_gizmoBefore.empty() ||
			std::any_of(m_gizmoBefore.begin(), m_gizmoBefore.end(), [&](auto const& m) { return m.first == entity; });

		Math::vec3 translation, scale;
		Math::quat rotation;
		// Back to local space. DecomposeTRS refuses a collapsed or mirrored matrix (scale
		// dragged through zero), leaving the Transform as it was rather than writing NaNs.
		bool const decomposed = Detail::DecomposeTRS(glm::inverse(parentWorld) * world, translation, rotation, scale);
		if (decomposed && activeMoves) {
			if (Transform* t = m_world.GetComponent<Transform>(entity)) {
				switch (m_gizmoMode) {
				case GizmoMode::Translate: t->positions = translation; break;
				case GizmoMode::Rotate:    t->rotation  = rotation;    break;
				case GizmoMode::Scale:     t->scale     = scale;       break;
				}
				m_scenes.MarkDirty();
			}
		}

		// The rest of a multi-selection gets the same change. Move and Rotate: this frame's
		// change in world space (new * old^-1 - a rotation turns them around the gizmo's centre,
		// like Unity's Pivot mode). Scale: each one's own scale is multiplied by the same factor,
		// in place, so differently rotated objects never get skewed.
		glm::mat4 const delta = world * glm::inverse(oldWorld);
		Math::vec3 scaleFactor(1.0f);
		if (m_gizmoMode == GizmoMode::Scale && decomposed) {
			for (int axis = 0; axis < 3; ++axis) {
				if (std::abs(local.scale[axis]) > 1e-6f) { scaleFactor[axis] = scale[axis] / local.scale[axis]; }
			}
		}

		// A zero-scale active entity has no inverse: its change can't be carried to the others.
		bool const deltaUsable = m_gizmoMode == GizmoMode::Scale || std::abs(glm::determinant(oldWorld)) > 1e-12f;

		for (auto const& [other, before] : m_gizmoBefore) {
			(void)before;
			if (!deltaUsable) { break; }
			if (other == entity || !m_world.IsAlive(other)) { continue; }
			Transform* t = m_world.GetComponent<Transform>(other);
			if (!t) { continue; }

			if (m_gizmoMode == GizmoMode::Scale) {
				if (decomposed) { t->scale *= scaleFactor; m_scenes.MarkDirty(); }
				continue;
			}

			glm::mat4 const otherParent = ParentWorldOf(m_world, other);
			glm::mat4 const otherWorld = otherParent * Detail::ComposeTRS(t->positions, t->rotation, t->scale);
			Math::vec3 otherT, otherS;
			Math::quat otherR;
			if (!Detail::DecomposeTRS(glm::inverse(otherParent) * (delta * otherWorld), otherT, otherR, otherS)) { continue; }
			t->positions = otherT;                                                   // Move and Rotate both move it
			if (m_gizmoMode == GizmoMode::Rotate) { t->rotation = otherR; }          // only Rotate turns it
			m_scenes.MarkDirty();
		}
	}

	if (!usingGizmo && m_gizmoWasUsing) { EndGizmoDrag(); }
	m_gizmoWasUsing = usingGizmo;
}

void Crucible::SceneView::EndGizmoDrag() {
	// One EntityStateCommand per entity that actually changed, pushed as one undo step.
	// Pushed in play mode too: the history keeps play-mode steps separately and drops them on
	// Stop (UndoHistory::BeginPlaySession / EndPlaySession).
	std::vector<std::unique_ptr<EditorCommand>> parts;
	UndoHistory& history = EditorManager::GetInstance().GetHistory();
	for (auto& [moved, before] : m_gizmoBefore) {
		if (!moved.IsValid() || before.empty() || !m_world.IsAlive(moved)) { continue; }
		std::string after = EntityState::Capture(m_world, moved);
		if (after != before) {
			parts.push_back(std::make_unique<EntityStateCommand>(history.KeyOf(moved), std::move(before), std::move(after), m_gizmoVerb));
		}
	}

	// The gizmo's own entity last, so it is the active one again after an undo / redo.
	std::stable_partition(parts.begin(), parts.end(), [&](auto const& part) { return !(part->Target() == history.KeyOf(m_gizmoEntity)); });

	if (!parts.empty()) {
		std::string const label = parts.size() == 1 ? m_gizmoVerb : m_gizmoVerb + " " + std::to_string(parts.size()) + " Entities";
		history.Push(GroupCommand::Make(std::move(parts), label));
	}

	m_gizmoEntity = kNullEntity;
	m_gizmoBefore.clear();
	m_gizmoWasUsing = false;
}
