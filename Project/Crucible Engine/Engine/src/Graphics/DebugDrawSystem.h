#pragma once
/*************************************************************************
* \file:         DebugDrawSystem.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          1 October 2026
* \version       1.0
* \brief
*                ECS system that turns scene state into DebugDraw lines:
*                collider shapes, velocity arrows, light gizmos, the ground
*                grid, and the selected entity's bounds + axes.
*
* \details
*                Runs in PreRender after HierarchyTransformSystem, every
*                frame, and only emits categories that are switched on (see
*                DebugDraw::SetCategoryEnabled) - and nothing at all while no
*                view displays debug lines (DebugDraw::IsActive()).
*
*                What it draws, per DebugCategory:
*                  - Colliders:  every Collider at the pose physics uses
*                                (Transform position/rotation + m_center;
*                                colliders are not scaled). Green = static,
*                                cyan = dynamic (has a RigidBody), yellow =
*                                CharacterController capsule.
* 
*                  - Velocities: an arrow from each RigidBody /
*                                CharacterController along its velocity,
*                                |v| x VelocityScale long, yellow -> red as
*                                speed rises towards 20 m/s.
* 
*                  - Lights:     the selected light's full shape (point range
*                                sphere, spot cone to its range, sun arrow);
*                                other lights get a short direction arrow
*                                (their position is shown by the editor icon).
* 
*                  - Audio:      a 3D AudioSource's ranges: an inner sphere
*                                at Min Distance (full volume inside it) and
*                                an outer one at Max Distance - bright with
*                                Linear rolloff (silent there), dim with
*                                Logarithmic (it only stops getting quieter
*                                there). For the selected source, or every
*                                source (SetAudioRangesForAll). Grey when
*                                the source is switched off or has no clip.
*                                2D sources (Spatial Blend 0) have no range.
*                                Each AudioListener gets an arrow the way it
*                                faces (local -Z), so ears turned the wrong
*                                way - left and right swapped - show.

*                  - Particles:	 the selected ParticleEmitter's shape, as it
								 spawns (scaled and turned with the entity):
								 the sphere, box or disc they start in, and
								 an arrow along local +Y. A Cone shows its
								 near disc (shapeRadius) and, 1 m along, how
								 wide coneAngleDeg has opened it.
* 
*                  - Grid:       a 40 x 40 m, 1 m ground grid on y = 0 plus
*                                the world X (red) and Z (blue) axes.
* 
*                  - Selection:  the selected entity's mesh bounds (orange)
*                                and its local axes, drawn on top.
*
*                The selection is a plain static the Editor sets every frame
*                (SetSelectedEntity) - the engine has no other notion of
*                "selected"; in the Game build it stays kNullEntity.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "EngineExport.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/IECSSystems.h"

namespace Crucible
{
	class Renderer;
	class ResourceManager;

	/*************************************************************************
	 \brief  See the file header.
	 *************************************************************************/
	class ENGINE_API DebugDrawSystem final : public IECSSystem
	{
	public:
		/*********************************************************************
		 \brief  Holds the Renderer (mesh bounds) and ResourceManager (asset
				 id -> mesh) used for the selection box. Both outlive systems.
		 \param[in] renderer  The Core-owned Renderer.
		 \param[in] resources The Core-owned ResourceManager (may be null).
		 *********************************************************************/
		DebugDrawSystem(Renderer& renderer, ResourceManager* resources)
			: m_renderer(renderer), m_resources(resources) {}

		/*********************************************************************
		 \brief  PreRender, like RenderExtractSystem.
		 \return SystemStage::PreRender.
		 *********************************************************************/
		SystemStage Stage() const override { return SystemStage::PreRender; }

		/*********************************************************************
		 \brief  Default priority.
		 \return 0.
		 *********************************************************************/
		int Priority() const override { return 0; }

		/*********************************************************************
		 \brief  Reads only; runs after HierarchyTransformSystem on the main
				 thread (it calls into the Renderer/ResourceManager).
		 \param[in] desc The descriptor to fill in.
		 *********************************************************************/
		void Describe(SystemDesc& desc) const override;

		/*********************************************************************
		 \brief  Emits this frame's debug lines for every enabled category.
		 \param[in] world The World to read.
		 \param[in] ctx   Unused.
		 *********************************************************************/
		void Update(World& world, SystemContext& ctx) override;

		/*********************************************************************
		 \brief  Which entity the editor has selected (kNullEntity = none).
		 \param[in] entity The selected entity.
		 *********************************************************************/
		static void   SetSelectedEntity(Entity entity);

		/*********************************************************************
		 \brief  The entity last passed to SetSelectedEntity().
		 \return The selected entity, or kNullEntity.
		 *********************************************************************/
		static Entity GetSelectedEntity();

		/*********************************************************************
		 \brief  Arrow length per unit of speed (metres per m/s). Default
				 0.25, so 4 m/s draws a 1 m arrow.
		 \param[in] scale The new scale (clamped to >= 0).
		 *********************************************************************/
		static void   SetVelocityScale(float scale);

		/*********************************************************************
		 \brief  The current velocity arrow scale.
		 \return Metres of arrow per m/s.
		 *********************************************************************/
		static float  GetVelocityScale();

		/*********************************************************************
		 \brief  Whether colliders are drawn through whatever is in front of
				 them. Off by default: a collider behind a wall is hidden,
				 like the object it belongs to.
		 \param[in] enabled True to see every collider regardless of what
							covers it (e.g. to find one buried in a mesh).
		 *********************************************************************/
		static void   SetCollidersThroughWalls(bool enabled);

		/*********************************************************************
		 \brief  The current setting of SetCollidersThroughWalls().
		 \return True when colliders are drawn through other objects.
		 *********************************************************************/
		static bool   GetCollidersThroughWalls();

		/*********************************************************************
		 \brief  Whether every 3D AudioSource shows its ranges, or only the
				 selected one. Off by default: Max Distance is 500 m unless
				 changed, so a scene of sources is mostly spheres.
		 \param[in] enabled True to draw every source's ranges (e.g. to see
							where ambient sounds overlap).
		 *********************************************************************/
		static void   SetAudioRangesForAll(bool enabled);

		/*********************************************************************
		 \brief  The current setting of SetAudioRangesForAll().
		 \return True when every source's ranges are drawn.
		 *********************************************************************/
		static bool   GetAudioRangesForAll();

		/*********************************************************************
		 \brief  The camera the editor's Scene view looks through, so the
				 outline of a billboarded world canvas turns the way that
				 view shows it. Set every frame by the Scene view. Without
				 one, billboarded canvases are outlined as their Transform
				 has them.
		 \param[in] position The camera's position.
		 \param[in] view     Its view matrix.
		 *********************************************************************/
		static void   SetViewCamera(const glm::vec3& position, const glm::mat4& view);

	private:
		void DrawColliders(World& world);
		void DrawVelocities(World& world);
		void DrawLights(World& world);
		void DrawAudio(World& world);	  		// DebugCategory::Audio
		void DrawParticleShape(World& world);	// DebugCategory::Particles
		void DrawGrid();
		void DrawSelection(World& world);
		void DrawWorldUI(World& world);   // DebugCategory::WorldUI

		Renderer&        m_renderer;
		ResourceManager* m_resources = nullptr;
	};
}   // NAMESPACE CRUCIBLE
