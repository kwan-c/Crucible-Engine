/*************************************************************************
* \file:         MissionPresets.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          26 November 2026
* \version       1.6
* \brief         Each mission's level numbers, from the GDD's side quest
*                specs (M3): map size, which chunks it places, their sizes
*                and spacing. Plain data, so it can be tested alone.
*
* \details
*    Replaces Unity's SubLevelGenSettings assets (one per mission): the
*    engine has no settings assets and its Inspector can't show lists, so
*    the values live here and a level scene's SubLevelGenerator only names
*    its mission. Values marked "GDD" are the spec's; the rest are Unity
*    M2's defaults.
*
*    Capture (GDD): map 500 m; 3 capture areas (circles, 30 m radius),
*    cleared in order; spacing range 50-70 m = the least gap between any
*    two chunks (Unity's minSpacing, picked per level); edge margin 20 m.
*    Laid out as in Unity: spawn in the middle third, areas spread out,
*    extract as far from the spawn as it can; the areas are numbered
*    1st / 2nd / 3rd as a route from the spawn, and roads (RoadBuilder)
*    follow that route. Spawn and extract are 24 m boxes (Unity's start chunk;
*    the GDD gives no size).
*
*    Escort and Liberation: not yet (later port steps); asking for them
*    gives no preset, and the generator warns.
*
* \history
*    -v1.0       Capture
*    -v1.1       Capture as a journey: spawn first, the 1st area linked to it
*    -v1.2       back to Unity's layout; spacing range = minimum gap; route order
*    -v1.3       road settings per mission (Capture: roads along the route)
*    -v1.4       decor (tree scatter) settings per mission
*    -v1.5       Capture areas get a generated interior (InteriorSettings)
*    -v1.6       Capture objective numbers (CaptureSettings)
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    // Capture's objective numbers (GDD). Arrays are the GDD's a/b/c/d: for 1, 2, 3, 4 players.
    public sealed class CaptureSettings
    {
        public float[] holdSeconds = { 40f, 50f, 60f, 70f };   // seconds to hold an area
        public float[] graceSeconds = { 5f, 4f, 3f, 3f };      // seconds to get back in after leaving
        public float failedSeconds = 2f;                       // the Failed state shows this long, then resets (Unity)
        public bool inOrder = true;                            // 1st before 2nd before 3rd (GDD); off = any order
    }

    public sealed class MissionPreset
    {
        public string Name;
        public float MapSize;                               // metres, square
        public List<ChunkRequest> Chunks = new List<ChunkRequest>();   // in placement order
        public ChunkPlacementSettings Placement = new ChunkPlacementSettings();
        public RoadSettings Roads = new RoadSettings();     // Unity's road numbers (SubLevelGen_*.asset)
        public DecorSettings Decor = new DecorSettings();   // Unity's "Trees" decor numbers; quadrant themes are temporary
        public CaptureSettings Capture;                     // Capture's objective rules (MissionController); null for other missions
    }

    public static class MissionPresets
    {
        // Chunk roles (ChunkRequest.Role, ChunkDefinition.Role).
        public const string Spawn = "Spawn";
        public const string Extract = "Extract";
        public const string Capture = "Capture";

        // Unity's start chunk: 6 x 6 golems of 4 m.
        private const float kSpawnSize = 24f;
        private const float kExtractSize = 24f;

        /// <summary>The preset for a mission name ("Capture", ...), or null when there is none (yet).</summary>
        public static MissionPreset For(string mission)
        {
            if (mission == "Capture") return CapturePreset();
            return null;
        }

        private static MissionPreset CapturePreset()
        {
            MissionPreset p = new MissionPreset();
            p.Name = "Capture";
            p.MapSize = 500f;                       // GDD
            p.Placement.minSpacing = 50f;           // GDD: spacing range 70 - 50 (Unity's minSpacing, as a range)
            p.Placement.minSpacingHigh = 70f;
            p.Placement.edgeMargin = 20f;           // GDD

            // Unity's layout: spawn first (middle third), the areas spread out, the extract far from the
            // spawn. The areas' 1st / 2nd / 3rd are set after placing, as a route from the spawn.
            ChunkFootprint area = new ChunkFootprint(ChunkShape.Circle, 60f, 60f);   // GDD: radius 30
            p.Chunks.Add(new ChunkRequest { Role = Spawn, Footprint = new ChunkFootprint(ChunkShape.Box, kSpawnSize, kSpawnSize), Start = true });
            for (int i = 0; i < 3; i++)
                p.Chunks.Add(new ChunkRequest { Role = Capture, Order = i + 1, Footprint = area, Interior = new InteriorSettings() });   // Unity's KOTH interior
            p.Chunks.Add(new ChunkRequest { Role = Extract, Footprint = new ChunkFootprint(ChunkShape.Box, kExtractSize, kExtractSize), End = true });

            // Roads follow the objective route (spawn -> 1st -> 2nd -> 3rd -> extract). Unity's KOTH
            // used a network instead (chain off: shortest links + 1 loop); the other numbers are Unity's.
            p.Roads.chain = true;
            p.Capture = new CaptureSettings();
            return p;
        }
    }
}
