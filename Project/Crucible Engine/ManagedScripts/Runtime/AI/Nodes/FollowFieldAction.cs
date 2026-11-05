namespace Crucible.AI
{
    [NodeDescription("Follow Field", "Follows the flow field towards the players; succeeds when close to one.", "Navigation", "follow_field")]
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
