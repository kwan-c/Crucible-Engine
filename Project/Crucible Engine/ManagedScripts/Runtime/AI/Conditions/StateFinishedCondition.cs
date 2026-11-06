namespace Crucible.AI
{
    /// <summary>Which ending State Finished waits for. Names are saved in graph files: don't rename.</summary>
    public enum FinishedWith
    {
        Either,     // succeeded or failed, it doesn't matter
        Success,
        Failure
    }

    [NodeDescription(
        name: "State Finished",
        description: "Passes once the state's behaviour has finished (with Success, Failure, or either).",
        story: "The state finished with [With]",
        category: "Conditions/State",
        id: "state_finished")]
    public sealed class StateFinishedCondition : Condition
    {
        public FinishedWith With = FinishedWith.Either;

        public override bool IsTrue()
        {
            if (!StateFinished) return false;
            switch (With)
            {
                case FinishedWith.Success: return StateFinishedWith == Node.Status.Success;
                case FinishedWith.Failure: return StateFinishedWith == Node.Status.Failure;
                default:                   return true;
            }
        }
    }
}
