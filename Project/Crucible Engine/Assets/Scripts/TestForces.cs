using System.Numerics;

namespace @Crucible.@Scripts
{
    public sealed class @TestForces : global::Crucible.EntityScript
    {
        public Vector3 force = new Vector3(10, 0, 0);
        float time;
        public override void OnCreate()
        {
        }

        public override void OnUpdate(float deltaTime)
        {
            time += deltaTime;
            if (time >= 3.0f)
            {
                Log.Info("Adding Force");
                RigidBody.AddForce(force);
                time = 0;
            }
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("TestForces destroyed for entity " + this.Entity);
        }
    }
}
