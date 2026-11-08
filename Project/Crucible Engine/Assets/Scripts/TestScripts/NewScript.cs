namespace @Crucible.@Scripts
{
    public sealed class @NewScript : global::Crucible.EntityScript
    {
        public override void OnCreate()
        {
            global::Crucible.Log.Info("NewScript created for entity " + this.Entity);
        }

        public override void OnUpdate(float deltaTime)
        {
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("NewScript destroyed for entity " + this.Entity);
        }
    }
}
