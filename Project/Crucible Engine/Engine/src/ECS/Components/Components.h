/*************************************************************************
* \file:         Components.h
* \author : Abhishek Akilandeswari Patchamuthu
* \par : Email : a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.6
* \brief
* Defines the actual per - entity component structs(plain
	* data, no logic) and binds each one to its
	* ComponentKind via CRUCIBLE_COMPONENT(see
		* ComponentKind.h).
	*
	*\details
	* This is the second(and last) file to touch when
	* adding a new component type - after adding the enum
	* value / list entry in ComponentKind.h, define the
	* struct here and bind it with CRUCIBLE_COMPONENT(Type,
		*KindValue) at the bottom.ComponentRegistry::RegisterAll()
	* (ComponentRegistry.cpp) then picks it up
	* automatically.Systems(e.g.ECSPhysicsSystem.h / .cpp)
	* read / write these structs through
	* World::GetComponent<T> / AddComponent<T>(see World.h).
	*
	*\history
	* -v1.0       initial Transform / MeshRenderer / Audio / RigidBody components
	* -v1.1       Transform rotation/scale; WorldTransform and HierarchyNode;
	*             mandatory base components (Transform, WorldTransform,
	*             HierarchyNode)
	* -v1.2       GLM: Transform's identity rotation is a constant (w, x, y, z)
	*             instead of an Euler-angle constructor	
	* -v1.3       Audio renamed to AudioSource (clashed with class Crucible::Audio)
	* -v1.4       AudioSource moved to Audio/AudioComponents.h
	* -v1.5       EntityName (mandatory display name, WorldValidated)
	* -v1.6       RequiresComponents pairs (what switching a component off
	*             also stops); component fields unchanged
	*
	*
	**************************************************************************/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "Animation/ModelPose.h"
#include "Animation/AnimatorController.h"

#include"ComponentKind.h"
#include"Math/Math.h"
#include"Physics/PhysicsComponents.h"
#include"Player/PlayerComponents.h"   // PlayerController, CameraOrbit (moved out of PhysicsComponents.h)
#include"Audio/AudioComponents.h"
#include"../Core/ECSCore.h"   // Entity (HierarchyNode links)
#include"Graphics/MeshHandle.h"   // MeshHandle - deliberately NOT RenderTypes.h, which pulls in Vulkan
#include"Graphics/MaterialHandle.h"   // MaterialHandle - same reasoning: header-only, no Vulkan
#include"Graphics/CubemapHandle.h"    // CubemapHandle (Skybox) - same reasoning
#include"Graphics/LightTypes.h"       // LightType (Light) - same reasoning
#include"../../UI/UIComponents.h"
#include"Text/WorldText.h"        // WorldText (text in the 3D world)
#include "Scripting/ScriptTypes.h"

namespace Crucible
{
    // Authored configuration only. Managed objects and transient diagnostics
    // belong to EntityScriptSystem, never to scene/undo/component copies.
    struct ScriptComponent {
        bool enabled = true;
        std::string assembly = "Managed/Crucible.Scripts.dll";
        std::string nameSpace = "Crucible.Scripts";
        std::string className;
        std::vector<ScriptFieldValue> fields;
        bool operator==(const ScriptComponent&) const = default;
    };
	/*************************************************************************
	 \brief  Position of an entity in local/world space.
	 *************************************************************************/
	struct Transform
	{
		Math::vec3  positions{0.f,0.f,0.f};                   // local: relative to the parent (or the world, for a root)
		Math::quat  rotation{ 1.f, 0.f, 0.f, 0.f };           // identity - glm::quat takes (w, x, y, z)
		Math::vec3  scale{ 1.f,1.f,1.f };
	};

	/*************************************************************************
	 \brief  The entity's final transform in world space.
	 \note   Written ONLY by HierarchyTransformSystem, after CPU poses in PreRender
			 (after every earlier stage's commands and events). Read it from
			 PreRender systems that RunAfter<HierarchyTransformSystem>(), or
			 after the frame's systems (the renderer); edit Transform instead.
			 valid == false means "not computed yet" (e.g. spawned during
			 PreRender) - skip it.
	 *************************************************************************/
	struct WorldTransform
	{
		Math::mat4 matrix = Math::mat4(1.0f);
		bool valid = false;   // false until the propagation system has computed it
		bool socketApplied = false; // detects removal/disable without a local TRS edit
	};

	/*************************************************************************
	 \brief  An entity's place in the scene tree, as intrusive links.
	 \details
		Children form a doubly linked list through prevSibling/nextSibling,
		so attach/detach are O(1) and nothing is heap-allocated. Every
		entity has one (it is a mandatory base component); a root with no
		children simply has all links null.
	 \note   Read-only outside World: every public write path refuses it at
			 compile time (see WorldManaged). Use World::SetParent/
			 DetachChildren/SetSiblingIndex, which keep both ends of every
			 link consistent. (Named HierarchyNode because the editor already
			 has a Crucible::Hierarchy panel class.)
			 Its change tick (LastChanged / change filters) means "this
			 entity was reparented": SetSiblingIndex and edits to a parent's
			 or siblings' links don't stamp it, because nothing moves.
	 *************************************************************************/
	struct HierarchyNode
	{
		Entity parent{};
		Entity firstChild{};
		Entity lastChild{};
		Entity prevSibling{};
		Entity nextSibling{};
		std::uint32_t childCount = 0;
	};

	/*************************************************************************
	 \brief
		Data needed to draw an entity's mesh.
	 \details
		Purely descriptive - no dependency on a specific renderer, only on
		the Math types and the Renderer's opaque MeshHandle (a plain
		uint32_t-wrapping struct - see Graphics/MeshHandle.h - so this
		still doesn't pull in Vulkan or any renderer internals).
		`mesh` starts Invalid (MeshHandle::Invalid); it's set once the
		resource manager has uploaded the entity's model (Renderer::
		CreateMesh/CreateMeshAsync) and knows the resulting handle.
		RenderExtractSystem (Graphics/RenderExtractSystem.h) skips any
		entity whose mesh IsValid() is still false, or whose visible is
		false, so a not-yet-loaded model is simply not drawn yet rather
		than crashing.
	 *************************************************************************/
	struct AnimatedModel
	{
		// Playback/pose for the complete model owned by MeshRenderer on this entity.
		std::uint32_t clip = 0;
		float speed = 1.0f;
		bool loop = true;
		bool previewInEditor = true;
		bool playOnStart = true;
		std::uint64_t controllerAsset = 0; // 0 keeps direct single-clip playback

		// Per-entity CPU state only. Never reflected/serialized or shared between instances.
		float time = 0.0f;
		bool playing = true;
		bool initialized = false, poseValid = false, poseFailed = false;
		std::uint64_t evaluatedAsset = 0;
		std::uint32_t evaluatedClip = InvalidModelIndex;
		ModelPose pose;
		std::uint64_t evaluatedController = 0;
		bool controllerFailed = false;
		AnimatorInstance controller;
		std::shared_ptr<const AnimatorController> controllerDefinition; // pins a saved revision until reset
	};

	struct MeshRenderer
	{
		bool visible = true;
		Math::vec4 colour{ 0.f,0.f,0.f,0.f };
		MeshHandle mesh{};
		// Invalid -> RenderExtractSystem falls back to Renderer::GetDefaultMaterial() (a plain
		// field, not a separate ECS component, because Query's "optional" component matching is
		// an unimplemented no-op - see Query.h).
		MaterialHandle material{};

		// Asset ids (the .meta GUID, or a ResourceManager built-in id such as "Cube"); 0 = none.
		// These are what scene files save and what the Inspector's pickers set (via
		// ResourceManager::AssignMesh/AssignMaterial). When non-zero they WIN over the handles
		// above: RenderExtractSystem resolves them through the ResourceManager every frame, so an
		// entity loaded from a scene (ids only, handles Invalid) draws with no extra step. Leave
		// them 0 for procedural/runtime-created meshes and materials - the handles are used as-is.
		std::uint64_t meshAsset = 0;
		std::uint64_t materialAsset = 0;

		// Complete compiled model; when nonzero this takes precedence over meshAsset/mesh.
		std::uint64_t modelAsset = 0;
		std::uint32_t modelRoot = InvalidModelIndex; // complete model, or a rebased node subtree
		// Indexed by imported material index. 0 inherits the global material or import default.
		// materialAsset/material above are the optional override for every model primitive.
		std::vector<std::uint64_t> materialOverrides;
	};

	// Mount this entity on a node of its ECS parent's model. Transform is the
	// alignment offset in that node's coordinates. No entity-per-bone hierarchy.
	struct SocketAttachment
	{
		bool enabled = true;
		std::string socketName; // optional gameplay alias; indices disambiguate duplicate node names
		std::uint64_t modelAsset = 0; // expected parent model; changing models requires rebinding
		std::uint32_t node = InvalidModelIndex;
		// Runtime-only shared CPU resources, never serialized.
		std::uint64_t cachedAsset = 0;
		std::shared_ptr<const ModelResource> model;
		std::shared_ptr<const ModelPose> restPose;
	};
	
	/*************************************************************************
	 \brief
		A light source. Position and direction come from the entity's
		WorldTransform, so lights parent, move and rotate like anything else.
	 \details
		RenderExtractSystem submits every enabled Light each frame
		(Renderer::SubmitLight). Conventions:
		  - Position: the WorldTransform's translation (Point/Spot only).
		  - Direction: the WorldTransform's local -Z axis (GLM's "forward"),
		    i.e. the way the light travels (Directional/Spot only). With an
		    identity rotation a directional light shines along -Z; pitch the
		    entity down to aim it at the ground.
		  - intensity: Directional = radiance; Point/Spot = radiant intensity
		    with inverse-square falloff (radiance 1 at distance 1 per unit of
		    intensity), smoothly windowed to zero at `range`.
		  - innerConeDeg/outerConeDeg: Spot half-angles (full intensity inside
		    inner, fading to zero at outer).
		If no Light is submitted at all in a frame, the Renderer falls back
		to one built-in directional "sun" so an unlit scene is never black;
		adding any Light entity takes over from it.
	 *************************************************************************/
	struct Light
	{
		LightType  type = LightType::Point;
		Math::vec3 colour{ 1.f, 1.f, 1.f };
		float      intensity = 10.f;
		float      range = 10.f;
		float      innerConeDeg = 20.f;
		float      outerConeDeg = 30.f;
		bool       enabled = true;
		// Directional lights only (for now): the first enabled directional
		// light with castShadows renders cascaded shadow maps.
		bool       castShadows = true;
	};

	/*************************************************************************
	 \brief
		Scene-wide sky and ambient settings. Put it on exactly one entity
		(if several exist, whichever the query visits last wins).
	 \details
		RenderExtractSystem hands it to Renderer::SetEnvironment() each
		frame; with no Skybox entity the Renderer uses its defaults (the
		built-in procedural sky, default ambient, exposure 1).
		  - cubemap: from Renderer::CreateCubemap()/CreateCubemapFromEquirect();
		    Invalid -> the built-in procedural sky. Runtime-only (not saved
		    in scene files yet), same as MeshRenderer::mesh - see the asset
		    GUID -> handle mapping planned for the Inspector.
		  - visible: false -> no sky is drawn (the clear colour shows).
		  - ambientSky/ambientGround/ambientIntensity: hemisphere ambient
		    light (a cheap stand-in for image-based lighting).
		  - exposure: multiplier before tonemapping, for meshes and sky alike.
		  - ambientFromSky: true -> objects are lit by the sky image itself
		    (and shiny ones reflect it), scaled by ambientIntensity * intensity;
		    false -> the two flat ambient colours above.
		  - fog: distant things fade into fogColour. fogDensity 0 = off (try
		    0.01-0.05); fogHeightFalloff = how quickly the fog thins above
		    fogBaseHeight (0 = equally thick at every height); fogStart =
		    clear distance in front of the camera; fogMaxOpacity = the most
		    the fog may hide (1 = everything).
		  - the look of the final picture, each neutral by default:
		    contrast (1), saturation (1; 0 = black and white), temperature
		    (0; negative = cooler/bluer, positive = warmer), colourFilter
		    (white; a tint over everything), vignetteIntensity (0; dark
		    corners) with vignetteSoftness (how far in they reach), filmGrain
		    (0) and chromaticAberration (0; colour fringes toward the edges).
	 *************************************************************************/
	struct Skybox
	{
		CubemapHandle cubemap{};
		bool          visible = true;
		float         intensity = 1.f;
		Math::vec3    ambientSky{ 0.30f, 0.36f, 0.45f };
		Math::vec3    ambientGround{ 0.20f, 0.18f, 0.16f };
		float         ambientIntensity = 1.f;
		float         exposure = 1.f;
		bool          ambientFromSky = true;
		float         fogDensity = 0.f;
		Math::vec3    fogColour{ 0.55f, 0.63f, 0.72f };
		float         fogHeightFalloff = 0.08f;
		float         fogBaseHeight = 0.f;
		float         fogStart = 0.f;
		float         fogMaxOpacity = 1.f;
		// Volumetric fog: the fog lit by the sun (light shafts through its shadows) and by point and
		// spot lights. intensity = how strongly the lights scatter in it; anisotropy 0 = scatters
		// evenly, toward 1 = the glow gathers around lights you look toward; distance = metres the
		// volume (and so the shafts) reaches - beyond it the fog stays lit, unshadowed.
		// ambient: how bright the fog is in shade - 1 = fogColour (the plain fog's look), lower =
		// darker shade, so light shafts stand out more (the fog keeps its colour).
		// skyAmbient: the fog in shade is lit by the sky's average light instead, like surfaces in
		// shade (ambientFromSky / ambientIntensity), tinted by fogColour; follows the sky (a dark
		// night sky gives dark fog). Off = fogColour.
		bool          volumetricFog = true;
		float         volumetricIntensity = 1.f;
		float         volumetricAnisotropy = 0.5f;
		float         volumetricDistance = 100.f;
		float         volumetricAmbient = 1.f;
		bool          volumetricSkyAmbient = false;
		float         contrast = 1.f;
		float         saturation = 1.f;
		float         temperature = 0.f;
		Math::vec3    colourFilter{ 1.f, 1.f, 1.f };
		float         vignetteIntensity = 0.f;
		float         vignetteSoftness = 0.5f;
		float         filmGrain = 0.f;
		float         chromaticAberration = 0.f;
		// Toon shading for every mesh whose material's Shading is "Scene" (instead of the
		// realistic lighting); these settings are also the toon look of materials set to "Toon":
		//   threshold / softness: where the light/shade edge falls (0..1 of the light a surface
		//     gets, so 0.1 = just before the light grazes it) and how soft it is (small = hard).
		//   shadeColour: tint of the shaded side (sRGB; slightly cool - the sky's own colour
		//     does most of the work).
		//   shadeBrightness: the shaded side is lit by the sky's light (ambientFromSky / the
		//     ambient colours, x ambientIntensity) times this - so shade follows the sky (blue by
		//     day, warm at sunset, dark at night) without brightening the lit side.
		//   litShading: how much the lit side still shades by angle (0 = one flat tone, so faces
		//     turned differently look the same; 1 = full shading).
		//   edgeColour / edgeWidth / edgeStrength: the coloured band painters put just inside the
		//     lit side of the edge; the tint keeps its brightness, only the hue shifts.
		//   ambientFlatten: 0 = the sky's light shades surfaces softly by direction, 1 = flat.
		//   highlight: a hard-edged shine on smooth (rough < 0.85) and metal surfaces; 0 = none.
		//   reflections: how much sky reflection toon surfaces keep (0 = none, the toon look).
		// Toon outlines (toon-shaded materials only; each material scales them by its Outline):
		//   lines: on/off. lineDarkness: how much darker than the surface a line is (0..1).
		//   lineWidth: in pixels (0.5..3, fractions give a partly darkened last pixel).
		//   lineCreaseAngle / lineCreaseStrength: how sharp a fold inside a shape must be to get a line
		//     (degrees), and how dark those lines are next to silhouettes (0 = silhouettes only).
		//   lineDepthGap: how far (as a fraction of the distance) a shape must stand in front of what
		//     is behind it to get a silhouette line. lineBreakup / lineBreakupSize: how much of each
		//     line is left out (0 = solid) and the size of the gaps (metres; fixed to the surfaces).
		//   lineFadeDistance: lines fade out by this distance (metres), and fade with fog too.
		bool          toonShading = false;
		float         toonThreshold = 0.1f;
		float         toonSoftness = 0.03f;
		Math::vec3    toonShadeColour{ 0.85f, 0.87f, 0.95f };
		float         toonShadeBrightness = 1.5f;
		float         toonLitShading = 0.4f;
		Math::vec3    toonEdgeColour{ 1.0f, 0.62f, 0.42f };
		float         toonEdgeWidth = 0.15f;
		float         toonEdgeStrength = 0.6f;
		float         toonAmbientFlatten = 0.7f;
		float         toonHighlight = 1.f;
		float         toonReflections = 0.f;
		bool          toonLines = true;
		float         toonLineDarkness = 0.6f;
		float         toonLineWidth = 1.f;
		float         toonLineCreaseAngle = 75.f;
		float         toonLineCreaseStrength = 0.5f;
		float         toonLineDepthGap = 0.03f;
		float         toonLineBreakup = 0.35f;
		float         toonLineBreakupSize = 0.25f;
		float         toonLineFadeDistance = 60.f;
		// Texture asset id of an equirectangular sky (.hdr ideally), resolved through
		// ResourceManager::GetCubemap. Saved in scenes; when non-zero it wins over `cubemap`.
		std::uint64_t skyboxAsset = 0;
	};

	/*************************************************************************
	 \brief
		A game camera. The Game view (and the window, in a standalone build)
		looks through the enabled camera with the highest priority; with no
		enabled camera in the scene, nothing is rendered there.
	 \details
		Placement comes from the entity's Transform: position = where it is,
		looking down its local -Z axis with local +Y up (the same convention
		as Light). The aspect ratio is not stored - it always follows the
		render target.
		Named CameraComponent only because Crucible::Camera is already the
		Renderer's camera class; it is shown and saved as "Camera".
		The editor's Scene-view camera is NOT one of these: it is owned by
		the Scene view panel, never appears in the hierarchy, and so cannot
		be edited or deleted with the scene.
	 *************************************************************************/
	struct CameraComponent
	{
		bool  enabled  = true;
		float fovY     = 60.f;     // vertical field of view, degrees
		float nearZ    = 0.1f;     // near clip plane, metres
		float farZ     = 1000.f;   // far clip plane, metres
		int   priority = 0;        // highest enabled camera wins (ties: the first one found)
	};

	/*************************************************************************
	 \brief
		Per-object adjustments to the material a MeshRenderer on the same
		entity draws with. The material itself is shared by every object
		using it; these values belong to this one object, and objects with
		different values are still drawn together in one instanced draw.
	 \details
		- emissiveScale: multiplies the material's emissive (glow). 0 = off,
		  1 = as authored, above 1 = brighter (and, past the bloom
		  threshold, glowing). Animate it from gameplay code for "charged /
		  ready to fire" effects.
		- custom: three spare numbers passed to the shader untouched
		  (InstanceData::params.yzw), reserved for effects to come.
		An entity without this component draws as if it had the defaults.
	 *************************************************************************/
	struct MaterialParams
	{
		float      emissiveScale = 1.f;
		Math::vec3 custom{ 0.f, 0.f, 0.f };
	};

	/*************************************************************************
	 \brief
		TEMPORARY - load testing only. StressMoverSystem walks the entity
		around a circle: Transform::positions = origin + a point on a circle
		of `radius`, turning at `speed` radians per second, starting at
		`phase` radians. Used by the "Stress Moving" scenes to measure what
		thousands of moving entities cost. Remove it (component, system,
		reflection file, scenes) once real gameplay movement exists.
	 *************************************************************************/
	struct StressMover
	{
		Math::vec3 origin{ 0.f, 0.f, 0.f };
		float      radius = 6.f;
		float      speed = 0.5f;
		float      phase = 0.f;
	};

	/*************************************************************************
	 \brief
		A water surface: a pond, lake or slow river, drawn in the style of
		the "Umiko" reference (WaterPass / Water.slang).

	 \details
		The surface is the built-in Plane (1 x 1 m, facing up) scaled by the
		entity's Transform - scale X and Z to size the water, and place it at
		the water line. If the entity also has a MeshRenderer, that mesh is
		the surface instead (shaped in Blender, say) and is not drawn as an
		ordinary object. Seen from above only.

		What it shows comes from what lies under it, so it needs ground (a
		bottom and shores) beneath it:
		  - shallowColour -> deepColour with the water's depth (sRGB, as
		    picked); deepDepth: metres of water for the full deep colour.
		  - clarity: how far (m) through the water the bottom still shows.
		  - refraction: how much the ripples bend the view of the bottom,
		    and how much cast shadows on the surface wobble with them (0..1).
		  - foamColour; foamWidth: the crisp shore band, in metres of water
		    depth (0 = no foam), plus a thinner line lapping further out;
		    foamBreakup: 0 = an even band, 1 = ragged and broken.
		  - rippleSize (m), rippleSpeed (m/s), rippleStrength (how far they
		    tilt the surface); rippleLines: thin drawn strokes on open water
		    (0 = none); rippleFoam: switches those strokes on or off, keeping
		    the rippleLines amount (the shore foam is not affected);
		    rippleLineColour: their colour (the shore foam keeps foamColour).
		  - glints: strength of the hard sparkles of the sun and lamps;
		    glintColour tints them (white = the light's own colour).
		  - reflection: how much sky the surface reflects at grazing angles;
		    reflectionColour tints the reflected sky (white = as it is).
		  Colours are sRGB, as picked.
		Toon-shaded scenes light the water with the Skybox's toon edge and
		shade tint, so shadows on it are crisp.
	 *************************************************************************/
	struct Water
	{
		Math::vec3 shallowColour{ 0.45f, 0.85f, 0.72f };
		Math::vec3 deepColour{ 0.05f, 0.35f, 0.55f };
		float      deepDepth = 2.5f;
		float      clarity = 3.f;
		float      refraction = 0.4f;
		Math::vec3 foamColour{ 1.f, 1.f, 1.f };
		float      foamWidth = 0.2f;
		float      foamBreakup = 0.5f;
		float      rippleSize = 2.f;
		float      rippleSpeed = 0.25f;
		float      rippleStrength = 0.25f;
		bool       rippleFoam = true;
		float      rippleLines = 0.3f;
		Math::vec3 rippleLineColour{ 1.f, 1.f, 1.f };
		float      glints = 1.f;
		Math::vec3 glintColour{ 1.f, 1.f, 1.f };
		float      reflection = 0.5f;
		Math::vec3 reflectionColour{ 1.f, 1.f, 1.f };
	};

	/// Where a ParticleEmitter's new particles appear, in the emitter's local space.
	enum class ParticleShape : std::uint8_t
	{
		Point,    // all at the emitter
		Sphere,   // anywhere inside a sphere of shapeRadius
		Box       // anywhere inside a box of boxExtents (half sizes)
	};

	/// How a ParticleEmitter's particles are mixed with what is behind them.
	enum class ParticleBlend : std::uint8_t
	{
		Additive,   // colours add up: sparks, fire, magic. Order doesn't matter, never darkens.
		Alpha       // covers what is behind: smoke, dust, splashes. Sorted back to front.
	};

	/*************************************************************************
	 \brief
		A particle effect: sparks, fire, smoke, a splash... One component,
		many effects - every kind is the same machine (spawn, move, age, draw
		as a camera-facing quad) with different numbers, so the differences
		are these fields rather than different code. MakeParticlePreset()
		(Graphics/ParticlePresets.h) fills them in for the common effects.

	 \details
		ParticleSystem simulates the particles on the CPU and hands them to
		the Renderer each frame. The particles are NOT entities; they live
		in a pool owned by ParticleSystem, keyed by this entity. The emitter
		sits at the entity's Transform and emits around its local +Y axis.

		Spawning
		  - playing / loop / duration: an emitter runs for `duration`
		    seconds, then restarts (loop) or stops spawning. Particles already
		    alive always finish their lives.
		  - rate: particles per second, continuously.
		  - burstCount: particles all at once at the start of each run.
		  - maxParticles: hard cap on how many are alive at once.
		  - shape / shapeRadius / boxExtents: where they appear.
		  - spreadDeg: half-angle of the cone of directions around local +Y
		    (0 = straight up, 90 = a hemisphere, 180 = every direction).
		  - worldSpace: true = once spawned, particles ignore the emitter's
		    movement (a trail); false = they move with it.
		Each particle
		  - lifetimeMin/Max (seconds), speedMin/Max (m/s): picked at random.
		  - sizeMin/Max: starting width in metres; endSizeScale multiplies it
		    by the end of its life (0.2 = shrinks, 3 = grows).
		  - gravityScale: 1 = falls like a rock, 0 = floats, negative = rises.
		  - drag: how fast it loses speed (1/s).
		  - spinDeg: random spin up to +- this many degrees per second
		    (also gives each particle a random starting angle).
		  - stretch: 0 = square; above 0 the quad is stretched along its
		    motion by this many seconds of travel (sparks, rain).
		Look
		  - colourStart/End + alphaStart/End: blended over its life.
		  - brightness: multiplies the colour. Above 1 it glows (bloom).
		  - blend: Additive or Alpha (see ParticleBlend).
		  - textureAsset: a Texture asset; 0 = a soft round dot.
		  - flipbookCols/Rows: the texture is an atlas of this many frames,
		    played once over each particle's life.
		Lighting the scene
		  - emitLight: the emitter also adds ONE point light at the middle
		    of its live particles, fading as they die - not a light per
		    particle. lightColour / lightIntensity (at full strength) /
		    lightRange as on a Light component.
	 *************************************************************************/
	struct ParticleEmitter
	{
		bool          playing = true;
		bool          loop = true;
		float         duration = 2.f;
		float         rate = 20.f;
		int           burstCount = 0;
		int           maxParticles = 500;

		ParticleShape shape = ParticleShape::Point;
		float         shapeRadius = 0.25f;
		Math::vec3    boxExtents{ 0.5f, 0.5f, 0.5f };
		float         spreadDeg = 25.f;
		bool          worldSpace = true;

		float         lifetimeMin = 0.8f;
		float         lifetimeMax = 1.5f;
		float         speedMin = 1.f;
		float         speedMax = 2.f;
		float         sizeMin = 0.1f;
		float         sizeMax = 0.2f;
		float         endSizeScale = 1.f;
		float         gravityScale = 0.f;
		float         drag = 0.f;
		float         spinDeg = 0.f;
		float         stretch = 0.f;

		Math::vec3    colourStart{ 1.f, 1.f, 1.f };
		Math::vec3    colourEnd{ 1.f, 1.f, 1.f };
		float         alphaStart = 1.f;
		float         alphaEnd = 0.f;
		float         brightness = 1.f;
		ParticleBlend blend = ParticleBlend::Additive;
		std::uint64_t textureAsset = 0;
		int           flipbookCols = 1;
		int           flipbookRows = 1;

		bool          emitLight = false;
		Math::vec3    lightColour{ 1.f, 0.6f, 0.25f };
		float         lightIntensity = 8.f;
		float         lightRange = 6.f;

		int           seed = 1;   // different seeds give different-looking runs of the same effect
	};

	/*************************************************************************
	 \brief
		Linear motion state used by ECSPhysicsSystem to integrate Transform.
	 *************************************************************************/
	// Unity's Rigidbody.constraints. Each flag stops the SIMULATION moving the body along
	// (position) or turning it around (rotation) that WORLD axis - gravity, hits and pushes
	// included. Gameplay can still teleport it by writing the Transform.
	// All six frozen = nothing left to simulate: the body becomes static (part of the arena).
	struct RigidBodyConstraints
	{
		bool freezePositionX = false;
		bool freezePositionY = false;
		bool freezePositionZ = false;
		bool freezeRotationX = false;
		bool freezeRotationY = false;
		bool freezeRotationZ = false;
	};

	struct RigidBody
	{
	    Math::vec3 velocity{ 0.f,0.f,0.f };
		float mass = 1.f;

		// Unity's Rigidbody.useGravity / drag / angularDrag, with Unity's defaults.
		bool  useGravity  = true;    // false: the body floats, but still collides and can be pushed
		float drag        = 0.f;     // linear damping, 1/s. 0 = keeps its speed; higher = slows down faster
		float angularDrag = 0.05f;   // rotational damping, 1/s. Stops a knocked-over crate spinning forever

		RigidBodyConstraints constraints{};   // frozen axes; e.g. freeze rotation X+Z so a player body never tips over

		// Runtime only: not saved, not in the Inspector. Gameplay adds to these
		// any time during the frame; ECSPhysicsSystem hands the totals to every
		// fixed step this frame, then zeroes them. Unlike Unity, calling this
		// from a normal Update is correct - the push does not depend on frame rate.
		Math::vec3 force { 0.f, 0.f, 0.f };   // newtons, world space, at the centre of mass
		Math::vec3 torque{ 0.f, 0.f, 0.f };   // newton-metres, world space

		// An instant kick (a jump, a knockback). Unlike force it is kept until a
		// physics step actually runs, so a kick from a frame with no step is
		// not lost - it lands one frame later instead.
		Math::vec3 impulse{ 0.f, 0.f, 0.f };  // kg*m/s, world space, at the centre of mass

		void AddForce    (const Math::vec3& f) { force   += f; }   // Unity's Rigidbody.AddForce
		void AddTorque   (const Math::vec3& t) { torque  += t; }   // Unity's Rigidbody.AddTorque
		void ApplyImpulse(const Math::vec3& i) { impulse += i; }   // Unity's AddForce(..., ForceMode.Impulse)
	};
    
	/*************************************************************************
	 \brief
		The entity's display name (Hierarchy, Inspector). Mandatory: every
		entity has one, saved with the scene like any other component.
	 \details
		Empty means "not named yet": the World gives such an entity the next
		numbered default ("Entity 1", "Entity 2"...) when it is created - see
		World::CreateNamedEntity and World::SetEntityName. Names need not be
		unique; only the generated defaults are kept distinct. Read-only
		outside World (WorldValidated): rename with World::SetEntityName.
	 *************************************************************************/
	struct EntityName
	{
		std::string name;
	};

	/*************************************************************************
	 \brief  Links an entity to the prefab (.cprefab) it was placed from.
	 \details
		Prefab::Instantiate puts one on every entity it creates; the editor's
		Create Prefab puts them on the entities a prefab was made from.
		prefabAsset is the .cprefab's asset guid (from its .meta), fileId the
		entry the entity came from - stable when the file is re-saved, unlike
		the entry's position - and isRoot marks the instance's top entity.
		Saved in scenes, so the link survives a reload, undo and copy/paste;
		Prefab::SaveFrom never writes it into a .cprefab. The Inspector does
		not list it: it shows a prefab bar instead (Prefabs/PrefabAuthoring.h).
	 *************************************************************************/
	struct PrefabInstance
	{
		std::uint64_t prefabAsset = 0;      // asset guid of the .cprefab
		std::uint64_t fileId      = 0;      // the entry's id inside that file
		bool          isRoot      = false;  // the instance's top entity
		// Root only: content hash of the .cprefab this instance was last brought up to date with
		// (0 = not known yet). When the file's hash differs - changed in another scene, or by a
		// git pull - the editor updates the instance on scene open / Asset Browser Refresh.
		std::uint64_t prefabHash  = 0;
		// What this entity changed from its prefab entry, as of the last scene save (the editor
		// refreshes it before every save): "Collider/m_size", "Collider/$enabled", "+Light"
		// (added), "-Collider" (removed). Propagation uses it to know what to keep.
		std::vector<std::string> overrides;
	};

	// Bind each component above to its ComponentKind (see ComponentKind.h)
	// and give ComponentRegistry a compile-time name for it.
	CRUCIBLE_COMPONENT(Transform, Transform);
	CRUCIBLE_COMPONENT(RigidBody, RigidBody);
	CRUCIBLE_COMPONENT(MeshRenderer, MeshRenderer);
	CRUCIBLE_COMPONENT(AnimatedModel, AnimatedModel);
	CRUCIBLE_COMPONENT(SocketAttachment, SocketAttachment);
	CRUCIBLE_COMPONENT(ScriptComponent, ScriptComponent);
	CRUCIBLE_COMPONENT(HierarchyNode, HierarchyNode);
	CRUCIBLE_COMPONENT(WorldTransform, WorldTransform);
	CRUCIBLE_COMPONENT(Light, Light);
	CRUCIBLE_COMPONENT(Skybox, Skybox);
	CRUCIBLE_COMPONENT(ParticleEmitter, ParticleEmitter);
	CRUCIBLE_COMPONENT(MaterialParams, MaterialParams);
	CRUCIBLE_COMPONENT(StressMover, StressMover);
	CRUCIBLE_COMPONENT(EntityName, EntityName);
	CRUCIBLE_COMPONENT(Water, Water);
	CRUCIBLE_COMPONENT(PrefabInstance, PrefabInstance);
	// Written out by hand instead of CRUCIBLE_COMPONENT so the name shown in the Inspector and
	// used as the scene-file key is "Camera", not "CameraComponent".
	template<>
	struct ComponentTraits<CameraComponent>
	{
		static constexpr ComponentKind kind = ComponentKind::Camera;
		static constexpr std::string_view name = "Camera";
	};

	// Links span several entities: only World may write them.
	template <> struct WorldManaged<HierarchyNode> : std::true_type {};

	// Mandatory base scene components: every entity has them for its whole
	// life (see World::CreateEntity and kMandatoryComponents in World.h).
	template <> struct Mandatory<Transform> : std::true_type {};
	template <> struct Mandatory<WorldTransform> : std::true_type {};
	template <> struct Mandatory<HierarchyNode> : std::true_type {};
	template <> struct Mandatory<EntityName> : std::true_type {};

	// Read anywhere; written only through World (SetEntityName, AddComponent), which applies the naming rules.
	template <> struct WorldValidated<EntityName> : std::true_type {};

	// What each component needs on the SAME entity to run (RequiresComponents in
	// ComponentKind.h): while a requirement is switched off, the component counts as
	// off too and its systems skip it; it comes back on by itself. The same pairs the
	// Inspector's Add Component rules create. Requirements follow through: switching
	// UIRect off also stops UIButton (UIButton -> UIImage -> UIRect).
	template <> struct RequiresComponents<RigidBody>        { using type = ComponentTypeList<Collider>; };
	template <> struct RequiresComponents<PlayerController> { using type = ComponentTypeList<CharacterController>; };
	template <> struct RequiresComponents<AnimatedModel>    { using type = ComponentTypeList<MeshRenderer>; };
	template <> struct RequiresComponents<MaterialParams>   { using type = ComponentTypeList<MeshRenderer>; };
	template <> struct RequiresComponents<UIImage>          { using type = ComponentTypeList<UIRect>; };
	template <> struct RequiresComponents<UIText>           { using type = ComponentTypeList<UIRect>; };
	template <> struct RequiresComponents<UISlider>         { using type = ComponentTypeList<UIRect>; };
	template <> struct RequiresComponents<UIButton>         { using type = ComponentTypeList<UIImage>; };


}
