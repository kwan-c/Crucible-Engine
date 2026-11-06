using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    // Fades UI (and world text) in and out of view, over and over.
    //
    // Setup:
    //   Put this on a UIText or UIImage in a world canvas. To flash a whole group, put it on the
    //   parent and leave "Include Children" ticked - every UIText, UIImage and WorldText under it
    //   fades together.
    //
    // Each element fades between Min Alpha and its own alpha from the Inspector, so a
    // half-transparent image stays half-transparent at its brightest.
    //
    // From another script: GetComponent<Flashing>().flashing = false stops it at full view.
    public sealed class Flashing : EntityScript
    {
        public float period = 1.5f;          // seconds for one full fade out and back in
        public float minAlpha = 0f;          // 0 = fully gone at the dimmest, 0.3 = never quite gone
        public float startOffset = 0f;       // seconds; give items different values so they don't flash in sync
        public bool hardBlink = false;       // on/off blink instead of a smooth fade
        public bool includeChildren = true;  // also flash UI under this entity
        public bool flashing = true;         // untick (or set from code) to stop and show fully

        // One fading thing and its alpha from the Inspector.
        sealed class Target
        {
            public UIText text;
            public UIImage image;
            public WorldText worldText;
            public float fullAlpha;
        }

        readonly List<Target> targets = new List<Target>();
        float time;
        bool wasFlashing = true;

        public override void OnCreate()
        {
            Collect(Entity, includeChildren);
            if (targets.Count == 0)
                Log.Warning("[Flashing] " + Entity + " has no UIText, UIImage or WorldText" +
                            (includeChildren ? " (or children with one)." : "."));
            time = startOffset;
        }

        void Collect(Entity e, bool recurse)
        {
            UIText text = e.GetComponent<UIText>();
            if (text != null) targets.Add(new Target { text = text, fullAlpha = text.Color.A });

            UIImage image = e.GetComponent<UIImage>();
            if (image != null) targets.Add(new Target { image = image, fullAlpha = image.Color.A });

            WorldText worldText = e.GetComponent<WorldText>();
            if (worldText != null) targets.Add(new Target { worldText = worldText, fullAlpha = worldText.Color.A });

            if (!recurse)
                return;
            for (int i = 0; i < e.Transform.ChildCount; ++i)
            {
                Entity child = e.Transform.GetChild(i);
                if (child != null) Collect(child, true);
            }
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!flashing)
            {
                if (wasFlashing) Apply(1f);   // stopped: show fully once, then leave it alone
                wasFlashing = false;
                return;
            }
            wasFlashing = true;

            time += deltaTime;
            float p = period > 0.01f ? period : 0.01f;
            float phase = (time % p) / p;     // 0..1 through one flash

            // Starts fully visible, fades out to the middle of the period, then back in.
            float t = hardBlink
                ? (phase < 0.5f ? 1f : 0f)
                : 0.5f + 0.5f * (float)Math.Cos(phase * 2.0 * Math.PI);

            float clampedMin = Math.Max(0f, Math.Min(1f, minAlpha));
            Apply(clampedMin + (1f - clampedMin) * t);
        }

        // amount 0..1 of each target's full alpha.
        void Apply(float amount)
        {
            foreach (Target target in targets)
            {
                float a = target.fullAlpha * amount;
                if (target.text != null)      { Color c = target.text.Color;      c.A = a; target.text.Color = c; }
                if (target.image != null)     { Color c = target.image.Color;     c.A = a; target.image.Color = c; }
                if (target.worldText != null) { Color c = target.worldText.Color; c.A = a; target.worldText.Color = c; }
            }
        }
    }
}
