/*************************************************************************
* \file:         ECSPlayerControllerSystem.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          2 October 2026
* \version       1.3
* \brief
*                Keyboard and mouse in: walks and jumps the mech that
*                carries a PlayerController, and turns the third-person
*                camera pivot that carries a CameraOrbit.
*
* \details
*                Each Update():
*                  1. locks the cursor if Play or Resume just started
*                  2. while the cursor is locked, turns every CameraOrbit
*                     pivot with the mouse, and turns its parent mech to
*                     face the same way if m_faceCamera is on
*                  3. reads WASD and Space, and turns WASD into a direction
*                     relative to the main camera entity, flat on the ground
*                  4. calls SimpleMove() and Jump() on every entity with a
*                     PlayerController and a CharacterController
*
*                It never touches physics. ECSPhysicsSystem runs later in
*                the frame (PostUpdate) and does the actual moving, the
*                same way Unity's CharacterController only moves when a
*                script tells it to.
*
*                It only runs while Playing, like a Unity script's Update:
*                in Edit and Paused the keyboard and mouse belong to the
*                editor. Freeing the cursor again (Esc, Pause, Stop) is the
*                editor's job - see EditorManager in ImGuiManager.cpp.
*
* \history
*    -v1.0       WASD, Space, camera-relative (the Renderer's camera)
*    -v1.1       reads the CameraComponent entity; SimulationOnly, so the
*                right-mouse-button guard for the editor is gone
*    -v1.2       third-person camera: CameraOrbit and the cursor lock
*    -v1.3       the mech turns to face the camera (m_faceCamera)
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>

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

    private:

        // The World's tick from the last time Update ran. The tick goes up by
        // one every frame whether or not this system runs, so a jump of more
        // than one means frames went by without us - Edit or Paused - and Play
        // or Resume has just started. That is the moment to lock the cursor.
        // Nothing tells a system "you were just switched back on", hence this.
        std::uint64_t m_lastTick = 0;

    };  // ECSPLAYERCONTROLLERSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
