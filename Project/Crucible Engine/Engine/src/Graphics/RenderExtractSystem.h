#pragma once
/*************************************************************************
* \file:         RenderExtractSystem.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                ECS -> Renderer bridge: the "POPULATE RENDER QUEUE" step
*                RenderSystem.h's Renderer::Update() comment refers to.
*
* \details
*                Queries every entity with a WorldTransform + MeshRenderer
*                and calls Renderer::Submit(mesh, worldMatrix) for each
*                visible, loaded one; likewise every enabled Light
*                (Renderer::SubmitLight) and the scene's Skybox
*                (Renderer::SetEnvironment). Deliberately the ONLY place that
*                depends on both the ECS and the Renderer at once - Renderer
*                itself stays entirely unaware the ECS exists (see
*                RenderSystem.h/.cpp), and the ECS's Components.h only
*                depends on the small, Vulkan-free Graphics/MeshHandle.h.
*
*                Reads WorldTransform, NOT Transform: WorldTransform is the
*                already-hierarchy-composed parent*local matrix
*                (HierarchyTransformSystem.h), which is what actually needs
*                to reach the GPU. Runs in SystemStage::PreRender, after
*                HierarchyTransformSystem - see that file's header comment
*                ("Runs FIRST in PreRender ... PreRender systems that read
*                WorldTransform should declare RunAfter<HierarchyTransformSystem>()")
*                and the SystemStage::PreRender comment in IECSSystems.h
*                ("transform propagation (first), then render extraction"),
*                which this system is that second half of.
*
*                Registered once, from Core::RegisterEngineSystems(), with
*                a reference to Core's already-owned Renderer:
*                    m_systems.RegisterSystem<RenderExtractSystem>(*m_renderer);
*                Core::Run() already calls m_systems.Update(*m_world) (which
*                runs this system, filling the Renderer's per-frame render
*                queue via Submit()) BEFORE m_renderer->Update(dt) (which
*                drains that queue via DrawFrame()), so no change to the
*                frame loop's ordering is needed.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <optional>

#include "EngineExport.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/IECSSystems.h"
#include "ECS/Systems/Query.h"
#include "Graphics/RenderSystem.h"

namespace Crucible
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251 4275)   // STL members; IECSSystem is a header-only interface
#endif

	/*************************************************************************
	 \brief
		Engine system that submits every visible, mesh-bearing entity's
		world matrix to the Renderer once per frame.
	 *************************************************************************/
	class ENGINE_API RenderExtractSystem final : public IECSSystem
	{
	public:
		/*********************************************************************
		 \brief  Holds the Renderer this system extracts into. The Renderer
				 is Core-owned and outlives every system (see Core.h's
				 member destruction order), so a plain reference is safe.
		 \param[in] renderer The Core-owned Renderer to extract into.
		 *********************************************************************/
		explicit RenderExtractSystem(Renderer& renderer) : m_renderer(renderer) {}

		/*********************************************************************
		 \brief  PreRender: after HierarchyTransformSystem has filled this
				 frame's WorldTransforms, before the Renderer draws them.
		 \return SystemStage::PreRender.
		 *********************************************************************/
		SystemStage Stage() const override { return SystemStage::PreRender; }

		/*********************************************************************
		 \brief  Runs after HierarchyTransformSystem's own (lowest-possible)
				 priority within PreRender, so the default (0) is fine here.
		 \return 0.
		 *********************************************************************/
		int Priority() const override { return 0; }

		/*********************************************************************
		 \brief  Declares its query's read access and its ordering
				 requirement on HierarchyTransformSystem (required: without
				 it, WorldTransform is never computed and nothing would
				 ever be drawn, so this is a hard dependency, not optional).
		 \param[in] desc The descriptor to fill in.
		 *********************************************************************/
		void Describe(SystemDesc& desc) const override;

		/*********************************************************************
		 \brief  Submits every visible, loaded entity's world matrix to the
				 Renderer's render queue for this frame, plus every enabled
				 Light and the scene's Skybox settings.
		 \param[in] world The World to query this frame's entities from.
		 \param[in] ctx   Unused by this system (accepted to satisfy
						  IECSSystem's interface).
		 *********************************************************************/
		void Update(World& world, SystemContext& ctx) override;

	private:
		using ExtractQuery = Query<const WorldTransform, const MeshRenderer>;
		using LightQuery   = Query<const WorldTransform, const Light>;
		using SkyboxQuery  = Query<const Skybox>;

		Renderer& m_renderer;
		std::optional<ExtractQuery> m_query;         // built once, on first Update() (needs a World&)
		std::optional<LightQuery>   m_lightQuery;    // same
		std::optional<SkyboxQuery>  m_skyboxQuery;   // same
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}   // NAMESPACE CRUCIBLE
