using System;
using Crucible;
using Crucible.AI;
using Action = Crucible.AI.Action;   // System.Action has the same name

namespace Crucible.Scripts
{
    // Port of Gam3333's ChaseState in Zigzag mode (the crossbow golem): it runs at the
    // target while swaying left and right, so it is harder to hit.
    //
    // Each frame it aims at a point LookAhead metres along a smoothed line to the target,
    // pushed sideways by smooth noise times Amplitude, and gives that point to MoveTo.
    //
    // NOT 1:1 with Unity:
    //  - Unity's line was the flow field's direction, which routes round walls. C# can't
    //    read the field yet, so the line here is straight at the target: no wall routing
    //    (the agent still swerves round obstacles it sees).
    //  - Unity faced the player while swaying; the agent faces where it walks.
    //  - Unity used Mathf.PerlinNoise; this uses an equivalent smooth 1D noise (below).
    [NodeDescription(
        name: "Zigzag Chase",
        description: "Runs at the target, swaying side to side. Never finishes: an arrow takes it out. Straight line, no routing round walls.",
        story: "Self zigzags towards [Target]",
        category: "Action/Navigation",
        id: "zigzag_chase")]
    public sealed class ZigzagChaseAction : NavigationAction
    {
        public BlackboardVariable<Entity> Target = BlackboardVariable<Entity>.Link("Target");
        public float Amplitude = 6f;         // how far sideways the aim point swings, in metres
        public float NoiseSpeed = 0.5f;      // how fast the sway changes; too fast and it can't keep up
        public float LookAhead = 6f;         // how far along the line the aim point is; smaller = sharper curves

        static readonly Random random = new Random();

        float timer;
        float noiseSeed;                     // per-chase offset, so a group doesn't sway in step
        Vector3 smoothedDirection;           // the steady line the sway swings round

        protected override Status OnStart()
        {
            if (Agent == null) return Status.Failure;
            timer = 0f;
            noiseSeed = (float)(random.NextDouble() * 1000.0);
            smoothedDirection = Vector3.Zero;   // "not set": the first frame snaps to the real line
            return Status.Running;
        }

        protected override Status OnUpdate()
        {
            if (Agent == null) return Status.Failure;
            Entity target = Target.Value;
            if (!IsThere(target)) return Status.Failure;

            Vector3 here = Self.Transform.WorldPosition;
            Vector3 toTarget = target.Transform.WorldPosition - here;
            Vector3 line = new Vector3(toTarget.X, 0f, toTarget.Z).Normalized;
            if (line.Length < 0.5f) return Status.Running;   // standing on the target: nothing to do

            // Ease towards a new line over about half a second (Unity's Slerp(dt * 2)).
            smoothedDirection = smoothedDirection.Length < 0.5f
                ? line
                : (smoothedDirection + (line - smoothedDirection) * Math.Min(1f, DeltaTime * 2f)).Normalized;

            timer += DeltaTime;
            float lateral = Noise(noiseSeed + timer * NoiseSpeed) * Amplitude;

            // Sideways = up x line: unit length, since both are unit and at right angles.
            Vector3 side = Vector3.Cross(Vector3.Up, smoothedDirection);
            Agent.MoveTo(here + smoothedDirection * LookAhead + side * lateral);
            return Status.Running;
        }

        // Smooth 1D gradient noise in about -1..1: a random slope at each whole number,
        // blended with a smooth curve between them. Unity's Mathf.PerlinNoise remapped
        // with "noise * 2 - 1" does the same job: smooth, and never repeats.
        static float Noise(float x)
        {
            int cell = (int)Math.Floor(x);
            float t = x - cell;
            float a = Slope(cell) * t;
            float b = Slope(cell + 1) * (t - 1f);
            float fade = t * t * t * (t * (t * 6f - 15f) + 10f);   // Perlin's smootherstep
            return (a + (b - a) * fade) * 2f;                      // * 2: this noise peaks near 0.5
        }

        // A fixed pseudo-random slope in -1..1 for a whole number (an integer hash).
        static float Slope(int cell)
        {
            unchecked
            {
                uint h = (uint)cell * 374761393u;
                h = (h ^ (h >> 13)) * 1274126177u;
                h ^= h >> 16;
                return (h & 0xFFFF) / 32767.5f - 1f;
            }
        }
    }
}
