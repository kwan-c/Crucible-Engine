namespace Crucible.AI
{
    [NodeDescription(
        name: "On Start",
        description: "The root of a behaviour graph. Runs its child; with Repeat, again and again.",
        category: "Events",
        id: "start")]
    public sealed class OnStartNode : Modifier
    {
        public bool Repeat = true;

        protected override Status OnUpdate()
        {
            if (Child == null) return Status.Success;   // an empty graph: nothing to do, done
            Status status = Child.Tick(DeltaTime);
            if (status == Status.Running) return Status.Running;

            // The child finished. Without Repeat, so has the graph. With it, report
            // Running: the child is idle now, and its next Tick - NEXT frame - starts
            // it again. Restarting it here instead would let a child that finishes
            // instantly (a lone Log) loop forever inside one frame.
            return Repeat ? Status.Running : status;
        }
    }
}
