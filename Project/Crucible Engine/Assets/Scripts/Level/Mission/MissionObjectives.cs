/*************************************************************************
* \file:         MissionObjectives.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         The objective slot MissionController reads: a mission's
*                own objective script (EscortObjective, later Liberation's)
*                registers here, and the mission flow runs it like Capture.
*
* \details
*    Port of Unity's MissionObjectives / IMissionObjective. The engine's
*    GetComponent only takes concrete script types, so objectives register
*    themselves (in OnCreate) under the entity they are on - the Sub Level
*    Generator, next to MissionController - and unregister in OnDestroy.
*    MissionController opens the extract once every registered objective
*    (and every Capture area) is complete, and shows the first unfinished
*    objective's Title / Detail / bar on the Mission HUD.
*    Static state resets with each script reload; entries whose entity is
*    gone are skipped.
*
* \history
*    -v1.0       new: IMissionObjective + registry by owner entity
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    /// <summary>A mission objective MissionController can run (complete -> the extract opens).</summary>
    public interface IMissionObjective
    {
        bool IsComplete { get; }
        string Title { get; }                   // the HUD's first line while it is the current objective
        string Detail { get; }                  // the HUD's second line
        bool ShowBar { get; }                   // the HUD bar
        float BarProgress { get; }              // 0..1
        HoldZoneState BarState { get; }         // the bar's colour, as the zone outlines: Waiting yellow, Holding blue, Failed red, Complete green
    }

    public static class MissionObjectives
    {
        private sealed class Entry
        {
            public Entity Owner;
            public IMissionObjective Objective;
        }
        private static readonly List<Entry> entries = new List<Entry>();

        public static void Register(Entity owner, IMissionObjective objective)
        {
            if (owner == null || objective == null) return;
            foreach (Entry e in entries) if (e.Objective == objective) { e.Owner = owner; return; }
            entries.Add(new Entry { Owner = owner, Objective = objective });
        }

        public static void Unregister(IMissionObjective objective)
        {
            for (int i = entries.Count - 1; i >= 0; i--) if (entries[i].Objective == objective) entries.RemoveAt(i);
        }

        /// <summary>The objectives registered on this entity, in registration order, into `into` (cleared first).</summary>
        public static void For(Entity owner, List<IMissionObjective> into)
        {
            into.Clear();
            for (int i = entries.Count - 1; i >= 0; i--)
                if (entries[i].Owner == null || !entries[i].Owner.IsAlive) entries.RemoveAt(i);
            foreach (Entry e in entries) if (e.Owner == owner) into.Add(e.Objective);
        }
    }
}
