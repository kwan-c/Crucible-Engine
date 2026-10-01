/*!
* @file			ImGuiPanel_SceneView.cpp
* @brief		The editor's Scene view: the free-camera view of the level you are editing.
*/

#include "ImGui/Panels/ImGuiPanel_SceneView.h"
#include "Scene/SceneManager.h"
#include "ImGui/EditorWidgets.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Graphics/DebugDraw.h"
#include "Graphics/DebugDrawSystem.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"

#include <cmath>

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
}

void Crucible::SceneView::Update(float dt) {
	// Zero padding so the scene image fills the window edge to edge.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	bool const visible = ImGui::Begin(m_panelName.c_str(), nullptr, m_flags);
	ImGui::PopStyleVar();

	if (!visible) {
		// Tab hidden behind another tab, or window collapsed: nothing to render this frame.
		m_hovered = m_focused = false;
		m_flying = false;
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
	ImGui::SetCursorScreenPos(min);
	ImGui::InvisibleButton("##SceneViewport", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)),
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	m_hovered = ImGui::IsItemHovered();
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
	HandleClickPicking(min, avail);

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

	CameraInput input{};
	input.active = m_flying;

	if (m_flying) {
		// While flying, hide the OS cursor and read raw mouse delta / wheel / WASDQE + Shift
		// straight from ImGui - the engine's own Input system only tracks keys today (see
		// Engine/src/Input/InputSystem.h) and isn't attached/updated by Core yet, and this is
		// editor-only UI input anyway, so there's no reason to route it through the gameplay
		// input system.
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);

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

void Crucible::SceneView::DrawToolbar() {
	EditorWidgets::BeginToolbarRow();

	using EditorWidgets::ToolbarToggle;
	if (ToolbarToggle("Move", m_gizmoMode == GizmoMode::Translate))  m_gizmoMode = GizmoMode::Translate;
	ImGui::SameLine();
	if (ToolbarToggle("Rotate", m_gizmoMode == GizmoMode::Rotate))   m_gizmoMode = GizmoMode::Rotate;
	ImGui::SameLine();
	if (ToolbarToggle("Scale", m_gizmoMode == GizmoMode::Scale))     m_gizmoMode = GizmoMode::Scale;

	// Unity-style W / E / R shortcuts while the Scene view is focused (and no text box is active).
	// Suppressed while flying: W/E are also the fly-camera's forward/up-move keys, and R would be
	// a very unwelcome surprise mid-flight.
	if (!m_flying && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_gizmoMode = GizmoMode::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_gizmoMode = GizmoMode::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_gizmoMode = GizmoMode::Scale;
	}

	ImGui::SameLine();
	if (ToolbarToggle("Gizmos", m_viewport.GetDebugDraw() || m_showIcons)) { ImGui::OpenPopup("##SceneGizmos"); }
	if (ImGui::BeginPopup("##SceneGizmos")) {
		DrawGizmoMenu();
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
			ImGui::SetTooltip("Entities: %u of %u in view (%u moving)\nImmediate (demo) items: %u of %u",
				stats.proxiesVisible, stats.proxies, stats.movingProxies, stats.immediateVisible, stats.immediate);
		}
	}

	ImGui::SameLine();
	ImGui::TextDisabled("|  Current Scene: %s", m_scenes.CurrentName().c_str());

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
	void GlyphDiamond(ImDrawList* dl, ImVec2 p, ImU32 col) {
		dl->AddQuad(ImVec2(p.x, p.y - 6.0f), ImVec2(p.x + 6.0f, p.y), ImVec2(p.x, p.y + 6.0f), ImVec2(p.x - 6.0f, p.y), col, 1.5f);
	}

	constexpr float kIconRadius = 12.0f;   // backdrop disc + click radius, in pixels

}   // anonymous namespace

void Crucible::SceneView::SyncSelection() {
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	DebugDrawSystem::SetSelectedEntity(inspector ? inspector->GetSelectedEntity() : kNullEntity);
}

void Crucible::SceneView::DrawIcons(ImVec2 viewMin, ImVec2 viewSize) {

	if (!m_viewport.IsValid() || viewSize.x <= 0.0f || viewSize.y <= 0.0f) { return; }

	Camera& camera = m_viewport.GetCamera();
	camera.SetAspect(viewSize.x, viewSize.y);   // match the image this frame (the Renderer does the same)
	const glm::mat4 viewProj = camera.GetProjection() * camera.GetView();
	const Entity selected = DebugDrawSystem::GetSelectedEntity();

	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(viewMin, ImVec2(viewMin.x + viewSize.x, viewMin.y + viewSize.y), true);

	// Backdrop + selection ring + glyph, and remember where it went for picking.
	auto place = [&](Entity entity, const WorldTransform& wt, auto&& glyph) {
		if (!wt.valid) { return; }
		ImVec2 screen; float depth;
		if (!ProjectToScreen(viewProj, glm::vec3(wt.matrix[3]), viewMin, viewSize, screen, depth)) { return; }
		dl->AddCircleFilled(screen, kIconRadius, IM_COL32(18, 18, 22, 170));
		if (entity == selected) { dl->AddCircle(screen, kIconRadius + 1.5f, IM_COL32(255, 140, 26, 255), 0, 2.0f); }
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

	// Anything else with nothing to click on in the scene (empties, collider-only, characters...).
	m_world.BuildQuery<const WorldTransform>().Exclude<MeshRenderer, Light, Skybox>().Build().ForEach(
		[&](Entity e, const WorldTransform& wt) {
			place(e, wt, [&](ImVec2 p) { GlyphDiamond(dl, p, IM_COL32(200, 200, 200, 255)); });
		});

	dl->PopClipRect();
}

void Crucible::SceneView::Select(Entity entity) {
	if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) {
		inspector->SetSelectedEntity(entity);
	}
	DebugDrawSystem::SetSelectedEntity(entity);
}

void Crucible::SceneView::HandleClickPicking(ImVec2 viewMin, ImVec2 viewSize) {

	if (!m_hovered || m_flying || !m_viewport.IsValid()) { return; }
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
		{ "Selection",      DebugCategory::Selection  },
		{ "Gameplay lines", DebugCategory::Gameplay   },
		{ "Culling bounds", DebugCategory::Bounds     },
	};
	for (const Entry& e : entries) {
		bool on = DebugDraw::IsCategoryEnabled(e.category);
		if (ImGui::Checkbox(e.label, &on)) { DebugDraw::SetCategoryEnabled(e.category, on); }
	}
	float scale = DebugDrawSystem::GetVelocityScale();
	ImGui::SetNextItemWidth(140.0f);
	if (ImGui::SliderFloat("Velocity scale", &scale, 0.01f, 2.0f, "%.2f m per m/s")) { DebugDrawSystem::SetVelocityScale(scale); }
	ImGui::Unindent();
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::Checkbox("Icons (lights, sky, empties)", &m_showIcons);
}
