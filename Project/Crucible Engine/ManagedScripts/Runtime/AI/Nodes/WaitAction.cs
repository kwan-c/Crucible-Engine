namespace Crucible.AI
{
    [NodeDescription(
        name: "Wait",
        description: "Waits for a number of seconds, then succeeds.",
        story: "Wait [Seconds] seconds",
        category: "Action/Delay",
        id: "wait")]
    public sealed class WaitAction : Action
    {
        public BlackboardVariable<float> Seconds = new BlackboardVariable<float>(1f);

        private float elapsed;   // per enemy: every enemy has its own copy of this node

        protected override Status OnStart()
        {
            elapsed = 0f;
            return Status.Running;
        }

        protected override Status OnUpdate()
        {
            // The length is read every frame, so a linked variable that changes
            // mid-wait (a timer the brain shortens) takes effect at once.
            elapsed += DeltaTime;
            return elapsed >= Seconds.Value ? Status.Success : Status.Running;
        }
    }
}
