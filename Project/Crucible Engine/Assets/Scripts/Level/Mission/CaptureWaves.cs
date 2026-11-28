/*************************************************************************
* \file:         CaptureWaves.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Runs Capture's enemy wave on each area while it's being held:
*                the spawning half of Unity's KOTH_Zone.
*
* \details
*    On the Sub Level Generator entity, next to MissionController (which
*    keeps the hold rules and knows nothing of spawning). It listens to
*    MissionController.AreaStateChanged:
*      Waiting -> Holding    the Spawn Director runs `wave` (a child of
*                            the Spawn Director prefab, "Wave Capture") on
*                            a ring centred on the area, spawnDistance
*                            outside its edge (circle: radius + 10 m);
*                            the area is the wave's owner
*      Warning               nothing: the wave carries on while you're out
*      Failed / Complete     the area's wave stops (enemies already out stay)
*    A new map (MissionController rebuilt) stops any wave still running.
*    Retries quietly until the controller and the Spawn Director have
*    attached (scripts attach a frame late), warns once if they never do.
*
* \history
*    -v1.0       ported from Unity's KOTH_Zone (wave part)
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class CaptureWaves : EntityScript
    {
        public string wave = "Wave Capture";   // Spawn Director child holding Capture's wave
        public float spawnDistance = 10f;      // metres outside an area's edge enemies appear

        private MissionController controller;
        private int seenBuild = -1;
        private int framesWaiting;
        private bool warnedNoDirector;
        private readonly Dictionary<int, HoldZoneState> lastState = new Dictionary<int, HoldZoneState>();
        private readonly List<MissionController.Zone> running = new List<MissionController.Zone>();

        public override void OnUpdate(float deltaTime)
        {
            if (controller == null)
            {
                controller = Entity.GetComponent<MissionController>();
                if (controller == null)
                {
                    if (++framesWaiting == 120) Log.Warning("CaptureWaves: no MissionController on " + Entity.Name + " - no Capture waves");
                    return;
                }
                controller.AreaStateChanged += OnAreaStateChanged;
            }
            if (controller.BuildCount != seenBuild)      // a new map: forget the old areas
            {
                seenBuild = controller.BuildCount;
                StopAll();
                lastState.Clear();
            }
        }

        public override void OnDestroy()
        {
            if (controller != null) controller.AreaStateChanged -= OnAreaStateChanged;
            StopAll();
        }

        private void OnAreaStateChanged(int order, HoldZoneState state)
        {
            HoldZoneState previous;
            if (!lastState.TryGetValue(order, out previous)) previous = HoldZoneState.Waiting;
            lastState[order] = state;

            MissionController.Zone area = FindArea(order);
            if (area == null) return;
            if (previous == HoldZoneState.Waiting && state == HoldZoneState.Holding) StartWave(area);
            if (state == HoldZoneState.Failed || state == HoldZoneState.Complete) StopWave(area);
        }

        private MissionController.Zone FindArea(int order)
        {
            foreach (MissionController.Zone z in controller.Zones)
                if (!z.IsExtract && z.Order == order) return z;
            return null;
        }

        private void StartWave(MissionController.Zone area)
        {
            SpawnDirector director = SpawnDirector.Instance;
            if (director == null)
            {
                if (!warnedNoDirector) Log.Warning("CaptureWaves: no Spawn Director in the scene - no Capture waves (drop in Prefabs/Spawning/Spawn Director)");
                warnedNoDirector = true;
                return;
            }
            Wave settings = director.FindWave(wave);
            if (settings == null) { Log.Warning("CaptureWaves: the Spawn Director has no child '" + wave + "' with a Wave"); return; }

            Entity chunk = area.Chunk;
            float ring = EdgeRadius(area) + spawnDistance;
            RingSource source = director.MakeRing(() => chunk != null && chunk.IsAlive ? chunk.Transform.WorldPosition : (Vector3?)null, ring);
            if (!director.RunWave(settings, new ISpawnSource[] { source }, area))
            {
                Log.Warning("CaptureWaves: area " + area.Order + " - another wave is already running");
                return;
            }
            if (!running.Contains(area)) running.Add(area);
            Log.Info("CaptureWaves: area " + area.Order + " - enemies from " + ring.ToString("0") + " m round it");
        }

        private void StopWave(MissionController.Zone area)
        {
            SpawnDirector director = SpawnDirector.Instance;
            if (director != null) director.StopWave(area);
            running.Remove(area);
        }

        private void StopAll()
        {
            for (int i = running.Count - 1; i >= 0; i--) StopWave(running[i]);
        }

        // Centre to edge: a circle's radius, a box's half diagonal (as Unity's KOTH_Zone).
        private static float EdgeRadius(MissionController.Zone area)
        {
            if (area.Definition == null) return 0f;
            ChunkFootprint f = area.Definition.Footprint;
            return f.Circle ? f.Radius : 0.5f * (float)Math.Sqrt(f.Width * f.Width + f.Depth * f.Depth);
        }
    }
}
