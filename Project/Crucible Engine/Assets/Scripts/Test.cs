using System;
using System.Numerics;

namespace @Crucible.@Scripts
{
    public sealed class @Test : global::Crucible.EntityScript
    {
        int i = 0;

        public override void OnCreate()
        {
            
        }

        public override void OnUpdate(float deltaTime)
        {
            ++i;
            Log.Info($"Value: {i}");
        }

        public override void OnDestroy()
        {
        }
    }
}