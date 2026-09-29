/*************************************************************************
* \file:         NetworkEvents.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 September 2026
* \version       1.1
* \brief
*                Placeholder home for network event types (the network bus:
*                Core::Events().network).
*
* \details
*                Intentionally empty. The networking layer (transport, peer
*                model, replication) has not been designed yet, so any event
*                types declared here now would be guesses.
*
*                TODO(network-events):
*                  - Connection-level events (e.g. connected, disconnected
*                    with a reason, timeout), sent by the system that owns
*                    the connection through ctx.events.EmitNetwork(...).
*                  - Events are plain structs: copyable, nothrow-movable, no
*                    pointers to things that can die. Never put a raw Entity
*                    in anything sent to another machine - its index and
*                    generation mean nothing there.
*
* \history
*    -v1.0       placeholder only (no event types yet)
*    -v1.1       trimmed to a single placeholder; the rest of the network
*                design is left for later
*
**************************************************************************/
#pragma once

namespace Crucible::NetworkEvents
{
	// TODO(network-events): event types arrive once the networking layer is designed.
}
