/*************************************************************************
* \file:         MechTargetLock.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       2.0
* \brief         Target selection for the three aim modes. Port of Unity's
*                MechTargetLockController (LockMode NoLockOn /
*                FreeCameraLockOn / CameraLockOn), with Crucible's own
*                hard-lock switching:
*
*                - Free Aim: no target. Weapons aim at the screen centre.
*                - Soft Lock: the camera stays free. The enemy under the
*                  hidden centre-screen targeting cursor becomes the target;
*                  with nothing under it, the selection is kept while it stays
*                  inside the auto-aim circle (Unity: 0.4 of the smaller
*                  screen side on the player prefab) and lock range (60 m),
*                  and an empty selection takes the enemy nearest the centre
*                  of that circle.
*                - Hard Lock: the camera follows the target (MechPlayer). The
*                  player switches the same way as in soft lock: the same
*                  hidden cursor, acquisition and hysteresis. MechPlayer lets
*                  the mouse add a bounded look offset on top of the camera
*                  tracking, so the cursor can be moved onto another enemy;
*                  the camera then follows the new one. (Unity used a mouse
*                  flick toward a screen direction instead. That was dropped
*                  at the design owner's request.)
*
* \details
*    A plain object owned by MechPlayer (one script per entity). Candidates
*    are the damageable targets in MechCombatTargets. Valid means: alive,
*    switched on, registered, not the player, inside LockRange and in front
*    of the camera inside the screen (minus ScreenPadding for a new pick).
*
*    Stability (hysteresis), all tunable from MechPlayer's Inspector fields:
*      - an enemy must stay under the cursor for SwitchDwellSeconds before it
*        replaces the current target, so a body passing through the crosshair
*        or two overlapping enemies cannot make the selection flicker;
*      - the current target wins while it is under the cursor itself;
*      - soft lock drops its target only once it leaves KeepRadiusScale x the
*        acquisition circle; empty space under the cursor keeps the target;
*      - a dead, destroyed, unregistered or out-of-range target is dropped at
*        once (hard lock then recovers the nearest visible enemy by world
*        distance, as Unity did).
*
*    v2.0  Three explicit modes (F cycles them, as Unity), dwell hysteresis,
*          hard-lock switching through the shared cursor.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    /// <summary>Unity's LockMode order: NoLockOn, FreeCameraLockOn, CameraLockOn.</summary>
    public enum MechAimMode { FreeAim = 0, SoftLock = 1, HardLock = 2 }

    public sealed class MechTargetLock
    {
        // Unity MechTargetLockController values (FullPlayer_mech_specialist keeps them).
        public float LockRange = 60f;
        public float ScreenPadding = 0.04f;        // viewport fraction; acquisition only
        public float TargetSwitchRadius = 0.025f;  // hidden cursor radius, fraction of the smaller screen dimension
        public float AutoAimRadius = 0.4f;         // Unity main's player prefab (the code default there is 0.18)
        // Crucible hysteresis (MechPlayer copies its Inspector fields here every frame).
        public float SwitchDwellSeconds = 0.12f;   // a new enemy must stay under the cursor this long
        public float KeepRadiusScale = 1.25f;      // soft lock keeps its target out to this x the auto-aim circle

        /// <summary>The selected enemy (null = none). The setter is private: QA sets it by reflection.</summary>
        public MechCombatTargets.Target CurrentTarget { get; private set; }
        public MechAimMode Mode { get; private set; }
        public bool IsFreeAim { get { return Mode == MechAimMode.FreeAim; } }
        public bool IsSoftLock { get { return Mode == MechAimMode.SoftLock; } }
        public bool IsHardLockMode { get { return Mode == MechAimMode.HardLock; } }
        public bool IsHardLocked { get { return IsHardLockMode && IsTargetInRange(CurrentTarget); } }
        /// <summary>The valid enemy under the hidden targeting cursor this frame (null = empty space).</summary>
        public MechCombatTargets.Target CursorTarget { get; private set; }
        /// <summary>Every change of CurrentTarget, for QA and presentation.</summary>
        public int TargetChanges { get; private set; }

        public event Action<bool> HardLockChanged;
        public event Action<MechAimMode> ModeChanged;
        /// <summary>(previous, next) whenever the selection changes.</summary>
        public event Action<MechCombatTargets.Target, MechCombatTargets.Target> TargetChanged;

        MechAimMode rememberedLockMode = MechAimMode.SoftLock;
        MechCombatTargets.Target pendingTarget;   // waiting out SwitchDwellSeconds under the cursor
        float pendingTime;
        readonly Entity owner;

        public MechTargetLock(Entity owner) { this.owner = owner; Mode = MechAimMode.SoftLock; }

        Vector3 OwnerPosition { get { return owner.Transform.WorldPosition; } }

        // ---------------------------------------------------------------------------- //
        // Modes
        // ---------------------------------------------------------------------------- //

        /// <summary>F: Free Aim -> Soft Lock -> Hard Lock -> Free Aim (Unity's CycleLockMode).</summary>
        public void CycleMode(Camera camera)
        {
            SetMode((MechAimMode)(((int)Mode + 1) % 3), camera);
        }

        public void SetMode(MechAimMode mode, Camera camera)
        {
            if (mode == Mode) return;
            bool wasHard = IsHardLockMode;
            Mode = mode;
            ResetDwell();
            if (mode == MechAimMode.FreeAim) SetTarget(null);
            else
            {
                rememberedLockMode = mode;
                // Entering soft lock picks what the cursor shows. Entering hard lock keeps the
                // visible selection; it must not silently choose another enemy.
                if (mode == MechAimMode.SoftLock || camera == null || !IsValidTarget(CurrentTarget, camera))
                    AcquireNew(camera);
            }
            if (wasHard != IsHardLockMode && HardLockChanged != null) HardLockChanged(IsHardLockMode);
            if (ModeChanged != null) ModeChanged(mode);
        }

        /// <summary>Soft <-> hard (Unity's ToggleHardLock). From Free Aim it enters hard lock.</summary>
        public void ToggleHardLock(Camera camera)
        {
            SetMode(IsHardLockMode ? MechAimMode.SoftLock : MechAimMode.HardLock, camera);
        }

        public void EnterFreeAim() { SetMode(MechAimMode.FreeAim, null); }

        /// <summary>Back to the last lock mode used (Unity's ResumeTargeting).</summary>
        public void ResumeTargeting(Camera camera)
        {
            if (IsFreeAim) SetMode(rememberedLockMode, camera);
        }

        /// <summary>Forget the selection but keep the mode (respawn, scene change).</summary>
        public void ClearTarget()
        {
            ResetDwell();
            SetTarget(null);
        }

        // ---------------------------------------------------------------------------- //
        // Per frame
        // ---------------------------------------------------------------------------- //

        /// <summary>Once per frame after the camera moved: keep, switch or re-pick the target.</summary>
        public void Update(Camera camera, float deltaTime)
        {
            if (IsFreeAim || camera == null) { CursorTarget = null; return; }

            // A destroyed, dead, unregistered or out-of-range lock is dropped before anything else.
            if (CurrentTarget != null && !IsTargetInRange(CurrentTarget)) SetTarget(null);

            Vector2 size = Camera.ScreenSize;
            if (size.X <= 0f || size.Y <= 0f) return;
            CursorTarget = FindCursorTarget(camera, size);

            if (CurrentTarget == null)
            {
                // Nothing selected: take what the cursor shows at once, else (soft lock: the enemy
                // nearest the centre of the auto-aim circle; hard lock: the nearest by distance).
                ResetDwell();
                if (CursorTarget != null) SetTarget(CursorTarget);
                else AcquireNew(camera);
                return;
            }

            // Soft lock lets a selection go once it leaves the (slightly larger) keep circle.
            if (IsSoftLock && !InsideCircle(CurrentTarget, camera, size, AutoAimRadius * Math.Max(1f, KeepRadiusScale)))
            {
                ResetDwell();
                SetTarget(null);
                if (CursorTarget != null) SetTarget(CursorTarget); else AcquireNew(camera);
                return;
            }

            // Switching: only another enemy held under the cursor for the dwell time. The current
            // target under the cursor, or empty space, keeps the selection.
            if (CursorTarget == null || CursorTarget == CurrentTarget) { ResetDwell(); return; }
            if (CursorTarget != pendingTarget) { pendingTarget = CursorTarget; pendingTime = 0f; }
            pendingTime += Math.Max(0f, deltaTime);
            if (pendingTime + 1e-5f < Math.Max(0f, SwitchDwellSeconds)) return;
            ResetDwell();
            SetTarget(CursorTarget);
        }

        void ResetDwell() { pendingTarget = null; pendingTime = 0f; }

        void SetTarget(MechCombatTargets.Target next)
        {
            if (ReferenceEquals(CurrentTarget, next)) return;
            MechCombatTargets.Target previous = CurrentTarget;
            CurrentTarget = next;
            TargetChanges++;
            if (TargetChanged != null) TargetChanged(previous, next);
        }

        // ---------------------------------------------------------------------------- //
        // The hidden targeting cursor (screen centre) and acquisition
        // ---------------------------------------------------------------------------- //

        /// <summary>
        /// The valid enemy under the hidden cursor: the one whose collider the centre ray crosses
        /// first, else the one whose aim point is nearest the centre within TargetSwitchRadius.
        /// When the current target is under the cursor too, it wins (no flicker between overlaps).
        /// </summary>
        MechCombatTargets.Target FindCursorTarget(Camera camera, Vector2 size)
        {
            Vector2 centre = size * 0.5f;
            float switchRadius = TargetSwitchRadius * Math.Min(size.X, size.Y);

            MechCombatTargets.Target rayTarget = null;
            Ray ray; RaycastHit hit;
            if (camera.ViewportPointToRay(new Vector2(0.5f, 0.5f), out ray)
                && Physics.Raycast(ray, out hit, LockRange + (OwnerPosition - ray.Origin).Length, owner))
            {
                MechCombatTargets.Target owning = MechCombatTargets.Owning(hit.Entity);
                if (owning != null && IsValidTarget(owning, camera)) rayTarget = owning;
            }

            MechCombatTargets.Target centred = null;
            float centredScore = float.MaxValue;
            bool currentCentred = false;
            foreach (var candidate in MechCombatTargets.All)
            {
                Vector2 screen;
                if (!IsValidTarget(candidate, camera) || !Project(candidate, camera, out screen)) continue;
                Vector2 d = screen - centre;
                float score = Vector2.Dot(d, d);
                if (score > switchRadius * switchRadius) continue;
                if (candidate == CurrentTarget) currentCentred = true;
                if (score < centredScore) { centredScore = score; centred = candidate; }
            }
            if (CurrentTarget != null && (rayTarget == CurrentTarget || currentCentred)) return CurrentTarget;
            return rayTarget ?? centred;
        }

        /// <summary>A fresh pick with nothing selected (Unity's SelectTarget fallback).</summary>
        void AcquireNew(Camera camera)
        {
            if (IsFreeAim || camera == null) { SetTarget(null); return; }
            Vector2 size = Camera.ScreenSize;
            if (size.X <= 0f || size.Y <= 0f) return;
            Vector2 centre = size * 0.5f;
            float acquisition = AutoAimRadius * Math.Min(size.X, size.Y);
            MechCombatTargets.Target best = FindCursorTarget(camera, size);
            if (best == null)
            {
                float bestScore = float.MaxValue;
                foreach (var candidate in MechCombatTargets.All)
                {
                    Vector2 screen;
                    if (!IsValidTarget(candidate, camera) || !Project(candidate, camera, out screen)) continue;
                    Vector2 d = screen - centre;
                    float screenScore = Vector2.Dot(d, d);
                    // Soft acquisition stays inside the auto-aim circle; hard-lock recovery keeps
                    // Unity's world-distance rule over every visible enemy.
                    if (!IsHardLockMode && screenScore > acquisition * acquisition) continue;
                    Vector3 toTarget = candidate.AimPosition - OwnerPosition;
                    float score = IsHardLockMode ? Vector3.Dot(toTarget, toTarget) : screenScore;
                    if (score < bestScore) { bestScore = score; best = candidate; }
                }
            }
            SetTarget(best);
        }

        bool InsideCircle(MechCombatTargets.Target target, Camera camera, Vector2 size, float radiusFraction)
        {
            Vector2 screen;
            if (!IsValidTarget(target, camera) || !Project(target, camera, out screen)) return false;
            Vector2 d = screen - size * 0.5f;
            float radius = radiusFraction * Math.Min(size.X, size.Y);
            return Vector2.Dot(d, d) <= radius * radius;
        }

        static bool Project(MechCombatTargets.Target target, Camera camera, out Vector2 screen)
        {
            float depth;
            return camera.WorldToScreenPoint(target.AimPosition, out screen, out depth);
        }

        // ---------------------------------------------------------------------------- //
        // Validity
        // ---------------------------------------------------------------------------- //

        /// <summary>Alive, switched on, still registered, not the player, within LockRange.</summary>
        public bool IsTargetInRange(MechCombatTargets.Target target)
        {
            if (target == null || target.Entity == null || !target.Entity.IsAlive || !target.Entity.IsActive
                || MechCombatTargets.Same(target.Entity, owner) || !MechCombatTargets.All.Contains(target)) return false;
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
