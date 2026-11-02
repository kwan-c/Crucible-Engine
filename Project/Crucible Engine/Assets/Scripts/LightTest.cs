using System;

namespace Crucible.Scripts
{
    // Tests the Light API.
    //   1. Attach to an entity with a Light component (a Point or Spot light works best).
    //      Optionally set "Target" - a Spot light will aim at it.
    //   2. Press Play, then:
    //        F  toggle flicker (random intensity every frame)
    //        G  toggle alarm (colour pulses white <-> red)
    //        H  switch the light on / off
    //        B  cycle the type: Point -> Spot -> Directional
    //        9 / 0  range down / up       (Point / Spot)
    //   A Spot light keeps pointing at Target every frame.
    public sealed class LightTest : EntityScript
    {
        public Entity target;
        public float flickerAmount = 0.5f;   // 0..1 of the base intensity
        public float alarmSpeed = 3f;        // pulses per second

        Light lamp;
        float baseIntensity;
        Color baseColor;
        bool flicker, alarm;
        readonly Random rng = new Random();

        public override void OnCreate()
        {
            lamp = GetComponent<Light>();
            if (lamp == null) { Log.Warning("[LightTest] " + Entity + " has no Light component."); return; }
            baseIntensity = lamp.Intensity;
            baseColor = lamp.Color;
            Log.Info("[LightTest] " + lamp + ": colour " + lamp.Color + ", intensity " + lamp.Intensity + ", range " +
                     lamp.Range + ", cone " + lamp.InnerSpotAngle + "-" + lamp.SpotAngle + ", shadows " + lamp.CastShadows);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (lamp == null)
                return;

            if (Input.IsPressed(KeyCode.F)) { flicker = !flicker; if (!flicker) lamp.Intensity = baseIntensity; Log.Info("[LightTest] Flicker " + flicker); }
            if (Input.IsPressed(KeyCode.G)) { alarm = !alarm; if (!alarm) lamp.Color = baseColor; Log.Info("[LightTest] Alarm " + alarm); }
            if (Input.IsPressed(KeyCode.H)) { lamp.Enabled = !lamp.Enabled; Log.Info("[LightTest] Enabled " + lamp.Enabled); }
            if (Input.IsPressed(KeyCode.B))
            {
                lamp.Type = lamp.Type == LightType.Point ? LightType.Spot
                          : lamp.Type == LightType.Spot ? LightType.Directional : LightType.Point;
                Log.Info("[LightTest] Type " + lamp.Type);
            }
            if (Input.IsPressed(KeyCode.Num9)) { lamp.Range = Math.Max(0f, lamp.Range - 2f); Log.Info("[LightTest] Range " + lamp.Range); }
            if (Input.IsPressed(KeyCode.Num0)) { lamp.Range += 2f; Log.Info("[LightTest] Range " + lamp.Range); }

            if (flicker)
                lamp.Intensity = baseIntensity * (1f - flickerAmount + (float)rng.NextDouble() * flickerAmount);
            if (alarm)
            {
                float t = 0.5f + 0.5f * (float)Math.Sin(Time.ElapsedTime * alarmSpeed * 2.0 * Math.PI);
                lamp.Color = Color.Lerp(Color.White, Color.Red, t);
            }

            if (target != null && lamp.Type == LightType.Spot)
                Transform.LookAt(target);
        }
    }
}
