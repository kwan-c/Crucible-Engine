namespace @Crucible.@Scripts
{
    public sealed class @RefTest : global::Crucible.EntityScript
    {
        public Entity en;
        public override void OnCreate()
        {
            global::Crucible.Log.Info("RefTest created for entity " + this.Entity);
        }

        public override void OnUpdate(float deltaTime)
        {
            Log.Info("[RefTest] Entity: " + en);
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("RefTest destroyed for entity " + this.Entity);
        }
    }
}
