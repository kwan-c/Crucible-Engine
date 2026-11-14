/*************************************************************************
* \file:         DebugDrawSystem.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          1 October 2026
* \version       1.1
* \brief
*                See DebugDrawSystem.h for what is drawn and the colour key.
*
* \history
*    -v1.0       initial implementation
*    -v1.1       HeightField colliders are not outlined (Jean David)
*
**************************************************************************/

#include "DebugDrawSystem.h"

#include <algorithm>
#include <atomic>

#include "ECS/World.h"
#include "ECS/HierarchyTransformSystem.h"
#include "Graphics/DebugDraw.h"
#include "Graphics/RenderSystem.h"
#include "Graphics/ParticleSystem.h"   // ParticleSystem::ConeFarRadius: the gizmo's cone is the one particles follow
#include "Resources/ResourceManager.h"
#include "UI/UISystem.h"      // UI::LayoutWorldCanvas: the selected element's rect on its page
#include "UI/WorldUIPlane.h"  // where a world canvas's page is for the Scene view's camera

namespace Crucible {

	namespace {

		// Editor-written, read by the system on the main thread. Atomics keep it well-defined if a
		// future editor sets them from another thread.
		std::atomic<std::uint64_t> g_selected{ 0 };   // (index << 32) | generation; 0 = kNullEntity
		std::atomic<float>         g_velocityScale{ 0.25f };
		std::atomic<bool>          g_collidersThroughWalls{ false };
		std::atomic<bool>		   g_audioRangesForAll{ false };

		const glm::vec4 kStaticCollider  { 0.30f, 0.90f, 0.40f, 1.0f };
		const glm::vec4 kDynamicCollider { 0.25f, 0.80f, 1.00f, 1.0f };
		const glm::vec4 kCharacter       { 1.00f, 0.85f, 0.20f, 1.0f };
		const glm::vec4 kSlow            { 1.00f, 0.90f, 0.20f, 1.0f };
		const glm::vec4 kFast            { 1.00f, 0.20f, 0.20f, 1.0f };
		const glm::vec4 kGrid            { 0.32f, 0.32f, 0.34f, 1.0f };
		const glm::vec4 kWorldCanvas     { 0.35f, 0.75f, 1.00f, 1.0f };   // a world canvas's page
		const glm::vec4 kWorldCanvasOff  { 0.45f, 0.45f, 0.50f, 1.0f };   // ...switched off (not drawn)
		const glm::vec4 kWorldUISelected { 1.00f, 0.55f, 0.10f, 1.0f };   // the selected element on one (the selection orange)
		const glm::vec4 kAudioRange      { 0.45f, 0.80f, 1.00f, 1.0f };   // a sound's edge: Min Distance, and Max Distance with Linear rolloff
		const glm::vec4 kAudioRangeDim   { 0.22f, 0.40f, 0.55f, 1.0f };	  // Max Distance with Logarithmic rolloff (still heard beyond it)
		const glm::vec4 kAudioOff        { 0.50f, 0.50f, 0.50f, 1.0f };	  // switched off, or no clip
		const glm::vec4 kAudioListener   { 0.95f, 0.95f, 0.95f, 1.0f };
		const glm::vec4 kParticleShape   { 1.00f, 0.75f, 0.30f, 1.0f };

		// The Scene view's camera (SetViewCamera). Main thread only, like the rest of the system.
		bool      g_hasViewCamera = false;
		glm::vec3 g_viewCameraPosition{ 0.0f };
		glm::mat4 g_viewCameraView{ 1.0f };

		constexpr float kFastSpeed = 20.0f;   // m/s at which the arrow is fully red

		// One Collider at the pose physics gives it: Transform position/rotation, then m_center in
		// the body's local frame (Jolt's RotatedTranslatedShape). Sizes are scaled by Transform::scale
		// through ScaledCollider (PhysicsComponents.h), the same call physics builds bodies with, so
		// the wireframe is always the shape that actually collides.
		// Where an entity really is, as a Transform: its WorldTransform decomposed. A child's
		// Transform is relative to its parent, so drawing at it put children's shapes in the wrong
		// place. Falls back to the Transform until the world pose has been computed.
		Transform WorldPoseOf(const Transform& local, const WorldTransform& wt) {
			Transform world;
			if (wt.valid && Detail::DecomposeTRS(wt.matrix, world.positions, world.rotation, world.scale)) { return world; }
			return local;
		}

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
				case ShapeType::HeightField:
					// Not outlined: the terrain's own mesh IS its collision shape (same grid, same
					// triangles), and a wireframe of thousands of cells would bury the view.
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

	void DebugDrawSystem::SetCollidersThroughWalls(bool enabled) { g_collidersThroughWalls.store(enabled); }
	bool DebugDrawSystem::GetCollidersThroughWalls() { return g_collidersThroughWalls.load(); }

	void DebugDrawSystem::SetAudioRangesForAll(bool enabled) { g_audioRangesForAll.store(enabled); }
	bool DebugDrawSystem::GetAudioRangesForAll() { return g_audioRangesForAll.load(); }

	void DebugDrawSystem::SetViewCamera(const glm::vec3& position, const glm::mat4& view) {
		g_hasViewCamera      = true;
		g_viewCameraPosition = position;
		g_viewCameraView     = view;
	}

	// ------------------------------------------------------------------------------------------------------------- //
	//  SYSTEM
	// ------------------------------------------------------------------------------------------------------------- //

	void DebugDrawSystem::Describe(SystemDesc& desc) const {

		desc.Reads<Transform, WorldTransform, Collider, RigidBody, CharacterController, Light, MeshRenderer, CameraComponent,
		           UICanvas, UIRect, HierarchyNode, AudioSource, AudioListener, ParticleEmitter>()   // world canvases' pages and the selected element's rect
			.RunAfter<HierarchyTransformSystem>()        // WorldTransform must be this frame's
			.Affinity(SystemAffinity::MainThread);       // Renderer/ResourceManager aren't thread-safe

	}   // DebugDrawSystem::Describe()

	void DebugDrawSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {

		if (!DebugDraw::IsActive()) { return; }   // nothing displays debug lines: skip all the queries

		if (DebugDraw::IsCategoryEnabled(DebugCategory::Colliders))  { DrawColliders(world); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::Velocities)) { DrawVelocities(world); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::Lights))     { DrawLights(world); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::Audio))      { DrawAudio(world); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::Particles))  { DrawParticleShape(world); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::Grid))       { DrawGrid(); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::Selection))  { DrawSelection(world); }
		if (DebugDraw::IsCategoryEnabled(DebugCategory::WorldUI))    { DrawWorldUI(world); }

	}   // DebugDrawSystem::Update()

	void DebugDrawSystem::DrawWorldUI(World& world) {

		// Drawn on top of everything: the page is often in front of or inside other objects, and
		// these lines are for finding it.
		DebugStyle style;
		style.category  = DebugCategory::WorldUI;
		style.depthTest = false;

		auto outline = [&](const std::array<glm::vec3, 4>& c, const glm::vec4& colour) {
			style.color = colour;
			for (int i = 0; i < 4; ++i) { DebugDraw::Line(c[i], c[(i + 1) % 4], style); }
		};
		auto planeFor = [&](const UICanvas& canvas, const glm::mat4& matrix) {
			return g_hasViewCamera ? WorldCanvasPlaneFor(canvas, matrix, g_viewCameraPosition, g_viewCameraView)
			                       : WorldUIPlaneFor(WorldBillboard::Off, matrix, glm::vec3(0.0f), glm::mat4(1.0f));
		};

		// Every world canvas's page; a switched-off one in grey (it isn't drawn, but is still there),
		// the selected one in the selection colour.
		const Entity selected = GetSelectedEntity();
		world.CreateQuery<const WorldTransform, const UICanvas>().ForEach([&](Entity entity, const WorldTransform& wt, const UICanvas& canvas) {
			if (canvas.renderMode != UICanvas::RenderMode::WorldSpace || !wt.valid) { return; }
			const glm::vec4& colour = entity == selected ? kWorldUISelected : canvas.enabled ? kWorldCanvas : kWorldCanvasOff;
			outline(WorldUIBoxCorners(planeFor(canvas, wt.matrix), WorldCanvasBoxInPlane(canvas)), colour);
		});

		// The selected element on a world canvas: its rect (turned, if it is), in the selection colour.
		if (selected == kNullEntity || !world.IsAlive(selected) || !world.HasComponent<UIRect>(selected)) { return; }
		Entity canvasEntity = kNullEntity;
		for (Entity p = world.GetParent(selected); p.IsValid(); p = world.GetParent(p)) {
			if (world.HasComponent<UICanvas>(p)) { canvasEntity = p; }   // the topmost canvas owns the tree
		}
		if (canvasEntity == kNullEntity) { return; }
		const UICanvas* canvas = world.Read<UICanvas>(canvasEntity);
		const WorldTransform* wt = world.Read<WorldTransform>(canvasEntity);
		if (!canvas || !wt || !wt->valid || canvas->renderMode != UICanvas::RenderMode::WorldSpace) { return; }

		static std::vector<UI::LayoutItem> s_layout;   // reused: no allocation per frame
		UI::LayoutWorldCanvas(world, canvasEntity, *canvas, s_layout);
		const WorldUIPlane plane = planeFor(*canvas, wt->matrix);
		for (const UI::LayoutItem& item : s_layout) {
			if (item.entity != selected) { continue; }
			const auto page = item.Corners();   // page pixels, turned with the element
			std::array<glm::vec3, 4> corners;
			for (int i = 0; i < 4; ++i) { corners[i] = WorldUIPlanePoint(plane, WorldCanvasToPlane(*canvas, page[i])); }
			outline(corners, kWorldUISelected);
			break;
		}
	}   // DebugDrawSystem::DrawWorldUI()

	void DebugDrawSystem::DrawColliders(World& world) {

		DebugStyle style;
		style.category  = DebugCategory::Colliders;
		// Hidden behind other objects unless asked otherwise. A collider that coincides with its
		// own mesh still shows: debug lines are drawn a hair nearer the camera (DebugLine.slang).
		style.depthTest = !g_collidersThroughWalls.load();

		// Static: a Collider with no RigidBody (and not a character - the CharacterController wins).
		style.color = kStaticCollider;
		world.BuildQuery<const Transform, const WorldTransform, const Collider>().Exclude<RigidBody, CharacterController>().Build()
			.ForEach([&](Entity, const Transform& t, const WorldTransform& wt, const Collider& c) { DrawCollider(WorldPoseOf(t, wt), c, style); });

		// Dynamic: Collider + RigidBody.
		style.color = kDynamicCollider;
		world.BuildQuery<const Transform, const WorldTransform, const Collider, const RigidBody>().Exclude<CharacterController>().Build()
			.ForEach([&](Entity, const Transform& t, const WorldTransform& wt, const Collider& c, const RigidBody&) { DrawCollider(WorldPoseOf(t, wt), c, style); });

		// Characters: an upright capsule at position + m_center (CharacterVirtual doesn't rotate).
		style.color = kCharacter;
		world.CreateQuery<const Transform, const WorldTransform, const CharacterController>()
			.ForEach([&](Entity, const Transform& local, const WorldTransform& wt, const CharacterController& cc) {
				const Transform t = WorldPoseOf(local, wt);
				DebugDraw::Capsule(t.positions + cc.m_center, cc.m_radius,
					std::max(cc.m_height * 0.5f - cc.m_radius, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), style);
			});

	}   // DebugDrawSystem::DrawColliders()

	void DebugDrawSystem::DrawVelocities(World& world) {

		world.CreateQuery<const Transform, const WorldTransform, const RigidBody>()
			.ForEach([&](Entity, const Transform& t, const WorldTransform& wt, const RigidBody& rb) { DrawVelocity(WorldPoseOf(t, wt).positions, rb.velocity); });

		world.CreateQuery<const Transform, const WorldTransform, const CharacterController>()
			.ForEach([&](Entity, const Transform& t, const WorldTransform& wt, const CharacterController& cc) { DrawVelocity(WorldPoseOf(t, wt).positions + cc.m_center, cc.m_velocity); });

	}   // DebugDrawSystem::DrawVelocities()

	void DebugDrawSystem::DrawLights(World& world) {

		const Entity selected = GetSelectedEntity();

		// IncludeDisabled: a switched-off light keeps its (grey) gizmo, like one whose own flag is off.
		world.BuildQuery<const WorldTransform, const Light>().IncludeDisabled().Build()
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
				if (!light.enabled || !world.IsComponentActive<Light>(entity)) { style.color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f); }

				const bool isSelected = (entity == selected);
				switch (light.type) {
					case LightType::Directional:
					{
						// A disc with a beam of parallel rays leaving it, pointing the way the light
						// travels (a directional light has no position: only this direction matters).
						const float     radius = 0.4f;
						const float     length = isSelected ? 3.0f : 1.5f;
						const glm::vec3 side   = glm::normalize(glm::cross(dir, std::abs(dir.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f)));
						const glm::vec3 up     = glm::cross(side, dir);
						DebugDraw::Circle(p, dir, radius, style);
						DebugDraw::Arrow(p, p + dir * length, style);
						const int rays = 8;
						for (int i = 0; i < rays; ++i) {
							const float     angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(rays);
							const glm::vec3 start = p + (side * std::cos(angle) + up * std::sin(angle)) * radius;
							DebugDraw::Line(start, start + dir * length, style);
						}
						break;
					}
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

	void DebugDrawSystem::DrawAudio(World& world) {

		const Entity selected = GetSelectedEntity();
		const bool   all      = g_audioRangesForAll.load();

		DebugStyle style;
		style.category = DebugCategory::Audio;
		style.depthTest = false;	// like the light gizmos: drawn on top, for finding the edge

		// Source: a 3D sound is at full volume inside Min Distance and fades out to Max Distance.
		// Spatial Blend 0 is plain 2D - as loud everywhere - so there is no range to show. Distances
		// are metres, not scaled by the Transform, and clamped as AudioVoices clamps them for FMOD.
		// IncludeDisabled: a switched-off source keeps its spheres, in grey, as lights do.
		world.BuildQuery<const WorldTransform, const AudioSource>().IncludeDisabled().Build()
			.ForEach([&](Entity entity, const WorldTransform& wt, const AudioSource& source) {

				if (!wt.valid || source.m_spatialBlend <= 0.0f) { return; }
				if (!all && entity != selected) { return; }

				const glm::vec3 p(wt.matrix[3]);
				const float minDistance = std::max(source.m_minDistance, 0.0f);
				const float maxDistance = std::max(source.m_maxDistance, minDistance);
				const bool  on = source.m_clip != 0 && world.IsComponentActive<AudioSource>(entity);

				if (minDistance > 0.0f) {
					style.color = on ? kAudioRange : kAudioOff;
					DebugDraw::Sphere(p, minDistance, style);
				}
				// Linear rolloff is silent at Max Distance, so that sphere is where the sound:
				// bright. Logarithmic only stops getting quieter there and is still heard beyond it:
				// dim. More segments, since it is often hundreds of metres across.
				if (maxDistance > minDistance) {
					style.color = !on ? kAudioOff : source.m_rolloffMode == AudioRolloffMode::Linear ? kAudioRange : kAudioRangeDim;
					DebugDraw::Sphere(p, maxDistance, style, 64);
				}
			});
		
		// Listeners: which way the ears face (local -Z forward, the same as ECSAudioSystem), so
		// a listener turned round - left and right swapped - is easy to spot.
		style.color = kAudioListener;
		world.CreateQuery<const WorldTransform, const AudioListener>()
			.ForEach([&](Entity, const WorldTransform& wt, const AudioListener&) {
				if (!wt.valid) { return; }
				const glm::vec3 p(wt.matrix[3]);
				const glm::vec3 forward = -glm::vec3(wt.matrix[2]);
				const float		length = glm::length(forward);
				if (length > 1e-6f) { DebugDraw::Arrow(p, p + forward / length, style); }
			});
	}	// DebugDrawSystem::DrawAudio()

	void DebugDrawSystem::DrawParticleShape(World& world) {

		const Entity selected = GetSelectedEntity();
		if (selected == kNullEntity || !world.IsAlive(selected)) { return; }
		const ParticleEmitter* 	emitter = world.Read<ParticleEmitter>(selected);
		const WorldTransform*	wt		= world.Read<WorldTransform>(selected);
		if (!emitter || !wt || !wt->valid) { return; }

		DebugStyle style;
		style.category 	= DebugCategory::Particles;
		style.depthTest = false;	// the shape is usually inside the effect's own particles or a mesh
		style.color		= kParticleShape;

		const glm::mat4& m = wt->matrix;
		auto world3 = [&](const glm::vec3& local) { return glm::vec3(m * glm::vec4(local, 1.0f)); };
		// A circle of 'radius' around local point 'centre', in the plane of local axes a and b.
		auto circle = [&](const glm::vec3& centre, const glm::vec3& a, const glm::vec3& b, float radius) {
			if (radius <= 0.0f) { return; }
			constexpr int kSegments = 32;
			for (int i = 0; i < kSegments; ++i) {
				const float a0 = 6.2831853f * static_cast<float>(i) / kSegments;
				const float a1 = 6.2831853f * static_cast<float>(i + 1) / kSegments;
				DebugDraw::Line(world3(centre + (a * std::cos(a0) + b * std::sin(a0)) * radius),
								world3(centre + (a * std::cos(a1) + b * std::sin(a1)) * radius), style);
			}
		};
		const glm::vec3 X(1.0f, 0.0f, 0.0f), Y(0.0f, 1.0f, 0.0f), Z(0.0f, 0.0f, 1.0f), O(0.0f);
		const float radius = std::max(emitter->shapeRadius, 0.0f);

		switch (emitter->shape) {
			case ParticleShape::Point: {
				break;
			}
			case ParticleShape::Sphere: {
				circle(O, X, Z, radius);
				circle(O, X, Y, radius);
				circle(O, Y, Z, radius);
				break;
			}
			case ParticleShape::Box: {
				const glm::vec3 half = glm::max(emitter->boxExtents, glm::vec3(0.0f));
				DebugDraw::TransformedBox(-half, half, m, style);
				break;
			}
			case ParticleShape::Circle: {
				// The disc, and four short arrows from its rim: particles go straight out, along it.
				circle(O, X, Z, radius);
				for (const glm::vec3& side : { X, Z, -X, -Z }) {
					DebugDraw::Arrow(world3(side * radius), world3(side * (radius + 0.3f)), style);
				}
				break;
			}
			case ParticleShape::Cone: {
				// The near end (where particles start) and the far end coneLength along +Y, joined by
				// four lines down the cone's wall: what a rim particle follows. A cone that closes
				// has no far circle (radius 0): the four lines meet at its tip.
				const float length = std::max(emitter->coneLength, 0.01f);
				const float farRadius = ParticleSystem::ConeFarRadius(*emitter);   // not "far": a macro in <windows.h>
				circle(O, X, Z, radius);
				circle(Y * length, X, Z, farRadius);
				for (const glm::vec3& side : { X, Z, -X, -Z }) {
					DebugDraw::Line(world3(side * radius), world3(Y * length + side * farRadius), style);
				}
				break;
			}
		}
	}

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
