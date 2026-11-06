namespace Crucible.AI
{
    [NodeDescription(
        name: "Arrive",
        description: "Goes straight at a target, stopping in range and facing it; succeeds in range.",
        story: "Self arrives at [Target]",
        category: "Action/Navigation",
        id: "arrive")]
    public sealed class ArriveAction : NavigationAction
    {
        public BlackboardVariable<Entity> Target = BlackboardVariable<Entity>.Link("Target");   // starts linked to "Target"

        private Entity issued;   // the target last given to the agent

        protected override Status OnStart()
        {
            if (Agent == null) return Status.Failure;
            issued = Target.Value;
            if (!IsThere(issued)) return Status.Failure;   // nobody to go at
            Agent.Arrive(issued);
            return Status.Running;
        }

        protected override Status OnUpdate()
        {
            if (Agent == null) return Status.Failure;
            Entity target = Target.Value;
            if (!IsThere(target)) return Status.Failure;   // gone mid-chase

            // Same rule as Move To: only a NEW target is re-issued.
            if (target != issued)
            {
                issued = target;
                Agent.Arrive(target);
            }
            return Agent.HasArrived ? Status.Success : Status.Running;
        }
    }
}
