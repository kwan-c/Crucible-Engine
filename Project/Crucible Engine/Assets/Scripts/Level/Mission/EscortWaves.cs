/*************************************************************************
* \file:         EscortWaves.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Escort's wave: runs the Spawn Director's escort wave round
*                the payload from its first push until it arrives. Put it
*                on the same entity as EscortObjective.
*
* \details
*    The wave half of Unity's EscortPayload (as CaptureWaves is for
*    Capture's areas). Listens to EscortObjective:
*      Started            -> SpawnDirector.RunWave(child `wave`, a ring of
*                            spawnRingRadius metres round the payload,
*                            following it)
*      CheckpointReached  -> ScaleWave(1 + waveIncreasePerCheckpoint x
*                            checkpoints passed), from its next refill
*      Arrived            -> StopWave
*    Also stops when the mission fails (MissionController.MissionFailed)
*    or the level is rebuilt. While the payload is destroyed the wave keeps
*    running (Unity). This script is the wave's anchor (IWaveAnchor = the
*    payload), so the wave's leash keeps its enemies near the payload. The
*    wave's own numbers (resource, interval, max alive, leash, pauses
*    roaming) are on the Spawn Director's child.
*    The enemies still chase the players; going for the payload is the
*    targeting step (later).
*
* \history
*    -v1.0       new: Unity's escort wave (start on first push, +25% per
*                checkpoint, stop on arrival)
*
**************************************************************************/
using System;

namespace Crucible.@Scripts
{
    public sealed class EscortWaves : EntityScript, IWaveAnchor
    {
        public string wave = "Wave Escort";             // Spawn Director child holding Escort's wave
        public float spawnRingRadius = 40f;             // metres from the payload enemies appear (Unity)
        public float waveIncreasePerCheckpoint = 0.25f; // 0.25 = 25% more resource for each checkpoint passed (Unity)

        private EscortObjective objective;
        private MissionController controller;
        private int seenBuild = -1;
        private int framesWaiting;
        private bool running, warnedNoDirector;

        /// <summary>The wave's leash anchor: the payload (where it is, even while destroyed).</summary>
        public Vector3 WaveAnchor
        {
            get
            {
                Entity p = objective != null ? objective.Payload : null;
                return p != null && p.IsAlive ? p.Transform.WorldPosition : Entity.Transform.WorldPosition;
            }
        }

        public override void OnUpdate(float deltaTime)
        {
            if (objective == null)
            {
                objective = Entity.GetComponent<EscortObjective>();
                if (objective == null)
                {
                    if (++framesWaiting == 120) Log.Warning("EscortWaves: no EscortObjective on " + Entity.Name + " - no escort wave");
                    return;
                }
                objective.Started += OnStarted;
                objective.CheckpointReached += OnCheckpoint;
                objective.Arrived += Stop;
                controller = Entity.GetComponent<MissionController>();
                if (controller != null) controller.MissionFailed += Stop;
            }
            if (controller == null)
            {
                controller = Entity.GetComponent<MissionController>();
                if (controller != null) controller.MissionFailed += Stop;
            }
            if (objective.BuildCount != seenBuild)      // a new map: the old wave goes
            {
                seenBuild = objective.BuildCount;
                Stop();
            }
        }

        public override void OnDestroy()
        {
            if (objective != null)
            {
                objective.Started -= OnStarted;
                objective.CheckpointReached -= OnCheckpoint;
                objective.Arrived -= Stop;
            }
            if (controller != null) controller.MissionFailed -= Stop;
            Stop();
        }

        private void OnStarted()
        {
            SpawnDirector director = SpawnDirector.Instance;
            if (director == null)
            {
                if (!warnedNoDirector) Log.Warning("EscortWaves: no Spawn Director in the scene - no escort wave (drop in Prefabs/Spawning/Spawn Director)");
                warnedNoDirector = true;
                return;
            }
            Wave settings = director.FindWave(wave);
            if (settings == null) { Log.Warning("EscortWaves: the Spawn Director has no child '" + wave + "' with a Wave"); return; }

            EscortObjective o = objective;
            RingSource source = director.MakeRing(delegate
            {
                Entity p = o.Payload;
                return p != null && p.IsAlive ? p.Transform.WorldPosition : (Vector3?)null;
            }, spawnRingRadius);
            if (!director.RunWave(settings, new ISpawnSource[] { source }, this))
            {
                Log.Warning("EscortWaves: another wave is already running - no escort wave");
                return;
            }
            running = true;
            Log.Info("EscortWaves: escort wave started, enemies from " + spawnRingRadius.ToString("0") + " m round the payload");
        }

        private void OnCheckpoint(int passed)
        {
            if (!running || objective.IsComplete) return;
            SpawnDirector director = SpawnDirector.Instance;
            if (director == null) return;
            float scale = 1f + Math.Max(0f, waveIncreasePerCheckpoint) * passed;
            director.ScaleWave(this, scale);
            Log.Info("EscortWaves: checkpoint " + passed + " - wave x" + scale.ToString("0.00") + " from its next refill");
        }

        private void Stop()
        {
            if (!running) return;
            SpawnDirector director = SpawnDirector.Instance;
            if (director != null) director.StopWave(this);
            running = false;
        }
    }
}
