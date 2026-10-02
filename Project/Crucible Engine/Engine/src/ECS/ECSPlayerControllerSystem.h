/*************************************************************************
* \file:         ECSPlayerControllerSystem.h
* \author:       Hugo
* \date          2 October 2026
* \version       1.0
* \brief
*                Keyboard in, CharacterController out: walks and jumps the
*                mech that carries a PlayerController.
*
* \details
*                Each Update(), for every entity with a PlayerController
*                and a CharacterController:
*                  1. reads WASD and Space
*                  2. turns WASD into a direction relative to the camera,
*                     flat on the ground
*                  3. calls SimpleMove() and Jump() on the controller
*
*                It never touches physics. ECSPhysicsSystem runs later in
*                the frame (PostUpdate) and does the actual moving, the
*                same way Unity's CharacterController only moves when a
*                script tells it to.
*
* \history
*    -v1.0       WASD, Space, camera-relative
*
**************************************************************************/
#pragma once

// Crucible engine
#include "Systems/IECSSystems.h"

namespace Crucible {

    class Renderer;

    class ECSPlayerControllerSystem final : public IECSSystem {

    public:

        // The Renderer only lends its camera, so W can mean "away from the
        // camera". The camera is not an entity yet; once it is, read its
        // Transform instead and drop this. Core owns the Renderer and destroys
        // systems first, so the reference cannot dangle.
        explicit ECSPlayerControllerSystem(const Renderer& renderer);

        // --- IECSSystem ---------------------------------------------------- //

        void Update(World& world, CommandBuffer& commands) override;

        // Gameplay decides, physics carries it out. Update runs before
        // PostUpdate, so the mech moves on the same frame the key goes down
        // instead of one frame late.
        SystemStage Stage() const override { return SystemStage::Update; }

    private:

        const Renderer& m_renderer;

    };  // ECSPLAYERCONTROLLERSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
