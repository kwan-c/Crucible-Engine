namespace Crucible.AI
{
    [NodeDescription("Blackboard Bool", "Passes when a bool variable has the given value.", "Blackboard", "blackboard_bool")]
    public sealed class BlackboardBoolCondition : Condition
    {
        public string Variable = "";
        public bool Value = true;

        public override bool IsTrue()
        {
            // A missing variable counts as false (the engine warns once about the name).
            bool current;
            if (Self == null || Variable.Length == 0 || !Self.StateGraphAgent.GetVariableValue(Variable, out current)) current = false;
            return current == Value;
        }
    }
}
