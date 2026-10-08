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
*                  - Velocities: an arrow from each RigidBody /
*                                CharacterController along its velocity,
*                                |v| x VelocityScale long, yellow -> red as
*                                speed rises towards 20 m/s.
*                  - Lights:     the selected light's full shape (point range
*                                sphere, spot cone to its range, sun arrow);
*                                other lights get a short direction arrow
*                                (their position is shown by the editor icon).
*                  - Grid:       a 40 x 40 m, 1 m ground grid on y = 0 plus
*                                the world X (red) and Z (blue) axes.
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

	private:
		void DrawColliders(World& world);
		void DrawVelocities(World& world);
		void DrawLights(World& world);
		void DrawGrid();
		void DrawSelection(World& world);

		Renderer&        m_renderer;
		ResourceManager* m_resources = nullptr;
	};
}   // NAMESPACE CRUCIBLE
