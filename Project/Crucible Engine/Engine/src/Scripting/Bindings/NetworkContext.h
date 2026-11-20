/*************************************************************************
* \file:         NetworkContext.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          16 November 2026
* \version       1.3
* \brief         INTERNAL: the network events waiting for the C# scripts
*                (EntityScript.OnNetworkStarted, OnPeerJoined,
*                OnNetworkCommand, ...).
*
* \details
*    NetworkBindings.cpp listens on the network bus and queues what scripts
*    should hear; EntityScriptSystem hands the queue out once per Playing
*    frame, before OnUpdate, the same way it does UI and physics events:
*
*        TrimEvents()        a Play session began or none runs
*        BeginDispatch()     this frame's events, in arrival order
*        ... ScriptRuntime::DispatchNetworkEvent for each script ...
*        EndDispatch()       forget them
*
*    Events that arrive while a batch is being handed out (a script that
*    sends itself a command from inside a callback) wait for the next frame.
*
*    Only include this from .cpp files inside Engine/src/Scripting.
*
* \history
*    -v1.0       session and player events (Networking C# Phase 1), commands
*                (Networking C# Phase 2)
*    -v1.1       Event: the host's gameplay events (Networking C# Phase 5A)
*    -v1.2       VarChanged and GatherVariableChanges (Networking C# Phase 4)
*    -v1.3       SceneLoading, AllPeersReady (Networking C# Phase 9)
*
**************************************************************************/
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "ECS/Core/ECSCore.h"   // Entity

namespace Crucible
{
	class World;
}

namespace Crucible::ScriptBindings::NetworkContext
{
	/*********************************************************************
	 \brief  One network event for the scripts. `kind` is the number
	         EntityScript.DispatchNetworkEvent switches on (EntityScript.cs):
	         keep the two in step.
	 *********************************************************************/
	struct ScriptNetEvent
	{
		enum class Kind : int32_t
		{
			Started = 0,     // this machine is in a session (host: listening; client: the world arrived)
			Stopped = 1,     // the session ended, or a join failed; text = why
			PeerJoined = 2,  // peer = the other player
			PeerReady = 3,   // peer = the other player, now in the game
			PeerLeft = 4,    // peer = the other player
			Command = 5,     // peer = the sender, code = the command's kind, serial names its payload
			Event = 6,       // code = the event's kind, serial names its payload
			VarChanged = 7,  // code = the NetVars slot whose value changed; always targeted
			SceneLoading = 8,   // the session is moving to another scene (Phase 9)
			AllPeersReady = 9,  // host: every client has the new scene's world (Phase 9)
		};

		Kind    kind = Kind::Started;
		int32_t peer = 0;
		int32_t code = 0;
		int32_t serial = 0;       // Command, Event: Network.cs asks for the payload by this number
		bool    targeted = false; // Command, Event: only the scripts on `target` hear it
		Entity  target{};
		std::string text;
		std::vector<std::byte> payload;
	};

	/*********************************************************************
	 \brief  Forgets what an earlier Play session left behind: everything
	         before the newest Started, or everything if there is none (a
	         host's session starts before its scripts do, so its Started is
	         kept). EntityScriptSystem calls it whenever a Play session
	         begins and on every frame without one.
	 *********************************************************************/
	void TrimEvents();

	/*********************************************************************
	 \brief  Queues a VarChanged for every NetVariables slot that changed
	         since the last call (a snapshot on a client, a script's Set on
	         the authority) and clears the marks. EntityScriptSystem calls
	         it once per Playing frame, right before BeginDispatch.
	 *********************************************************************/
	void GatherVariableChanges(World& world);

	/*********************************************************************
	 \brief  Makes everything received since the last call the batch being
	         handed out, and returns it (valid until the next call).
	 *********************************************************************/
	const std::vector<ScriptNetEvent>& BeginDispatch();

	/*********************************************************************
	 \brief  Every script has heard the batch: forget it (and its payloads).
	 *********************************************************************/
	void EndDispatch();
}
