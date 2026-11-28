/*************************************************************************
* \file:         HoldZoneRules.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         The rules of an area players must hold for a set time
*                (Capture's areas), as plain data (no engine calls), so
*                they can be tested alone.
*
* \details
*    Ported from the Unity prototype (Level/Objectives/HoldZone.cs,
*    HoldZoneRules), unchanged. The GDD's Capture states:
*      Waiting   idle; the area starts here
*      Holding   players are inside and the hold timer counts
*      Warning   they left: the timer pauses and the grace timer counts;
*                going back in resumes the hold
*      Failed    the grace ran out; shown for failedSeconds, then back to
*                Waiting with the hold timer reset
*      Complete  the hold timer finished; stays complete
*    Whether players count as "inside" is the caller's rule (Capture:
*    every player inside - MissionController).
*
* \history
*    -v1.0       ported from Unity
*
**************************************************************************/
using System;

namespace Crucible.@Scripts
{
    public enum HoldZoneState { Waiting, Holding, Warning, Failed, Complete }

    public sealed class HoldZoneRules
    {
        public float HoldSeconds { get; private set; }
        public float GraceSeconds { get; private set; }
        public float FailedSeconds { get; private set; }
        public HoldZoneState State { get; private set; }
        public float Elapsed { get; private set; }                 // seconds held so far
        public float Remaining { get { return Math.Max(0f, HoldSeconds - Elapsed); } }
        public float GraceRemaining { get; private set; }
        private float failedAge;

        public HoldZoneRules(float holdSeconds, float graceSeconds, float failedSeconds)
        {
            HoldSeconds = Math.Max(0.1f, holdSeconds);
            GraceSeconds = Math.Max(0f, graceSeconds);
            FailedSeconds = Math.Max(0f, failedSeconds);
            State = HoldZoneState.Waiting;
        }

        /// <summary>Advances one frame. Returns true when the state changed.</summary>
        public bool Tick(float deltaTime, bool inside)
        {
            deltaTime = Math.Max(0f, deltaTime);
            HoldZoneState next = State;
            switch (State)
            {
                case HoldZoneState.Waiting:
                    if (inside) { Elapsed = 0f; next = HoldZoneState.Holding; }
                    break;
                case HoldZoneState.Holding:
                    if (!inside) { GraceRemaining = GraceSeconds; next = HoldZoneState.Warning; break; }
                    Elapsed = Math.Min(HoldSeconds, Elapsed + deltaTime);
                    if (Elapsed >= HoldSeconds) next = HoldZoneState.Complete;
                    break;
                case HoldZoneState.Warning:
                    if (inside) { next = HoldZoneState.Holding; break; }
                    GraceRemaining = Math.Max(0f, GraceRemaining - deltaTime);
                    if (GraceRemaining <= 0f) { failedAge = 0f; next = HoldZoneState.Failed; }
                    break;
                case HoldZoneState.Failed:
                    failedAge += deltaTime;
                    if (failedAge >= FailedSeconds) { Elapsed = 0f; next = HoldZoneState.Waiting; }
                    break;
            }
            bool changed = next != State;
            State = next;
            return changed;
        }
    }
}
