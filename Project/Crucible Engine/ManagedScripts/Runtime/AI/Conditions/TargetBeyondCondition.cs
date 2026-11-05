namespace Crucible.AI
{
    [NodeDescription("Target Beyond", "Passes when the target is further than this, or gone.", "Distance", "target_beyond")]
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
