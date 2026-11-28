/*************************************************************************
* \file:         ObjectiveFireworks.cs
* \brief         Plays the tutorial's finish sparks when a Capture objective
*                is completed: each captured area, and the extract when the
*                mission completes.
*
* \details
*    On the Sub Level Generator entity, next to MissionController and
*    CaptureWaves. It only listens to MissionController (AreaStateChanged ->
*    Complete, MissionCompleted), so the mission logic is unchanged.
*
*    `fireworks` is Prefabs/VFX/Objective Fireworks: the three "Sparks"
*    emitters from the end of the TutorialMap (LastPart), scaled up for an
*    open map (150 sparks a burst instead of 60, about 3x the size, twice
*    the speed, longer life and light), in a row 4 m apart and 5 m up. They
*    loop a burst every 1.5 s, so playSeconds 4.5 = three bursts. Tune the
*    look in the prefab. `copies` of it are placed
*    round the area, ringFraction of the way from its centre to its edge,
*    each row lying along the ring. After playSeconds the emitters stop
*    and the copy is destroyed once the last sparks have faded.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class ObjectiveFireworks : EntityScript
    {
        public Prefab fireworks;               // Prefabs/VFX/Objective Fireworks
        public int copies = 4;                 // placed round the completed area
        public float ringFraction = .6f;       // 0 = all at the centre, 1 = on the edge
        public float playSeconds = 4.5f;       // the emitters burst every 1.5 s
        public bool onExtract = true;          // also at the extract when the mission completes

        const float FadeSeconds = 1.5f;        // longest spark life is 0.9 s
        static readonly string[] emitterNames = { "Sparks 1", "Sparks 2", "Sparks 3" };

        sealed class Show { public Entity Root; public float StopAt; }

        MissionController controller;
        int framesWaiting;
        bool warnedNoPrefab;
        readonly List<Show> shows = new List<Show>();

        public override void OnUpdate(float deltaTime)
        {
            if (controller == null)
            {
                controller = Entity.GetComponent<MissionController>();     // scripts attach a frame late
                if (controller == null)
                {
                    if (++framesWaiting == 120) Log.Warning("ObjectiveFireworks: no MissionController on " + Entity.Name + " - no fireworks");
                    return;
                }
                controller.AreaStateChanged += OnAreaStateChanged;
                controller.MissionCompleted += OnMissionCompleted;
            }

            for (int i = shows.Count - 1; i >= 0; i--)
            {
                Show show = shows[i];
                if (show.Root == null || !show.Root.IsAlive) { shows.RemoveAt(i); continue; }
                if (Time.ElapsedTime < show.StopAt) continue;
                foreach (string name in emitterNames)
                {
                    Entity child = show.Root.FindChild(name);
                    ParticleEmitter fx = child != null ? child.GetComponent<ParticleEmitter>() : null;
                    if (fx != null) fx.Stop();                     // live sparks finish
                }
                Destroy(show.Root, FadeSeconds);
                shows.RemoveAt(i);
            }
        }

        public override void OnDestroy()
        {
            if (controller != null)
            {
                controller.AreaStateChanged -= OnAreaStateChanged;
                controller.MissionCompleted -= OnMissionCompleted;
            }
            foreach (Show show in shows) Destroy(show.Root);
            shows.Clear();
        }

        private void OnAreaStateChanged(int order, HoldZoneState state)
        {
            if (state != HoldZoneState.Complete) return;
            foreach (MissionController.Zone z in controller.Zones)
                if (!z.IsExtract && z.Order == order) { Celebrate(z); return; }
        }

        private void OnMissionCompleted()
        {
            if (!onExtract) return;
            foreach (MissionController.Zone z in controller.Zones)
                if (z.IsExtract) { Celebrate(z); return; }
        }

        private void Celebrate(MissionController.Zone zone)
        {
            if (fireworks == null)
            {
                if (!warnedNoPrefab) Log.Warning("ObjectiveFireworks: set fireworks to Prefabs/VFX/Objective Fireworks");
                warnedNoPrefab = true;
                return;
            }
            if (zone.Chunk == null || !zone.Chunk.IsAlive) return;

            Vector3 centre = zone.Chunk.Transform.WorldPosition;
            int count = Math.Max(1, copies);
            float ring = count > 1 ? EdgeRadius(zone) * Math.Max(0f, Math.Min(1f, ringFraction)) : 0f;
            for (int i = 0; i < count; i++)
            {
                float degrees = 360f * i / count;
                double radians = degrees * Math.PI / 180.0;
                Vector3 position = centre + new Vector3((float)Math.Sin(radians), 0f, (float)Math.Cos(radians)) * ring;
                // The prefab's row runs along its local Z: turn it a quarter so it lies along the ring.
                Entity copy = fireworks.Instantiate(position, Quaternion.Euler(0f, degrees + 90f, 0f));
                if (copy != null) shows.Add(new Show { Root = copy, StopAt = Time.ElapsedTime + playSeconds });
            }
            Log.Info("ObjectiveFireworks: " + (zone.IsExtract ? "extract" : "area " + zone.Order) + " complete - " + count + " firework(s)");
        }

        // Centre to edge: a circle's radius, a box's half diagonal (as CaptureWaves).
        private static float EdgeRadius(MissionController.Zone zone)
        {
            if (zone.Definition == null) return 0f;
            ChunkFootprint f = zone.Definition.Footprint;
            return f.Circle ? f.Radius : 0.5f * (float)Math.Sqrt(f.Width * f.Width + f.Depth * f.Depth);
        }
    }
}
