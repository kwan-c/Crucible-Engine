using System;

namespace @Crucible.@Scripts
{
    public sealed class @Test : global::Crucible.EntityScript
    {
        public float speed;
        float time;

        public override void OnCreate()
        {
            
        }

        public override void OnUpdate(float deltaTime)
        {
            time += deltaTime;
            Transform.Position += new Vector3(0, 0, speed * time);
        }

        public override void OnDestroy()
        {
        }
    }
}
