/*************************************************************************
* \file:         Components.h
* \author : Abhishek Akilandeswari Patchamuthu
* \par : Email : a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.2
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
	*
	**************************************************************************/
#pragma once

#include <cstdint>

#include"ComponentKind.h"
#include"Math/Math.h"
#include"Physics/PhysicsComponents.h"
#include"../Core/ECSCore.h"   // Entity (HierarchyNode links)
#include"Graphics/MeshHandle.h"   // MeshHandle - deliberately NOT RenderTypes.h, which pulls in Vulkan
#include"Graphics/MaterialHandle.h"   // MaterialHandle - same reasoning: header-only, no Vulkan
#include"Graphics/CubemapHandle.h"    // CubemapHandle (Skybox) - same reasoning
#include"Graphics/LightTypes.h"       // LightType (Light) - same reasoning

namespace Crucible
{
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
	 \note   Written ONLY by HierarchyTransformSystem, first in PreRender
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
		// Texture asset id of an equirectangular sky (.hdr ideally), resolved through
		// ResourceManager::GetCubemap. Saved in scenes; when non-zero it wins over `cubemap`.
		std::uint64_t skyboxAsset = 0;
	};

	/*************************************************************************
	 \brief  Playback state for an entity's audio source.
	 *************************************************************************/
	struct Audio
	{
		bool play = false;
		bool loop = false;
	};

	/*************************************************************************
	 \brief
		Linear motion state used by ECSPhysicsSystem to integrate Transform.
	 *************************************************************************/
	struct RigidBody
	{
	    Math::vec3 velocity{ 0.f,0.f,0.f };
		float mass = 1.f;
	};

	// Bind each component above to its ComponentKind (see ComponentKind.h)
	// and give ComponentRegistry a compile-time name for it.
	CRUCIBLE_COMPONENT(Transform, Transform);
	CRUCIBLE_COMPONENT(RigidBody, RigidBody);
	CRUCIBLE_COMPONENT(MeshRenderer, MeshRenderer);
	CRUCIBLE_COMPONENT(Audio, Audio);
	CRUCIBLE_COMPONENT(HierarchyNode, HierarchyNode);
	CRUCIBLE_COMPONENT(WorldTransform, WorldTransform);
	CRUCIBLE_COMPONENT(Light, Light);
	CRUCIBLE_COMPONENT(Skybox, Skybox);

	// Links span several entities: only World may write them.
	template <> struct WorldManaged<HierarchyNode> : std::true_type {};

	// Mandatory base scene components: every entity has them for its whole
	// life (see World::CreateEntity and kMandatoryComponents in World.h).
	template <> struct Mandatory<Transform> : std::true_type {};
	template <> struct Mandatory<WorldTransform> : std::true_type {};
	template <> struct Mandatory<HierarchyNode> : std::true_type {};


}