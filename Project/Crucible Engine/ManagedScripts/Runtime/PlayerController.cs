/*************************************************************************
* \file:         PlayerController.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Script access to an entity's PlayerController component:
*                the built-in mech controls' move speed, jump and facing.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    The engine's ECSPlayerControllerSystem walks the mech (camera-relative)
*    and jumps it. Get it with GetComponent<PlayerController>() or
*    Entity.PlayerController:
*
*        var pc = GetComponent<PlayerController>();
*        pc.MoveSpeed = 10f;                  // sprint
*        SetEnabled<PlayerController>(false); // a cutscene: the mech stops
*
*    The KEYS are not here: they are the player's settings, shared by every
*    scene and saved between launches - see InputSettings.
*
*    C++ side: Engine/src/Scripting/Bindings/PlayerControllerBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>The built-in mech controls: move speed, jump height, facing. Keys: InputSettings.</summary>
    public sealed class PlayerController
    {
        private readonly EntityIdentity entity;
        internal PlayerController(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this controller is on.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        /// <summary>Metres per second at full stick or a held key. Not negative.</summary>
        public float MoveSpeed
        {
            get { float v; return GetMoveSpeedInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetMoveSpeedInternal(I, G, ref value); }
        }

        /// <summary>Jump height in metres. 0 disables jumping.</summary>
        public float JumpHeight
        {
            get { float v; return GetJumpHeightInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetJumpHeightInternal(I, G, ref value); }
        }

        /// <summary>Turn the mech to face where the camera looks (needs a CameraOrbit child).</summary>
        public bool FaceCamera
        {
            get { int v; return GetFaceCameraInternal(I, G, out v) != 0 && v != 0; }
            set { SetFaceCameraInternal(I, G, value ? 1 : 0); }
        }

        public override string ToString() { return "PlayerController on " + Entity; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMoveSpeedInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetMoveSpeedInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetJumpHeightInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetJumpHeightInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetFaceCameraInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetFaceCameraInternal(int index, int generation, int value);
    }
}
