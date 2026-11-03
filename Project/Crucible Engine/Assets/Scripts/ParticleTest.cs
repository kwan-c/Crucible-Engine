using System;

namespace Crucible.Scripts
{
    // Tests the ParticleEmitter API.
    //   1. Attach to an entity with a ParticleEmitter component. For Restart, give it a
    //      Burst Count above 0 (and Loop off to see the burst clearly).
    //   2. Press Play, then:
    //        Q  Play          E  Stop (live particles finish)
    //        R  Restart (fires the burst again, this frame)
    //        T  Emit(30) - extra particles now, even while stopped
    //        X  toggle red / original start colour
    //        1 / 2  rate down / up        3 / 4  gravity down / up
    //        L  toggle the emitted light
    //   It logs once when the effect has fully faded out (IsAlive turns false).
    public sealed class ParticleTest : EntityScript
    {
        ParticleEmitter fx;
        Color originalColor;
        bool red;
        bool wasAlive = true;

        public override void OnCreate()
        {
            fx = GetComponent<ParticleEmitter>();
            if (fx == null) { Log.Warning("[ParticleTest] " + Entity + " has no ParticleEmitter component."); return; }
            originalColor = fx.StartColor;
            Log.Info("[ParticleTest] " + fx + ": emitting " + fx.IsEmitting + ", loop " + fx.Loop + ", rate " + fx.Rate +
                     ", burst " + fx.BurstCount + ", life " + fx.LifetimeMin + "-" + fx.LifetimeMax + ", colour " +
                     fx.StartColor + " -> " + fx.EndColor);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (fx == null)
                return;

            if (Input.IsPressed(KeyCode.Q)) { fx.Play(); Log.Info("[ParticleTest] Play"); }
            if (Input.IsPressed(KeyCode.E)) { fx.Stop(); Log.Info("[ParticleTest] Stop"); }
            if (Input.IsPressed(KeyCode.R)) { fx.Restart(); Log.Info("[ParticleTest] Restart (frame " + Time.FrameCount + ")"); }
            if (Input.IsPressed(KeyCode.T)) { fx.Emit(30); Log.Info("[ParticleTest] Emit(30); " + fx.ParticleCount + " alive before it"); }

            bool alive = fx.IsAlive;
            if (alive != wasAlive)
            {
                Log.Info("[ParticleTest] " + (alive ? "Active again" : "Faded out - nothing emitting or alive (safe to Destroy)"));
                wasAlive = alive;
            }

            if (Input.IsPressed(KeyCode.X))
            {
                red = !red;
                fx.StartColor = red ? Color.Red : originalColor;
                Log.Info("[ParticleTest] Start colour " + fx.StartColor);
            }

            if (Input.IsPressed(KeyCode.Num1)) { fx.Rate = Math.Max(0f, fx.Rate - 10f); Log.Info("[ParticleTest] Rate " + fx.Rate); }
            if (Input.IsPressed(KeyCode.Num2)) { fx.Rate += 10f; Log.Info("[ParticleTest] Rate " + fx.Rate); }
            if (Input.IsPressed(KeyCode.Num3)) { fx.GravityScale -= 0.5f; Log.Info("[ParticleTest] Gravity " + fx.GravityScale); }
            if (Input.IsPressed(KeyCode.Num4)) { fx.GravityScale += 0.5f; Log.Info("[ParticleTest] Gravity " + fx.GravityScale); }
            if (Input.IsPressed(KeyCode.L)) { fx.Loop = !fx.Loop; Log.Info("[ParticleTest] Looping " + fx.Loop); }
        }
    }
}
