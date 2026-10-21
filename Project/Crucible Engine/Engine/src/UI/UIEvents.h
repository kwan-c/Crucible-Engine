/*************************************************************************
  \file       UIEvents.h
  \brief
    Events the game's UI sends (UIInputSystem), on the engine bus.
  \details
    Facts, like CollisionStarted: "this button was clicked". What a click
    MEANS (start the game, quit, resume) is game code's decision - today a
    C++ listener, later the script bridge, both subscribing the same way:

        // e.g. in GameApp, kept alive as long as you want the callbacks
        Subscription m_onClick = core.Events().engine.Subscribe<UIEvents::ButtonClicked>(
            [](const UIEvents::ButtonClicked& click) {
                // click.button is the entity with the UIButton
            });

    Delivered at the end of the PreUpdate stage, so gameplay systems in the
    same frame already see the result. Like every event, it describes the
    past: check world.IsAlive(click.button) before using it.
*************************************************************************/
#pragma once

#include "ECS/Core/ECSCore.h"   // Entity

#include <string>

namespace Crucible::UIEvents {

	/// A UIButton was clicked: pressed AND released over it while it was interactable.
	/// Pressing, dragging off and releasing elsewhere sends nothing (Unity's rule). Play mode only.
	struct ButtonClicked {
		Entity button{};
	};

	/// A UISlider's value changed because the player pressed or dragged it (not when code sets
	/// it). Sent once per frame of change while dragging; value is already clamped / rounded.
	struct SliderChanged {
		Entity slider{};
		float  value = 0.0f;
	};

	/// A button with onClick = Quit was clicked (sent after its ButtonClicked). The engine doesn't
	/// quit by itself: the game (GameApp) calls Core::RequestQuit(); the editor stops Play mode.
	struct QuitRequested {
		Entity button{};
	};

	/// A button with onClick = Custom was clicked (sent after its ButtonClicked): run the action
	/// named actionId. GameApp keeps a table of them (GameApp::RegisterAction); later, scripts.
	struct CustomAction {
		Entity      button{};
		std::string actionId;
	};

}   // NAMESPACE CRUCIBLE::UIEVENTS
