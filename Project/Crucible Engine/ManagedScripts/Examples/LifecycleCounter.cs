using System;

namespace Crucible.Examples
{
    // S3 standalone lifecycle example. No entity or engine API dependencies.
    public sealed class LifecycleCounter
    {
        private int updates;
        private double elapsed;

        public void OnCreate()
        {
            updates = 0;
            elapsed = 0;
            Console.WriteLine("LifecycleCounter: created.");
        }

        public void OnUpdate(float deltaTime)
        {
            ++updates;
            elapsed += deltaTime;
        }

        public void OnDestroy()
        {
            Console.WriteLine("LifecycleCounter: destroyed after {0} updates ({1:F3}s).", updates, elapsed);
        }
    }
}
