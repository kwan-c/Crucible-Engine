/*************************************************************************
* \file:         DebugDrawSystem.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          1 October 2026
* \version       1.0
* \brief
*                See DebugDrawSystem.h for what is drawn and the colour key.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "DebugDrawSystem.h"

#include <algorithm>
#include <atomic>

#include "ECS/World.h"
#include "ECS/HierarchyTransformSystem.h"
#include "Graphics/DebugDraw.h"
#include "Graphics/RenderSystem.h"
#include "Resources/ResourceManager.h"

namespace Crucible {

	namespace {

		// Editor-written, read by the system on the main thread. Atomics keep it well-defined if a
		// future editor sets them from another thread.
		std::atomic<std::uint64_t> g_selected{ 0 };   // (index << 32) | generation; 0 = kNullEntity
		std::atomic<float>         g_velocityScale{ 0.25f };

		const glm::vec4 kStaticCollider  { 0.30f, 0.90f, 0.40f, 1.0f };
		const glm::vec4 kDynamicCollider { 0.25f, 0.80f, 1.00f, 1.0f };
		const glm::vec4 kCharacter       { 1.00f, 0.85f, 0.20f, 1.0f };
		const glm::vec4 kSlow            { 1.00f, 0.90f, 0.20f, 1.0f };
		const glm::vec4 kFast            { 1.00f, 0.20f, 0.20f, 1.0f };
		const glm::vec4 kGrid            { 0.32f, 0.32f, 0.34f, 1.0f };

		constexpr float kFastSpeed = 20.0f;   // m/s at which the arrow is fully red

		// One Collider at the pose physics gives it: Transform position/rotation, then m_center in
		// the body's local frame (Jolt's RotatedTranslatedShape). Sizes are scaled by Transform::scale
		// through ScaledCollider (PhysicsComponents.h), the same call physics builds bodies with, so
		// the wireframe is always the shape that actually collides.
		void DrawCollider(const Transform& t, const Collider& typed, const DebugStyle& style) {

			const Collider c = ScaledCollider(typed, t.scale);
			const glm::vec3 center = t.positions + t.rotation * c.m_center;
			switch (c.m_type) {
				case ShapeType::Box:
					DebugDraw::Box(center, c.m_size * 0.5f, t.rotation, style);
					break;
				case ShapeType::Sphere:
					DebugDraw::Sphere(center, c.m_radius, style);
					break;
				case ShapeType::Capsule:
					DebugDraw::Capsule(center, c.m_radius, std::max(c.m_height * 0.5f - c.m_radius, 0.0f), t.rotation, style);
					break;
			}
		}

		void DrawVelocity(const glm::vec3& origin, const glm::vec3& velocity) {

			const float speed = glm::length(velocity);
			if (speed < 0.01f) { return; }
			DebugStyle style;
			style.category  = DebugCategory::Velocities;
			style.depthTest = false;   // starts inside the body: would be hidden otherwise
			style.color     = glm::mix(kSlow, kFast, std::clamp(speed / kFastSpeed, 0.0f, 1.0f));
			DebugDraw::Arrow(origin, origin + velocity * g_velocityScale.load(), style);
		}

		Entity Unpack(std::uint64_t v) { return Entity(static_cast<std::uint32_t>(v >> 32), static_cast<std::uint32_t>(v & 0xFFFFFFFFu)); }

	}   // anonymous namespace

	// ------------------------------------------------------------------------------------------------------------- //
	//  STATICS
	// ------------------------------------------------------------------------------------------------------------- //

	void DebugDrawSystem::SetSelectedEntity(Entity entity) {
		g_selected.store((static_cast<std::uint64_t>(entity.index) << 32) | entity.generation);
	}

	Entity DebugDrawSystem::GetSelectedEntity() { return Unpack(g_selected.load()); }

	void  DebugDrawSystem::SetVelocityScale(float scale) { g_velocityScale.store(std::max(scale, 0.0f)); }
	float DebugDrawSystem::GetVelocityScale() { return g_velocityScale.load(); }

	// ------------------------------------------------------------------------------------------------------------- //
	//  SYSTEM
	// ------------------------------------------------------------------------------------------------------------- //

	void DebugDrawSystem::Describe(SystemDesc& desc) const {

		desc.Reads<Transform, WorldTransform, Collider, RigidBody, CharacterController, Light, MeshRenderer, CameraComponent>()
			.RunAfter<HierarchyTransformSystem>()        // WorldTransform must be this frame's
			.Affinity(SystemAffinity::MainThread);       // Renderer/ResourceManager aren't thread-safe

	}   // DebugDrawSystem::Describe()

	void DebugDrawSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {

		if (!DebugDraw::IsActive()) { return; }   // nothing displays debug lines: skip all the queries

		if (DebugDraw::IsCategoryEnabled(DebugCategory::Colliders))  { DrawColliders(world); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::Velocities)) { DrawVelocities(world); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::Lights))     { DrawLights(world); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::Grid))       { DrawGrid(); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::Selection))  { DrawSelection(world); }

	}   // DebugDrawSystem::Update()

	void DebugDrawSystem::DrawColliders(World& world) {

		DebugStyle style;
		style.category  = DebugCategory::Colliders;
		style.depthTest = false;   // a collider usually coincides with its mesh surface; show it through

		// Static: a Collider with no RigidBody (and not a character - the CharacterController wins).
		style.color = kStaticCollider;
		world.BuildQuery<const Transform, const Collider>().Exclude<RigidBody, CharacterController>().Build()
			.ForEach([&](Entity, const Transform& t, const Collider& c) { DrawCollider(t, c, style); });

		// Dynamic: Collider + RigidBody.
		style.color = kDynamicCollider;
		world.BuildQuery<const Transform, const Collider, const RigidBody>().Exclude<CharacterController>().Build()
			.ForEach([&](Entity, const Transform& t, const Collider& c, const RigidBody&) { DrawCollider(t, c, style); });

		// Characters: an upright capsule at position + m_center (CharacterVirtual doesn't rotate).
		style.color = kCharacter;
		world.CreateQuery<const Transform, const CharacterController>()
			.ForEach([&](Entity, const Transform& t, const CharacterController& cc) {
				DebugDraw::Capsule(t.positions + cc.m_center, cc.m_radius,
					std::max(cc.m_height * 0.5f - cc.m_radius, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), style);
			});

	}   // DebugDrawSystem::DrawColliders()

	void DebugDrawSystem::DrawVelocities(World& world) {

		world.CreateQuery<const Transform, const RigidBody>()
			.ForEach([&](Entity, const Transform& t, const RigidBody& rb) { DrawVelocity(t.positions, rb.velocity); });

		world.CreateQuery<const Transform, const CharacterController>()
			.ForEach([&](Entity, const Transform& t, const CharacterController& cc) { DrawVelocity(t.positions + cc.m_center, cc.m_velocity); });

	}   // DebugDrawSystem::DrawVelocities()

	void DebugDrawSystem::DrawLights(World& world) {

		const Entity selected = GetSelectedEntity();

		world.CreateQuery<const WorldTransform, const Light>()
			.ForEach([&](Entity entity, const WorldTransform& wt, const Light& light) {

				if (!wt.valid) { return; }
				const glm::vec3 p(wt.matrix[3]);
				glm::vec3 dir = -glm::vec3(wt.matrix[2]);   // local -Z, same as RenderExtractSystem
				const float len = glm::length(dir);
				dir = len > 1e-6f ? dir / len : glm::vec3(0.0f, -1.0f, 0.0f);

				DebugStyle style;
				style.category  = DebugCategory::Lights;
				style.depthTest = false;
				style.color     = glm::vec4(glm::clamp(light.colour, glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f);
				if (!light.enabled) { style.color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f); }

				const bool isSelected = (entity == selected);
				switch (light.type) {
					case LightType::Directional:
						DebugDraw::Arrow(p, p + dir * (isSelected ? 2.5f : 1.5f), style);
						if (isSelected) { DebugDraw::Circle(p, dir, 0.4f, style); }
						break;
					case LightType::Point:
						if (isSelected) { DebugDraw::Sphere(p, light.range, style); }
						break;
					case LightType::Spot:
						if (isSelected) {
							DebugDraw::Cone(p, dir, light.range, light.outerConeDeg, style);
							DebugStyle inner = style;
							inner.color *= glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
							DebugDraw::Cone(p, dir, light.range, light.innerConeDeg, inner, 16);
						} else {
							DebugDraw::Arrow(p, p + dir * 1.0f, style);
						}
						break;
				}
			});

	}   // DebugDrawSystem::DrawLights()

	void DebugDrawSystem::DrawGrid() {

		DebugStyle style;
		style.category = DebugCategory::Grid;
		style.color    = kGrid;
		DebugDraw::Grid(glm::vec3(0.0f), 20, 1.0f, style);

		// World axes through the origin, drawn over the grid lines they coincide with.
		style.color = { 0.85f, 0.25f, 0.25f, 1.0f };
		DebugDraw::Line({ -20.0f, 0.001f, 0.0f }, { 20.0f, 0.001f, 0.0f }, style);
		style.color = { 0.25f, 0.45f, 0.95f, 1.0f };
		DebugDraw::Line({ 0.0f, 0.001f, -20.0f }, { 0.0f, 0.001f, 20.0f }, style);

	}   // DebugDrawSystem::DrawGrid()

	void DebugDrawSystem::DrawSelection(World& world) {

		const Entity selected = GetSelectedEntity();
		if (selected == kNullEntity || !world.IsAlive(selected)) { return; }

		const WorldTransform* wt = world.Read<WorldTransform>(selected);
		if (!wt || !wt->valid) { return; }

		// XYZ axis arrows on the selection are disabled: the transform gizmo already shows the
		// axes. Uncomment to bring them back.
		// DebugStyle style;
		// style.category  = DebugCategory::Selection;
		// style.depthTest = false;
		// DebugDraw::Axes(wt->matrix, 1.0f, style);
		// The selected mesh itself gets the Renderer's orange silhouette outline instead of a box
		// (Renderer::SetOutlinedObject, driven by the Scene view).

		// A selected camera shows what it sees: its view frustum (16:9, far end capped so a
		// 1000 m far plane doesn't fill the scene with lines).
		if (const CameraComponent* cam = world.Read<CameraComponent>(selected)) {
			DebugStyle style;
			style.category  = DebugCategory::Selection;
			style.depthTest = false;
			style.color     = { 0.85f, 0.85f, 0.85f, 1.0f };

			const glm::vec3 p(wt->matrix[3]);
			const glm::vec3 f = glm::normalize(-glm::vec3(wt->matrix[2]));
			const glm::vec3 u = glm::normalize( glm::vec3(wt->matrix[1]));
			const glm::vec3 r = glm::normalize(glm::cross(f, u));
			const float tanY  = std::tan(glm::radians(std::clamp(cam->fovY, 1.0f, 179.0f)) * 0.5f);
			const float tanX  = tanY * (16.0f / 9.0f);
			const float nearD = std::max(cam->nearZ, 0.001f);
			const float farD  = std::min(std::max(cam->farZ, nearD + 0.01f), 5.0f);

			glm::vec3 n[4], fa[4];
			const float sx[4] = { -1.0f, 1.0f, 1.0f, -1.0f }, sy[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
			for (int i = 0; i < 4; ++i) {
				const glm::vec3 dir = f + r * (sx[i] * tanX) + u * (sy[i] * tanY);
				n[i]  = p + dir * nearD;
				fa[i] = p + dir * farD;
			}
			for (int i = 0; i < 4; ++i) {
				DebugDraw::Line(n[i],  n[(i + 1) % 4],  style);
				DebugDraw::Line(fa[i], fa[(i + 1) % 4], style);
				DebugDraw::Line(n[i],  fa[i],           style);
			}
		}

	}   // DebugDrawSystem::DrawSelection()

}   // NAMESPACE CRUCIBLE
