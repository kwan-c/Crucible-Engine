/*************************************************************************
* \file:         RagdollDeathTest.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          23 November 2026
* \brief         TEMPORARY TEST for ragdoll steps 6 and 7 - delete before pushing.
*                Runs only as a scripting "instance" that the test run adds to
*                EngineConfig.json for the run (and removes afterwards).
*
* \details
*    In TutorialMap: spawns 24 Golem Maces in a grid in front of the camera,
*    waits 2.5 s, then kills all 24 in one frame (plain ApplyDamage).
*    Environment variable CRU_RAGDOLL_TEST picks the run:
*      (unset)  ragdolls on, and the step 6 checks (cap, removal at rest,
*               scene golems untouched)
*      off      every batch golem has ragdollOnDeath turned off first: the
*               same deaths without ragdolls, as the baseline for timing
*    Both log the frame times: one second before the kill, each of the 8
*    frames after it, and the average from 1 to 3 s after (ragdolls moving).
*    The prefab is made by reflection (test only): scripts normally get a
*    Prefab from an Inspector field.
*
**************************************************************************/
using System;
using System.Collections.Generic;
using System.Reflection;

namespace Crucible.Scripts
{
    public sealed class RagdollDeathTest
    {
        const ulong GolemMacePrefab = 0xAE5C80DE50C380A9UL;   // Assets/Prefabs/Golem Mace.cprefab
        const int Count = 24;

        bool ragdollsOff;
        float time, stageAt = -1f, killedAt;
        int stage, sceneGolems, maxRagdolls, checks, failures;
        float beforeSum, beforeFrames, afterSum, afterFrames;
        readonly List<Entity> batch = new List<Entity>();
        readonly List<float> killFrames = new List<float>();
        bool checkedCap, checkedEarly;

        public void OnCreate()
        {
            ragdollsOff = Environment.GetEnvironmentVariable("CRU_RAGDOLL_TEST") == "off";
            if (ragdollsOff)
                Log.Info("[RagdollCapTest] MODE off: 24 golems killed in one frame with ragdollOnDeath off (timing baseline, no checks)");
            else
                Log.Info("[RagdollCapTest] EXPECT: Spawn refused (false, with a WARN) for the camera and a health bar; " +
                         "24 golems spawned and killed in one frame; Ragdoll.Count never above 20; " +
                         "20 of the 24 golems left 0.5 s after (4 replaced); all ragdolls and their golems gone within 33 s; " +
                         "the scene's own golems untouched; then a sleeping scene golem killed and woken keeps its brain off");
        }

        public void OnUpdate(float deltaTime)
        {
            time += deltaTime;
            Camera camera = Camera.Main;
            if (stage == 0)
            {
                if (camera == null || Enemy.All.Count == 0) return;
                if (stageAt < 0f) { stageAt = time; return; }
                if (time - stageAt < 1.0f) return;
                sceneGolems = Alive(Enemy.All);
                Spawn(camera);
                stageAt = time;
                stage = 1;
            }
            else if (stage == 1)
            {
                if (time - stageAt > 1.5f) { beforeSum += deltaTime; beforeFrames += 1f; }
                if (time - stageAt < 2.5f) return;
                if (!ragdollsOff)
                {
                    // Review fix 1: a model that can't be built is refused at once (and logged).
                    Check(!Ragdoll.Spawn(camera.Entity, Vector3.Zero), "Spawn refused for the camera (no model)", 1f);
                    Entity bar = batch.Count > 0 ? batch[0].FindChild("Health Bar") : null;
                    Check(bar != null && !Ragdoll.Spawn(bar, Vector3.Zero), "Spawn refused for a health bar (no AnimatedModel)", bar != null ? 1f : 0f);
                }
                int killed = 0;
                foreach (Entity golem in batch)
                {
                    Enemy enemy = golem.IsAlive ? golem.GetComponent<Enemy>() : null;
                    if (enemy == null) continue;
                    if (ragdollsOff) enemy.ragdollOnDeath = false;
                    if (enemy.ApplyDamage(1000000f)) ++killed;
                }
                Log.Info("[RagdollCapTest] killed " + killed + " of " + batch.Count + " in one frame (ragdolls before: " + Ragdoll.Count + ")");
                killedAt = time;
                stage = 2;
            }
            else if (stage == 2)
            {
                maxRagdolls = Math.Max(maxRagdolls, Ragdoll.Count);
                if (killFrames.Count < 8) killFrames.Add(deltaTime);
                float since = time - killedAt;
                if (since >= 1f && since < 3f) { afterSum += deltaTime; afterFrames += 1f; }
                if (!ragdollsOff && !checkedEarly && since >= 0.5f)
                {
                    checkedEarly = true;
                    Check(Ragdoll.Count == 20, "ragdolls alive 0.5 s after the kill", Ragdoll.Count);
                    Check(AliveBatch() == 20, "batch golems left 0.5 s after (4 replaced)", AliveBatch());
                }
                if (!checkedCap && since >= 3f)
                {
                    checkedCap = true;
                    string frames = "";
                    foreach (float f in killFrames) frames += (f * 1000f).ToString("0.0") + " ";
                    Log.Info("[RagdollCapTest] frame times (ms): before the kill " + Average(beforeSum, beforeFrames) +
                             " | the 8 frames after: " + frames + "| 1-3 s after: " + Average(afterSum, afterFrames) +
                             " (" + Ragdoll.Count + " ragdolls alive)");
                    if (ragdollsOff) { Log.Info("[RagdollCapTest] DONE (timing baseline)"); stage = 3; return; }
                }
                if (ragdollsOff) return;
                if (Ragdoll.Count == 0 && AliveBatch() == 0 && since > 3f)
                {
                    Check(true, "all ragdolls and their golems gone, seconds after the kill", since);
                    Finish();
                }
                else if (since > 33f)
                {
                    Check(false, "all gone within 33 s (ragdolls left)", Ragdoll.Count);
                    Finish();
                }
            }
        }

        public void OnDestroy() { }

        void Spawn(Camera camera)
        {
            var prefab = (Prefab)Activator.CreateInstance(typeof(Prefab), true);
            typeof(Prefab).GetField("assetId", BindingFlags.NonPublic | BindingFlags.Instance).SetValue(prefab, GolemMacePrefab);
            Transform view = camera.Entity.Transform;
            Vector3 flat = new Vector3(view.Forward.X, 0f, view.Forward.Z);
            Vector3 forward = flat.Length > 1e-3f ? flat.Normalized : new Vector3(0f, 0f, -1f);
            Vector3 right = Vector3.Cross(forward, Vector3.Up).Normalized;
            float groundY = Enemy.All[0].Transform.WorldPosition.Y;
            int noFloor = 0;
            for (int i = 0; i < Count; ++i)
            {
                // 8 x 3, kept within 15 m: rows past that ran off the end of TutorialMap's floor.
                int column = i % 8, row = i / 8;
                Vector3 at = view.WorldPosition + forward * (10f + row * 2.5f) + right * ((column - 3.5f) * 2.5f);
                Entity golem = prefab.Instantiate(new Vector3(at.X, groundY, at.Z), Quaternion.Identity);
                if (golem != null) batch.Add(golem);
                RaycastHit hit;
                if (!Physics.Raycast(new Vector3(at.X, groundY + 3f, at.Z), new Vector3(0f, -1f, 0f), out hit, 10f,
                                     LayerMask.GetMask(PhysicsLayer.NonMoving), golem)) ++noFloor;
            }
            Log.Info("[RagdollCapTest] spawned " + batch.Count + " golems ahead (" + noFloor + " with no floor under them); scene golems alive: " + sceneGolems);
        }

        void Finish()
        {
            Check(maxRagdolls <= 20, "most ragdolls alive at once", maxRagdolls);
            Check(Alive(Enemy.All) == sceneGolems, "scene golems still alive", Alive(Enemy.All));

            // Review fix 3: a golem killed while asleep must stay brain-off when a checkpoint wakes it.
            Enemy sleeper = null;
            foreach (Enemy e in Enemy.All)
                if (e != null && e.startAsleep && !e.IsDead && e.Entity.IsActive && !e.Entity.IsEnabled<StateGraphAgent>()) { sleeper = e; break; }
            if (sleeper == null) Check(false, "found a sleeping scene golem for the Wake check", 0f);
            else
            {
                sleeper.ApplyDamage(1000000f);
                sleeper.Wake();
                Check(sleeper.IsDead && !sleeper.Entity.IsEnabled<StateGraphAgent>(), "golem killed asleep keeps its brain off after Wake()", 1f);
            }
            Log.Info("[RagdollCapTest] " + (failures == 0 ? "PASS" : "FAIL") + " - " + (checks - failures) + " of " + checks + " checks passed");
            stage = 3;
        }

        static string Average(float sum, float frames)
        {
            return frames > 0f ? (sum / frames * 1000f).ToString("0.0") + " ms avg" : "?";
        }

        int AliveBatch()
        {
            int alive = 0;
            foreach (Entity golem in batch) if (golem.IsAlive) ++alive;
            return alive;
        }

        static int Alive(List<Enemy> enemies)
        {
            int alive = 0;
            foreach (Enemy enemy in enemies) if (enemy != null && enemy.Entity.IsAlive && !enemy.IsDead) ++alive;
            return alive;
        }

        void Check(bool ok, string what, float value)
        {
            ++checks;
            if (!ok) ++failures;
            Log.Info("[RagdollCapTest] " + (ok ? "ok  " : "FAIL") + " " + what + " = " + value.ToString("0.###"));
        }
    }
}
