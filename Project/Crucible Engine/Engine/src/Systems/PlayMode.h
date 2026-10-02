/*************************************************************************
* \file:         PlayMode.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          2 October 2026
* \version       1.0
* \brief
*                Unity-style Play / Pause / Resume / Stop for the engine:
*                the play state, its queued requests, which systems run,
*                and the scene snapshot Stop restores.
*
* \details
*                Core owns one PlayMode and drives it from Core::Run, once
*                per frame, in this order:
*                  1. SceneManager::ApplyPending()  - scene New/Load
*                  2. ApplyRequests()               - Play/Pause/Resume/Stop
*                  3. UpdateSystems()               - after the World tick
*                  4. TickEvents(dt)
*                so every transition happens at the frame's safe point,
*                before any system iterates the world.
*
*                Systems: every system is registered once (Core::Systems())
*                and lives until the engine shuts down. Those that declared
*                SystemDesc::SimulationOnly() (physics, gameplay) run only
*                while Playing - PlayMode switches the manager's simulation
*                flag; everything else (transform hierarchy, rendering,
*                debug draw, audio) runs in every state. One scheduler
*                orders them all, so physics (PostUpdate) still runs before
*                hierarchy propagation and render extraction (PreRender).
*                Session state is handled explicitly: ECSPhysicsSystem's
*                BeginSession/EndSession (a fresh Jolt world per Play),
*                ECSAudioSystem's edit/pause modes, and the manager's
*                ResetSimulationSystems (commands, outboxes, failures).
*
*                Event time: the engine and network buses tick in real
*                time; the gameplay bus only while Playing, so delayed
*                gameplay events freeze in Pause. Stop clears what is
*                still queued or delayed on the gameplay and network buses
*                (listeners stay); the engine bus is never touched.
*
* \history
*    -v1.0       initial version
*
**************************************************************************/
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "EngineExport.h"
#include "Systems/PlayState.h"
#include "Scene/SceneManager.h"
#include "ECS/Systems/ECSSystemManager.h"

namespace Crucible
{
	class World;
	class ECSAudioSystem;
	class ECSPhysicsSystem;
	struct EventBuses;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members in an exported class; only used inside Engine.dll
#endif
	/*************************************************************************
	 \brief
		The engine's play mode: state, requests, simulation switch, snapshot.

	 \details
		Requests are checked against the state the engine will be in once
		the requests already queued have run: Play needs Edit, Pause needs
		Playing, Resume needs Paused, Stop needs Playing or Paused. Anything
		else - a repeated Play, a Pause in Edit - is ignored with a warning
		and returns false. A request that undoes the one still waiting
		cancels it instead (Play then Stop before the next frame does
		nothing). At the safe point they run in order; if one fails, the
		ones queued after it are dropped (they assumed it worked).

		Every applied transition is dispatched immediately on the engine bus
		as EngineEvents::PlayStateChanged; every failure as
		EngineEvents::PlayRequestFailed, and logged for the user.

		Scene requests during a play session: SceneManager::RequestNew and
		RequestLoad are applied as runtime scene changes (a level switch),
		and Stop still restores the scene that was open at Play. They run
		before the play requests of the same frame, so a Load then Play
		snapshots the loaded scene. Saving is refused until Stop.
	 *************************************************************************/
	class ENGINE_API PlayMode
	{
	public:
		/*********************************************************************
		 \brief  Binds to the engine's world, scenes, buses and systems; all
				 must outlive this object. Turns the simulation off: the
				 engine starts in Edit.
		 *********************************************************************/
		PlayMode(World& world, SceneManager& scenes, EventBuses& events, ECSSystemManager& systems);

		/*********************************************************************
		 \brief  Ends a session still running (see Shutdown).
		 *********************************************************************/
		~PlayMode();

		PlayMode(const PlayMode&) = delete;
		PlayMode& operator=(const PlayMode&) = delete;

		// ===================================================================
		//  State and requests
		// ===================================================================

		PlayState State() const noexcept { return m_state; }

		//! True from Play to Stop (Playing or Paused).
		bool InSession() const noexcept { return m_state != PlayState::Edit; }

		/*********************************************************************
		 \brief  Edit -> Playing at the next safe point.
		 \param[in] restoreOnStop
			true: the open scene - unsaved edits included - is captured in
			memory first, and Stop restores it. false: no snapshot; Stop
			only ends the session and keeps the world (a standalone game).
		 \details Then the simulation systems get a clean slate
				  (ECSSystemManager::ResetSimulationSystems), physics starts
				  a fresh session from the current components, and the
				  simulation is switched on. If the capture fails, nothing
				  changes and PlayRequestFailed is sent.
		 \return false if the request was ignored (see the class notes).
		 *********************************************************************/
		bool RequestPlay(bool restoreOnStop = true);

		/*********************************************************************
		 \brief  Playing -> Paused: simulation systems stop updating (physics
				 keeps its session), gameplay event time freezes and runtime
				 audio pauses. Editing, input and rendering carry on.
		 *********************************************************************/
		bool RequestPause();

		/*********************************************************************
		 \brief  Paused -> Playing: the same session carries on - no system,
				 physics state or timer is reset.
		 *********************************************************************/
		bool RequestResume();

		/*********************************************************************
		 \brief  Playing/Paused -> Edit: the simulation is switched off, the
				 physics session ends (bodies, Jolt, clock), the simulation
				 systems' commands, events and failures are cleared,
				 gameplay and network events still queued or delayed are
				 dropped, and the world is rebuilt from the Play snapshot
				 (entities, serialized component values, guids, scene name,
				 path and dirty flag). No system object is destroyed.
		 \details The snapshot is checked before the world is touched. If
				  it cannot be restored the session still ends, the world
				  from the session is kept and marked unsaved, and
				  PlayRequestFailed is sent after PlayStateChanged.
		 *********************************************************************/
		bool RequestStop();

		//! True while a request waits for the next safe point.
		bool HasPendingRequest() const noexcept { return !m_requests.empty(); }

		//! Why the last failed request failed; cleared by the next applied transition.
		const std::string& LastError() const noexcept { return m_lastError; }

		// ===================================================================
		//  Driven by Core::Run, once per frame, in this order
		// ===================================================================

		/*********************************************************************
		 \brief  The safe point: applies the queued requests in order. Call
				 after SceneManager::ApplyPending and before any system runs.
		 *********************************************************************/
		void ApplyRequests();

		/*********************************************************************
		 \brief  One frame of systems (SimulationOnly ones only while
				 Playing - the manager filters them).
		 *********************************************************************/
		void UpdateSystems();

		/*********************************************************************
		 \brief  Advances event time: engine and network always, gameplay
				 only while Playing.
		 *********************************************************************/
		void TickEvents(float dt);

		/*********************************************************************
		 \brief  Engine exit: ends a session still running - simulation off,
				 physics session ended, nothing restored, no events sent,
				 queued requests dropped. Safe to call more than once.
		 *********************************************************************/
		void Shutdown() noexcept;

	private:
		struct PendingRequest
		{
			PlayRequest request;
			bool        restoreOnStop;   // Play only
		};

		bool Queue(PlayRequest request, bool restoreOnStop);
		PlayState EffectiveState() const noexcept;   // after every queued request
		bool ApplyPlay(bool restoreOnStop);
		bool ApplyPause();
		bool ApplyResume();
		bool ApplyStop();
		ECSAudioSystem*   FindAudio() noexcept;      // null if audio is not registered
		ECSPhysicsSystem* FindPhysics() noexcept;    // null if physics is not registered
		void Notify(PlayState from, PlayRequest request, bool sceneRestored);
		bool Fail(PlayRequest request, std::string reason);

		World&            m_world;
		SceneManager&     m_scenes;
		EventBuses&       m_events;
		ECSSystemManager& m_systems;

		PlayState                         m_state = PlayState::Edit;
		std::vector<PendingRequest>       m_requests;     // applied in order at the next safe point
		SceneManager::Snapshot            m_snapshot;     // the scene at Play; empty when not restorable
		std::string                       m_lastError;

		// Level switch during a play session: the old scene's bodies must leave Jolt before the
		// new scene's bodies are built. Subscribed in the constructor (SceneManager::OnSceneChanged).
		void OnSceneChangedDuringPlay();
		// SceneManager has no unsubscribe, so the listener checks this token is still alive before
		// touching `this` (a PlayMode destroyed before its SceneManager is then simply skipped).
		std::shared_ptr<int>              m_lifetime = std::make_shared<int>(0);
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
