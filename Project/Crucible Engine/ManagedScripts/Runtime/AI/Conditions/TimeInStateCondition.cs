namespace Crucible.AI
{
    [NodeDescription("Time In State", "Passes once the enemy has been in this state this many seconds.", "State", "time_in_state")]
    public sealed class TimeInStateCondition : Condition
    {
        public BlackboardVariable<float> Seconds = new BlackboardVariable<float>(10f);

        public override bool IsTrue() { return TimeInState >= Seconds.Value; }
    }
}
