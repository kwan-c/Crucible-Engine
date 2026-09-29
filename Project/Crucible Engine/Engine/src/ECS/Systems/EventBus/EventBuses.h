/*************************************************************************
* \file:         EventBuses.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.1
* \brief
*                Declares EventBuses: the engine's three event domains -
*                one EventBus implementation, three instances with
*                different rules.
*
* \details
*                  engine   - window, asset and scene events. The only bus
*                             that allows immediate Dispatch() (main thread).
*                  gameplay - ECS entity/component lifecycle events (from the
*                             EcsEventBridge), plus gameplay decisions sent
*                             through EventWriter::EmitGameplay - only by
*                             producers that own them (the future script
*                             bridge, or a system that declares
*                             SystemDesc::EmitsGameplayEvents()). Native
*                             engine systems don't. Deferred only; at a
*                             flush, lifecycle events come before the
*                             systems' gameplay events.
*                  network  - connection-level events. Deferred only.
*
*                Routing rule: native facts go to the engine bus; gameplay
*                decisions are made by scripts that listen there and then
*                raise gameplay events. Nothing is forwarded between buses
*                automatically.
*
*                Separate buses give each domain a hard boundary: nothing
*                crosses between them unless code explicitly forwards it.
*                Core owns the one EventBuses instance (see Core::Events()).
*
* \history
*    -v1.0       initial three-domain bus set
*    -v1.1       docs: gameplay bus ownership rule (EmitGameplay needs
*                EmitsGameplayEvents())
*
**************************************************************************/
#pragma once

#include "EventBus.h"

namespace Crucible
{
	/*************************************************************************
	 \brief  The three event domains, owned by Core.
	 \note   Must be constructed on the main thread: each bus remembers the
			 thread that created it as its owner.
	 *************************************************************************/
	struct EventBuses
	{
		EventBus engine   { EventBusConfig{ "Engine",   /*allowImmediate*/ true  } };
		EventBus gameplay { EventBusConfig{ "Gameplay", /*allowImmediate*/ false } };
		EventBus network  { EventBusConfig{ "Network",  /*allowImmediate*/ false } };

		/*********************************************************************
		 \brief  Delivers every queued event on all three buses.
		 \details Engine first (window/asset state), then network
				  (connections may spawn players), then gameplay (reacts to
				  both).
		 *********************************************************************/
		void DeliverAll()
		{
			engine.DeliverPending();
			network.DeliverPending();
			gameplay.DeliverPending();
		}

		/*********************************************************************
		 \brief  Advances every bus's clock so delayed events become due.
		 \param[in] dt Seconds since the last frame.
		 *********************************************************************/
		void TickAll(float dt)
		{
			engine.Tick(dt);
			network.Tick(dt);
			gameplay.Tick(dt);
		}
	};
}
