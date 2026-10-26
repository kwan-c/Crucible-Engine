/*************************************************************************
* \file:         PlayerComponents.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          2 October 2026
* \version       1.1
* \brief
*                The local player's controls: PlayerController (walk and
*                jump the mech) and CameraOrbit (the third-person camera
*                pivot). ECSPlayerControllerSystem reads both.
*
* \details
*
*                The numbers here are the DESIGNER'S feel for this rig -
*                how fast the camera turns at full tilt, how fast the mech
*                walks. A player's own preferences (a sensitivity slider,
*                invert Y) are a different thing: they belong with the
*                player's settings and multiply these, once there is an
*                options menu to set them from.
*
* \history
*    -v1.0       in Physics/PhysicsComponents.h: PlayerController,
*                CameraOrbit (2-3 October 2026)
*    -v1.1       moved here, out of the physics components;
*                CameraOrbit::m_stickSpeed for the gamepad's right stick
*
**************************************************************************/
#pragma once

// Crucible engine
#include "ECS/Components/ComponentKind.h"
#include "Math/Math.h"

namespace Crucible {

    /*
        Marks the mech THIS machine controls: ECSPlayerControllerSystem reads
        the keyboard and gamepad and drives the CharacterController on the same
        entity. Unity's equivalent is a PlayerMovement script calling SimpleMove.

            world.AddComponent(mech, CharacterController{});
            world.AddComponent(mech, PlayerController{});     // ...and WASD / the left stick walks it

        A separate component, not a flag on CharacterController, because in
        multiplayer every mech has a CharacterController but only the local
        one reads the controls. The other mechs are moved by network packets,
        so they simply never get this component.

        Does nothing without a CharacterController next to it.
    */
    struct PlayerController {

        float m_moveSpeed  = 6.0f;      // m/s on the ground at full stick or a held key. Diagonals are no faster
        float m_jumpHeight = 1.5f;      // metres, measured from the feet. 0 disables jumping

        // Turn the whole mech to face where the camera looks (yaw only - it stays
        // upright). Needs the CameraOrbit pivot to be a child of this entity. Off:
        // the mech keeps its rotation and the camera orbits freely round it.
        // Safe to flip mid-Play: the pivot cancels the mech's turn, so the view
        // never jumps either way.
        bool  m_faceCamera = true;

    };  // PLAYERCONTROLLER STRUCT

    CRUCIBLE_COMPONENT(PlayerController, PlayerController)

    /*
        The third-person camera. The mouse and the right stick turn the entity
        this sits on, and that entity is a PIVOT - an empty point the camera
        hangs off - not the camera itself:

            Mech            CharacterController, PlayerController
            '- Pivot        CameraOrbit, local position about (0, 1.5, 0): shoulder height
               '- Camera    CameraComponent, local position about (0, 0, 5): 5m behind

        Turning the pivot swings "5m behind me" around, so the camera circles
        the mech and the hierarchy does all the maths; nothing ever touches the
        camera entity. Unity's Starter Assets do the same with PlayerCameraRoot.
        Putting this on the camera instead would only spin it in place - it
        would look away from the mech rather than go round it.

        ECSPlayerControllerSystem writes the pivot's Transform.rotation from
        m_yaw and m_pitch every frame while Playing, so rotate the pivot with
        these two fields, not with the gizmo. They are saved: what they hold
        in the editor is the view the game starts with.
    */
    struct CameraOrbit {

        // SETTINGS: the designer's feel for this camera ---------------------- //

        float m_sensitivity = 0.1f;     // degrees turned per pixel of mouse movement
        float m_stickSpeed  = 180.0f;   // degrees per SECOND with the gamepad's right stick fully tilted
        float m_minPitch    = -30.0f;   // degrees. Below 0 the camera dips under the pivot and looks up
        float m_maxPitch    =  70.0f;   // degrees. Short of 90, or at the top the view flips over

        // STATE: the current view, in degrees -------------------------------- //

        float m_yaw         = 0.0f;     // round the mech. + turns the view left, as the mouse moving left does
        float m_pitch       = 0.0f;     // + raises the camera to look down on the mech, as the mouse moving down does

    };  // CAMERAORBIT STRUCT

    CRUCIBLE_COMPONENT(CameraOrbit, CameraOrbit)

}   // NAMESPACE CRUCIBLE
