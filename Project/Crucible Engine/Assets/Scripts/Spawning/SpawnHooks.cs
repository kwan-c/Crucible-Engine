/*************************************************************************
* \file:         SpawnHooks.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         What spawning asks of the level, without depending on the
*                level code: areas that pause roaming, and the ground height.
*
* \details
*    The level registers here; spawning only reads. So spawning works in
*    any scene (a test scene has neither), and the level can change
*    without touching spawning.
*      RoamingBlockers  Unity's RoamingBlockers: areas where roaming pauses
*                       while a player is inside (Capture's areas; never
*                       the spawn or extract). MissionController registers.
*      SpawnGround      the ground height at (x, z) for spawn spots: the
*                       generated terrain's own heights (SubLevelGenerator
*                       registers), which see through trees and pieces.
*                       NaN = nothing registered (the director then casts
*                       a ray down instead).
*    Each registration has an owner entity: once it's gone (Stop, a new
*    scene) the entry is dropped, so nothing stale carries over between
*    Play sessions (static data outlives Play).
*
* \history
*    -v1.0       new (Unity's RoamingBlockers + the ground height hook)
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public static class RoamingBlockers
    {
        private struct Area
        {
            public Entity Owner;
            public Func<Vector3, bool> Contains;
        }

        private static readonly List<Area> areas = new List<Area>();

        // Adds (or replaces) owner's area.
        public static void Register(Entity owner, Func<Vector3, bool> contains)
        {
            if (owner == null || contains == null) return;
            Unregister(owner);
            areas.Add(new Area { Owner = owner, Contains = contains });
        }

        public static void Unregister(Entity owner)
        {
            if (owner == null) return;
            for (int i = areas.Count - 1; i >= 0; i--)
                if (areas[i].Owner == null || !areas[i].Owner.IsAlive || areas[i].Owner == owner) areas.RemoveAt(i);
        }

        // Is this position inside an area that pauses roaming?
        public static bool Contains(Vector3 position)
        {
            for (int i = areas.Count - 1; i >= 0; i--)
            {
                if (areas[i].Owner == null || !areas[i].Owner.IsAlive) { areas.RemoveAt(i); continue; }
                if (areas[i].Contains(position)) return true;
            }
            return false;
        }

        public static int Count { get { return areas.Count; } }
    }

    public static class SpawnGround
    {
        private static Entity owner;
        private static Func<float, float, float> height;

        public static void Register(Entity heightOwner, Func<float, float, float> heightAt)
        {
            owner = heightOwner;
            height = heightAt;
        }

        // The registered ground height at (x, z); NaN when nothing (alive) is registered.
        public static float HeightAt(float x, float z)
        {
            if (height == null || owner == null || !owner.IsAlive) { height = null; owner = null; return float.NaN; }
            return height(x, z);
        }
    }
}
