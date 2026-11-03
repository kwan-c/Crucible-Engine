using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    // Tests WorldText and a world-space UICanvas from scripts.
    //   1. Attach to an entity with a WorldText component (any text).
    //   2. Optional: "Damage Number" = a prefab whose root has a WorldText (Billboard FaceCamera
    //      looks best). "World Canvas" = an entity with a UICanvas set to World Space, with a
    //      UIText child.
    //   3. Press Play. The text counts up every second (the layout only rebuilds when it changes).
    //        C  flash the colour red <-> white       B  cycle Billboard: Off -> FaceCamera -> Upright
    //        O  toggle Outline                       G  toggle Glow (Intensity 1 <-> 4)
    //        F  toggle Fade (5 .. 10 m)              T  toggle AlwaysOnTop
    //        J  cycle HorizontalAlign                K  spawn a damage number (floats up, gone in 1 s)
    //        N  World Canvas: cycle its Billboard    M  World Canvas: toggle AlwaysOnTop
    public sealed class WorldTextTest : EntityScript
    {
        public Prefab damageNumber;
        public Entity worldCanvas;

        WorldText label;
        int seconds = -1;
        bool red, glow;
        readonly Random rng = new Random();
        readonly List<Entity> floating = new List<Entity>();

        public override void OnCreate()
        {
            label = GetComponent<WorldText>();
            if (label == null) { Log.Warning("[WorldTextTest] " + Entity + " has no WorldText component."); return; }
            Log.Info("[WorldTextTest] " + label + ", size " + label.FontSize + ", box " + label.Size +
                     ", billboard " + label.Billboard + ", align " + label.HorizontalAlign + "/" + label.VerticalAlign);
            if (worldCanvas != null)
            {
                UICanvas canvas = worldCanvas.GetComponent<UICanvas>();
                if (canvas == null) Log.Warning("[WorldTextTest] World Canvas has no UICanvas.");
                else Log.Info("[WorldTextTest] canvas " + canvas.RenderMode + ", " + canvas.PixelsPerUnit + " px/m, billboard " + canvas.Billboard);
            }
        }

        public override void OnUpdate(float deltaTime)
        {
            // Damage numbers float up (they destroy themselves).
            for (int i = floating.Count - 1; i >= 0; --i)
            {
                if (!floating[i].IsAlive) { floating.RemoveAt(i); continue; }
                floating[i].Transform.Position += new Vector3(0f, 1.5f * deltaTime, 0f);
            }

            if (label == null)
                return;

            // Same text every frame costs nothing; it only changes once a second.
            int now = (int)Time.ElapsedTime;
            if (now != seconds) { seconds = now; }
            label.Text = "Time <b>" + seconds + "</b>";

            if (Input.IsPressed(KeyCode.C)) { red = !red; label.Color = red ? Color.Red : Color.White; }
            if (Input.IsPressed(KeyCode.B)) { label.Billboard = (Billboard)(((int)label.Billboard + 1) % 3); Log.Info("[WorldTextTest] Billboard " + label.Billboard); }
            if (Input.IsPressed(KeyCode.O)) { label.Outline = !label.Outline; label.OutlineWidth = 0.03f; Log.Info("[WorldTextTest] Outline " + label.Outline); }
            if (Input.IsPressed(KeyCode.G)) { glow = !glow; label.Glow = glow; label.Intensity = glow ? 4f : 1f; Log.Info("[WorldTextTest] Glow " + glow); }
            if (Input.IsPressed(KeyCode.F)) { label.Fade = !label.Fade; label.FadeStart = 5f; label.FadeEnd = 10f; Log.Info("[WorldTextTest] Fade " + label.Fade); }
            if (Input.IsPressed(KeyCode.T)) { label.AlwaysOnTop = !label.AlwaysOnTop; Log.Info("[WorldTextTest] AlwaysOnTop " + label.AlwaysOnTop); }
            if (Input.IsPressed(KeyCode.J)) { label.HorizontalAlign = (HorizontalAlign)(((int)label.HorizontalAlign + 1) % 3); Log.Info("[WorldTextTest] Align " + label.HorizontalAlign); }

            if (Input.IsPressed(KeyCode.K))
            {
                if (damageNumber == null) Log.Warning("[WorldTextTest] Damage Number prefab is not set.");
                else
                {
                    Vector3 at = Transform.WorldPosition + new Vector3((float)rng.NextDouble() - 0.5f, 1f, 0f);
                    Entity e = damageNumber.Instantiate(at, Quaternion.Identity);
                    WorldText number = e != null ? e.GetComponent<WorldText>() : null;
                    if (number == null) Log.Warning("[WorldTextTest] the Damage Number prefab has no WorldText.");
                    else
                    {
                        number.Text = (rng.Next(5, 99)).ToString();
                        number.Color = Color.Red;
                        floating.Add(e);
                        Destroy(e, 1f);
                    }
                }
            }

            if (worldCanvas != null)
            {
                UICanvas canvas = worldCanvas.GetComponent<UICanvas>();
                if (canvas != null && Input.IsPressed(KeyCode.N)) { canvas.Billboard = (Billboard)(((int)canvas.Billboard + 1) % 3); Log.Info("[WorldTextTest] canvas Billboard " + canvas.Billboard); }
                if (canvas != null && Input.IsPressed(KeyCode.M)) { canvas.AlwaysOnTop = !canvas.AlwaysOnTop; Log.Info("[WorldTextTest] canvas AlwaysOnTop " + canvas.AlwaysOnTop); }
            }
        }
    }
}
