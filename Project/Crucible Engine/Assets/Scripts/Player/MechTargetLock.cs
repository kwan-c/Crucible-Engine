/*************************************************************************
* \file:         MechTargetLock.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.0
* \brief         Target selection and camera lock. Port of Unity's
*                MechTargetLockController (Combat/ControlScheme.md):
*
*                - Automatic weapon aim is always on: every frame the enemy
*                  under the screen centre wins, then one within the small
*                  switch radius of the centre, else the nearest-to-centre
*                  enemy inside the auto-aim circle (0.18 of the smaller
*                  screen side) and lock range (60 m). A valid selection is
*                  kept while it stays in that area.
*                - F toggles hard lock: the camera and body track the target
*                  (MechPlayer); losing it picks the nearest visible one.
*                - While hard locked, a quick mouse flick switches to the
*                  nearest visible enemy in that screen direction.
*
* \details
*    A plain object owned by MechPlayer (one script per entity). Candidates
*    are the damageable targets in MechCombatTargets (Unity: colliders with a
*    MechTargetable within lockRange). "Under the screen centre" is the
*    centre ray's first collider belonging to a target. All screen maths
*    uses Camera.WorldToScreenPoint in game-screen pixels.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class MechTargetLock
    {
        // Unity MechTargetLockController defaults (FullPlayer_mech_specialist keeps them).
        public float LockRange = 60f;
        public float ScreenPadding = 0.04f;        // viewport fraction; acquisition only
        public float TargetSwitchRadius = 0.025f;  // fraction of the smaller screen dimension
        public float AutoAimRadius = 0.18f;
        public float SwitchFlickDistance = 80f;    // pixels within the gesture window
        public float SwitchGestureWindow = 0.2f;
        public float SwitchMinimumSpeed = 120f;    // pixels / second
        public float SwitchReleaseTime = 0.1f;
        public float SwitchCooldown = 0.25f;
        public float SwitchDirectionHalfAngle = 60f;

        public MechCombatTargets.Target CurrentTarget { get; private set; }
        public bool IsFreeAim { get { return !targetingEnabled; } }
        public bool IsHardLockMode { get { return targetingEnabled && hardLockEnabled; } }
        public bool IsHardLocked { get { return IsHardLockMode && IsTargetInRange(CurrentTarget); } }
        public event Action<bool> HardLockChanged;

        bool targetingEnabled = true;   // Unity: OnEnable -> ResumeTargeting
        bool hardLockEnabled, rememberedHardLock;
        bool switchLatched;
        Vector2 switchTravel;
        float gestureTime, quietTime, cooldownRemaining;
        Entity owner;

        public MechTargetLock(Entity owner) { this.owner = owner; }

        Vector3 OwnerPosition { get { return owner.Transform.WorldPosition; } }

        /// <summary>F: toggles camera lock on press. Weapon auto-aim stays on either way.</summary>
        public void ToggleHardLock(Camera camera)
        {
            if (IsFreeAim) ResumeTargeting(camera);
            if (hardLockEnabled) { SetHardLock(false); return; }
            // Toggling follows the visible selection; it must not silently choose another enemy.
            SetHardLock(true);
            if (camera == null || !IsValidTarget(CurrentTarget, camera)) SelectTarget(camera);
        }

        public void EnterFreeAim()
        {
            if (targetingEnabled) rememberedHardLock = hardLockEnabled;
            targetingEnabled = false;
            SetHardLock(false);
            CurrentTarget = null;
        }

        public void ResumeTargeting(Camera camera)
        {
            if (targetingEnabled) return;
            targetingEnabled = true;
            SelectTarget(camera);
            SetHardLock(rememberedHardLock);
        }

        public void CancelSwitchGesture()
        {
            switchTravel = Vector2.Zero; gestureTime = quietTime = 0f; switchLatched = false;
        }

        /// <summary>Once per frame after the camera moved: keep or re-pick the target.</summary>
        public void Update(Camera camera, Vector2 mouseDelta, float deltaTime)
        {
            if (IsFreeAim || camera == null) return;
            if (hardLockEnabled)
            {
                ProcessSwitchInput(camera, mouseDelta, deltaTime);
                RefreshHardLockTarget(camera);
                return;
            }
            SelectTarget(camera);
        }

        // --- hard lock: keep the target while it lives and is in range, else the nearest visible ---------
        void RefreshHardLockTarget(Camera camera)
        {
            if (!IsHardLockMode) return;
            if (CurrentTarget != null && IsTargetInRange(CurrentTarget) && MechCombatTargets.All.Contains(CurrentTarget)) return;
            CurrentTarget = null;
            SelectTarget(camera);
        }

        void ProcessSwitchInput(Camera camera, Vector2 mouseDelta, float deltaTime)
        {
            if (!IsHardLocked || deltaTime <= 0f) return;
            cooldownRemaining = Math.Max(0f, cooldownRemaining - deltaTime);
            gestureTime += deltaTime;
            if (mouseDelta.Length / deltaTime < SwitchMinimumSpeed || (mouseDelta.X == 0f && mouseDelta.Y == 0f))
            {
                quietTime += deltaTime;
                if (quietTime >= SwitchReleaseTime) { switchLatched = false; switchTravel = Vector2.Zero; gestureTime = 0f; }
                return;
            }
            quietTime = 0f;
            if (switchLatched || cooldownRemaining > 0f) return;
            if (gestureTime > SwitchGestureWindow || Vector2.Dot(switchTravel, mouseDelta) < 0f)
            {
                switchTravel = Vector2.Zero;
                gestureTime = deltaTime;
            }
            switchTravel = switchTravel + mouseDelta;
            if (switchTravel.Length < SwitchFlickDistance) return;
            SwitchInDirection(camera, switchTravel.Normalized);
            // Consume even a gesture toward empty space, so holding it cannot switch later.
            switchLatched = true;
            cooldownRemaining = SwitchCooldown;
            switchTravel = Vector2.Zero;
            gestureTime = 0f;
        }

        void SwitchInDirection(Camera camera, Vector2 direction)
        {
            Vector2 origin; float depth;
            if (CurrentTarget == null || !camera.WorldToScreenPoint(CurrentTarget.AimPosition, out origin, out depth)) return;
            float minimumDot = (float)Math.Cos(SwitchDirectionHalfAngle * Math.PI / 180.0);
            MechCombatTargets.Target next = null;
            float nearest = float.MaxValue;
            foreach (var candidate in MechCombatTargets.All)
            {
                if (candidate == CurrentTarget || !IsValidTarget(candidate, camera)) continue;
                Vector2 screen;
                if (!camera.WorldToScreenPoint(candidate.AimPosition, out screen, out depth)) continue;
                Vector2 offset = screen - origin;
                float sqr = Vector2.Dot(offset, offset);
                if (sqr < 1f || Vector2.Dot(offset.Normalized, direction) < minimumDot) continue;
                if (sqr < nearest) { nearest = sqr; next = candidate; }
            }
            if (next != null) CurrentTarget = next;
        }

        void SetHardLock(bool active)
        {
            if (hardLockEnabled == active) return;
            hardLockEnabled = active;
            if (targetingEnabled) rememberedHardLock = active;
            CancelSwitchGesture();
            cooldownRemaining = 0f;
            if (HardLockChanged != null) HardLockChanged(active);
        }

        // --- selection (Unity's SelectTarget) --------------------------------------------------------
        void SelectTarget(Camera camera)
        {
            if (IsFreeAim || camera == null) { CurrentTarget = null; return; }
            Vector2 size = Camera.ScreenSize;
            if (size.X <= 0f || size.Y <= 0f) return;
            Vector2 centre = size * 0.5f;
            float smaller = Math.Min(size.X, size.Y);
            float switchRadius = TargetSwitchRadius * smaller, acquisition = AutoAimRadius * smaller;

            // The enemy whose collider the centre ray crosses first.
            MechCombatTargets.Target rayTarget = null;
            Ray ray; RaycastHit hit;
            if (camera.ViewportPointToRay(new Vector2(0.5f, 0.5f), out ray)
                && Physics.Raycast(ray, out hit, LockRange + (OwnerPosition - ray.Origin).Length, owner))
            {
                MechCombatTargets.Target owning = MechCombatTargets.Owning(hit.Entity);
                if (owning != null && IsValidTarget(owning, camera)) rayTarget = owning;
            }

            MechCombatTargets.Target nearest = null, centred = null;
            float nearestScore = float.MaxValue, centredScore = float.MaxValue;
            bool keepCurrent = false;
            foreach (var candidate in MechCombatTargets.All)
            {
                if (!IsValidTarget(candidate, camera)) continue;
                Vector2 screen; float depth;
                if (!camera.WorldToScreenPoint(candidate.AimPosition, out screen, out depth)) continue;
                Vector2 d = screen - centre;
                float score = Vector2.Dot(d, d);
                bool crossed = candidate == rayTarget;
                if (!hardLockEnabled && !crossed && score > acquisition * acquisition) continue;
                keepCurrent |= candidate == CurrentTarget;
                // Soft acquisition follows the centre; hard-lock recovery uses world distance.
                Vector3 toTarget = candidate.AimPosition - OwnerPosition;
                float distance = hardLockEnabled ? Vector3.Dot(toTarget, toTarget) : score;
                if (distance < nearestScore) { nearestScore = distance; nearest = candidate; }
                if (score <= switchRadius * switchRadius && score < centredScore) { centredScore = score; centred = candidate; }
            }
            // Empty space keeps a valid selection instead of cycling through enemies.
            MechCombatTargets.Target aimed = rayTarget ?? centred;
            CurrentTarget = !hardLockEnabled && aimed != null ? aimed : keepCurrent ? CurrentTarget : nearest;
        }

        bool IsTargetInRange(MechCombatTargets.Target target)
        {
            if (target == null || !target.Entity.IsAlive || MechCombatTargets.Same(target.Entity, owner)) return false;
            Vector3 offset = target.AimPosition - OwnerPosition;
            return Vector3.Dot(offset, offset) <= LockRange * LockRange;
        }

        bool IsValidTarget(MechCombatTargets.Target target, Camera camera)
        {
            if (!IsTargetInRange(target)) return false;
            Vector2 screen; float depth;
            if (!camera.WorldToScreenPoint(target.AimPosition, out screen, out depth)) return false;
            Vector2 size = Camera.ScreenSize;
            if (size.X <= 0f || size.Y <= 0f) return false;
            // Padding affects acquisition only. Keep a selected enemy until it actually exits.
            float padding = target == CurrentTarget ? 0f : ScreenPadding;
            float vx = screen.X / size.X, vy = screen.Y / size.Y;
            return depth >= camera.NearClip && depth <= camera.FarClip
                && vx >= padding && vx <= 1f - padding && vy >= padding && vy <= 1f - padding;
        }
    }
}
