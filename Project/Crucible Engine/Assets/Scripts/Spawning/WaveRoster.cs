/*************************************************************************
* \file:         WaveRoster.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         The enemies one wave has spawned, with what each cost, and
*                the leash rule, as plain data (no engine calls).
*
* \details
*    Ported from the Unity prototype (AI/Spawning/WaveRoster.cs). Its alive
*    count is the wave's own population (WaveSettings.MaxConcurrentEnemies),
*    TotalCost is checked against an owner's budget, and Cull picks out
*    members to remove (e.g. strayed past the leash) with their costs so
*    they can be refunded. Unity returned (member, cost) tuples; the
*    engine's Mono has no ValueTuple, so Cull returns WaveMember structs.
*
* \history
*    -v1.0       ported from Unity
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public struct WaveMember<T> where T : class
    {
        public T Member;
        public int Cost;
        public WaveMember(T member, int cost) { Member = member; Cost = cost; }
    }

    public sealed class WaveRoster<T> where T : class
    {
        readonly List<T> members = new List<T>();
        readonly List<int> costs = new List<int>();

        public int Count { get { return members.Count; } }
        public int TotalCost { get { int t = 0; foreach (int c in costs) t += c; return t; } }
        public T this[int index] { get { return members[index]; } }

        public void Add(T member, int cost) { members.Add(member); costs.Add(Math.Max(0, cost)); }

        // Drops members that are no longer alive (killed, destroyed or switched off).
        public void Prune(Func<T, bool> alive)
        {
            for (int i = members.Count - 1; i >= 0; i--)
                if (members[i] == null || !alive(members[i])) { members.RemoveAt(i); costs.RemoveAt(i); }
        }

        // Removes every member matching the rule and returns them with their costs (to remove and refund).
        public List<WaveMember<T>> Cull(Func<T, bool> rule)
        {
            var culled = new List<WaveMember<T>>();
            for (int i = members.Count - 1; i >= 0; i--)
            {
                if (members[i] == null || !rule(members[i])) continue;
                culled.Add(new WaveMember<T>(members[i], costs[i]));
                members.RemoveAt(i); costs.RemoveAt(i);
            }
            return culled;
        }

        public void Clear() { members.Clear(); costs.Clear(); }
    }

    // A member further than the leash (flat distance) from its wave's anchor gets removed.
    // A leash of 0 or less is off.
    public static class WaveLeash
    {
        public static bool Beyond(float dx, float dz, float leash) { return leash > 0f && dx * dx + dz * dz > leash * leash; }
    }

    // A wave owner that says where its wave should stay near (e.g. the escort payload). Waves with a
    // leash distance remove members that stray further than that from here.
    public interface IWaveAnchor
    {
        Vector3 WaveAnchor { get; }
    }
}
