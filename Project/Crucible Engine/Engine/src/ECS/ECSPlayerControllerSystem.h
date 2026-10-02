/*************************************************************************
* \file:         ECSPlayerControllerSystem.h
* \author:       Hugo
* \date          2 October 2026
* \version       1.1
* \brief
*                Keyboard in, CharacterController out: walks and jumps the
*                mech that carries a PlayerController.
*
* \details
*                Each Update(), for every entity with a PlayerController
*                and a CharacterController:
*                  1. reads WASD and Space
*                  2. turns WASD into a direction relative to the main
*                     camera entity, flat on the ground
*                  3. calls SimpleMove() and Jump() on the controller
*
*                It never touches physics. ECSPhysicsSystem runs later in
*                the frame (PostUpdate) and does the actual moving, the
*                same way Unity's CharacterController only moves when a
*                script tells it to.
*
*                It only runs while Playing, like a Unity script's Update:
*                in Edit and Paused the keyboard belongs to the editor.
*
* \history
*    -v1.0       WASD, Space, camera-relative (the Renderer's camera)
*    -v1.1       reads the CameraComponent entity; SimulationOnly, so the
*                right-mouse-button guard for the editor is gone
*
**************************************************************************/
#pragma once

// Crucible engine
#include "Systems/IECSSystems.h"

namespace Crucible {

    class ECSPlayerControllerSystem final : public IECSSystem {

    public:

        // --- IECSSystem ---------------------------------------------------- //

        void Update(World& world, CommandBuffer& commands) override;

        // SimulationOnly: runs only while Playing. Without it the mech would
        // walk whenever WASD is pressed in the editor, typing included.
        void Describe(SystemDesc& desc) const override;

        // Gameplay decides, physics carries it out. Update runs before
        // PostUpdate, so the mech moves on the same frame the key goes down
        // instead of one frame late.
        SystemStage Stage() const override { return SystemStage::Update; }

    };  // ECSPLAYERCONTROLLERSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
