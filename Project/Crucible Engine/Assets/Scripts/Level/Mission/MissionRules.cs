/*************************************************************************
* \file:         MissionRules.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         A mission's overall flow, as plain data (no engine calls):
*                objectives in progress -> extract open -> complete.
*
* \details
*    Ported from the Unity prototype (Level/Mission/MissionTracker.cs,
*    MissionRules), unchanged: once every objective is complete the
*    extract opens (the GDD's extraction point goes from neutral to
*    active); once the players have extracted the mission is complete.
*    What "extracted" means is the caller's rule (M3: every player inside
*    the extract - MissionController).
*
* \history
*    -v1.0       ported from Unity
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public enum MissionState { InProgress, ExtractOpen, Complete }

    public sealed class MissionRules
    {
        public MissionState State { get; private set; }

        /// <summary>Advances with the current counts. Returns true when the state changed.</summary>
        public bool Update(int total, int complete, bool extracted)
        {
            MissionState next = State;
            if (State == MissionState.InProgress && complete >= total) next = MissionState.ExtractOpen;
            else if (State == MissionState.ExtractOpen && extracted) next = MissionState.Complete;
            bool changed = next != State;
            State = next;
            return changed;
        }
    }
}
