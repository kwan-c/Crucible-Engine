using System;
using Crucible;
using Crucible.AI;
using Action = Crucible.AI.Action;   // System.Action has the same name

namespace Crucible.Scripts
{
    // Port of Gam3333's WanderState + Steering.Wander (Reynolds wander): idle drifting at a
    // fraction of top speed. Unity steered with a force; here the same "where do I want to
    // go" direction is turned into a MoveTo point far ahead, every frame, and the
    // FlowFieldAgent does the steering, crowd spacing and wall avoidance.
    [NodeDescription(
        name: "Wander",
        description: "Drifts about slowly in a smoothly changing direction. Never finishes: an arrow takes it out.",
        story: "Self wanders at [SpeedFraction] of its speed",
        category: "Action/Navigation",
        id: "wander")]
    public sealed class WanderAction : NavigationAction
    {
        public BlackboardVariable<float> SpeedFraction = new BlackboardVariable<float>(0.4f);
        public float Jitter = 2f;            // radians per second of random drift (Steering.WanderJitter)
        public float Radius = 1f;            // wander circle radius (Steering.WanderRadius)
        public float Distance = 2f;          // wander circle distance ahead (Steering.WanderDistance)

        // MoveTo slows and stops near its point, so the point is pushed this far out to
        // keep it walking at wander speed. Not a design setting.
        const float AimDistance = 20f;

        static readonly Random random = new Random();

        float wanderAngle;
        float normalSpeed;

        protected override Status OnStart()
        {
            if (Agent == null) return Status.Failure;

            // Wander speed is a fraction of the agent's own speed. The agent has one Speed,
            // so it is lowered here and put back in OnEnd.
            normalSpeed = Agent.Speed;
            Agent.Speed = normalSpeed * SpeedFraction.Value;
            wanderAngle = (float)(random.NextDouble() * Math.PI * 2.0);
            return Status.Running;
        }

        protected override Status OnUpdate()
        {
            if (Agent == null) return Status.Failure;

            // Heading: where it is moving, or (standing still) where it faces. The agent
            // turns +Z towards its travel, and Crucible's Transform.Forward is -Z, hence * -1.
            Vector3 velocity = Agent.Velocity;
            Vector3 heading = new Vector3(velocity.X, 0f, velocity.Z);
            if (heading.Length < 0.01f)
            {
                Vector3 facing = Self.Transform.Forward * -1f;
                heading = new Vector3(facing.X, 0f, facing.Z);
            }
            heading = heading.Normalized;

            // The wander circle: a point on a circle ahead, nudged a little each frame,
            // so the direction drifts smoothly instead of jittering.
            wanderAngle += (float)(random.NextDouble() * 2.0 - 1.0) * Jitter * DeltaTime;
            Vector3 displacement = new Vector3((float)Math.Cos(wanderAngle), 0f, (float)Math.Sin(wanderAngle)) * Radius;
            Vector3 desired = (heading * Distance + displacement).Normalized;

            Agent.MoveTo(Self.Transform.WorldPosition + desired * AimDistance);
            return Status.Running;
        }

        protected override void OnEnd()
        {
            if (Agent != null && normalSpeed > 0f) Agent.Speed = normalSpeed;   // full speed again for Chase
            base.OnEnd();                                                       // NavigationAction: stop
        }
    }
}
