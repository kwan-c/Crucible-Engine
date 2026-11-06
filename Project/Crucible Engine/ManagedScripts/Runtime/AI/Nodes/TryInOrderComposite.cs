namespace Crucible.AI
{
    [NodeDescription(
        name: "Try In Order",
        description: "Executes branches in order until one succeeds.",
        category: "Flow",
        id: "try_in_order")]
    public sealed class TryInOrderComposite : Composite
    {
        private int current;

        protected override Status OnStart()
        {
            current = 0;
            return Children.Count == 0 ? Status.Failure : Status.Running;   // nothing to succeed: fail
        }

        protected override Status OnUpdate()
        {
            // Sequence's mirror image: Success stops it, Failure moves on.
            float step = DeltaTime;
            while (current < Children.Count)
            {
                Status status = Children[current].Tick(step);
                step = 0f;
                if (status == Status.Running) return Status.Running;
                if (status == Status.Success) return Status.Success;
                ++current;   // that one failed: try the next
            }
            return Status.Failure;
        }
    }
}
