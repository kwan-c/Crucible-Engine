/*************************************************************************
* \file:         MissionPresets.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          26 November 2026
* \version       1.9
* \brief         A mission level's preset (its chunks, placement, roads and
*                trees as plain data) and the chunk role names.
*
* \details
*    The numbers themselves live in the Inspector, on the mission's layout
*    script on the Sub Level Generator entity (MissionLayout: CaptureLayout,
*    EscortLayout, later LiberationLayout), which builds this preset.
*    For(mission) is the fallback when no layout script is found (e.g. the
*    Edit-mode Generate preview, or a scene without one): the layout
*    script's own defaults, so there is still one source of numbers.
*    Liberation: not yet (a later port step).
*
* \history
*    -v1.0       Capture
*    -v1.1       Capture as a journey: spawn first, the 1st area linked to it
*    -v1.2       back to Unity's layout; spacing range = minimum gap; route order
*    -v1.3       road settings per mission (Capture: roads along the route)
*    -v1.4       decor (tree scatter) settings per mission
*    -v1.5       Capture areas get a generated interior (InteriorSettings)
*    -v1.6       Capture objective numbers (CaptureSettings)
*    -v1.7       CaptureSettings removed: the Capture rules are Inspector
*                fields on MissionController
*    -v1.8       the numbers moved to the Inspector (MissionLayout /
*                CaptureLayout); For() falls back to the layout's defaults
*    -v1.9       Escort (EscortLayout) and the Checkpoint role; For() takes
*                the seed
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class MissionPreset
    {
        public string Name;
        public float MapSize;                               // metres, square
        public List<ChunkRequest> Chunks = new List<ChunkRequest>();   // in placement order
        public ChunkPlacementSettings Placement = new ChunkPlacementSettings();
        public RoadSettings Roads = new RoadSettings();     // Unity's road numbers (SubLevelGen_*.asset)
        public DecorSettings Decor = new DecorSettings();   // Unity's "Trees" decor numbers; quadrant themes are temporary
    }

    public static class MissionPresets
    {
        // Chunk roles (ChunkRequest.Role, ChunkDefinition.Role).
        public const string Spawn = "Spawn";
        public const string Extract = "Extract";
        public const string Capture = "Capture";
        public const string Escort = "Escort";           // mission name
        public const string Checkpoint = "Checkpoint";   // Escort's checkpoints

        /// <summary>The default preset for a mission name ("Capture", "Escort"): its layout script's defaults; null when there is none (yet).</summary>
        public static MissionPreset For(string mission, int seed)
        {
            if (mission == Capture) return new CaptureLayout().BuildPreset(seed);
            if (mission == Escort) return new EscortLayout().BuildPreset(seed);
            return null;
        }
    }
}
