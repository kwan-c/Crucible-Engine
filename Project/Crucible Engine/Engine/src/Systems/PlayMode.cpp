/*************************************************************************
* \file:         PlayMode.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          2 October 2026
* \version       1.0
* \brief
*                Implementation of PlayMode - see PlayMode.h.
*
* \history
*    -v1.0       initial version
*
**************************************************************************/
#include "Systems/PlayMode.h"

#include <exception>

#include "Diagnostics/Logger.h"
#include "ECS/ECSAudioSystem.h"
#include "ECS/ECSPhysicsSystem.h"
#include "ECS/Systems/EventBus/EventBuses.h"
#include "ECS/World.h"

namespace Crucible
{
	namespace
	{
		// The state a request leads to, if it succeeds.
		PlayState StateAfter(PlayRequest request) noexcept
		{
			switch (request)
			{
			case PlayRequest::Play:   return PlayState::Playing;
			case PlayRequest::Pause:  return PlayState::Paused;
			case PlayRequest::Resume: return PlayState::Playing;
			case PlayRequest::Stop:   return PlayState::Edit;
			}
			return PlayState::Edit;
		}

		bool IsAllowed(PlayRequest request, PlayState state) noexcept
		{
			switch (request)
			{
			case PlayRequest::Play:   return state == PlayState::Edit;
			case PlayRequest::Pause:  return state == PlayState::Playing;
			case PlayRequest::Resume: return state == PlayState::Paused;
			case PlayRequest::Stop:   return state != PlayState::Edit;
			}
			return false;
		}

		// b undoes a, so the pair cancels out while both are still queued.
		bool Undoes(PlayRequest a, PlayRequest b) noexcept
		{
			return (a == PlayRequest::Play && b == PlayRequest::Stop)
				|| (a == PlayRequest::Pause && b == PlayRequest::Resume)
				|| (a == PlayRequest::Resume && b == PlayRequest::Pause);
		}
	}

	const char* ToString(PlayState state) noexcept
	{
		switch (state)
		{
		case PlayState::Edit:    return "Edit";
		case PlayState::Playing: return "Playing";
		case PlayState::Paused:  return "Paused";
		}
		return "Unknown";
	}

	const char* ToString(PlayRequest request) noexcept
	{
		switch (request)
		{
		case PlayRequest::Play:   return "Play";
		case PlayRequest::Pause:  return "Pause";
		case PlayRequest::Resume: return "Resume";
		case PlayRequest::Stop:   return "Stop";
		}
		return "Unknown";
	}

	PlayMode::PlayMode(World& world, SceneManager& scenes, EventBuses& events, ECSSystemManager& systems)
		: m_world(world), m_scenes(scenes), m_events(events), m_systems(systems)
	{
		m_systems.SetSimulationActive(false);   // the engine starts in Edit

		std::weak_ptr<int> const alive = m_lifetime;
		m_scenes.OnSceneChanged([this, alive] {
			if (alive.expired()) return;   // this PlayMode is gone
			OnSceneChangedDuringPlay();
		});
	}

	/*!
		\brief
		A scene was loaded or created (SceneManager::ApplyLoad / ApplyNew). In Edit there is no Jolt
		world, so nothing to do. During Play or Pause it is a level switch: restart the physics
		session so Jolt starts empty, the same way Stop then Play would.

		Without this, the old level's bodies stayed in Jolt until the next Sync noticed their
		entities were gone - and that Sync builds the NEW bodies before removing the old ones, so
		both levels briefly shared Jolt's fixed body limit (a body that fails to create is not
		retried), gameplay queries could hit the old level for a frame, and the fixed-step clock
		carried over from the old level.

		Runs inside SceneManager::ApplyPending, which Core calls before UpdateSystems, so the new
		level's first physics update builds every body into a fresh world. Not called by Stop's
		RestoreSnapshot - Stop already ends the session itself (ApplyStop).
	*/
	void PlayMode::OnSceneChangedDuringPlay()
	{
		if (m_state == PlayState::Edit) return;

		if (ECSPhysicsSystem* physics = FindPhysics())
		{
			physics->EndSession();     // every body and character out, Jolt shut down, clock reset
			physics->BeginSession();   // a new, empty Jolt world
			CRU_INFO("PlayMode: scene changed during play - physics world restarted for '{}'", m_scenes.CurrentName());
		}
	}

	PlayMode::~PlayMode()
	{
		Shutdown();
	}

	// =======================================================================
	//  Requests
	// =======================================================================

	bool PlayMode::RequestPlay(bool restoreOnStop) { return Queue(PlayRequest::Play, restoreOnStop); }
	bool PlayMode::RequestPause()                  { return Queue(PlayRequest::Pause, true); }
	bool PlayMode::RequestResume()                 { return Queue(PlayRequest::Resume, true); }
	bool PlayMode::RequestStop()                   { return Queue(PlayRequest::Stop, true); }

	PlayState PlayMode::EffectiveState() const noexcept
	{
		PlayState state = m_state;
		for (const PendingRequest& pending : m_requests) state = StateAfter(pending.request);
		return state;
	}

	bool PlayMode::Queue(PlayRequest request, bool restoreOnStop)
	{
		const PlayState effective = EffectiveState();
		if (!IsAllowed(request, effective))
		{
			CRU_WARN("PlayMode: {} ignored - the engine is {}{}", ToString(request), ToString(effective),
				m_requests.empty() ? "" : " once its queued requests run");
			return false;
		}

		// Undoing the request still waiting cancels both: Play then Stop in one frame is nothing.
		if (!m_requests.empty() && Undoes(m_requests.back().request, request))
		{
			m_requests.pop_back();
			return true;
		}

		m_requests.push_back({ request, restoreOnStop });
		return true;
	}

	// =======================================================================
	//  The safe point
	// =======================================================================

	void PlayMode::ApplyRequests()
	{
		if (m_requests.empty()) return;

		// Take the queue first: a listener that requests something while a transition is
		// being reported queues it for the NEXT frame.
		std::vector<PendingRequest> requests;
		requests.swap(m_requests);

		for (std::size_t i = 0; i < requests.size(); ++i)
		{
			const PendingRequest& pending = requests[i];

			// Re-checked here: an earlier request this frame may have failed.
			bool ok = false;
			if (!IsAllowed(pending.request, m_state))
			{
				ok = Fail(pending.request, std::string("not possible while ") + ToString(m_state));
			}
			else
			{
				switch (pending.request)
				{
				case PlayRequest::Play:   ok = ApplyPlay(pending.restoreOnStop); break;
				case PlayRequest::Pause:  ok = ApplyPause();                     break;
				case PlayRequest::Resume: ok = ApplyResume();                    break;
				case PlayRequest::Stop:   ok = ApplyStop();                      break;
				}
			}

			// Later requests assumed this one worked (e.g. Pause after Play): drop them.
			if (!ok && i + 1 < requests.size())
			{
				CRU_WARN("PlayMode: {} queued request(s) dropped after {} failed", requests.size() - i - 1, ToString(pending.request));
				break;
			}
		}
	}

	bool PlayMode::ApplyPlay(bool restoreOnStop)
	{
		// 1. The snapshot. Nothing has changed yet, so failing here costs nothing.
		SceneManager::Snapshot snapshot;
		if (restoreOnStop && !m_scenes.CaptureSnapshot(snapshot))
		{
			return Fail(PlayRequest::Play, "the scene could not be captured, so Stop could not restore it");
		}

		// 2. A clean simulation session: no commands, events or failures left from an
		//    earlier one, and a fresh physics world built from the components as they are.
		m_systems.ResetSimulationSystems();
		if (ECSPhysicsSystem* physics = FindPhysics()) physics->BeginSession();
		m_systems.SetSimulationActive(true);

		m_snapshot = std::move(snapshot);
		m_scenes.SetPlaySessionActive(true);
		if (ECSAudioSystem* audio = FindAudio())
		{
			audio->SetPaused(false);
			audio->SetEditMode(false);   // every playOnAwake fires, as at a scene start
		}

		const PlayState from = m_state;
		m_state = PlayState::Playing;
		CRU_USER_INFO("Play mode: started ('{}')", m_scenes.CurrentName());
		++m_sessionRevision;
		Notify(from, PlayRequest::Play, false);
		return true;
	}

	bool PlayMode::ApplyPause()
	{
		m_systems.SetSimulationActive(false);   // physics stops stepping; its session is kept
		if (ECSAudioSystem* audio = FindAudio()) audio->SetPaused(true);

		const PlayState from = m_state;
		m_state = PlayState::Paused;
		Notify(from, PlayRequest::Pause, false);
		return true;
	}

	bool PlayMode::ApplyResume()
	{
		m_systems.SetSimulationActive(true);    // the same session carries on
		if (ECSAudioSystem* audio = FindAudio()) audio->SetPaused(false);

		const PlayState from = m_state;
		m_state = PlayState::Playing;
		Notify(from, PlayRequest::Resume, false);
		return true;
	}

	bool PlayMode::ApplyStop()
	{
		// 1. End the session before the world is restored: simulation off, physics bodies,
		//    Jolt and clock gone, nothing the session queued carried into the next Play.
		m_systems.SetSimulationActive(false);
		if (ECSPhysicsSystem* physics = FindPhysics()) physics->EndSession();
		m_systems.ResetSimulationSystems();

		// Runtime events still queued or delayed belong to the session that just ended.
		// Engine events (window, input, editor) are untouched; listeners stay subscribed.
		m_events.gameplay.Clear();
		m_events.network.Clear();

		// Silence first, THEN clear the pause. The other way round, Stop pressed while paused
		// unpaused every voice for a moment before stopping it - FMOD mixes on its own thread,
		// so that moment can be heard as a click. Silencing drops the voice handles, so the
		// unpause afterwards only resets the flag for the next Play.
		if (ECSAudioSystem* audio = FindAudio())
		{
			audio->SetEditMode(true);    // everything a source started stops
			audio->SetPaused(false);
		}

		// 2. Put the scene back as it was at Play. The snapshot is checked before the world
		//    is touched; if it cannot be used, the session's world is kept rather than lost.
		bool restored = false;
		std::string failure;
		if (m_snapshot.IsValid())
		{
			restored = m_scenes.RestoreSnapshot(m_snapshot);
			if (!restored)
			{
				m_scenes.MarkDirty();
				failure = "the scene from before Play could not be restored - the play-mode world was kept (unsaved)";
			}
		}
		m_snapshot = SceneManager::Snapshot{};
		m_scenes.SetPlaySessionActive(false);

		const PlayState from = m_state;
		m_state = PlayState::Edit;
		if (restored) CRU_USER_INFO("Play mode: stopped, '{}' restored", m_scenes.CurrentName());
		++m_sessionRevision;
		Notify(from, PlayRequest::Stop, restored);

		// The session did end; the failure is reported after the state change.
		if (!failure.empty()) Fail(PlayRequest::Stop, std::move(failure));
		return true;
	}

	// =======================================================================
	//  The frame
	// =======================================================================

	void PlayMode::UpdateSystems()
	{
		// One scheduler for every system: physics (PostUpdate) runs before hierarchy
		// propagation and render extraction (PreRender). SimulationOnly systems are
		// skipped by the manager unless Playing.
		m_systems.Update(m_world);
	}

	void PlayMode::TickEvents(float dt)
	{
		// Engine and network time is real time. Gameplay time is game time: it only moves
		// while Playing, so delayed gameplay events freeze in Pause (and never pile up in Edit).
		m_events.engine.Tick(dt);
		m_events.network.Tick(dt);
		if (m_state == PlayState::Playing) m_events.gameplay.Tick(dt);
	}

	void PlayMode::Shutdown() noexcept
	{
		m_requests.clear();
		m_systems.SetSimulationActive(false);
		if (ECSPhysicsSystem* physics = FindPhysics()) physics->EndSession();
		m_snapshot = SceneManager::Snapshot{};
		m_scenes.SetPlaySessionActive(false);
		m_state = PlayState::Edit;
	}

	// =======================================================================
	//  Helpers
	// =======================================================================

	ECSAudioSystem* PlayMode::FindAudio() noexcept
	{
		return m_systems.FindSystem<ECSAudioSystem>();
	}

	ECSPhysicsSystem* PlayMode::FindPhysics() noexcept
	{
		return m_systems.FindSystem<ECSPhysicsSystem>();
	}

	void PlayMode::Notify(PlayState from, PlayRequest request, bool sceneRestored)
	{
		m_lastError.clear();

		// Immediate, so listeners fix their cached entity handles before anything runs.
		try
		{
			m_events.engine.Dispatch(EngineEvents::PlayStateChanged{ from, m_state, request, sceneRestored });
		}
		catch (const std::exception& e)
		{
			CRU_ERROR("PlayMode: reporting {} -> {} failed - {}", ToString(from), ToString(m_state), e.what());
		}
	}

	bool PlayMode::Fail(PlayRequest request, std::string reason)
	{
		CRU_USER_ERROR("{} failed: {}", ToString(request), reason);
		m_lastError = std::string(ToString(request)) + " failed: " + reason;

		try
		{
			m_events.engine.Dispatch(EngineEvents::PlayRequestFailed{ request, m_state, std::move(reason) });
		}
		catch (const std::exception& e)
		{
			CRU_ERROR("PlayMode: reporting a failed {} failed - {}", ToString(request), e.what());
		}
		return false;
	}
}
