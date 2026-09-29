/*************************************************************************
* \file:         SystemContext.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.2
* \brief
*                Declares SystemContext: everything a system may write to
*                during Update() - its CommandBuffer and its EventWriter.
*
* \details
*                Passed to IECSSystem::Update(World&, SystemContext&). Both
*                members refer to storage owned by the system's own slot in
*                the ECSSystemManager, so nothing here is shared with another
*                system. Commands are applied and events delivered at the end
*                of the system's stage, on the main thread (see
*                EventBus/EventWriter.h for the exact order and the failure
*                rules).
*
*                The context is created for one Update() call only: don't
*                keep references to it, its CommandBuffer or its EventWriter.
*
*                Migrating a system that used the previous per-bus outbox
*                fields:
*                \code
*                // before: ctx.engineEvents.Send(WindowResized{ w, h });
*                ctx.events.EmitEngine(WindowResized{ w, h });
*                // before: ctx.networkEvents.Send(Connected{ peer });
*                ctx.events.EmitNetwork(Connected{ peer });
*                // before: ctx.gameplayEvents.Send(DamageDealt{ ... });
*                ctx.events.EmitGameplay(DamageDealt{ ... });   // only with
*                //   desc.EmitsGameplayEvents() in Describe() - gameplay
*                //   decisions belong to scripting, not native systems
*                \endcode
*
* \history
*    -v1.0       initial context (commands + engine/gameplay/network outboxes)
*    -v1.1       outbox fields replaced by one EventWriter (explicit
*                engine/network routing, no native gameplay emission)
*    -v1.2       the writer also has the gameplay route, restricted to
*                systems that declare EmitsGameplayEvents()
*
**************************************************************************/
#pragma once

namespace Crucible
{
	class CommandBuffer;
	class EventWriter;

	/*************************************************************************
	 \brief  Everything a system may write during Update().
	 \note   Only references, valid for one Update() call.
	 *************************************************************************/
	struct SystemContext
	{
		CommandBuffer& commands;   // structural changes, applied at the stage flush
		EventWriter&   events;     // engine/gameplay/network events (routes per ownership), delivered after the stage's commands
	};
}
