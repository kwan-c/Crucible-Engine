namespace Crucible.AI
{
    [NodeDescription(
        name: "Follow Field",
        description: "Follows the flow field towards the players; succeeds when close to one.",
        story: "Self follows the flow field to the players",
        category: "Action/Navigation",
        id: "follow_field")]
    public sealed class FollowFieldAction : NavigationAction
    {
        protected override Status OnStart()
        {
            if (Agent == null) return Status.Failure;
            Agent.FollowField();
            return Status.Running;
        }

        protected override Status OnUpdate()
        {
            if (Agent == null) return Status.Failure;
            return Agent.HasArrived ? Status.Success : Status.Running;
        }
    }
}
