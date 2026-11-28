/*************************************************************************
* \file:         EscortRules.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         The escort payload's movement along its route as plain
*                rules (no engine types): when it advances, stops or rolls
*                back, its checkpoints and where it respawns.
*
* \details
*    Ported from the Unity prototype (Level/Objectives/EscortRules.cs),
*    with the GDD's change (no contested stop):
*      a player in range            -> it advances, enemies or not
*                                      (Advancing with no enemies near,
*                                      Contested with enemies near);
*                                      contestedStops = Unity's rule
*                                      instead (enemies near stop it)
*      only enemies in range        -> Retreating: rolls back, never past
*                                      the last checkpoint reached
*      nobody                       -> Idle
*    It starts at `start` (the 1st checkpoint). Passing a checkpoint makes
*    it the floor (and the respawn point); the last checkpoint is the end,
*    where the escort has Arrived. Distances are along the route
*    (EscortRoute). EscortObjective runs it.
*
* \history
*    -v1.0       ported from Unity; player in range always advances
*                (contestedStops = Unity's stop, off by default)
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public enum EscortState { Idle, Advancing, Contested, Retreating, Arrived }

    public sealed class EscortRules
    {
        private readonly float length;
        private readonly List<float> checkpoints = new List<float>();

        public float Distance { get; private set; }                 // along the route
        public float Start { get; private set; }                    // the 1st checkpoint
        public float Floor { get; private set; }                    // the last checkpoint reached (respawn point)
        public int CheckpointsPassed { get; private set; }
        public int CheckpointCount { get { return checkpoints.Count; } }   // the ones after the start, the end included
        public EscortState State { get; private set; }
        public bool Arrived { get { return State == EscortState.Arrived; } }
        public float Length { get { return length; } }
        public bool Moving { get { return State == EscortState.Advancing || State == EscortState.Contested; } }

        /// <summary>start and end: distances along the route; checkpointDistances: the ones after the start (the end included or not).</summary>
        public EscortRules(float start, float end, IEnumerable<float> checkpointDistances)
        {
            length = Math.Max(start, end);
            Distance = Floor = Start = Math.Max(0f, start);
            State = EscortState.Idle;
            foreach (float d in checkpointDistances) if (d > start + 0.0001f && d <= length + 0.0001f) checkpoints.Add(d);
            checkpoints.Sort();
            if (checkpoints.Count == 0 || checkpoints[checkpoints.Count - 1] < length - 0.0001f) checkpoints.Add(length);
        }

        /// <summary>Distance to the next checkpoint (or the end), or the end itself when all are passed.</summary>
        public float NextCheckpoint { get { return CheckpointsPassed < checkpoints.Count ? checkpoints[CheckpointsPassed] : length; } }

        /// <summary>One frame. contestedStops: Unity's rule (a player and enemies in range = stop). Returns the checkpoints passed this step.</summary>
        public int Step(float deltaTime, bool playerInRange, bool enemiesInRange, float speed, float reverseSpeed, bool contestedStops)
        {
            if (Arrived) return 0;
            if (playerInRange)
            {
                State = enemiesInRange ? EscortState.Contested : EscortState.Advancing;
                if (!(enemiesInRange && contestedStops)) Distance += Math.Max(0f, speed) * deltaTime;
            }
            else if (enemiesInRange)
            {
                State = EscortState.Retreating;
                Distance = Math.Max(Floor, Distance - Math.Max(0f, reverseSpeed) * deltaTime);
            }
            else State = EscortState.Idle;

            int passed = 0;
            while (CheckpointsPassed < checkpoints.Count && Distance >= checkpoints[CheckpointsPassed] - 0.0001f)
            {
                Floor = checkpoints[CheckpointsPassed];
                CheckpointsPassed++;
                passed++;
            }
            if (Distance >= length - 0.0001f) { Distance = length; State = EscortState.Arrived; }
            return passed;
        }

        /// <summary>After the payload is destroyed: back to the last checkpoint reached.</summary>
        public void ReturnToCheckpoint()
        {
            Distance = Floor;
            State = EscortState.Idle;
        }
    }
}
