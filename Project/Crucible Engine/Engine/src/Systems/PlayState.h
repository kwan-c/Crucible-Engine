/*************************************************************************
* \file:         PlayState.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          2 October 2026
* \version       1.0
* \brief
*                Unity-style play mode: the engine's Edit / Playing /
*                Paused state, the requests that move between them, and
*                the engine-bus events that report each move.
*
* \details
*                Core owns the state (Core::GetPlayState) and applies
*                requests (Core::RequestPlay/Pause/Resume/Stop) at the
*                start of the next frame, before any system runs. Every
*                applied transition is dispatched immediately on the
*                engine bus as EngineEvents::PlayStateChanged; a request
*                that could not be carried out is reported as
*                EngineEvents::PlayRequestFailed. Subscribe through
*                Core::Events().engine and keep the Subscription.
*
* \history
*    -v1.0       initial version
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <string>

#include "EngineExport.h"

namespace Crucible
{
	/*************************************************************************
	 \brief  Where the engine is in the Play / Pause / Stop cycle.
	 *************************************************************************/
	enum class PlayState : std::uint8_t
	{
		Edit,      // no play session: simulation systems (physics, gameplay) do not run; hierarchy, rendering and audio previews do
		Playing,   // play session: simulation systems and gameplay time advance
		Paused     // play session frozen: simulation systems, gameplay time and runtime audio wait; editing and rendering carry on
	};

	/*************************************************************************
	 \brief  A queued change of PlayState (see Core::RequestPlay etc.).
	 *************************************************************************/
	enum class PlayRequest : std::uint8_t
	{
		Play,      // Edit -> Playing: snapshot the scene, start a fresh simulation session
		Pause,     // Playing -> Paused
		Resume,    // Paused -> Playing: the same session carries on
		Stop       // Playing/Paused -> Edit: end the session, restore the snapshot
	};

	/*************************************************************************
	 \brief  "Edit", "Playing", "Paused" (for logs and UI).
	 *************************************************************************/
	ENGINE_API const char* ToString(PlayState state) noexcept;

	/*************************************************************************
	 \brief  "Play", "Pause", "Resume", "Stop" (for logs and UI).
	 *************************************************************************/
	ENGINE_API const char* ToString(PlayRequest request) noexcept;

	namespace EngineEvents
	{
		/*********************************************************************
		 \brief  A play-mode transition was applied (dispatched immediately
				 on the engine bus, at the start of the frame).
		 \details When to is Edit and sceneRestored is true, Stop rebuilt the
				  world from the Play snapshot: every Entity handle from
				  before is stale. Find entities again by guid through
				  SceneManager::Current().EntityOf().
		 *********************************************************************/
		struct PlayStateChanged
		{
			PlayState   from          = PlayState::Edit;
			PlayState   to            = PlayState::Edit;
			PlayRequest request       = PlayRequest::Play;
			bool        sceneRestored = false;
		};

		/*********************************************************************
		 \brief  A request could not be carried out (dispatched immediately
				 on the engine bus).
		 \details state is the state the engine is in afterwards. A failed
				  Play leaves it in Edit with nothing changed. A Stop always
				  ends the session; it "fails" only when the snapshot could
				  not be restored, and the world from the session is kept
				  (scene marked unsaved).
		 *********************************************************************/
		struct PlayRequestFailed
		{
			PlayRequest request = PlayRequest::Play;
			PlayState   state   = PlayState::Edit;
			std::string reason;
		};
	}
}
