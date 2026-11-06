namespace Crucible.AI
{
    [NodeDescription(
        name: "Time In State",
        description: "Passes once the enemy has been in this state this many seconds.",
        story: "In this state for [Seconds] s",
        category: "Conditions/State",
        id: "time_in_state")]
    public sealed class TimeInStateCondition : Condition
    {
        public BlackboardVariable<float> Seconds = new BlackboardVariable<float>(10f);

        public override bool IsTrue() { return TimeInState >= Seconds.Value; }
    }
}
