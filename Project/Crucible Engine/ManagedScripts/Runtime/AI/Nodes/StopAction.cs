namespace Crucible.AI
{
    [NodeDescription("Stop", "Tells the enemy to stand still; succeeds at once.", "Navigation", "stop")]
    public sealed class StopAction : NavigationAction
    {
        protected override Status OnStart()
        {
            if (Agent == null) return Status.Failure;
            Agent.Stop();
            return Status.Success;
        }
    }
}
