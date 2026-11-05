namespace Crucible.AI
{
    [NodeDescription("Move To", "Walks straight to a point; succeeds on arrival.", "Navigation", "move_to")]
    public sealed class MoveToAction : NavigationAction
    {
        public BlackboardVariable<Vector3> Destination = new BlackboardVariable<Vector3>();

        private Vector3 issued;   // the point last given to the agent

        protected override Status OnStart()
        {
            if (Agent == null) return Status.Failure;
            issued = Destination.Value;
            Agent.MoveTo(issued);
            return Status.Running;
        }

        protected override Status OnUpdate()
        {
            if (Agent == null) return Status.Failure;

            // Re-issue only when the destination really changed (a moving variable).
            // Re-issuing every frame would reset "arrived" every frame, and the node
            // would never finish.
            Vector3 destination = Destination.Value;
            if (destination.X != issued.X || destination.Y != issued.Y || destination.Z != issued.Z)
            {
                issued = destination;
                Agent.MoveTo(destination);
            }
            return Agent.HasArrived ? Status.Success : Status.Running;
        }
    }
}
