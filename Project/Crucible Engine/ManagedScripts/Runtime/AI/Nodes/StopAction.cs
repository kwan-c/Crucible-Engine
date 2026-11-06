namespace Crucible.AI
{
    [NodeDescription(
        name: "Stop",
        description: "Tells the enemy to stand still; succeeds at once.",
        story: "Self stops",
        category: "Action/Navigation",
        id: "stop")]
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
