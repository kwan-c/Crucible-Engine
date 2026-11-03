/*************************************************************************
* \file:         NetReceiveSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.1
* \brief
*                The ECS system that runs the network session once per
*                frame, first in PreUpdate.
*
* \details
*                PreUpdate is the stage IECSSystems.h reserves for "input,
*                network receive"; priority -100 puts it before UI and input
*                systems, so everything later in the frame sees this frame's
*                network state.
*
*                Not SimulationOnly: it must keep running while Paused, or
*                messages pile up in GNS and a client leaving would go
*                unnoticed. In Edit the session is Offline and Update returns
*                at once.
*
*                Phase 4 handles connections; Phase 6 adds the identity
*                upkeep: entities whose NetworkIdentity the session rejected
*                are destroyed here (through this system's CommandBuffer, at
*                the end of PreUpdate), and in Debug builds the id map is
*                checked against the components every frame. Phase 8 makes
*                it Exclusive, because creating client proxies is a
*                structural change.
*
* \history
*    -v1.0       connection handling (Networking Guide PR 4)
*    -v1.1       destroys rejected identities; Debug id-map check (PR 6)
*
**************************************************************************/
#pragma once

#include <vector>

#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"

namespace Crucible
{
	class NetworkSession;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL member of an exported class: used only inside Engine.dll
#endif
	class ENGINE_API NetReceiveSystem final : public IECSSystem
	{
	public:
		/// `session` is owned by Core and outlives every system.
		explicit NetReceiveSystem(NetworkSession& session) noexcept : m_session(session) {}

		SystemStage Stage() const override { return SystemStage::PreUpdate; }
		int         Priority() const override { return -100; }
		void        Describe(SystemDesc& desc) const override;
		void        Update(World& world, SystemContext& ctx) override;

	private:
		NetworkSession&     m_session;
		std::vector<Entity> m_rejected;   // reused every frame
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
