/*************************************************************************
* \file:         NetReceiveSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
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
*                Phase 4 handles connections only and touches no components
*                (NoComponentAccess). Phase 8 makes it Exclusive, because
*                creating client proxies is a structural change.
*
* \history
*    -v1.0       connection handling (Networking Guide PR 4)
*
**************************************************************************/
#pragma once

#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"

namespace Crucible
{
	class NetworkSession;

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
		NetworkSession& m_session;
	};
}
