namespace Crucible.AI
{
    [NodeDescription(
        name: "Sequence",
        description: "Executes branches in order until one fails or all succeed.",
        category: "Flow",
        id: "sequence")]
    public sealed class SequenceComposite : Composite
    {
        private int current;   // the child running now

        protected override Status OnStart()
        {
            current = 0;
            return Children.Count == 0 ? Status.Success : Status.Running;   // nothing to fail: done
        }

        protected override Status OnUpdate()
        {
            // As many children as finish this frame run this frame: a child that
            // succeeds instantly hands straight on to the next. Only the first gets
            // the frame's time - the ones after it start at this moment, so a Wait
            // after an instant Log doesn't count the frame twice.
            float step = DeltaTime;
            while (current < Children.Count)
            {
                Status status = Children[current].Tick(step);
                step = 0f;
                if (status == Status.Running) return Status.Running;
                if (status == Status.Failure) return Status.Failure;
                ++current;   // that one succeeded: on to the next
            }
            return Status.Success;
        }
    }
}
