/*************************************************************************
* \file:         MechAimModes.cs
* \brief         MechPlayer, part: the aim-mode input and the hard-lock camera.
*                Selection itself is MechTargetLock (Unity's
*                MechTargetLockController); this file feeds it its Inspector
*                tuning, reads the mode key and turns the camera while hard
*                locked (Unity's CameraControlThirdPerson lock orbit).
*
* \details
*    F (or the right stick press) cycles Free Aim -> Soft Lock -> Hard Lock,
*    Unity's single lock button. A Zoo floor pad that the player stands on
*    claims F (MechInputClaims), so pressing F there works the pad only.
*
*    Hard lock: the camera turns toward the target with hardLockOrbitResponse
*    damping. The mouse / right stick no longer orbit freely; they add a
*    bounded look offset (hardLookYawLimit / hardLookPitchLimit degrees) on
*    top of the tracking. That moves the hidden centre-screen targeting
*    cursor across the screen without fighting the tracking, so the player
*    can rest it on another enemy: MechTargetLock switches after its dwell
*    time, exactly as in soft lock. On a switch the offset is re-based onto
*    the new target (the view does not jump), and once the input is idle for
*    hardLookReturnDelay it eases back to zero over hardLookReturnTime, so
*    the camera settles on the new target.
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    public sealed partial class MechPlayer
    {
        // --- aim modes (Unity's MechTargetLockController on FullPlayer_mech_specialist) ------------ //
        public int startAimMode = 1;               // 0 Free Aim, 1 Soft Lock (Unity's start), 2 Hard Lock
        public float lockRange = 60f;              // metres
        public float autoAimRadius = 0.4f;         // soft acquisition circle, fraction of the smaller screen side
        public float targetSwitchRadius = 0.025f;  // hidden targeting cursor radius, same units
        public float targetSwitchDwell = 0.12f;    // seconds another enemy must stay under the cursor to take over
        public float softLockKeepScale = 1.25f;    // soft lock keeps its target out to this x the auto-aim circle

        // --- camera hard lock (Unity's CameraControlThirdPerson.UpdateCameraOrbit) ------------------- //
        public float hardLockPitch = 20f;          // base pitch while locked (looks down)
        public float hardLockHeight = 1.5f;        // pivot raise while locked (Unity metres)
        public float hardLockOrbitResponse = 0.12f;
        public float hardLookYawLimit = 40f;       // degrees the player can look away from the target to pick another
        public float hardLookPitchLimit = 15f;
        public float hardLookReturnDelay = 0.35f;  // seconds without look input before the offset eases back
        public float hardLookReturnTime = 0.3f;    // time constant of that return, seconds

        CameraOrbit orbit;
        Entity pivot;
        float orbitSensitivity = -1f, orbitStick, orbitMaxPitch, pivotBaseY, lockHeight;
        float lookYaw, lookPitch, lookIdle;
        float lastDesiredYaw, lastDesiredPitch;
        bool haveDesired;
        MechCombatTargets.Target cameraTarget;

        /// <summary>Degrees the hard-lock view currently looks away from its target (QA / debug).</summary>
        public Vector2 HardLookOffset { get { return new Vector2(lookYaw, lookPitch); } }

        void ApplyAimSettings()
        {
            Targeting.LockRange = Math.Max(0.1f, lockRange);
            Targeting.AutoAimRadius = MechMath.Clamp(autoAimRadius, 0.01f, 0.5f);
            Targeting.TargetSwitchRadius = MechMath.Clamp(targetSwitchRadius, 0f, 0.25f);
            Targeting.SwitchDwellSeconds = Math.Max(0f, targetSwitchDwell);
            Targeting.KeepRadiusScale = Math.Max(1f, softLockKeepScale);
        }

        void UpdateAimModeInput(bool gameplay)
        {
            if (!gameplay) return;
            bool key = Input.IsPressed(KeyCode.F) && !MechInputClaims.IsClaimed(KeyCode.F);
            if (key || Input.IsPressed(KeyCode.GamepadRightStick)) Targeting.CycleMode(gameplayCamera);
        }

        void ResetHardLook()
        {
            lookYaw = lookPitch = 0f;
            lookIdle = 0f;
            haveDesired = false;
            cameraTarget = null;
        }

        static float WrapDegrees(float angle) { return ((angle % 360f) + 540f) % 360f - 180f; }

        void UpdateLockCamera(float deltaTime)
        {
            if (orbit == null)
            {
                pivot = Entity.FindChild("Camera Pivot");
                if (pivot == null || !pivot.HasComponent<CameraOrbit>()) return;
                orbit = pivot.CameraOrbit;
                pivotBaseY = pivot.Transform.Position.Y;
            }
            float response = 1f - (float)Math.Exp(-deltaTime / Math.Max(0.01f, hardLockOrbitResponse));
            bool locked = Targeting.IsHardLocked && !Health.IsDead;
            if (locked)
            {
                // The mouse no longer orbits freely (restored when the lock ends); it looks around the target.
                if (orbitSensitivity < 0f) { orbitSensitivity = orbit.Sensitivity; orbitStick = orbit.StickSpeed; orbitMaxPitch = orbit.MaxPitch; }
                orbit.Sensitivity = 0f; orbit.StickSpeed = 0f; orbit.MaxPitch = 80f;

                Vector3 d = Targeting.CurrentTarget.AimPosition - Transform.WorldPosition;
                float planar = MechMath.Flat(d).Length;
                float targetYaw = planar > 0.001f ? (float)(Math.Atan2(-d.X, -d.Z) * 180.0 / Math.PI) : orbit.Yaw;   // view forward = (-sin, 0, -cos)
                // Raise the view for nearby low targets so the sightline clears the mech.
                float h = hardLockHeight * unityScale;
                float clearance = (float)(Math.Atan2(h - d.Y, Math.Max(0.1f, planar - (body != null ? body.Radius : 1f) - 0.35f)) * 180.0 / Math.PI);
                float basePitch = MechMath.Clamp(Math.Max(hardLockPitch, clearance), 5f, 80f);

                // A new target (switch or recovery): keep the current view and let the offset ease out.
                if (cameraTarget != Targeting.CurrentTarget)
                {
                    if (cameraTarget != null && haveDesired)
                    {
                        lookYaw = WrapDegrees(lastDesiredYaw - targetYaw);
                        lookPitch = lastDesiredPitch - basePitch;
                        lookIdle = Math.Max(lookIdle, hardLookReturnDelay);
                    }
                    else lookYaw = lookPitch = 0f;
                    cameraTarget = Targeting.CurrentTarget;
                }

                // Look offset (the engine's orbit signs: mouse right / stick right = -yaw, mouse down = +pitch).
                Vector2 mouse = GameplayInputActive ? Input.MouseDelta : Vector2.Zero;
                Vector2 stick = GameplayInputActive ? Input.GamepadRightStick : Vector2.Zero;
                float yawInput = -mouse.X * orbitSensitivity - stick.X * orbitStick * deltaTime;
                float pitchInput = mouse.Y * orbitSensitivity - stick.Y * orbitStick * deltaTime;
                if (Math.Abs(yawInput) + Math.Abs(pitchInput) > 0.01f)
                {
                    float yawLimit = Math.Max(0f, hardLookYawLimit), pitchLimit = Math.Max(0f, hardLookPitchLimit);
                    // Re-based offsets may sit past the limits; input never pushes them further out.
                    lookYaw = MechMath.Clamp(lookYaw + yawInput, Math.Min(-yawLimit, lookYaw), Math.Max(yawLimit, lookYaw));
                    lookPitch = MechMath.Clamp(lookPitch + pitchInput, Math.Min(-pitchLimit, lookPitch), Math.Max(pitchLimit, lookPitch));
                    lookIdle = 0f;
                }
                else
                {
                    lookIdle += deltaTime;
                    if (lookIdle >= hardLookReturnDelay)
                    {
                        float keep = (float)Math.Exp(-deltaTime / Math.Max(0.01f, hardLookReturnTime));
                        lookYaw *= keep; lookPitch *= keep;
                    }
                }

                float desiredYaw = targetYaw + lookYaw;
                float desiredPitch = MechMath.Clamp(basePitch + lookPitch, 5f, 80f);
                orbit.Yaw = orbit.Yaw + WrapDegrees(desiredYaw - orbit.Yaw) * response;
                orbit.Pitch = orbit.Pitch + (desiredPitch - orbit.Pitch) * response;
                lastDesiredYaw = desiredYaw; lastDesiredPitch = desiredPitch; haveDesired = true;
                lockHeight = MechMath.Lerp(lockHeight, h, response);
            }
            else
            {
                if (orbitSensitivity >= 0f) { orbit.Sensitivity = orbitSensitivity; orbit.StickSpeed = orbitStick; orbit.MaxPitch = orbitMaxPitch; orbitSensitivity = -1f; }
                ResetHardLook();
                lockHeight = MechMath.Lerp(lockHeight, 0f, response);
            }
            Vector3 p = pivot.Transform.Position;
            pivot.Transform.Position = new Vector3(p.X, pivotBaseY + lockHeight, p.Z);
        }
    }
}
