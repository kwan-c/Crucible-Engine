/*************************************************************************
* \file:         ECSPlayerControllerSystem.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          2 October 2026
* \version       1.6
* \brief
*                PlayerInput in: walks and jumps the mech that carries a
*                PlayerController, and turns it to face its camera.
*
* \details
*                Each Update():
*                  1. turns every mech whose PlayerInput carries a camera
*                     yaw to face that way, if m_faceCamera is on
*                  2. calls SimpleMove() and Jump() on every entity with a
*                     PlayerController, a CharacterController and a
*                     PlayerInput, from that PlayerInput
*
*                Reading the keyboard, mouse and gamepad (and the cursor
*                lock and the camera pivot that go with them) is
*                LocalPlayerInputSystem's (Network/Systems), which writes
*                PlayerInput in PreUpdate for this machine's players. So the
*                same code moves a player whatever wrote its PlayerInput:
*                the local sampler, or on the host a remote player's
*                network commands.
*
*                It never touches physics. ECSPhysicsSystem runs later in
*                the frame (PostUpdate) and does the actual moving, the
*                same way Unity's CharacterController only moves when a
*                script tells it to.
*
*                It only runs while Playing, like a Unity script's Update:
*                in Edit and Paused nothing moves. Freeing the cursor again
*                (Esc, Pause, Stop) is the editor's job - see EditorManager
*                in ImGuiManager.cpp.
*
* \history
*    -v1.0       WASD, Space, camera-relative (the Renderer's camera)
*    -v1.1       reads the CameraComponent entity; SimulationOnly, so the
*                right-mouse-button guard for the editor is gone
*    -v1.2       third-person camera: CameraOrbit and the cursor lock
*    -v1.3       the mech turns to face the camera (m_faceCamera)
*    -v1.4       gamepad: left stick walks, right stick turns the camera,
*                A jumps
*    -v1.5       takes the player's InputSettings (sensitivity, invert Y)
*    -v1.6       split (Networking Guide PR 10, Abhishek Akilandeswari
*                Patchamuthu): input, cursor and camera pivot moved to
*                LocalPlayerInputSystem (with the InputSettings); this
*                applies PlayerInput
*
**************************************************************************/
#pragma once

// Crucible engine
#include "EngineExport.h"
#include "Systems/IECSSystems.h"

namespace Crucible {

    // Exported so the engine's tests can drive it alongside LocalPlayerInputSystem.
    class ENGINE_API ECSPlayerControllerSystem final : public IECSSystem {

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

        // Registered .ReactiveTo<PlayerController>() (Core): a PlayerController that stops
        // running (switched off, or its CharacterController is) drops out of the query below,
        // and SimpleMove keeps walking until told otherwise - so stop the mech here.
        void OnComponentDeactivated(World& world, Entity entity, ComponentId component) override;

    };  // ECSPLAYERCONTROLLERSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
