using System;
using System.Numerics;

namespace @Crucible.@Scripts
{
    public sealed class @Test : global::Crucible.EntityScript
    {
        public float speed;
        float time;
        public bool useGravity;

        public override void OnCreate()
        {
            
        }

        public override void OnUpdate(float deltaTime)
        {
            time += deltaTime;
            Transform.Position += new Vector3(0, 0, speed * time);
            bool useGravity = RigidBody.UseGravity;
            Log.Info($"Use Gravity: {useGravity}");

            if (time >= 10.0f)
            {
                RigidBody.UseGravity = false;
            }

            if (!useGravity)
            {
                RigidBody.Velocity = new Vector3(0, 10.0f, 0);
            }
        }

        public override void OnDestroy()
        {
        }
    }
}