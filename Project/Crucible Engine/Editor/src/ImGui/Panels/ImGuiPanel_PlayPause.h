#pragma once

#include <string>

#include "ImGui/ImGuiManager.h"
#include "ECS/Systems/EventBus/Subscription.h"
#include "Systems/PlayState.h"

namespace Crucible
{
	class Core;

	//editor side play/pause/stop
	//
	// The play state belongs to Core (Core::GetPlayState); this toolbar only shows it and
	// queues requests (Core::RequestPlay/Pause/Resume/Stop), which Core applies at the start
	// of the next frame. Viewport focus follows the transitions Core actually applied:
	// Play, Pause and Resume select the Game view, Stop selects the Scene view.
	class PlayPause : public EditorPanel
	{
	public:
		explicit PlayPause(Core& core) : m_core(core) {}

		void Init() override;
		void Update(float dt) override;

	private:
		void OnPlayStateChanged(const EngineEvents::PlayStateChanged& change);
		void SelectViewport(bool game);

		Core&        m_core;
		Subscription m_stateChanged;    // EngineEvents::PlayStateChanged
		Subscription m_requestFailed;   // EngineEvents::PlayRequestFailed
		std::string  m_error;           // last failed request; cleared by the next applied transition
	};
}
