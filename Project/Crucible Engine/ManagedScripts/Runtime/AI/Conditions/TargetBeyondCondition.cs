namespace Crucible.AI
{
    [NodeDescription(
        name: "Target Beyond",
        description: "Passes when the target is further than this, or gone.",
        story: "[Target] is beyond [Distance] m",
        category: "Conditions/Distance",
        id: "target_beyond")]
    public sealed class TargetBeyondCondition : DistanceCondition
    {
        public BlackboardVariable<Entity> Target = BlackboardVariable<Entity>.Link("Target");
        public BlackboardVariable<float> Distance = new BlackboardVariable<float>(12f);

        public override bool IsTrue()
        {
            // No target at all is as far away as it gets: a chase with nobody to
            // chase should end, not carry on forever.
            float distance = FlatDistanceTo(Target.Value);
            return distance < 0f || distance > Distance.Value;
        }
    }
}
