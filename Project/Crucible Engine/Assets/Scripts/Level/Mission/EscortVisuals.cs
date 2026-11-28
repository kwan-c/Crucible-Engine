/*************************************************************************
* \file:         EscortVisuals.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.1
* \brief         Draws the escort payload's push radius (a ring coloured by
*                what it is doing) and a guide along the road to the next
*                checkpoint. Put it on the same entity as EscortObjective.
*
* \details
*    Unity's EscortPayload ring and guide line. The engine has no line
*    renderer, so both are made of the zone outline bar prefab (the one
*    MissionZoneVisuals uses):
*      ring   circleSegments bars round the payload at its push radius,
*             spawned under the payload (so they move with it), on the
*             ground under it; material by state: idle (yellow), advancing
*             (green), contested (enemies near, still moving), retreating
*             (red). Hidden once it has arrived.
*      guide  bars every guideSpacing metres along the route from the
*             payload to the next checkpoint (up to guideMarkers; the rest
*             hidden), under the Terrain; hidden while destroyed or after
*             arriving.
*      marker the checkpoint chunks' Beacon and Footprint (disc) show on
*             one checkpoint only: the 1st (where the payload waits)
*             before the first push, then the next one it is heading to;
*             none once it has arrived. The others are hidden.
*    Only reads EscortObjective, so real VFX can replace it.
*
* \history
*    -v1.0       new: Unity's ring colours and guide line, as bars
*    -v1.1       only the current checkpoint shows its beacon and disc
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class EscortVisuals : EntityScript
    {
        public Prefab outlineSegment;           // a thin bar 1 x 1 x 1 at scale 1 (scaled here); no collider
        public Material idleMaterial;           // nobody near (Unity: yellow)
        public Material advancingMaterial;      // a player near, no enemies: moving and healing (green)
        public Material contestedMaterial;      // a player and enemies near: still moving, no heal
        public Material retreatingMaterial;     // only enemies near: rolling back (red)
        public Material guideMaterial;          // the guide to the next checkpoint (Unity: yellow)
        public int circleSegments = 48;
        public float lineWidth = 0.4f;          // metres
        public float lineHeight = 0.15f;        // metres
        public float groundOffset = 0.3f;       // metres above the route for the ring and guide
        public float guideSpacing = 4f;         // metres between guide bars
        public float guideBarLength = 2f;       // metres
        public int guideMarkers = 100;          // at most this many (guideSpacing x guideMarkers metres ahead)

        private EscortObjective objective;
        private SubLevelGenerator generator;
        private int builtFor = -1;
        private Entity ringOwner;
        private readonly List<MeshRenderer> ring = new List<MeshRenderer>();
        private readonly List<Entity> guide = new List<Entity>();
        private readonly List<MeshRenderer> guideRenderers = new List<MeshRenderer>();
        private Material shown;
        private bool ringVisible = true;
        private int guideShown = -1;
        private float guideFrom = -1f;
        private bool warnedNoObjective, warnedNoSegment;

        // The checkpoint chunks' markers, by their order (1st, 2nd, ...).
        private sealed class Marker
        {
            public int Order;
            public Entity Beacon;
            public MeshRenderer Disc;
        }
        private readonly List<Marker> markers = new List<Marker>();
        private int markersFor = -1;
        private int markedOrder = -1;

        public override void OnUpdate(float deltaTime)
        {
            if (objective == null)
            {
                objective = Entity.GetComponent<EscortObjective>();
                generator = Entity.GetComponent<SubLevelGenerator>();
                if (objective == null)
                {
                    if (!warnedNoObjective) Log.Warning("EscortVisuals: put it on the entity with the EscortObjective");
                    warnedNoObjective = true;
                    return;
                }
            }
            if (objective.BuildCount != builtFor) Clear();
            UpdateMarkers();
            Entity payload = objective.Payload;
            if (payload == null || objective.Rules == null) return;
            if (ringOwner != payload) Build(payload);

            bool showRing = !objective.IsComplete;
            if (showRing != ringVisible)
            {
                foreach (MeshRenderer r in ring) r.Visible = showRing;
                ringVisible = showRing;
            }
            Material m = MaterialFor(objective.State);
            if (m != null && m != shown)
            {
                foreach (MeshRenderer r in ring) r.Material = m;
                shown = m;
            }
            UpdateGuide();
        }

        // A new map: the old payload took its ring with it; the guide bars stay and are reused.
        private void Clear()
        {
            builtFor = objective.BuildCount;
            ringOwner = null;
            ring.Clear();
            shown = null;
            ringVisible = true;
            guideFrom = -1f;
            HideGuideFrom(0);
        }

        private void Build(Entity payload)
        {
            ringOwner = payload;
            ring.Clear();
            shown = null;
            ringVisible = true;
            if (outlineSegment == null)
            {
                if (!warnedNoSegment) Log.Warning("EscortVisuals: no outline segment prefab - no ring or guide");
                warnedNoSegment = true;
                return;
            }
            int n = Math.Max(8, circleSegments);
            float r = Math.Max(0.5f, objective.radius);
            float length = 2f * (float)Math.PI * r / n * 1.05f;     // a little overlap so the ring has no gaps
            float y = groundOffset - objective.hoverHeight;         // the payload hovers; the ring lies on the ground under it
            for (int i = 0; i < n; i++)
            {
                float a = (i + 0.5f) * 2f * (float)Math.PI / n;
                Entity bar = outlineSegment.Instantiate(payload);
                if (bar == null) continue;
                bar.Transform.Position = new Vector3((float)Math.Cos(a) * r, y, (float)Math.Sin(a) * r);
                bar.Transform.Rotation = Quaternion.LookRotation(new Vector3(-(float)Math.Sin(a), 0f, (float)Math.Cos(a)), new Vector3(0f, 1f, 0f));
                bar.Transform.Scale = new Vector3(lineWidth, lineHeight, length);
                MeshRenderer renderer = bar.GetComponent<MeshRenderer>();
                if (renderer != null) ring.Add(renderer);
            }
        }

        // Beacon + disc on the current checkpoint only (see the header).
        private void UpdateMarkers()
        {
            if (generator == null || !generator.ChunksReady) return;
            if (generator.Generation != markersFor) FindMarkers();
            if (markers.Count == 0) return;
            int target;
            if (objective.IsComplete) target = 0;
            else if (!objective.HasStarted) target = 1;
            else target = Math.Min(objective.CheckpointsPassed + 2, markers.Count);
            if (target == markedOrder) return;
            markedOrder = target;
            foreach (Marker m in markers)
            {
                bool on = m.Order == target;
                if (m.Beacon != null && m.Beacon.IsAlive) m.Beacon.SetActive(on);
                if (m.Disc != null) m.Disc.Visible = on;
            }
        }

        private void FindMarkers()
        {
            markersFor = generator.Generation;
            markers.Clear();
            markedOrder = -1;
            foreach (Entity chunk in generator.SpawnedChunks)
            {
                if (chunk == null || !chunk.IsAlive) continue;
                ChunkDefinition definition = chunk.GetComponent<ChunkDefinition>();
                if (definition == null || definition.Role != MissionPresets.Checkpoint) continue;
                Entity disc = chunk.FindChild("Footprint");
                markers.Add(new Marker
                {
                    Order = definition.Order,
                    Beacon = chunk.FindChild("Beacon"),
                    Disc = disc != null ? disc.GetComponent<MeshRenderer>() : null,
                });
            }
        }

        private Material MaterialFor(EscortState state)
        {
            switch (state)
            {
                case EscortState.Advancing: return advancingMaterial;
                case EscortState.Contested: return contestedMaterial;
                case EscortState.Retreating: return retreatingMaterial;
                default: return idleMaterial;
            }
        }

        // Bars along the route from the payload to the next checkpoint; only redrawn when the payload has moved.
        private void UpdateGuide()
        {
            EscortRules rules = objective.Rules;
            if (outlineSegment == null) return;
            if (objective.IsComplete || objective.IsDestroyed) { HideGuideFrom(0); guideFrom = -1f; return; }
            if (Math.Abs(rules.Distance - guideFrom) < 0.05f && guideShown >= 0) return;
            guideFrom = rules.Distance;
            EscortRoute route = objective.Route;
            Entity parent = generator != null && generator.terrain != null ? generator.terrain : Entity;
            float spacing = Math.Max(0.5f, guideSpacing);
            float end = rules.NextCheckpoint;
            int count = 0;
            for (float d = rules.Distance + spacing; d < end && count < Math.Max(0, guideMarkers); d += spacing, count++)
            {
                while (guide.Count <= count)
                {
                    Entity made = outlineSegment.Instantiate(parent);
                    if (made == null) return;
                    guide.Add(made);
                    MeshRenderer mr = made.GetComponent<MeshRenderer>();
                    guideRenderers.Add(mr);
                    if (mr != null && guideMaterial != null) mr.Material = guideMaterial;
                }
                float x, y, z, dx, dz;
                route.Sample(d, out x, out y, out z, out dx, out dz);
                Entity bar = guide[count];
                if (!bar.IsAlive) continue;
                bar.Transform.Position = new Vector3(x, y + groundOffset, z);
                bar.Transform.Rotation = Quaternion.LookRotation(new Vector3(dx, 0f, dz), new Vector3(0f, 1f, 0f));
                bar.Transform.Scale = new Vector3(lineWidth, lineHeight, Math.Max(0.1f, guideBarLength));
                if (guideRenderers[count] != null) guideRenderers[count].Visible = true;
            }
            HideGuideFrom(count);
            guideShown = count;
        }

        private void HideGuideFrom(int first)
        {
            for (int i = first; i < guideRenderers.Count; i++)
                if (guideRenderers[i] != null) guideRenderers[i].Visible = false;
        }
    }
}
