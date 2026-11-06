namespace Crucible.AI
{
    [NodeDescription(
        name: "Log",
        description: "Writes a message to the log, then succeeds.",
        story: "Log [Message]",
        category: "Action/Debug",
        id: "log")]
    public sealed class LogAction : Action
    {
        public string Message = "Hello from a behaviour graph";

        protected override Status OnStart()
        {
            Log.Info("Behavior (entity " + (Self != null ? Self.Index.ToString() : "?") + "): " + Message);
            return Status.Success;   // nothing left to do: finish without an update
        }
    }
}
