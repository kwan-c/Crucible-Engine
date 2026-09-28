/*************************************************************************
* \file:         GameplayEvents.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.0
* \brief
*                Placeholder home for gameplay event types (the gameplay bus:
*                Core::Events().gameplay).
*
* \details
*                Intentionally empty. Gameplay events will be defined and
*                emitted from the SCRIPTING layer, so no native C++ gameplay
*                event types are declared here - that would duplicate them.
*
*                TODO(gameplay-events):
*                  - Script-to-event-bus bridge: let scripts define event
*                    types and Send/Subscribe on the gameplay bus.
*                  - Only add a native struct here if an engine-side C++
*                    system genuinely needs to send or receive that event.
*                    Events are plain structs: copyable, nothrow-movable, no
*                    pointers to things that can die (Entity handles are
*                    fine - check World::IsAlive before use).
*
* \history
*    -v1.0       placeholder only (no event types yet)
*
**************************************************************************/
#pragma once

namespace Game::Events
{
	// TODO(gameplay-events): event types arrive via the script-to-event-bus bridge.
}
