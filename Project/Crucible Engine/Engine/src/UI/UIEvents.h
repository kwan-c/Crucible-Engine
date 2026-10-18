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

namespace Crucible::UIEvents {

	/// A UIButton was clicked: pressed AND released over it while it was interactable.
	/// Pressing, dragging off and releasing elsewhere sends nothing (Unity's rule). Play mode only.
	struct ButtonClicked {
		Entity button{};
	};

}   // NAMESPACE CRUCIBLE::UIEVENTS
