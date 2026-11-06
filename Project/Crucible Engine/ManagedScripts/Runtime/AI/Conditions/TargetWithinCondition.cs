namespace Crucible.AI
{
    [NodeDescription(
        name: "Target Within",
        description: "Passes when the target is this close or closer.",
        story: "[Target] is within [Distance] m",
        category: "Conditions/Distance",
        id: "target_within")]
    public sealed class TargetWithinCondition : DistanceCondition
    {
        public BlackboardVariable<Entity> Target = BlackboardVariable<Entity>.Link("Target");
        public BlackboardVariable<float> Distance = new BlackboardVariable<float>(8f);

        public override bool IsTrue()
        {
            float distance = FlatDistanceTo(Target.Value);
            return distance >= 0f && distance <= Distance.Value;
        }
    }
}
