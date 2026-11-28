/*************************************************************************
* \file:         EscortRoute.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         The escort's path as plain data: from the spawn chunk's
*                centre, along each road of the route, through each
*                checkpoint's centre, to the extract's centre.
*
* \details
*    Ported from the Unity prototype (Level/Generation/EscortRoute.cs).
*    SubLevelGenerator builds it after the roads on a level with
*    checkpoints (MissionPresets.Checkpoint) and exposes it as Route. It is
*    in the map's space (the Terrain's, like the chunks and roads).
*    Distances are measured flat (x, z) along the route, as in Unity.
*    The roads must be a route along the chunks (RoadSettings.chain: each
*    road From the earlier chunk To the next); unlike Unity, the route
*    starts at the Start chunk wherever it is in the placements, not
*    placements[0].
*      CheckpointDistances  each chunk after the start, in order (the
*                           checkpoints, then the extract)
*      Chunks               the placement index of each of those
*      Complete             false when a road found no route: the route
*                           stops at the last chunk it reached
*
* \history
*    -v1.0       ported from Unity; Chunks added; starts at the Start chunk
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class EscortRoute
    {
        public readonly List<float> X = new List<float>(), Y = new List<float>(), Z = new List<float>();
        public readonly List<float> Distance = new List<float>();             // along the route to each point
        public readonly List<float> CheckpointDistances = new List<float>();  // each chunk after the start, in order; the last is the end
        public readonly List<int> Chunks = new List<int>();                   // placement index of each of those, in order
        public float Length { get { return Distance.Count > 0 ? Distance[Distance.Count - 1] : 0f; } }
        public bool Complete { get; private set; }                          // every chunk joined by its road

        /// <summary>
        /// The route through the placed chunks along the roads (RoadBuilder.Build with chain on).
        /// requests: the placements' requests, to find the Start chunk (else the first placement).
        /// </summary>
        public static EscortRoute Build(IList<ChunkPlacement> placed, IList<ChunkRequest> requests, IList<Road> roads)
        {
            EscortRoute route = new EscortRoute();
            if (placed.Count == 0) return route;
            int at = 0;
            for (int i = 0; i < placed.Count; i++)
                if (requests[placed[i].Request].Start) { at = i; break; }
            route.Add(placed[at].X, placed[at].Height, placed[at].Z);
            int joined = 0;
            foreach (Road road in roads)
            {
                if (road.From != at) break;
                for (int i = 0; i < road.X.Count; i++) route.Add(road.X[i], road.Height[i], road.Z[i]);
                ChunkPlacement next = placed[road.To];
                route.Add(next.X, next.Height, next.Z);
                route.CheckpointDistances.Add(route.Length);
                route.Chunks.Add(road.To);
                at = road.To;
                joined++;
            }
            route.Complete = joined == placed.Count - 1;
            return route;
        }

        private void Add(float x, float y, float z)
        {
            float d = 0f;
            if (X.Count > 0)
            {
                int last = X.Count - 1;
                float dx = x - X[last], dz = z - Z[last];
                d = Distance[last] + (float)Math.Sqrt(dx * dx + dz * dz);
                if (d - Distance[last] < 0.001f) return;   // skip repeated points
            }
            X.Add(x); Y.Add(y); Z.Add(z); Distance.Add(d);
        }

        /// <summary>The point at a distance along the route, and the flat direction of travel there.</summary>
        public void Sample(float distance, out float x, out float y, out float z, out float dirX, out float dirZ)
        {
            x = y = z = 0f; dirX = 0f; dirZ = 1f;
            if (X.Count == 0) return;
            if (X.Count == 1) { x = X[0]; y = Y[0]; z = Z[0]; return; }
            distance = Math.Max(0f, Math.Min(Length, distance));
            int i = 1;
            while (i < X.Count - 1 && Distance[i] < distance) i++;
            float span = Math.Max(0.0001f, Distance[i] - Distance[i - 1]);
            float t = Math.Max(0f, Math.Min(1f, (distance - Distance[i - 1]) / span));
            x = X[i - 1] + (X[i] - X[i - 1]) * t;
            y = Y[i - 1] + (Y[i] - Y[i - 1]) * t;
            z = Z[i - 1] + (Z[i] - Z[i - 1]) * t;
            float dx = X[i] - X[i - 1], dz = Z[i] - Z[i - 1], len = (float)Math.Sqrt(dx * dx + dz * dz);
            if (len > 0.0001f) { dirX = dx / len; dirZ = dz / len; }
        }

        /// <summary>The furthest checkpoint reached at or before this distance (-1 = none yet), i.e. the respawn point.</summary>
        public int LastCheckpoint(float distance)
        {
            int last = -1;
            for (int i = 0; i < CheckpointDistances.Count; i++) if (CheckpointDistances[i] <= distance + 0.001f) last = i;
            return last;
        }
    }
}
