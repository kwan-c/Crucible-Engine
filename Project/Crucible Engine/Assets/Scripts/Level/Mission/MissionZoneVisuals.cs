/*************************************************************************
* \file:         MissionZoneVisuals.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.1
* \brief         Placeholder look for the mission's zones: an outline along
*                each zone's edge coloured by its state, and the beacons as
*                a stand-in for the objective marker. Put it on the same
*                entity as the MissionController.
*
* \details
*    Port of Unity's HoldZoneVisual (Unity drew a LineRenderer; the engine
*    has none and scripts can't create entities, so the outline is a ring
*    of `outlineSegment` prefabs - thin glowing bars - spawned under each
*    zone's chunk): a circle gets circleSegments bars just inside its edge,
*    a box its four sides. The edge is level with the chunk's base (the
*    interior fades back to base there), so the ring is flat.
*
*    Unity's colours (a material each, set below):
*      Waiting   waitingMaterial (yellow)
*      Holding   holdingMaterial (blue)
*      Warning   blinks waiting / off, flashRate times a second
*      Failed    blinks failed (red) / off
*      Complete  completeMaterial (green)
*    A zone's outline and beacon only show while it is open (Zone.IsOpen:
*    the current area, the extract once open); a captured area's outline
*    stays, green. The spawn chunk's
*    beacon is hidden (hideSpawnBeacon).
*
*    Only reads MissionController, so real VFX can replace it.
*
* \history
*    -v1.0       Unity's zone outline colours; beacons as the marker
*    -v1.1       blink with MeshRenderer.Visible (the Warning blink was missing)
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class MissionZoneVisuals : EntityScript
    {
        public Prefab outlineSegment;       // a thin bar 1 x 1 x 1 at scale 1 (scaled here); no collider
        public Material waitingMaterial;    // yellow
        public Material holdingMaterial;    // blue
        public Material failedMaterial;     // red
        public Material completeMaterial;   // green
        public float flashRate = 4f;        // blinks per second while Warning / Failed
        public float lineWidth = 0.4f;      // metres
        public float lineHeight = 0.15f;    // metres
        public float heightOffset = 0.15f;  // metres above the chunk's base
        public float inset = 0.5f;          // metres in from the footprint's edge
        public int circleSegments = 64;
        public bool hideSpawnBeacon = true;

        // One zone's pieces: its outline bars and beacon, and what they show now.
        private sealed class Look
        {
            public MissionController.Zone Zone;
            public readonly List<Entity> Bars = new List<Entity>();
            public readonly List<MeshRenderer> Renderers = new List<MeshRenderer>();   // the bars' renderers, kept from spawning
            public Entity Beacon;
            public Material Shown;
            public bool Visible = true;
        }

        private MissionController controller;
        private SubLevelGenerator generator;
        private int builtFor = -1;
        private readonly List<Look> looks = new List<Look>();
        private float clock;
        private bool warnedNoController;

        public override void OnUpdate(float deltaTime)
        {
            if (controller == null)
            {
                controller = Entity.GetComponent<MissionController>();
                generator = Entity.GetComponent<SubLevelGenerator>();
                if (controller == null)
                {
                    if (!warnedNoController) Log.Warning("MissionZoneVisuals: put it on the entity with the MissionController");
                    warnedNoController = true;
                    return;
                }
            }
            if (controller.BuildCount != builtFor) Build();
            clock += deltaTime;
            bool blinkOn = (clock * Math.Max(0.1f, flashRate)) % 1f < 0.5f;
            foreach (Look look in looks) Show(look, blinkOn);
        }

        // A new map: outline every zone and find its beacon (the old bars went with the old chunks).
        private void Build()
        {
            builtFor = controller.BuildCount;
            looks.Clear();
            foreach (MissionController.Zone zone in controller.Zones)
            {
                if (zone.Chunk == null || !zone.Chunk.IsAlive) continue;
                Look look = new Look { Zone = zone, Beacon = zone.Chunk.FindChild("Beacon") };
                Outline(look);
                looks.Add(look);
            }
            if (hideSpawnBeacon && generator != null)
                foreach (Entity chunk in generator.SpawnedChunks)
                {
                    if (chunk == null || !chunk.IsAlive) continue;
                    ChunkDefinition definition = chunk.GetComponent<ChunkDefinition>();
                    Entity beacon = chunk.FindChild("Beacon");
                    if (definition != null && definition.Role == MissionPresets.Spawn && beacon != null) beacon.SetActive(false);
                }
            if (outlineSegment == null && looks.Count > 0) Log.Warning("MissionZoneVisuals: no outline segment prefab - zones get no outline");
        }

        // The zone's edge as bars under its chunk: a ring for a circle, four sides for a box.
        private void Outline(Look look)
        {
            if (outlineSegment == null) return;
            ChunkFootprint size = look.Zone.Definition.Footprint;
            if (size.Circle)
            {
                int n = Math.Max(8, circleSegments);
                float r = Math.Max(0.5f, size.Radius - inset);
                float length = 2f * (float)Math.PI * r / n * 1.05f;          // a little overlap so the ring has no gaps
                for (int i = 0; i < n; i++)
                {
                    float a = (i + 0.5f) * 2f * (float)Math.PI / n;
                    Vector3 at = new Vector3((float)Math.Cos(a) * r, heightOffset, (float)Math.Sin(a) * r);
                    Vector3 along = new Vector3(-(float)Math.Sin(a), 0f, (float)Math.Cos(a));
                    Bar(look, at, along, length);
                }
                return;
            }
            float hx = Math.Max(0.5f, size.Width * 0.5f - inset), hz = Math.Max(0.5f, size.Depth * 0.5f - inset);
            Bar(look, new Vector3(0f, heightOffset, -hz), new Vector3(1f, 0f, 0f), hx * 2f + lineWidth);
            Bar(look, new Vector3(0f, heightOffset, hz), new Vector3(1f, 0f, 0f), hx * 2f + lineWidth);
            Bar(look, new Vector3(-hx, heightOffset, 0f), new Vector3(0f, 0f, 1f), hz * 2f + lineWidth);
            Bar(look, new Vector3(hx, heightOffset, 0f), new Vector3(0f, 0f, 1f), hz * 2f + lineWidth);
        }

        // One bar centred at `at` (chunk space), lying along `along`. The bar's long axis is its Z, so
        // LookRotation lines it up; the bar is symmetric, so which way Z faces doesn't matter.
        private void Bar(Look look, Vector3 at, Vector3 along, float length)
        {
            Entity bar = outlineSegment.Instantiate(look.Zone.Chunk);
            if (bar == null) return;
            bar.Transform.Position = at;
            bar.Transform.Rotation = Quaternion.LookRotation(along, new Vector3(0f, 1f, 0f));
            bar.Transform.Scale = new Vector3(lineWidth, lineHeight, length);
            look.Bars.Add(bar);
            MeshRenderer renderer = bar.GetComponent<MeshRenderer>();
            if (renderer != null) look.Renderers.Add(renderer);
        }

        // Shows or hides the zone's pieces and gives the bars the state's material.
        private void Show(Look look, bool blinkOn)
        {
            MissionController.Zone zone = look.Zone;
            bool open = zone.IsOpen;
            if (look.Beacon != null && look.Beacon.IsAlive) look.Beacon.SetActive(open);

            Material material = null;
            bool visible = open;
            switch (zone.State)
            {
                case HoldZoneState.Holding: material = holdingMaterial; break;
                case HoldZoneState.Warning: material = waitingMaterial; visible = open && blinkOn; break;
                case HoldZoneState.Failed: material = failedMaterial; visible = open && blinkOn; break;
                case HoldZoneState.Complete: material = completeMaterial; visible = true; break;   // captured areas stay green
                default: material = waitingMaterial; break;
            }
            // Blinking toggles the renderers' Visible, not SetActive: the bars stay active, so their material
            // can change whatever the blink is showing (v1.0 swapped materials on switched-off bars, and the
            // Warning blink never appeared).
            if (material != null && material != look.Shown)
            {
                foreach (MeshRenderer renderer in look.Renderers) renderer.Material = material;
                look.Shown = material;
            }
            if (visible != look.Visible)
            {
                foreach (MeshRenderer renderer in look.Renderers) renderer.Visible = visible;
                look.Visible = visible;
            }
        }
    }
}
