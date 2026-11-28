/*************************************************************************
* \file:         MechRespawn.cs
* \brief         MechPlayer, part: putting a destroyed mech back into play.
*                Used by the Zoo's test respawn (Zoo/ZooPlayerRespawn.cs).
*                Production missions do not call it: their death and failure
*                flow is separate and out of scope here.
*
* \details
*    Respawn reuses the start-of-play paths instead of a second
*    initialisation: RefreshLoadout(true) is BuildLoadout's own call
*    (maximum health from the equipped parts, refilled, which also revives),
*    and the HUD rebinds on the LoadoutVersion change exactly as at start.
*
*    Everything temporary from the fight is cleared first: knockback, the
*    Quick Boost and its cooldown, hit stop, an assisted swing or punch
*    burst, the hook pull, rock sweeps, shield windows, weapon cooldowns and
*    reloads, running abilities (Damage Amp, Heal) and their cooldowns, the
*    weapon idle, camera shake and FOV pulse, the target lock selection and
*    the hard-lock look offset. The legs go back to the Neutral pose.
*
*    The current loadout is kept (Zoo testing: the tester respawns with the
*    weapon being tested) and every weapon is rearmed to a full magazine.
*    The aim mode the player chose is kept; only the selected target is
*    forgotten.
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    public sealed partial class MechPlayer
    {
        /// <summary>Raised after a successful Respawn.</summary>
        public event Action Respawned;
        public int RespawnCount { get; private set; }

        /// <summary>
        /// Puts the mech at position (its root, as Transform.WorldPosition) facing yawDegrees,
        /// alive at full health with the current loadout. False without a CharacterController.
        /// </summary>
        public bool Respawn(Vector3 position, float yawDegrees)
        {
            if (body == null || !MechMath.Finite(position.X + position.Y + position.Z) || !MechMath.Finite(yawDegrees)) return false;

            ClearTemporaryCombatState();

            // Place it. The controller keeps the last speed it was given, so it is stopped too.
            Transform.WorldPosition = position;
            Transform.WorldRotation = Quaternion.AngleAxis(yawDegrees, Vector3.Up);
            body.SimpleMove(Vector3.Zero);
            Velocity = Vector3.Zero;
            State = MechMovementState.Airborne;   // the next Tick reads the ground again
            IsGrounded = false;

            // The camera looks the way the mech faces (orbit yaw is a world yaw: view forward (-sin, 0, -cos)).
            if (orbit == null)
            {
                pivot = Entity.FindChild("Camera Pivot");
                if (pivot != null && pivot.HasComponent<CameraOrbit>()) { orbit = pivot.CameraOrbit; pivotBaseY = pivot.Transform.Position.Y; }
            }
            if (orbit != null)
            {
                if (orbitSensitivity >= 0f) { orbit.Sensitivity = orbitSensitivity; orbit.StickSpeed = orbitStick; orbit.MaxPitch = orbitMaxPitch; orbitSensitivity = -1f; }
                orbit.Yaw = yawDegrees;
            }
            lockHeight = 0f;
            if (pivot != null) { Vector3 p = pivot.Transform.Position; pivot.Transform.Position = new Vector3(p.X, pivotBaseY, p.Z); }
            if (gameplayCamera != null && baseFov > 0f) gameplayCamera.FieldOfView = baseFov;

            // Health and stats: the same call BuildLoadout makes at the start of play.
            RefreshLoadout(true);

            UpdateLegsAnimation();
            RespawnCount++;
            if (Respawned != null) Respawned();
            Log.Info("MechPlayer: respawned at " + position + ".");
            return true;
        }

        void ClearTemporaryCombatState()
        {
            attacksArmed = false;                 // the click held through death must not fire at once
            meleeWeapon = null; meleeTarget = null;
            ClearWeaponEffects();                 // punches, hook pull, rock sweeps, shield (damage blocking)
            externalVelocity = Vector3.Zero; externalRemaining = externalDuration = 0f;   // knockback
            planarVelocity = Vector3.Zero;
            quickBoostTimer = quickBoostRecoveryTimer = 0f;
            brakingAfterBoost = false;
            hitStopDash = false;
            uninterruptedMoveTime = 0f; IsAutoRunning = false;
            quickBoostFovAge = float.PositiveInfinity;
            shakeTime = shakeAmplitude = 0f;
            MechHitStop.End(Entity);              // and its held animation speeds
            foreach (MechWeapon weapon in socketWeapons) if (weapon != null) weapon.Rearm();
            foreach (MechAbility ability in Abilities) if (ability != null) ability.Reset();
            ResetWeaponIdle();
            if (Targeting != null) Targeting.ClearTarget();
            ResetHardLook();
            legWasBoosting = false; legStopAge = 0f; CurrentLegPose = LegPose.Neutral;
        }
    }
}
