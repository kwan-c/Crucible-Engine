namespace Crucible.AI
{
    [NodeDescription(
        name: "Blackboard Bool",
        description: "Passes when a bool variable has the given value.",
        story: "[Variable] is [Value]",
        category: "Conditions/Blackboard",
        id: "blackboard_bool")]
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
